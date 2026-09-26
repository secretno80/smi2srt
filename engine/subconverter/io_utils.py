"""File I/O helpers: encoding detection, long/UNC paths, collision-safe writes."""

import os

BOM_UTF8 = b"\xef\xbb\xbf"


def to_long_path(path: str) -> str:
    """Prefix long/UNC paths with the \\\\?\\ marker so I/O stays reliable
    near or beyond MAX_PATH and on \\\\server\\share paths."""
    path = os.path.abspath(path)
    if path.startswith("\\\\?\\"):
        return path
    if path.startswith("\\\\"):
        return "\\\\?\\UNC\\" + path[2:]
    if len(path) >= 248:
        return "\\\\?\\" + path
    return path


def decode_subtitle_bytes(data: bytes) -> str:
    """BOM (UTF-8/UTF-16) first, then strict UTF-8, then CP949 (the de facto
    encoding of legacy Korean SMI files), then the ANSI code page."""
    if data.startswith(BOM_UTF8):
        return data[3:].decode("utf-8", errors="replace")
    if data.startswith(b"\xff\xfe") or data.startswith(b"\xfe\xff"):
        return data.decode("utf-16", errors="replace")
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        pass
    try:
        return data.decode("cp949")
    except UnicodeDecodeError:
        return data.decode("mbcs", errors="replace")


def read_subtitle_text(path: str) -> str:
    with open(to_long_path(path), "rb") as f:
        return decode_subtitle_bytes(f.read())


def write_unique_utf8(candidate: str, text: str) -> str:
    """Writes |text| as UTF-8 with BOM to |candidate|, or to
    "stem (n).ext" if that name is taken. Exclusive-create makes the name
    pick atomic, so parallel workers can't overwrite each other's output.
    Returns the path actually written."""
    data = BOM_UTF8 + text.encode("utf-8")
    parent, name = os.path.split(candidate)
    stem, ext = os.path.splitext(name)
    for n in range(100000):
        path = candidate if n == 0 else os.path.join(parent, f"{stem} ({n}){ext}")
        try:
            with open(to_long_path(path), "xb") as f:
                f.write(data)
            return path
        except FileExistsError:
            continue
    raise OSError("사용 가능한 출력 파일명을 찾지 못했습니다.")


def to_crlf(text: str) -> str:
    return text.replace("\r\n", "\n").replace("\n", "\r\n")
