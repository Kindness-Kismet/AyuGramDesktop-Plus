import os
import subprocess
from pathlib import Path

from build_support.paths import TARGET_CMAKE_ARCH, TARGET_VCVARS_ARCH

VSWHERE = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"

# 已切到 VS 2026 的 v145 工具集，不再支持 Windows 7
# CI 可用同名 AYUGRAM_* 环境变量覆盖，指向 runner 上实际安装的 VS 版本
TOOLSET_VERSION = os.environ.get("AYUGRAM_TOOLSET_VERSION", "14.51")
CMAKE_TOOLSET = os.environ.get("AYUGRAM_CMAKE_TOOLSET", "v145")
CMAKE_GENERATOR = os.environ.get("AYUGRAM_CMAKE_GENERATOR", "Visual Studio 18 2026")

# 调用环境带进来的这些变量会改写构建工具的行为，一律剔除以对齐上游的干净 cmd 环境：
# MSYSTEM 让 msys2 切到 MINGW64 模式，mingw 原生 perl 会抢在 msys2 perl 之前；
# PYTHONUTF8 让 meson 按 UTF-8 解码 MSVC 的 GBK 输出，解码失败后直接崩溃。
_ENVIRONMENT_LEAKS = ("MSYSTEM", "MSYS_NO_PATHCONV", "EXEPATH", "PYTHONUTF8")

_CMAKE_SUBPATH = Path("Common7") / "IDE" / "CommonExtensions" / "Microsoft" / "CMake" / "CMake" / "bin"


def vs_installation() -> Path:
    if not VSWHERE.is_file():
        raise SystemExit(f"vswhere.exe not found at {VSWHERE}. Install Visual Studio or the Build Tools.")

    result = subprocess.run(
        [str(VSWHERE), "-all", "-products", "*", "-latest", "-property", "installationPath"],
        capture_output=True,
        text=True,
        errors="replace",
    )
    for line in result.stdout.splitlines():
        path = Path(line.strip())
        if (path / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat").is_file():
            return path
    raise SystemExit("vcvarsall.bat not found. Install the MSVC v145 build tools.")


def find_vcvars() -> Path:
    # vcvars64.bat 只是转发壳，直接用 vcvarsall.bat 避免 %~dp0 经多层 cmd 转发后解析失败
    return vs_installation() / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat"


def msvc_environment() -> dict[str, str]:
    """返回注入 MSVC 后的环境变量，已在当前进程内生效时直接复用。"""
    if os.environ.get("Platform", "").lower() == TARGET_CMAKE_ARCH.lower():
        return _finalize(os.environ.copy())

    vcvars = find_vcvars()
    # 用分隔符隔开 vcvars 自身输出，避免它的横幅被当成变量解析
    marker = "__VCVARS_ENV__"
    command = f'"{vcvars}" {TARGET_VCVARS_ARCH} -vcvars_ver={TOOLSET_VERSION} >nul && echo {marker} && set'
    # 必须 shell=True 传单字符串：list 传参会给带空格路径再套一层引号，cmd 无法解析
    result = subprocess.run(command, shell=True, capture_output=True, text=True, errors="replace")
    if result.returncode != 0:
        raise SystemExit(f"vcvarsall.bat failed:\n{result.stdout}\n{result.stderr}")

    _, _, dump = result.stdout.partition(marker)
    environment = os.environ.copy()
    for line in dump.splitlines():
        key, sep, value = line.partition("=")
        if sep and key:
            # cmd 的 set 沿用变量定义时的大小写，与 os.environ 的 key 可能不一致；
            # 直接写入会让环境块出现 PATH/Path 两个变量，子进程读到的是未更新的那份
            existing = next((k for k in environment if k.lower() == key.lower()), key)
            environment[existing] = value

    if environment.get("Platform", "").lower() != TARGET_CMAKE_ARCH.lower():
        raise SystemExit(f"MSVC environment did not report Platform={TARGET_CMAKE_ARCH}.")
    return _finalize(environment)


def _finalize(environment: dict[str, str]) -> dict[str, str]:
    for key in _ENVIRONMENT_LEAKS:
        environment.pop(key, None)

    cmake_bin = vs_installation() / _CMAKE_SUBPATH
    # PATH 里常有更旧的 CMake（MinGW 的 3.30.0 在多配置生成器下 FindPython 会报错），
    # VS 配套的那份版本与工具链同步，必须排在最前
    if (cmake_bin / "cmake.exe").is_file():
        environment["PATH"] = f"{cmake_bin}{os.pathsep}" + environment.get("PATH", "")
    return environment


def describe_toolset(environment: dict[str, str]) -> str:
    version = environment.get("VCToolsVersion", "Unknown")
    sdk = environment.get("WindowsSDKVersion", "Unknown").rstrip("\\")
    return f"MSVC {version}  SDK {sdk}"
