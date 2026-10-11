"""Subtitle parsing and serialization, delegated to established libraries.

- SRT/ASS read + write, SAMI read: pysubs2
- SAMI write: pycaption (pysubs2 cannot write SAMI)
- SAMI <STYLE> CSS: tinycss2, CSS color names: webcolors

pysubs2's SAMI reader merges every <P Class=...> of a SYNC into one line,
ignores <font color> and drops a final caption not followed by </BODY>.
TrackedSamiParser extends that parser with exactly those three things; all
other tag handling (<b>/<i>/<u>/<s>/<br>, entities) is the library's.
"""

import re
from dataclasses import dataclass, field

import tinycss2
import webcolors
from pycaption import Caption, CaptionList, CaptionNode, CaptionSet, SAMIWriter
from pysubs2 import Alignment, Color, SSAEvent, SSAFile, SSAStyle
from pysubs2.formats.sami import SAMIParser, SyncElement

ASS_FONT_NAME = "맑은 고딕"  # Arial has no Hangul glyphs; Malgun Gothic ships with Windows.
LAST_CUE_DURATION_MS = 2000  # a SAMI cue with no following SYNC

_SAMI_LANG = {"ko": "ko-KR", "en": "en-US", "jp": "ja-JP"}


@dataclass
class Track:
    """One subtitle track: an SMI <P Class> or a whole SRT/ASS file."""
    name: str
    events: list[SSAEvent]
    styles: dict[str, SSAStyle] = field(default_factory=dict)
    bold: bool = False  # SMI CSS font-weight for this class


def visible_text(event: SSAEvent) -> str:
    return event.plaintext.strip()


# ── Reading ──────────────────────────────────────────────────────────────────

def css_color_to_ass(value: str | None) -> str | None:
    """CSS color (#RRGGBB, #RGB, bare hex as real-world SMI often has, or a
    CSS color name) -> ASS "&HBBGGRR&"."""
    if not value:
        return None
    v = value.strip().strip("\"'").strip()
    if re.fullmatch(r"[0-9a-fA-F]{3}|[0-9a-fA-F]{6}", v):
        v = "#" + v
    try:
        rgb = webcolors.hex_to_rgb(v) if v.startswith("#") else webcolors.name_to_rgb(v)
    except ValueError:
        return None
    return f"&H{rgb.blue:02X}{rgb.green:02X}{rgb.red:02X}&"


@dataclass
class _Cue(SyncElement):
    track: str = ""


class TrackedSamiParser(SAMIParser):
    def __init__(self) -> None:
        super().__init__()
        self.sync_starts: list[int] = []
        self.css = ""
        self._sync_start: int | None = None
        self._font_colors: list[bool] = []
        self._in_style = False

    def begin_sync_element(self, start_ms: int) -> None:
        self.close_sync_element()
        self._sync_start = start_ms
        self.sync_starts.append(start_ms)
        self._font_colors.clear()
        self.current_sync_element = _Cue(start_ms=start_ms, text="")

    def _begin_paragraph(self, track: str) -> None:
        if self._sync_start is None:
            return
        self.close_sync_element()
        self._font_colors.clear()
        self.current_sync_element = _Cue(start_ms=self._sync_start, text="", track=track)

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "sync":
            m = re.search(r"\d+", a.get("start") or "")
            if m:
                self.begin_sync_element(int(m.group()))
        elif tag == "p":
            self._begin_paragraph((a.get("class") or "").strip().lower())
        elif tag == "font":
            color = css_color_to_ass(a.get("color"))
            if color:
                self.append_text("{\\c" + color + "}")
            self._font_colors.append(color is not None)
        elif tag == "style":
            self._in_style = True
        else:
            super().handle_starttag(tag, attrs)

    def handle_endtag(self, tag):
        if tag == "font":
            if self._font_colors and self._font_colors.pop():
                self.append_text("{\\c}")  # back to the style's color
        elif tag == "style":
            self._in_style = False
        else:
            if tag in ("body", "sami"):
                self._sync_start = None
            super().handle_endtag(tag)

    def handle_data(self, data):
        if self._in_style:
            self.css += data
        else:
            super().handle_data(data)


