"""Text-based language detection for a subtitle track and the matching
filename tag (".ko"/".en"/".jp")."""

import os
from typing import Sequence

UNDETERMINED = "und"
KNOWN_LANG_TAGS = ("ko", "en", "jp")


def guess_lang_from_text(text: str) -> str:
    ko = en = jp = 0
    for ch in text:
        code = ord(ch)
        if 0xAC00 <= code <= 0xD7A3:
            ko += 1
        elif ("a" <= ch <= "z") or ("A" <= ch <= "Z"):
            en += 1
        elif 0x3040 <= code <= 0x30FF or 0x31F0 <= code <= 0x31FF:
            jp += 1

    if ko > 0 and ko >= en and ko >= jp:
        return "ko"
    if en > 0 and en >= ko and en >= jp:
        return "en"
    if jp > 0:
        return "jp"
    return UNDETERMINED


def detect_track_language(texts: Sequence[str]) -> str:
    """One verdict per track rather than per line, so a stray foreign line
    can't split a single-language file into extra one-line outputs. Samples
    ~200 chars starting mid-track (skipping atypical opening lines); falls
    back to the whole track when that sample is too thin."""
    sample = ""
    for text in texts[len(texts) // 2:]:
        if len(sample) >= 200:
            break
        sample += text
    if len(sample) < 20:
        sample = "".join(texts)
    return guess_lang_from_text(sample)


def strip_lang_suffix(stem: str) -> str:
    """"Show.ko" -> "Show" (loops over stacked tags), so re-converting an
    already-tagged file doesn't produce "Show.ko.ko.srt"."""
    while True:
        base, dot_ext = os.path.splitext(stem)
        if not dot_ext or dot_ext[1:].lower() not in KNOWN_LANG_TAGS:
            return stem
        stem = base


def tagged_name(stem: str, lang: str, ext: str) -> str:
    if lang and lang != UNDETERMINED:
        return f"{stem}.{lang}{ext}"
    return stem + ext
