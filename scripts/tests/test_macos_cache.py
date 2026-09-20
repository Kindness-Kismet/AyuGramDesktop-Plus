import os
import platform
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TRIM_SCRIPT = ROOT / "scripts" / "trim_macos_libraries.sh"
LIBJXL_PATCH = ROOT / "Telegram" / "build" / "prepare" / "patches" / "libjxl-macos-universal.patch"

SKCMS_CMAKE_FIXTURE = """\
function(target_link_skcms TARGET_NAME)
  set(_sources_dir "${PROJECT_SOURCE_DIR}/third_party/skcms")
  set(_sources "${_sources_dir}/src/skcms.cc")
  set(_common_copts "-Wno-deprecated-declarations")

  set(_use_avx2 FALSE)
  set(_use_avx512 FALSE)
  if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64" AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if (CXX_MAVX2_SUPPORTED AND CXX_MF16C_SUPPORTED)
      set(_use_avx2 TRUE)
    endif()
    if (CXX_MAVX512F_SUPPORTED AND CXX_MAVX512DQ_SUPPORTED AND CXX_MAVX512CD_SUPPORTED AND CXX_MAVX512BW_SUPPORTED AND CXX_MAVX512VL_SUPPORTED)
      set(_use_avx512 TRUE)
    endif()
  endif()

  if (_use_avx2)
    list(APPEND _sources "${_sources_dir}/src/skcms_TransformHsw.cc")
    set_source_files_properties("${_sources_dir}/src/skcms_TransformHsw.cc"
      PROPERTIES COMPILE_OPTIONS "${_common_copts};-march=x86-64;-mavx2;-mf16c"
      TARGET_DIRECTORY ${TARGET_NAME}
    )
  else()
    target_compile_definitions(${TARGET_NAME} PRIVATE -DSKCMS_DISABLE_HSW)
  endif()

  if (_use_avx512)
    list(APPEND _sources "${_sources_dir}/src/skcms_TransformSkx.cc")
    set_source_files_properties("${_sources_dir}/src/skcms_TransformSkx.cc"
      PROPERTIES COMPILE_OPTIONS "${_common_copts};-march=x86-64;-mavx512f;-mavx512dq;-mavx512cd;-mavx512bw;-mavx512vl"
      TARGET_DIRECTORY ${TARGET_NAME}
    )
  else()
    target_compile_definitions(${TARGET_NAME} PRIVATE -DSKCMS_DISABLE_SKX)
  endif()

  if (MINGW)
    target_compile_definitions(${TARGET_NAME} PRIVATE -DSKCMS_HAS_MUSTTAIL=0)
  endif()

  target_sources(${TARGET_NAME} PRIVATE "${_sources}")
  target_include_directories(${TARGET_NAME} PRIVATE "${PROJECT_SOURCE_DIR}/third_party/skcms/")
endfunction()
"""


class LibjxlUniversalPatchTests(unittest.TestCase):
    def apply_patch(self, root: Path) -> str:
        source = root / "third_party" / "skcms.cmake"
        source.parent.mkdir(parents=True)
        source.write_text(SKCMS_CMAKE_FIXTURE, encoding="utf-8")
        subprocess.run(["git", "apply", LIBJXL_PATCH], cwd=root, check=True)
        return source.read_text(encoding="utf-8")

    def test_patch_applies_to_skcms_recipe_fixture(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            patched = self.apply_patch(Path(temporary))

        self.assertIn('if (APPLE AND "${CMAKE_OSX_ARCHITECTURES}" MATCHES ";")', patched)
        self.assertIn('COMPILE_OPTIONS "${_common_copts};${_avx2_copts}"', patched)
        self.assertIn('COMPILE_OPTIONS "${_common_copts};${_avx512_copts}"', patched)

    def test_prepare_applies_patch_only_to_libjxl_stage(self) -> None:
        prepare = (ROOT / "Telegram" / "build" / "prepare" / "prepare.py").read_text(
            encoding="utf-8"
        )
        libjxl_stage = prepare.split("stage('libjxl',", 1)[1].split("stage('libvpx',", 1)[0]
        self.assertIn("depends:\"\"\" + libjxlPatch", libjxl_stage)
        self.assertIn("git apply \"\"\" + '\"' + libjxlPatch", libjxl_stage)
        self.assertEqual(prepare.count("depends:\"\"\" + libjxlPatch"), 1)
        self.assertEqual(prepare.count("git apply \"\"\" + '\"' + libjxlPatch"), 1)

    @unittest.skipUnless(platform.system() == "Darwin", "requires AppleClang and lipo")
    def test_arch_scoped_flags_build_universal_objects(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            patched = self.apply_patch(root)
            source = root / "probe.cc"
            source.write_text("int probe() { return 42; }\n", encoding="utf-8")

            for variable in ("_avx2_copts", "_avx512_copts"):
                matches = re.findall(rf'set\({variable} "([^"]+)"\)', patched)
                self.assertEqual(len(matches), 2)
                flags = matches[-1].split(";")
                output = root / f"{variable}.o"
                subprocess.run(
                    [
                        "xcrun",
                        "clang++",
                        "-arch",
                        "x86_64",
                        "-arch",
                        "arm64",
                        *flags,
                        "-c",
                        source,
                        "-o",
                        output,
                    ],
                    check=True,
                )
                architectures = subprocess.check_output(
                    ["lipo", "-archs", output], text=True
                ).split()
                self.assertEqual(set(architectures), {"x86_64", "arm64"})


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
            archive_before_size = archive.stat().st_size
            unsupported_before = archive.read_bytes()

            unsupported = stage / "unsupported.a"
            unsupported.write_bytes(unsupported_before)
            fake_strip = root / "strip"
            fake_strip.write_text(
                "#!/bin/bash\n"
                "if [[ $2 == *unsupported.a ]]; then exit 1; fi\n"
                'exec "$REAL_STRIP" "$@"\n',
                encoding="utf-8",
            )
            fake_strip.chmod(0o755)

            (objects / "library.o").write_bytes(object_file.read_bytes())
            (include / "sample.h").write_text("int cache_answer(void);\n", encoding="utf-8")
            (cmake / "SampleTargets.cmake").write_text("# fixture\n", encoding="utf-8")
            (cache_keys / "sample").write_text("key\n", encoding="utf-8")
            (patches / "sample.patch").write_text("patch\n", encoding="utf-8")
            (stage / "discard.cpp").write_text("// discard\n", encoding="utf-8")
            tool = stage / "host-tool"
            tool.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            tool.chmod(0o755)

            environment = os.environ.copy()
            environment["STRIP"] = str(fake_strip)
            environment["REAL_STRIP"] = shutil.which("strip") or "strip"
            result = subprocess.run(
                [TRIM_SCRIPT, libraries],
                check=True,
                capture_output=True,
                env=environment,
                text=True,
            )

            self.assertLess(archive.stat().st_size, archive_before_size)
            self.assertEqual(unsupported.read_bytes(), unsupported_before)
            self.assertIn("跳过 strip 不支持的依赖文件", result.stdout)
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
