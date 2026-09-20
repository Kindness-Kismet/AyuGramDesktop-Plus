import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from build_support import dependency_cache
from build_support.recipe import Stage
from release_update_map import generate_update_map


class QtCacheTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.stage = Stage("qt_6.11.2", "Libraries", "")
        (self.directory / self.stage.name).mkdir()
        keys = self.directory / "cache_keys"
        keys.mkdir()
        (keys / self.stage.name).write_text("matching", encoding="utf-8")
        locations = patch.dict(dependency_cache._LOCATIONS, {"Libraries": self.directory})
        locations.start()
        self.addCleanup(locations.stop)

    def install_objects(self, missing=None):
        prefix = self.directory / "Qt-6.11.2"
        cmake = prefix / "lib/cmake/Qt6Gui"
        cmake.mkdir(parents=True)
        for configuration in ("Debug", "Release"):
            references = []
            for resource in ("qpdf_init", "qgui"):
                relative = f"lib/objects-{configuration}/Gui_resources_1/.qt/rcc/qrc_{resource}.cpp.obj"
                references.append("${_IMPORT_PREFIX}/" + relative)
                if (configuration, resource) != missing:
                    obj = prefix / relative
                    obj.parent.mkdir(parents=True, exist_ok=True)
                    obj.write_bytes(b"object")
            (cmake / f"Qt6GuiTargets-{configuration.lower()}.cmake").write_text(
                f'set_property(TARGET Qt6::Gui_resources_1 PROPERTY IMPORTED_OBJECTS_{configuration.upper()} "' + ";".join(references) + '")\n',
                encoding="utf-8",
            )

    def test_complete_installation_is_reused(self):
        self.install_objects()
        self.assertEqual(dependency_cache.check_cache_key(self.stage, "matching"), "Good")

    def test_one_missing_object_invalidates_matching_cache(self):
        self.install_objects(missing=("Debug", "qpdf_init"))
        self.assertEqual(dependency_cache.check_cache_key(self.stage, "matching"), "Stale")

    def test_missing_installation_invalidates_matching_cache(self):
        self.assertEqual(dependency_cache.check_cache_key(self.stage, "matching"), "Stale")

    def test_changed_recipe_invalidates_complete_installation(self):
        self.install_objects()
        self.assertEqual(dependency_cache.check_cache_key(self.stage, "changed"), "Stale")


class UpdateMapTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.version = 70200901

    def add_packages(self, *prefixes, version=None):
        for prefix in prefixes:
            (self.directory / f"{prefix}{version or self.version}").write_bytes(b"package")

    def test_each_architecture_selects_its_own_package(self):
        expected = {
            "win64": "tx64upd70200901",
            "winarm": "tarm64upd70200901",
            "linux": "tlinuxupd70200901",
            "linuxarm": "tlinuxarmupd70200901",
            "mac": "tmacupd70200901",
            "armac": "tarmacupd70200901",
        }
        for name in expected.values():
            (self.directory / name).write_bytes(b"package")
        result = generate_update_map(self.directory, self.version)
        self.assertEqual(result.keys(), expected.keys())
        for platform, name in expected.items():
            entry = result[platform]["stable"]
            self.assertEqual(entry["released"], self.version)
            self.assertEqual(entry["testing"], self.version)
            self.assertEqual(entry["link"].format(version=entry["released"]), "/" + name)

    def test_unavailable_optional_architectures_are_not_advertised(self):
        self.add_packages("tx64upd", "tlinuxupd", "tmacupd", "tarmacupd")
        result = generate_update_map(self.directory, self.version)
        self.assertEqual(set(result), {"win64", "linux", "mac", "armac"})

    def test_single_mac_architecture_cannot_supply_both_keys(self):
        self.add_packages("tx64upd", "tlinuxupd", "tarmacupd")
        with self.assertRaisesRegex(ValueError, "mac"):
            generate_update_map(self.directory, self.version)

    def test_old_package_cannot_satisfy_current_version(self):
        self.add_packages("tx64upd", "tlinuxupd", "tmacupd", "tarmacupd", version=7002009)
        with self.assertRaises(ValueError):
            generate_update_map(self.directory, self.version)

    def test_empty_package_is_rejected(self):
        self.add_packages("tx64upd", "tlinuxupd", "tmacupd", "tarmacupd")
        (self.directory / "tarmacupd70200901").write_bytes(b"")
        with self.assertRaisesRegex(ValueError, "为空"):
            generate_update_map(self.directory, self.version)

    def test_version_outside_packer_range_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "Packer"):
            generate_update_map(self.directory, 1_000_000_000)


if __name__ == "__main__":
    unittest.main()
