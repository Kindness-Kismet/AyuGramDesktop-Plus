import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from build_support import version as version_module
from build_support.version import apply_version, parse_version


ROOT = Path(__file__).resolve().parents[2]


class VersionEncodingTests(unittest.TestCase):
    def test_revision_keeps_official_code_and_advances_update_code(self):
        base = parse_version("7.2.9")
        revision = parse_version("7.2.9.1")
        next_revision = parse_version("7.2.9.2")
        next_official = parse_version("7.2.10")

        self.assertEqual(revision.full, 7_002_009)
        self.assertEqual(revision.update, 70_200_901)
        self.assertEqual(revision.text, "7.2.9")
        self.assertEqual(revision.text_small, "7.2.9.1")
        self.assertEqual(revision.file_version, "7.2.9.1")
        self.assertLess(7_002_009, revision.update)
        self.assertLess(base.update, revision.update)
        self.assertLess(revision.update, next_revision.update)
        self.assertLess(next_revision.update, next_official.update)

    def test_field_limits_fill_packer_ceiling_without_overflow(self):
        maximum = parse_version("99.99.999.99")
        self.assertEqual(maximum.update, 999_999_999)
        for invalid in (
            "100.0.0",
            "1.100.0",
            "1.0.1000",
            "1.0.0.100",
            "1.0.0.0",
            "0.0.0",
            "01.0.0",
            "1.00.0",
            "1.0",
        ):
            with self.subTest(invalid=invalid), self.assertRaises(SystemExit):
                parse_version(invalid)

    def test_storage_read_ceiling_is_limited_to_historical_test_build(self):
        self.assertEqual(parse_version("7.2.9.1").storage_read, 7_002_010)
        self.assertEqual(parse_version("7.2.10").storage_read, 7_002_010)
        self.assertEqual(parse_version("7.2.11").storage_read, 7_002_011)

    def test_beta_remains_separate_from_numeric_revision(self):
        beta = parse_version("7.2.9.beta")
        self.assertTrue(beta.beta)
        self.assertEqual(beta.revision, 0)
        self.assertEqual(beta.text_small, "7.2.9")


class VersionApplicationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.version_file = self.root / "Telegram/build/version"
        self.header = self.root / "Telegram/SourceFiles/core/version.h"
        self.telegram_rc = self.root / "Telegram/Resources/winrc/Telegram.rc"
        self.updater_rc = self.root / "Telegram/Resources/winrc/Updater.rc"
        self.manifest = self.root / "Telegram/Resources/uwp/AppX/AppxManifest.xml"
        self.upstream = self.root / ".github/upstream.json"
        self.changelog = self.root / "changelog.txt"
        for path in (
            self.version_file,
            self.header,
            self.telegram_rc,
            self.updater_rc,
            self.manifest,
            self.upstream,
            self.changelog,
        ):
            path.parent.mkdir(parents=True, exist_ok=True)

        self.version_file.write_text(
            """AppVersion 1
AppUpdateVersion 1
AppStorageReadVersion 1
AppVersionStrMajor 1.0
AppVersionStrOfficial 1.0.0
AppVersionStrSmall 1.0.0
AppVersionStrFile 1.0.0.0
AppVersionStr 1.0.0
BetaChannel 0
AlphaVersion 0
AppVersionOriginal 1.0.0
""",
            encoding="utf-8",
        )
        self.header.write_text(
            """#define TDESKTOP_REQUESTED_ALPHA_VERSION (0ULL)
constexpr auto AppVersion = 1;
constexpr auto AppUpdateVersion = 1;
constexpr auto AppStorageReadVersion = 1;
constexpr auto AppVersionStr = "1.0.0";
constexpr auto AppBetaVersion = false;
""",
            encoding="utf-8",
        )
        resource = """FILEVERSION 1,0,0,0
PRODUCTVERSION 1,0,0,0
VALUE "FileVersion", "1.0.0.0"
VALUE "ProductVersion", "1.0.0.0"
"""
        self.telegram_rc.write_text(resource, encoding="utf-8")
        self.updater_rc.write_text(resource, encoding="utf-8")
        self.manifest.write_text('<Identity Version="1.0.0.0" />\n', encoding="utf-8")
        self.upstream.write_text(json.dumps({"tdesktop": {"version": "7.2.9"}}), encoding="utf-8")
        self.changelog.write_text("7.2.9.2 (20.09.26)\n- test\n", encoding="utf-8")

    def locations(self):
        return patch.multiple(
            version_module,
            ROOT=self.root,
            VERSION_FILE=self.version_file,
            _CORE_VERSION=self.header,
            _TELEGRAM_RC=self.telegram_rc,
            _UPDATER_RC=self.updater_rc,
            _APPX_MANIFEST=self.manifest,
            _UPSTREAM_TRACKING=self.upstream,
            _CHANGELOG=self.changelog,
        )

    def test_apply_updates_numeric_display_and_platform_versions(self):
        with self.locations():
            touched = apply_version(parse_version("7.2.9.2"))

        self.assertEqual(len(touched), 5)
        values = dict(line.split() for line in self.version_file.read_text(encoding="utf-8").splitlines())
        self.assertEqual(values["AppVersion"], "7002009")
        self.assertEqual(values["AppUpdateVersion"], "70200902")
        self.assertEqual(values["AppStorageReadVersion"], "7002010")
        self.assertEqual(values["AppVersionStr"], "7.2.9.2")
        self.assertEqual(values["AppVersionStrFile"], "7.2.9.2")
        self.assertIn('Version="7.2.9.2"', self.manifest.read_text(encoding="utf-8"))
        self.assertIn("FILEVERSION 7,2,9,2", self.telegram_rc.read_text(encoding="utf-8"))
        self.assertIn("constexpr auto AppUpdateVersion = 70200902;", self.header.read_text(encoding="utf-8"))

    def test_apply_rejects_release_for_another_official_baseline(self):
        with self.locations(), self.assertRaisesRegex(SystemExit, "official baseline 7.2.9"):
            apply_version(parse_version("7.2.10"), check_changelog=False)


