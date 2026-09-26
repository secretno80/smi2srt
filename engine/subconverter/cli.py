"""Command line entry point.

    subConverterEngine --to srt [--list FILE] [--result FILE] [FILES...]

--list is a UTF-8 file with one input path per line (used by the Explorer
front end, which collects a multi-selection first). --result receives one
UTF-8 line per input: "OK<TAB>name" or "FAIL<TAB>name<TAB>reason".
Exit code: 0 all succeeded, 1 some failed, 2 usage error.
"""

import argparse
import os
import sys
from concurrent.futures import ThreadPoolExecutor

from .convert import ConvertError, convert_file
from .matching import SUBTITLE_EXTS, build_bare_episode_map, ext_of


def _worker_count(file_count: int) -> int:
    count = min(os.cpu_count() or 4, file_count)
    env = os.environ.get("SUBCONVERTER_THREADS") or os.environ.get("SMI2SRT_THREADS")
    if env:
        try:
            if int(env) > 0:
                count = min(int(env), file_count)
        except ValueError:
            pass
    return max(1, count)


def _convert_one(path: str, target_ext: str, bare_episodes) -> tuple[str, str]:
    try:
        convert_file(path, target_ext, bare_episodes)
        return "OK", ""
    except ConvertError as exc:
        return "FAIL", str(exc)
    except OSError as exc:
        return "FAIL", f"변환 파일 저장에 실패했습니다. ({exc.strerror or exc})"
    except Exception as exc:  # keep one bad file from aborting the batch
        return "FAIL", f"알 수 없는 오류: {exc}"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="subConverterEngine")
    parser.add_argument("--to", required=True, choices=["smi", "srt", "ass"])
    parser.add_argument("--list", dest="list_file")
    parser.add_argument("--result", dest="result_file")
    parser.add_argument("files", nargs="*")
    args = parser.parse_args(argv)

    files = list(args.files)
    if args.list_file:
        with open(args.list_file, encoding="utf-8-sig") as f:
            files += [line.strip() for line in f if line.strip()]
    files = [f for f in dict.fromkeys(files) if ext_of(f) in SUBTITLE_EXTS]
    if not files:
        return 2

    target_ext = "." + args.to
    bare_episodes = build_bare_episode_map(files)
    with ThreadPoolExecutor(_worker_count(len(files))) as pool:
        results = list(pool.map(lambda f: _convert_one(f, target_ext, bare_episodes), files))

    lines = []
    for path, (status, message) in zip(files, results):
        name = os.path.basename(path)
        lines.append(f"{status}\t{name}" + (f"\t{message}" if message else ""))
    report = "\n".join(lines) + "\n"
    if args.result_file:
        with open(args.result_file, "w", encoding="utf-8") as f:
            f.write(report)
    else:
        sys.stdout.buffer.write(report.encode("utf-8"))

    return 0 if all(status == "OK" for status, _ in results) else 1
