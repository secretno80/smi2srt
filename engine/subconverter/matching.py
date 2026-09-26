"""Matches a subtitle to the video file in the same folder it belongs to,
by year, episode number and filename-token overlap."""

import os
import re
from collections import defaultdict
from typing import Iterable

SUBTITLE_EXTS = {".smi", ".srt", ".ass"}
VIDEO_EXTS = {".mp4", ".mkv", ".avi", ".mov", ".wmv", ".m4v", ".ts", ".m2ts", ".webm", ".mpg", ".mpeg"}

_NOISE_TOKENS = {
    "1080p", "2160p", "720p", "480p", "x264", "x265", "h264", "h265", "hevc", "av1",
    "webrip", "web", "webdl", "bluray", "brrip", "dvdrip", "hdr", "uhd", "10bit", "8bit",
    "aac", "dts", "truehd", "atmos", "proper", "repack", "remux", "yts", "rarbg",
}

EpisodeMap = dict[str, int]


def ext_of(path: str) -> str:
    return os.path.splitext(path)[1].lower()


def stem_of(path: str) -> str:
    return os.path.splitext(os.path.basename(path))[0]


def path_key(path: str) -> str:
    """Case/separator-insensitive key so the same file reached through two
    different scans (argv vs. directory listing) compares equal."""
    return os.path.normcase(os.path.abspath(path))


def _normalize_name(value: str) -> str:
    return re.sub(r"[^0-9a-z]+", " ", value.lower())


def _meaningful_tokens(stem: str) -> set[str]:
    return {t for t in _normalize_name(stem).split() if len(t) > 1 and t not in _NOISE_TOKENS}


def _extract_year(stem: str) -> int:
    m = re.search(r"(19\d{2}|20\d{2})", _normalize_name(stem))
    return int(m.group(1)) if m else -1


def _extract_episode(stem: str) -> int:
    normalized = _normalize_name(stem)
    for pattern in (r"\bs\d{1,2}\s*e\s*(\d{1,3})\b", r"\bep\s*(\d{1,3})\b", r"\be\s*(\d{1,3})\b"):
        m = re.search(pattern, normalized)
        if m:
            return int(m.group(1))
    return -1


def _number_sequence(stem: str) -> list[int]:
    return [int(d) for d in re.findall(r"\d+", _normalize_name(stem))]


def infer_batch_episode_numbers(files: list[str]) -> EpisodeMap:
    """For a complete batch of same-template filenames (every subtitle or
    every video in a folder), finds the digit-run "column" that is the
    episode number: the left-most position whose values across all files
    form a run of len(files) consecutive integers (any start — 05~10 is as
    valid as 01~06). Constant columns such as resolution or year never
    qualify. Returns {} if the filenames don't share one numeric shape."""
    if len(files) < 2:
        return {}
    numbers = [_number_sequence(stem_of(f)) for f in files]
    width = len(numbers[0])
    if width == 0 or any(len(n) != width for n in numbers):
        return {}

    for col in range(width):
        values = [n[col] for n in numbers]
        ordered = sorted(values)
        if all(b == a + 1 for a, b in zip(ordered, ordered[1:])):
            return {path_key(f): v for f, v in zip(files, values)}
    return {}


def build_bare_episode_map(batch_files: Iterable[str]) -> EpisodeMap:
    """Batch-wide fallback episode numbers for filenames with a bare digit
    and no S/E/EP marker. Subtitles are only inferred when the batch covers
    every subtitle in that folder (a partial selection breaks the
    consecutive-run assumption); videos are always inferred from the folder."""
    result: EpisodeMap = {}
    selected_by_dir: dict[str, list[str]] = defaultdict(list)
    for f in batch_files:
        selected_by_dir[os.path.dirname(os.path.abspath(f))].append(f)

    for folder, selected in selected_by_dir.items():
        try:
            entries = [os.path.join(folder, n) for n in os.listdir(folder)]
        except OSError:
            continue
        files = [p for p in entries if os.path.isfile(p)]
        subs = [p for p in files if ext_of(p) in SUBTITLE_EXTS]
        videos = [p for p in files if ext_of(p) in VIDEO_EXTS]

        if len(selected) == len(subs):
            result.update(infer_batch_episode_numbers(subs))
        result.update(infer_batch_episode_numbers(videos))
    return result


def _resolve_episode(path: str, bare_episodes: EpisodeMap) -> int:
    explicit = _extract_episode(stem_of(path))
    if explicit > 0:
        return explicit
    return bare_episodes.get(path_key(path), -1)


def name_score(subtitle: str, video: str, bare_episodes: EpisodeMap) -> int:
    sub_stem, vid_stem = stem_of(subtitle), stem_of(video)
    if sub_stem.lower() == vid_stem.lower():
        return 1000

    score = 0
    sub_year, vid_year = _extract_year(sub_stem), _extract_year(vid_stem)
    if sub_year > 0 and vid_year > 0:
        score += 120 if sub_year == vid_year else -80

    sub_ep = _resolve_episode(subtitle, bare_episodes)
    vid_ep = _resolve_episode(video, bare_episodes)
    if sub_ep > 0 and vid_ep > 0:
        score += 180 if sub_ep == vid_ep else -120

    overlap = len(_meaningful_tokens(sub_stem) & _meaningful_tokens(vid_stem))
    score += overlap * 12
    if overlap == 0 and sub_year < 0 and sub_ep < 0:
        score -= 30
    return score


def find_matching_video(subtitle: str, bare_episodes: EpisodeMap) -> str | None:
    """The best-matching video in the subtitle's folder, or None when there
    is no video or the best candidate isn't clearly ahead of the runner-up."""
    folder = os.path.dirname(os.path.abspath(subtitle))
    try:
        videos = [os.path.join(folder, n) for n in os.listdir(folder)
                  if ext_of(n) in VIDEO_EXTS and os.path.isfile(os.path.join(folder, n))]
    except OSError:
        return None

    if not videos:
        return None
    if len(videos) == 1:
        return videos[0]

    scored = sorted(((name_score(subtitle, v, bare_episodes), v) for v in videos),
                    key=lambda sv: sv[0], reverse=True)
    best_score, best = scored[0]
    second_score = scored[1][0]
    if best_score >= 24 and (best_score - second_score >= 12 or second_score < 0):
        return best
    return None
