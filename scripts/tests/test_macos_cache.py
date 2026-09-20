import os
import platform
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TRIM_SCRIPT = ROOT / "scripts" / "trim_macos_libraries.sh"


@unittest.skipUnless(platform.system() == "Darwin", "requires the macOS strip tool")
class MacOSCacheTrimTests(unittest.TestCase):
    def test_trim_preserves_linkable_inputs_and_removes_build_debris(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            libraries = root / "Libraries"
            stage = libraries / "sample"
            objects = libraries / "Qt-6" / "lib" / "objects-Release"
            include = libraries / "local" / "include"
            cmake = libraries / "local" / "lib" / "cmake"
            cache_keys = libraries / "cache_keys"
            patches = libraries / "patches"
            for directory in (stage, objects, include, cmake, cache_keys, patches):
                directory.mkdir(parents=True)

            source = root / "library.c"
            source.write_text("int cache_answer(void) { return 42; }\n", encoding="utf-8")
            object_file = stage / "library.o"
            archive = stage / "libsample.a"
            subprocess.run(["clang", "-g", "-c", source, "-o", object_file], check=True)
            subprocess.run(["ar", "rcs", archive, object_file], check=True)
            before = archive.stat().st_size

            (objects / "library.o").write_bytes(object_file.read_bytes())
            (include / "sample.h").write_text("int cache_answer(void);\n", encoding="utf-8")
            (cmake / "SampleTargets.cmake").write_text("# fixture\n", encoding="utf-8")
            (cache_keys / "sample").write_text("key\n", encoding="utf-8")
            (patches / "sample.patch").write_text("patch\n", encoding="utf-8")
            (stage / "discard.cpp").write_text("// discard\n", encoding="utf-8")
            tool = stage / "host-tool"
            tool.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            tool.chmod(0o755)

            subprocess.run([TRIM_SCRIPT, libraries], check=True)

            self.assertLess(archive.stat().st_size, before)
            self.assertTrue((objects / "library.o").is_file())
            self.assertTrue((include / "sample.h").is_file())
            self.assertTrue((cmake / "SampleTargets.cmake").is_file())
            self.assertTrue((cache_keys / "sample").is_file())
            self.assertTrue((patches / "sample.patch").is_file())
            self.assertTrue(os.access(tool, os.X_OK))
            self.assertFalse((stage / "discard.cpp").exists())

            main = root / "main.c"
            binary = root / "main"
            main.write_text(
                "int cache_answer(void); int main(void) { return cache_answer() != 42; }\n",
                encoding="utf-8",
            )
            subprocess.run(["clang", main, archive, "-o", binary], check=True)
            subprocess.run([binary], check=True)


if __name__ == "__main__":
    unittest.main()
