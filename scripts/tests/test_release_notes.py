import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from release_notes import extract_release_notes, main


class ReleaseNotesTests(unittest.TestCase):
    def test_extracts_only_requested_version(self):
        changelog = """# Changelog

## 7.3.0

- 适配官方 Telegram Desktop 7.2.9。
- 修复视频播放问题。

## 7.2.9

- 旧版本功能。
"""
        self.assertEqual(
            extract_release_notes(changelog, "7.3.0"),
            "## 7.3.0\n\n- 适配官方 Telegram Desktop 7.2.9。\n- 修复视频播放问题。\n",
        )

    def test_subheadings_stay_inside_version_section(self):
        changelog = """## 7.3.0

### 修复

- 修复崩溃。

## 7.2.9

- 旧内容。
"""
        self.assertIn("### 修复", extract_release_notes(changelog, "7.3.0"))

    def test_missing_version_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "缺少版本 7.3.0"):
            extract_release_notes("## 7.2.9\n\n- 旧内容。\n", "7.3.0")

    def test_duplicate_version_is_rejected(self):
        changelog = "## 7.3.0\n\n- 第一处。\n\n## 7.3.0\n\n- 第二处。\n"
        with self.assertRaisesRegex(ValueError, "二级标题重复"):
            extract_release_notes(changelog, "7.3.0")

    def test_empty_version_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "发布说明为空"):
            extract_release_notes("## 7.3.0\n\n## 7.2.9\n\n- 旧内容。\n", "7.3.0")

    def test_cli_writes_only_selected_version(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            changelog = root / "CHANGELOG.md"
            output = root / "nested/release-notes.md"
            changelog.write_text(
                "## 7.3.0\n\n- 当前变化。\n\n## 7.2.9\n\n- 旧变化。\n",
                encoding="utf-8",
            )
            with patch.object(
                sys,
                "argv",
                ["release_notes.py", str(changelog), "7.3.0", str(output)],
            ):
                main()
            self.assertEqual(
                output.read_text(encoding="utf-8"),
                "## 7.3.0\n\n- 当前变化。\n",
            )


if __name__ == "__main__":
    unittest.main()
