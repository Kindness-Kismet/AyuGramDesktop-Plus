import shutil
import subprocess
import sys
import time
from pathlib import Path

from build_support.cmake_patch import LIBS_LOC_OPTION, PYTHON_OPTION, ensure_libs_loc_override
from build_support.console import format_bytes, header, print_summary, warn
from build_support.paths import (
    APP_NAME,
    BUILD_DIR,
    CMAKE_OUT_DIR,
    DEBUG_SYMBOLS,
    LIBRARIES_ARCH_DIR,
    PRODUCT_BINARIES,
    ROOT,
    SPECIAL_TARGET_FILE,
    VENV_PYTHON,
    NUGET_EXE,
)
from build_support.processes import run
from build_support.recipes import QT_VERSION
from build_support.timer import timed_step
from build_support.toolchain import CMAKE_GENERATOR, CMAKE_TOOLSET, msvc_environment
from build_support.version import parse_version, read_current_version

_CONFIGURATIONS = {"dev": "Debug", "release": "Release"}

# 每个编译进程都要映射一份 PCH，8 路约占 4 GB 提交量，对 32 GB 内存 + 6 GB
# 页面文件的机器留有余量；调高需同步扩大页面文件。
_DEFAULT_CL_JOBS = 8


def output_dir(profile: str) -> Path:
    # 版本号取自 Telegram/build/version，与 cli.py 的产物解析保持一致
    version = parse_version(read_current_version()).original
    return BUILD_DIR / f"{APP_NAME}-v{version}-win-x64-{profile}"


def build(configurations: list[str], api_id: str, api_hash: str, reconfigure: bool, jobs: int | None) -> None:
    environment = msvc_environment()
    # cmake/external/qt 靠 %QT% 定位 Qt-<版本> 目录，缺失会直接 FATAL_ERROR
    environment["QT"] = QT_VERSION

    if not LIBRARIES_ARCH_DIR.is_dir():
        raise SystemExit(f"Dependencies missing at {LIBRARIES_ARCH_DIR}. Run scripts/prebuild.py first.")

    with timed_step("Patch cmake helpers"):
        for note in ensure_libs_loc_override():
            print(f"  {note}", flush=True)

    if reconfigure and CMAKE_OUT_DIR.exists():
        with timed_step("Clear CMake cache"):
            shutil.rmtree(CMAKE_OUT_DIR, ignore_errors=True)
            print(f"  removed {CMAKE_OUT_DIR}", flush=True)

    with timed_step("Configure CMake"):
        configure(environment, api_id, api_hash)

    # 运行中的实例会占住产物，编译完再停就白等一轮链接，开工前先腾出来
    with timed_step("Release running instances"):
        stopped = stop_running_instances(configurations)
        print(f"  {stopped}", flush=True)

    produced: list[tuple[str, object]] = []
    for profile in configurations:
        cmake_config = _CONFIGURATIONS[profile]
        with timed_step(f"Build {cmake_config}"):
            compile_target(environment, cmake_config, jobs)
        with timed_step(f"Collect {cmake_config}"):
            produced.extend(collect(cmake_config, profile))

    total = sum(path.stat().st_size for _, path in produced)
    print_summary(
        *(f"Location {directory}" for directory in sorted({path.parent for _, path in produced})),
        f"Files {len(produced)}",
        f"Total {format_bytes(total)}",
    )


def cmake_executable(environment: dict[str, str]) -> str:
    """CreateProcess 按调用进程的 PATH 解析程序名，须显式取环境里那份 cmake。"""
    found = shutil.which("cmake", path=environment["PATH"])
    if not found:
        raise SystemExit("cmake not found in the MSVC environment PATH.")
    return found


def configure(environment: dict[str, str], api_id: str, api_hash: str) -> None:
    CMAKE_OUT_DIR.mkdir(parents=True, exist_ok=True)
    command = [
        cmake_executable(environment),
        "-B",
        str(CMAKE_OUT_DIR),
        "-S",
        str(ROOT),
        "-G",
        CMAKE_GENERATOR,
        "-A",
        "x64",
        "-T",
        CMAKE_TOOLSET,
        f"-DTDESKTOP_API_ID={api_id}",
        f"-DTDESKTOP_API_HASH={api_hash}",
        f"-D{LIBS_LOC_OPTION}={LIBRARIES_ARCH_DIR.as_posix()}",
        f"-D{PYTHON_OPTION}={VENV_PYTHON.as_posix()}",
        f"-DNUGET_EXE={NUGET_EXE.as_posix()}",
    ]

    # 官方发布构建才有该文件，存在时须转成 CMake 选项
    if SPECIAL_TARGET_FILE.is_file():
        target = SPECIAL_TARGET_FILE.read_text(encoding="utf-8").strip()
        if target:
            command.append(f"-DDESKTOP_APP_SPECIAL_TARGET={target}")

    # Qt5 的官方配置文件含有未初始化变量，开启该诊断会把外部包告警当成配置失败
    command += ["-Werror=dev", "-Werror=deprecated"]
    run(command, ROOT, environment, "CMake configure")