def _css_bold_rules(css: str) -> tuple[bool, dict[str, bool]]:
    """(default bold from the "P" rule, {class: bold}) from SAMI's <STYLE>."""
    default_bold = False
    class_bold: dict[str, bool] = {}
    for rule in tinycss2.parse_stylesheet(css, skip_comments=True, skip_whitespace=True):
        if rule.type != "qualified-rule":
            continue
        bold = None
        for decl in tinycss2.parse_declaration_list(rule.content, skip_comments=True, skip_whitespace=True):
            if decl.type == "declaration" and decl.lower_name == "font-weight":
                bold = _font_weight_is_bold(decl.value)
        if bold is None:
            continue
        for selector in tinycss2.serialize(rule.prelude).split(","):
            sel = selector.strip().lower()
            if sel.startswith("."):
                class_bold[sel[1:]] = bold
            elif sel == "p":
                default_bold = bold
    return default_bold, class_bold


def _font_weight_is_bold(tokens) -> bool | None:
    for token in tokens:
        if token.type == "ident":
            if token.lower_value in ("bold", "bolder"):
                return True
            if token.lower_value in ("normal", "lighter"):
                return False
        elif token.type == "number" and token.is_integer:
            return token.int_value >= 600
    return None


def _parse_sami(content: str) -> list[Track]:
    parser = TrackedSamiParser()
    parser.feed(content)
    parser.close()
    parser.close_sync_element()

    default_bold, class_bold = _css_bold_rules(parser.css)
    sync_starts = sorted(set(parser.sync_starts))

    def end_of(start: int) -> int:
        # A SAMI cue stays up until the next SYNC, whichever class it carries.
        for s in sync_starts:
            if s > start:
                return s
        return start + LAST_CUE_DURATION_MS

    tracks: dict[str, Track] = {}
    for cue in parser.sync_elements:
        track_name = getattr(cue, "track", "")
        lines = (line.strip() for line in cue.text.strip().splitlines())
        event = SSAEvent(start=cue.start_ms, end=end_of(cue.start_ms))
        event.plaintext = "\n".join(line for line in lines if line)
        if not visible_text(event):
            continue
        if track_name not in tracks:
            tracks[track_name] = Track(track_name, [], bold=class_bold.get(track_name, default_bold))
        tracks[track_name].events.append(event)

    for track in tracks.values():
        track.events.sort(key=lambda e: e.start)
    return list(tracks.values())


def load_tracks(ext: str, content: str) -> list[Track]:
    """Parses subtitle text into tracks of visible events, sorted by time."""
    if ext == ".smi":
        return _parse_sami(content)

    subs = SSAFile.from_string(content, format_="srt" if ext == ".srt" else "ass")
    events = [e for e in subs.events if not e.is_comment and not e.is_drawing and visible_text(e)]
    events.sort(key=lambda e: e.start)
    return [Track("", events, dict(subs.styles))] if events else []


# ── Writing ──────────────────────────────────────────────────────────────────

# ASS header for converted output: the same defaults FFmpeg writes when it
# converts SRT/SAMI to ASS (libavcodec/ass.h ASS_DEFAULT_*). The script
# resolution is fixed and the player scales it to whatever video it plays
# over, so the rendered size is the same share of the frame on every video.
ASS_PLAY_RES = (384, 288)
ASS_FONT_SIZE = 16


def _ass_style(bold: bool) -> SSAStyle:
    return SSAStyle(
        fontname=ASS_FONT_NAME, fontsize=ASS_FONT_SIZE,
        primarycolor=Color(255, 255, 255, 0), secondarycolor=Color(255, 255, 255, 0),
        outlinecolor=Color(0, 0, 0, 0), backcolor=Color(0, 0, 0, 0),
        bold=bold, borderstyle=1, outline=1, shadow=0, alignment=Alignment.BOTTOM_CENTER,
        marginl=10, marginr=10, marginv=10, encoding=0,
    )


def _copy_event(e: SSAEvent, style: str | None = None) -> SSAEvent:
    return SSAEvent(start=e.start, end=e.end, text=e.text, style=style or e.style)


