"""Per-file conversion: parse (library) -> subConverter rules -> write (library)."""

import os
from collections import defaultdict

from .credits import credit_bounds
from .formats import Track, load_tracks, visible_text, write_tracks
from .io_utils import read_subtitle_text, to_crlf, write_unique_utf8
from .language import UNDETERMINED, detect_track_language, strip_lang_suffix, tagged_name
from .matching import EpisodeMap, ext_of, find_matching_video, stem_of
from .rawedit import remove_credits_raw


class ConvertError(Exception):
    pass


def _track_language(track: Track) -> str:
    return detect_track_language([visible_text(e) for e in track.events])


def _primary_language(tracks: list[Track]) -> str:
    """For a same-format file (which can't be split per language): the
    language of the first track that has one — the track SAMI players show
    by default. Counting characters would favor Latin text, which spends
    more letters per word than Hangul."""
    for track in tracks:
        lang = _track_language(track)
        if lang != UNDETERMINED:
            return lang
    return UNDETERMINED


def _parse(ext: str, content: str) -> list[Track]:
    try:
        return load_tracks(ext, content)
    except Exception as exc:  # malformed input the library rejects
        raise ConvertError(f"자막을 해석하지 못했습니다. ({exc})") from exc


def _output_stem(input_path: str, bare_episodes: EpisodeMap) -> str:
    video = find_matching_video(input_path, bare_episodes)
    return strip_lang_suffix(stem_of(video or input_path))


def convert_file(input_path: str, target_ext: str, bare_episodes: EpisodeMap) -> list[str]:
    """Converts one subtitle file; returns the paths written."""
    try:
        content = read_subtitle_text(input_path)
    except OSError as exc:
        raise ConvertError(f"파일을 읽을 수 없습니다. ({exc.strerror or exc})") from exc
    if not content.strip():
        raise ConvertError("파일 인코딩을 해석하지 못했습니다.")

    input_ext = ext_of(input_path)
    folder = os.path.dirname(os.path.abspath(input_path))

    if not _parse(input_ext, content):
        raise ConvertError("변환 가능한 자막 구간을 찾지 못했습니다.")

    # Same format: only leading/trailing credits are cut from the raw text
    # (everything else survives byte-for-byte); the name gains the language
    # tag detected from the remaining text.
    if input_ext == target_ext:
        new_content = remove_credits_raw(input_ext, content)
        remaining = _parse(input_ext, new_content)
        if not remaining:
            raise ConvertError("크레딧(제작자 정보) 제거 후 변환 가능한 자막이 없습니다.")
        name = tagged_name(_output_stem(input_path, bare_episodes), _primary_language(remaining), target_ext)
        return [write_unique_utf8(os.path.join(folder, name), new_content)]

    tracks = []
    for track in _parse(input_ext, content):
        begin, end = credit_bounds(track.events, visible_text)
        track.events = track.events[begin:end]
        if track.events:
            tracks.append(track)
    if not tracks:
        raise ConvertError("크레딧(제작자 정보) 제거 후 변환 가능한 자막이 없습니다.")

    by_lang: dict[str, list[Track]] = defaultdict(list)
    for track in tracks:
        by_lang[_track_language(track)].append(track)

    stem = _output_stem(input_path, bare_episodes)
    written = []
    for lang, group in by_lang.items():
        text = to_crlf(write_tracks(target_ext, group, lang))
        written.append(write_unique_utf8(os.path.join(folder, tagged_name(stem, lang, target_ext)), text))
    return written
