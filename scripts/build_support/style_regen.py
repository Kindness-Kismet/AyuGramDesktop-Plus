import glob
import hashlib
import os
import shutil
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

from build_support.console import fail, warn
from build_support.paths import CMAKE_OUT_DIR, LIBRARIES_ARCH_DIR, ROOT, TARGET
from build_support.recipes import qt_version

# codegen 产物所在目录,与 CMake 的 gen/styles 输出一致
_GEN_STYLES = CMAKE_OUT_DIR / "Telegram" / "gen" / "styles"
_TIMESTAMP = _GEN_STYLES / "td_ui_style.timestamp"
_VCXPROJ = CMAKE_OUT_DIR / "Telegram" / "td_ui_styles.vcxproj"
_MSBUILD_NS = "http://schemas.microsoft.com/developer/msbuild/2003"

# 图标目录：内容会被嵌进生成的 style 文件，改动必须触发 codegen 重跑
_ICON_DIRS = (
    ROOT / "Telegram" / "Resources" / "icons",
    ROOT / "Telegram" / "lib_ui" / "icons",
)


def _sha1(path: Path) -> str:
    return hashlib.sha1(path.read_bytes()).hexdigest()


def _extract_codegen_command(cmake_config: str) -> list[str] | None:
    """从 CMake 生成的项目文件提取当前配置的样式生成命令。"""
    if not _VCXPROJ.is_file():
        return None
    tree = ET.parse(_VCXPROJ)
    condition = f"'$(Configuration)|$(Platform)'=='{cmake_config}|x64'"
    for command in tree.iter(f"{{{_MSBUILD_NS}}}Command"):
        if command.get("Condition") != condition:
            continue
        for line in (command.text or "").splitlines():
            line = line.strip()
            if "codegen_style" in line.lower():
                return _split_command(line)
    return None


def _split_command(line: str) -> list[str]:
    """把命令行拆成参数;.style 路径含空格的情况在本仓库不存在,按空白切分。"""
    return line.split()


def _style_inputs(cmake_config: str) -> list[Path]:
    """从 vcxproj 的 AdditionalInputs 取输入清单,含 lib_ui 的隐式依赖。"""
    if not _VCXPROJ.is_file():
        return []
    tree = ET.parse(_VCXPROJ)
    condition = f"'$(Configuration)|$(Platform)'=='{cmake_config}|x64'"
    for node in tree.iter(f"{{{_MSBUILD_NS}}}AdditionalInputs"):
        if node.get("Condition") != condition:
            continue
        items = (node.text or "").split(";")
        # codegen_style.exe 也在清单里,过滤掉,只留 .style/.palette 源
        return [Path(i) for i in items if i.strip().lower().endswith((".style", ".palette"))]
    return []


def _inputs_newer_than_timestamp(cmake_config: str) -> bool:
    """任一 .style/.palette 比 timestamp 新则需重跑,取不到清单则保守重跑。"""
    inputs = _style_inputs(cmake_config)
    if not inputs:
        return True
    stamp = _TIMESTAMP.stat().st_mtime
    return any(p.is_file() and p.stat().st_mtime > stamp for p in inputs)


def _icons_newer_than_timestamp() -> bool:
    """图标嵌入生成文件，内容或目录变化都需要重新生成。"""
    stamp = _TIMESTAMP.stat().st_mtime
    for directory in _ICON_DIRS:
        if not directory.is_dir():
            continue
        if directory.stat().st_mtime > stamp:
            return True
        for path in directory.rglob("*"):
            if path.stat().st_mtime > stamp:
                return True
    return False


def _codegen_environment(environment: dict[str, str]) -> dict[str, str]:
    env = dict(environment)
    qt_bin = LIBRARIES_ARCH_DIR / f"Qt-{qt_version(TARGET)}" / "bin"
    if qt_bin.is_dir():
        env["PATH"] = str(qt_bin) + os.pathsep + env.get("PATH", "")
    return env


def regenerate_styles(environment: dict[str, str], cmake_config: str) -> str:
    """仅更新内容变化的样式产物；生成成功后才刷新时间戳。"""
    if not _TIMESTAMP.is_file() or not _GEN_STYLES.is_dir():
        return "generated styles not present yet, leaving to CMake"

    parts = _extract_codegen_command(cmake_config)
    if not parts:
        raise SystemExit(fail("codegen command not found in vcxproj"))

    binary_rel = parts[0]
    binary = binary_rel if os.path.isabs(binary_rel) else str(CMAKE_OUT_DIR / "Telegram" / binary_rel)
    if not Path(binary).is_file():
        return warn(f"codegen_style not built yet ({binary_rel}), skipping style regen")

    # 没有 .style 比 timestamp 新,也没有图标变更,codegen 不会产出变化
    if not _inputs_newer_than_timestamp(cmake_config) and not _icons_newer_than_timestamp():
        return "no style inputs changed, skipping codegen"

    tmp = ROOT / "build" / "gen_tmp"
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp_styles = tmp / "styles"
    tmp_styles.mkdir(parents=True, exist_ok=True)

    args = []
    for part in parts[1:]:
        if part.startswith("-o"):
            args.append("-o" + str(tmp_styles))
        elif part.startswith("-t"):
            args.append("-t" + str(tmp_styles / "td_ui_style"))
        else:
            args.append(part)

    proc = subprocess.run(
        [binary] + args,
        cwd=str(CMAKE_OUT_DIR / "Telegram"),
        env=_codegen_environment(environment),
        capture_output=True,
        text=True,
        errors="replace",
    )
    if proc.returncode != 0:
        tail = (proc.stderr or proc.stdout or "").strip()[-400:]
        raise SystemExit(fail(f"style codegen failed (exit {proc.returncode}): {tail}"))

    before = {Path(p).name: _sha1(Path(p)) for p in glob.glob(str(_GEN_STYLES / "*")) if Path(p).is_file()}
    after = {Path(p).name: _sha1(Path(p)) for p in glob.glob(str(tmp_styles / "*")) if Path(p).is_file()}

    changed = [n for n, h in after.items() if n in before and before[n] != h]
    added = [n for n in after if n not in before]
    removed = [n for n in before if n not in after]

    for name in changed + added:
        shutil.copy2(tmp_styles / name, _GEN_STYLES / name)
    for name in removed:
        (_GEN_STYLES / name).unlink()

    # 顶到比源文件新,让 MSBuild 认为已最新,避免再次全量重跑 codegen
    os.utime(_TIMESTAMP, None)
    shutil.rmtree(tmp, ignore_errors=True)

    touched = len(changed) + len(added) + len(removed)
    if not touched:
        return "styles unchanged, refreshed timestamp"
    detail = ", ".join(changed[:6]) + (" ..." if len(changed) > 6 else "")
    return f"rewrote {touched} generated file(s): {detail}"