class VersionIntegrationTests(unittest.TestCase):
    @staticmethod
    def current():
        values = dict(
            line.split()
            for line in (ROOT / "Telegram/build/version").read_text(encoding="utf-8").splitlines()
            if line.strip()
        )
        return values, parse_version(values["AppVersionOriginal"])

    def test_checked_in_cli_reports_revision_and_update_code(self):
        _, current = self.current()
        result = subprocess.run(
            [sys.executable, "scripts/build_support/version.py"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        self.assertIn(current.original, result.stdout)
        self.assertIn(str(current.update), result.stdout)

    def test_checked_in_metadata_matches_version_and_official_baseline(self):
        values, current = self.current()
        upstream = json.loads((ROOT / ".github/upstream.json").read_text(encoding="utf-8"))["tdesktop"]
        self.assertEqual(current.text, upstream["version"])
        self.assertEqual(values["AppVersion"], str(current.full))
        self.assertEqual(values["AppUpdateVersion"], str(current.update))
        self.assertEqual(values["AppStorageReadVersion"], str(current.storage_read))
        self.assertEqual(values["AppVersionStr"], current.text_small)
        self.assertEqual(values["AppVersionStrFile"], current.file_version)

        header = (ROOT / "Telegram/SourceFiles/core/version.h").read_text(encoding="utf-8")
        self.assertRegex(header, rf"constexpr auto AppVersion = {current.full};")
        self.assertRegex(header, rf"constexpr auto AppUpdateVersion = {current.update};")
        self.assertIn(f'constexpr auto AppVersionStr = "{current.text_small}";', header)

        dotted = re.escape(current.file_version)
        for resource in ("Telegram.rc", "Updater.rc"):
            text = (ROOT / "Telegram/Resources/winrc" / resource).read_text(encoding="utf-8")
            self.assertRegex(text, rf'VALUE "FileVersion", "{dotted}"')
        manifest = (ROOT / "Telegram/Resources/uwp/AppX/AppxManifest.xml").read_text(encoding="utf-8")
        self.assertIn(f'Version="{current.file_version}"', manifest)

    @unittest.skipUnless(shutil.which("cmake"), "cmake is required")
    def test_cmake_override_preserves_official_and_revision_metadata(self):
        _, current = self.current()
        script = f"""
include(\"{(ROOT / 'Telegram/cmake/ayugram_version.cmake').as_posix()}\")
ayugram_override_version(\"{(ROOT / 'Telegram/build/version').as_posix()}\")
if (NOT desktop_app_version_string STREQUAL \"{current.text}\")
  message(FATAL_ERROR \"bad short version: ${{desktop_app_version_string}}\")
endif()
if (NOT desktop_app_version_cmake STREQUAL \"{current.file_version}\")
  message(FATAL_ERROR \"bad project version: ${{desktop_app_version_cmake}}\")
endif()
if (NOT desktop_app_update_version STREQUAL \"{current.update}\")
  message(FATAL_ERROR \"bad bundle version: ${{desktop_app_update_version}}\")
endif()
"""
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "version_probe.cmake"
            source.write_text(script, encoding="utf-8")
            subprocess.run(["cmake", "-P", source], cwd=ROOT, check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