def write_ass(tracks: list[Track]) -> str:
    subs = SSAFile()
    subs.info.update({
        "Title": "subConverter",
        "PlayResX": str(ASS_PLAY_RES[0]),
        "PlayResY": str(ASS_PLAY_RES[1]),
        "ScaledBorderAndShadow": "yes",
        "WrapStyle": "0",
    })
    subs.styles = {"Default": _ass_style(bold=False)}
    for track in tracks:
        style = "Default"
        if track.bold:
            style = "Bold"
            subs.styles.setdefault(style, _ass_style(bold=True))
        subs.events.extend(_copy_event(e, style) for e in track.events)
    subs.sort()
    return subs.to_string("ass")


_SRT_TOGGLE = re.compile(r"\\[ibus][01]")


def _srt_safe_text(text: str) -> str:
    """Keeps only the overrides SRT can express (italic/bold/underline/
    strikeout) and merges adjacent blocks; otherwise pysubs2 emits stray
    empty pairs like "<i></i>" around a dropped color tag."""
    text = re.sub(r"\{[^}]*\}", lambda m: "{" + "".join(_SRT_TOGGLE.findall(m.group())) + "}", text)
    return text.replace("}{", "").replace("{}", "")


def write_srt(tracks: list[Track]) -> str:
    subs = SSAFile()
    for track in tracks:
        subs.styles.update(track.styles)
        for e in track.events:
            event = _copy_event(e)
            event.text = _srt_safe_text(event.text)
            subs.events.append(event)
    subs.sort()
    return subs.to_string("srt")


_OVERRIDE_BLOCK = re.compile(r"(\{[^}]*\}|\\N|\\n)")
_TOGGLE_TAG = re.compile(r"\\([ibu])([01])")
_COLOR_TAG = re.compile(r"\\1?c(?:&H([0-9A-Fa-f]{1,8})&?)?")
_STYLE_KEYS = {"i": "italics", "b": "bold", "u": "underline"}


def _ass_color_to_css(bgr_hex: str) -> str:
    value = int(bgr_hex, 16) & 0xFFFFFF
    b, g, r = (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF
    return f"#{r:02x}{g:02x}{b:02x}"


def _event_to_caption(event: SSAEvent, base_style: SSAStyle | None) -> Caption:
    """ASS event text -> pycaption nodes, keeping italic/bold/underline/color."""
    state = {
        "italics": bool(base_style and base_style.italic),
        "bold": bool(base_style and base_style.bold),
        "underline": bool(base_style and base_style.underline),
    }
    nodes: list[CaptionNode] = []
    open_style: dict | None = None

    def sync_style():
        # Applied lazily right before text, so runs of override blocks with
        # nothing between them don't leave empty tags behind.
        nonlocal open_style
        content = {k: True for k in ("italics", "bold", "underline") if state[k]}
        if state.get("color"):
            content["color"] = state["color"]
        if content == (open_style or {}):
            return
        if open_style:
            nodes.append(CaptionNode.create_style(False, {}))
        open_style = content or None
        if content:
            nodes.append(CaptionNode.create_style(True, content))

    for part in _OVERRIDE_BLOCK.split(event.text):
        if not part:
            continue
        if part in ("\\N", "\\n"):
            nodes.append(CaptionNode.create_break())
        elif part.startswith("{"):
            for tag, on in _TOGGLE_TAG.findall(part):
                state[_STYLE_KEYS[tag]] = on == "1"
            for color in _COLOR_TAG.findall(part):
                state["color"] = _ass_color_to_css(color) if color else None
        else:
            sync_style()
            nodes.append(CaptionNode.create_text(part.replace("\\h", " ")))
    if open_style:
        nodes.append(CaptionNode.create_style(False, {}))
    return Caption(event.start * 1000, event.end * 1000, nodes)


def write_sami(tracks: list[Track], lang: str) -> str:
    captions = []
    for track in tracks:
        for e in track.events:
            captions.append(_event_to_caption(e, track.styles.get(e.style)))
    captions.sort(key=lambda c: c.start)
    caption_set = CaptionSet({_SAMI_LANG.get(lang, lang): CaptionList(captions)})
    return SAMIWriter().write(caption_set)


def write_tracks(target_ext: str, tracks: list[Track], lang: str) -> str:
    if target_ext == ".ass":
        return write_ass(tracks)
    if target_ext == ".smi":
        return write_sami(tracks, lang)
    return write_srt(tracks)

