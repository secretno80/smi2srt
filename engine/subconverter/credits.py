"""Rule-based detection of subtitle-author credit lines ("smi by ...",
"자막: ...", e-mail addresses, ...) at the start/end of a subtitle."""

import re
from typing import Callable, Sequence, TypeVar

T = TypeVar("T")

_EMAIL = re.compile(r"[a-z0-9._%+-]+@[a-z0-9.-]+\.[a-z]{2,}", re.IGNORECASE)

_CREDIT_TOKENS = (
    "smi by", "sub by", "subtitle by", "sync by", "sync correction by",
    "sync corrections by", "correction by", "corrections by", "provided by",
    "modify by", "modified by", "converted by", "conversion by", "encoded by",
    "edited by", "ripped by", "downloaded from", "download from", "ripped from",
    "번역 by", "자막 by", "제작 by", "싱크 by", "수정 by", "변환 by", "제공 by",
    "자막제작", "한글자막",
)

# Translation / sync / review / production keywords: any caption near the
# start/end that contains one is treated as a credit. Korean ones are matched
# with spaces removed, so "자막 제작" or "번 역" count as well.
_COMPACT_CREDIT_TOKENS = ("번역", "싱크", "검수", "제작")
_CREDIT_KEYWORDS = re.compile(
    r"\b(translat\w*|sync\w*|proofread\w*|review(?:ed|er|ers)?|qc"
    r"|(?:made|created|produced)\s+by)\b")

# Credit lines are searched for among this many captions at each end.
CREDIT_SCAN_COUNT = 30

_CREDIT_PATTERNS = (
    re.compile(r"\b(sync|timing|correction|corrections|modify|modified|provided|converted|conversion"
               r"|subtitle|encode|encoded|edit|edited|rip|ripped)\s+by\b"),
    re.compile(r"(제작|수정|변환|싱크|제공|번역)\s*by\b"),
)

# Lines that open with a "자막:"/"번역:"/"subtitle:" style label are almost
# always a standalone credit line, not dialogue.
_LABEL_COLON = re.compile(
    r"^\s*(자막|번역|제작|싱크|수정|변환|제공|sub|smi|srt|ass|subtitle|translat\w*|encode\w*|sync|rip\w*)\s*[:：]",
    re.IGNORECASE)

_ACTION_TOKENS = (
    "변환", "제작", "수정", "싱크", "번역", "자막", "제공", "전달",
    "sub", "smi", "srt", "ass", "subtitle", "convert", "conversion",
    "encode", "encoded", "sync", "translat", "rip",
)


def _normalize(text: str) -> str:
    return re.sub(r"[^0-9a-z가-힣]+", " ", text.lower()).strip()


def is_credit_text(text: str) -> bool:
    normalized = _normalize(text)
    if not normalized:
        return False

    raw_lower = text.strip().lower()
    if _EMAIL.search(raw_lower):
        return True
    if any(token in normalized for token in _CREDIT_TOKENS):
        return True
    compact = normalized.replace(" ", "")
    if any(token in compact for token in _COMPACT_CREDIT_TOKENS):
        return True
    if _CREDIT_KEYWORDS.search(normalized):
        return True
    if any(p.search(normalized) for p in _CREDIT_PATTERNS):
        return True
    if _LABEL_COLON.search(raw_lower):
        return True

    # Short lines combining two credit-action words without "by" or a colon
    # (e.g. "SUB 변환 Jone Dow") are also very likely to be a credit line.
    if len(normalized) <= 40:
        hits = sum(1 for token in _ACTION_TOKENS if token in normalized)
        if hits >= 2:
            return True
    return False


def credit_indices(items: Sequence[T], text_of: Callable[[T], str]) -> set[int]:
    """Indices of the credit lines among the first and last CREDIT_SCAN_COUNT
    items, even when dialogue sits between them. Credits further into the
    middle are left alone."""
    window = set(range(min(CREDIT_SCAN_COUNT, len(items))))
    window |= set(range(max(0, len(items) - CREDIT_SCAN_COUNT), len(items)))
    return {i for i in window if is_credit_text(text_of(items[i]))}
