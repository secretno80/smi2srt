import os
import shutil
import tempfile
import unittest

import pysubs2

from subconverter.convert import convert_file
from subconverter.formats import ASS_FONT_SIZE, ASS_PLAY_RES, load_tracks
from subconverter.matching import build_bare_episode_map, find_matching_video

SAMPLES = os.path.join(os.path.dirname(__file__), "..", "..", "samples")

LATE_SMI = """<SAMI><BODY>
<SYNC Start=2700000><P Class=KRCC>사십오 분에 나오는 자막입니다
<SYNC Start=2702500><P Class=KRCC>&nbsp;
<SYNC Start=5400000><P Class=KRCC>한 시간 반에 나오는 자막입니다
<SYNC Start=5401000><P Class=KRCC>&nbsp;
</BODY></SAMI>
"""


def _write(folder, name, text, encoding="utf-8"):
    path = os.path.join(folder, name)
    with open(path, "w", encoding=encoding, newline="") as f:
        f.write(text)
    return path


def _read(path):
    with open(path, encoding="utf-8-sig", newline="") as f:
        return f.read()


class EngineTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def test_timestamps_past_40_minutes_are_exact(self):
        # The old engine divided by 2,400,000 ms per "hour", shifting every
        # caption after 40:00 (45:00 came out as 01:05:00).
        src = _write(self.dir, "late.smi", LATE_SMI)
        [srt] = convert_file(src, ".srt", {})
        self.assertIn("00:45:00,000 --> 00:45:02,500", _read(srt))
        self.assertIn("01:30:00,000 --> 01:30:01,000", _read(srt))

        [ass] = convert_file(src, ".ass", {})
        starts = [e.start for e in pysubs2.load(ass)]
        self.assertEqual(starts, [2700000, 5400000])

    def test_ass_uses_ffmpeg_default_header(self):
        src = _write(self.dir, "late.smi", LATE_SMI)
        [ass] = convert_file(src, ".ass", {})
        subs = pysubs2.load(ass)
        self.assertEqual((subs.info["PlayResX"], subs.info["PlayResY"]), tuple(map(str, ASS_PLAY_RES)))
        self.assertEqual(subs.styles["Default"].fontsize, ASS_FONT_SIZE)

    def test_multilang_smi_splits_by_class_and_detects_language(self):
        src = os.path.join(SAMPLES, "sample_multilang.smi")
        shutil.copy(src, self.dir)
        written = convert_file(os.path.join(self.dir, "sample_multilang.smi"), ".srt", {})
        names = sorted(os.path.basename(p) for p in written)
        self.assertEqual(names, ["sample_multilang.en.srt", "sample_multilang.ko.srt"])
        self.assertNotIn("Hello", _read(os.path.join(self.dir, "sample_multilang.ko.srt")))

    def test_credits_removed_from_both_ends(self):
        shutil.copy(os.path.join(SAMPLES, "sample_with_credit.smi"), self.dir)
        [srt] = convert_file(os.path.join(self.dir, "sample_with_credit.smi"), ".srt", {})
        text = _read(srt).lower()
        self.assertNotIn("smi by", text)
        self.assertNotIn("sub by", text)

    def test_same_format_keeps_content_and_adds_language_tag(self):
        body = ("1\r\n00:00:01,000 --> 00:00:02,000\r\nsub by someone\r\n\r\n"
                "2\r\n00:00:03,000 --> 00:00:04,000\r\n안녕하세요 반갑습니다\r\n\r\n"
                "3\r\n00:00:05,000 --> 00:00:06,000\r\n<i>두 번째</i> 대사입니다\r\n")
        src = _write(self.dir, "Movie.srt", body)
        [out] = convert_file(src, ".srt", {})
        self.assertEqual(os.path.basename(out), "Movie.ko.srt")
        # Only the credit block is cut; the blank line that separated it stays.
        self.assertEqual(_read(out), "\r\n" + body[body.index("\r\n2\r\n"):])

    def test_same_format_smi_multilang_uses_first_track_language(self):
        shutil.copy(os.path.join(SAMPLES, "sample_multilang.smi"), self.dir)
        [out] = convert_file(os.path.join(self.dir, "sample_multilang.smi"), ".smi", {})
        self.assertEqual(os.path.basename(out), "sample_multilang.ko.smi")
        with open(os.path.join(SAMPLES, "sample_multilang.smi"), "rb") as f:
            original = f.read().decode("utf-8-sig")
        self.assertEqual(_read(out), original)

    def test_existing_language_tag_is_replaced(self):
        src = _write(self.dir, "Show.en.smi", LATE_SMI)
        [out] = convert_file(src, ".srt", {})
        self.assertEqual(os.path.basename(out), "Show.ko.srt")

    def test_cp949_smi_is_decoded(self):
        src = _write(self.dir, "legacy.smi", LATE_SMI, encoding="cp949")
        self.assertEqual(len(load_tracks(".smi", open(src, encoding="cp949").read())[0].events), 2)
        [srt] = convert_file(src, ".srt", {})
        self.assertIn("사십오 분", _read(srt))

    def test_font_color_survives_smi_to_ass(self):
        smi = '<SAMI><BODY><SYNC Start=1000><P Class=KRCC><font color="#FF0000">빨간</font> 글씨\n</BODY></SAMI>'
        src = _write(self.dir, "c.smi", smi)
        [ass] = convert_file(src, ".ass", {})
        self.assertIn(r"{\c&H0000FF&}빨간{\c}", pysubs2.load(ass)[0].text)

    def test_ass_to_smi_round_trip(self):
        src = _write(self.dir, "late.smi", LATE_SMI)
        [ass] = convert_file(src, ".ass", {})
        [smi] = convert_file(ass, ".smi", {})
        self.assertEqual([e.start for t in load_tracks(".smi", _read(smi)) for e in t.events], [2700000, 5400000])

    def test_bare_episode_numbers_match_videos(self):
        for n in range(5, 8):
            _write(self.dir, f"쇼.{n:02d}.smi", LATE_SMI)
        for n in range(1, 10):
            _write(self.dir, f"RandomTitle_{n:02d}_1080p.mkv", "")
        subs = [os.path.join(self.dir, f"쇼.{n:02d}.smi") for n in range(5, 8)]
        episodes = build_bare_episode_map(subs)
        video = find_matching_video(subs[1], episodes)
        self.assertEqual(os.path.basename(video), "RandomTitle_06_1080p.mkv")


class MatchFixtureTest(unittest.TestCase):
    def test_case_folders(self):
        base = os.path.join(SAMPLES, "..", "test_match")
        cases = {
            ("case1", "Movie.2001.RARBG.smi"): "Movie.2001.2160p.BluRay.x265.mkv",
            ("case2", "Show.E05.RARBG.smi"): "Show.S01E05.1080p.WEB.x264.mkv",
            ("case4", "NoVideo.smi"): None,
        }
        for (folder, sub), expected in cases.items():
            video = find_matching_video(os.path.join(base, folder, sub), {})
            self.assertEqual(os.path.basename(video) if video else None, expected, sub)


if __name__ == "__main__":
    unittest.main()