def compile_target(environment: dict[str, str], cmake_config: str, jobs: int | None) -> None:
    command = [
        cmake_executable(environment),
        "--build",
        str(CMAKE_OUT_DIR),
        "--config",
        cmake_config,
        "--target",
        "Telegram",
    ]
    if jobs:
        command.extend(["--parallel", str(jobs)])
    # /MP 会让每个 cl.exe 再自行开满逻辑核，绕过 --parallel。Telegram 的 PCH 约
    # 514 MB 且每个进程各映射一份，撞上提交上限就是 C3859/C1076，故显式限流。
    command.extend(["--", f"/p:CL_MPCount={jobs or _DEFAULT_CL_JOBS}"])
    run(command, ROOT, environment, f"Build {cmake_config}")


def stop_running_instances(configurations: list[str]) -> str:
    """停掉本项目产物目录里正在运行的实例，按可执行文件绝对路径匹配。"""
    targets = [
        output_dir(profile) / name
        for profile in configurations
        for name in PRODUCT_BINARIES
        if (output_dir(profile) / name).is_file()
    ]
    stopped = 0
    for target in targets:
        if locking_pids(target):
            release_target(target)
            stopped += 1
    return f"stopped {stopped} instance(s)" if stopped else "nothing running"


def locking_pids(path: Path) -> list[int]:
    """按可执行文件绝对路径匹配进程，不按进程名，避免误杀正式安装版。"""
    if sys.platform != "win32":
        return []
    escaped_path = str(path).replace("'", "''")
    command = [
        "powershell",
        "-NoProfile",
        "-Command",
        f"$expected = '{escaped_path}'; "
        "Get-CimInstance Win32_Process | "
        "Where-Object { $_.ExecutablePath -eq $expected } | "
        "ForEach-Object { $_.ProcessId }",
    ]
    result = subprocess.run(command, cwd=str(ROOT), text=True, errors="replace", capture_output=True, check=False)
    return [int(line.strip()) for line in result.stdout.splitlines() if line.strip().isdigit()]


def release_target(path: Path) -> None:
    """腾出被运行中实例占用的产物，先请求正常退出，超时才强制结束。"""
    pids = locking_pids(path)
    if not pids:
        return

    for pid in pids:
        print(warn(f"  {path.name} in use by pid {pid}, stopping"), flush=True)
        subprocess.run(["taskkill", "/PID", str(pid)], capture_output=True, check=False)

    for _ in range(20):
        if not locking_pids(path):
            return
        time.sleep(0.25)

    for pid in locking_pids(path):
        subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True, check=False)
    time.sleep(0.5)


def collect(cmake_config: str, profile: str) -> list[tuple[str, object]]:
    source_dir = CMAKE_OUT_DIR / cmake_config
    destination = output_dir(profile)
    destination.mkdir(parents=True, exist_ok=True)

    # 运行中的旧产物会占住 exe 与同目录 pdb，拷贝时报 WinError 32，先停掉它
    for name in PRODUCT_BINARIES:
        release_target(destination / name)

    # Debug 还要带 .pdb，否则调试器只能看到地址而没有符号。Release 的调试信息
    # 已经由 CMAKE_MSVC_DEBUG_INFORMATION_FORMAT 内嵌，不单独产出 pdb。
    wanted = PRODUCT_BINARIES + (DEBUG_SYMBOLS if profile == "dev" else ())

    collected: list[tuple[str, object]] = []
    for name in wanted:
        source = source_dir / name
        if not source.is_file():
            print(warn(f"  {name} not produced, skipped"), flush=True)
            continue
        target = destination / name
        shutil.copy2(source, target)
        print(f"  {name:<14} {format_bytes(target.stat().st_size)}", flush=True)
        collected.append((profile, target))

    if not any(path.suffix == ".exe" for _, path in collected):
        raise SystemExit(f"No executable found in {source_dir}")
    return collected
