"""Same-format conversion (e.g. .smi -> .smi) must keep every surviving byte
of the source as-is, so instead of parse-and-rebuild these helpers locate
each caption's raw span in the original text and cut out only the leading/
trailing credit captions."""

import html
import re

from pysubs2 import SSAEvent

from .credits import credit_bounds


def _excise(content: str, spans: list[tuple[int, int]]) -> str:
    out, cursor = [], 0
    for start, end in sorted(spans):
        start, end = max(start, cursor), max(end, cursor)
        out.append(content[cursor:start])
        cursor = end
    out.append(content[cursor:])
    return "".join(out)


def _plain_lines(text: str) -> str:
    return "\n".join(line.strip() for line in text.splitlines() if line.strip())


def _html_to_plain(fragment: str) -> str:
    fragment = re.sub(r"<br\s*/?>", "\n", fragment, flags=re.IGNORECASE)
    fragment = re.sub(r"<[^>]+>", "", fragment)
    return _plain_lines(html.unescape(fragment).replace(" ", " "))


def _credit_spans(items: list[tuple[int, int, str]]) -> list[tuple[int, int]]:
    begin, end = credit_bounds(items, lambda item: item[2])
    return [(s, e) for s, e, _ in items[:begin] + items[end:]]


_SRT_BLOCK = re.compile(
    r"(?:\A|\r?\n)\s*\d+\s*\r?\n\s*(\d{2}:\d{2}:\d{2},\d{3})\s*-->\s*(\d{2}:\d{2}:\d{2},\d{3})[^\r\n]*\r?\n"
    r"([\s\S]*?)(?=\r?\n\s*\r?\n|\Z)")


def remove_srt_credits(content: str) -> str:
    items = [(m.start(), m.end(), _plain_lines(m.group(3))) for m in _SRT_BLOCK.finditer(content)]
    return _excise(content, _credit_spans([i for i in items if i[2]]))


def remove_ass_credits(content: str) -> str:
    items = []
    in_events = False
    fields: list[str] = []
    pos = 0
    for line in content.splitlines(keepends=True):
        start, pos = pos, pos + len(line)
        stripped = line.strip()
        lowered = stripped.lower()
        if lowered.startswith("["):
            in_events = lowered == "[events]"
        elif in_events and lowered.startswith("format:"):
            fields = [f.strip().lower() for f in stripped[7:].split(",")]
        elif in_events and lowered.startswith("dialogue:"):
            count = len(fields) or 10
            text_idx = fields.index("text") if "text" in fields else 9
            parts = stripped[9:].split(",", count - 1)
            if len(parts) == count and text_idx < count:
                text = SSAEvent(text=parts[text_idx]).plaintext.strip()
                if text:
                    items.append((start, pos, text))
    return _excise(content, _credit_spans(items))


_SMI_SYNC = re.compile(r"<sync[^>]*start\s*=\s*[\"']?\d+[^>]*>", re.IGNORECASE)
_SMI_P = re.compile(r"<p[^>]*class\s*=\s*[\"']?[^\"'\s>]+[^>]*>([\s\S]*?)(?=<p[^>]*>|\Z)", re.IGNORECASE)
_SMI_CLOSING = re.compile(r"</\s*(?:body|sami)\b[^>]*>", re.IGNORECASE)


def remove_smi_credits(content: str) -> str:
    syncs = list(_SMI_SYNC.finditer(content))
    items: list[tuple[int, int, str, int]] = []  # start, end, text, sync index

    def clamp(start: int, end: int) -> int:
        # The last block runs to EOF; never let a caption span swallow the
        # structural </BODY></SAMI> markup.
        m = _SMI_CLOSING.search(content, start, end)
        return m.start() if m else end

    for i, sync in enumerate(syncs):
        block_start = sync.end()
        block_end = syncs[i + 1].start() if i + 1 < len(syncs) else len(content)
        block = content[block_start:block_end]
        added = False
        for p in _SMI_P.finditer(block):
            text = _html_to_plain(p.group(1))
            if text:
                start = block_start + p.start()
                items.append((start, clamp(start, block_start + p.end()), text, i))
                added = True
        if not added:
            text = _html_to_plain(block)
            if text:
                items.append((block_start, clamp(block_start, block_end), text, i))

    begin, end = credit_bounds(items, lambda item: item[2])
    removed = items[:begin] + items[end:]
    spans = [(s, e) for s, e, _, _ in removed]

    # Drop the <SYNC> tag itself once every caption in it was a credit.
    for sync_index in {r[3] for r in removed}:
        total = sum(1 for it in items if it[3] == sync_index)
        if total == sum(1 for r in removed if r[3] == sync_index):
            spans.append((syncs[sync_index].start(), syncs[sync_index].end()))
    return _excise(content, spans)


def remove_credits_raw(ext: str, content: str) -> str:
    if ext == ".smi":
        return remove_smi_credits(content)
    if ext == ".srt":
        return remove_srt_credits(content)
    if ext == ".ass":
        return remove_ass_credits(content)
    return content
