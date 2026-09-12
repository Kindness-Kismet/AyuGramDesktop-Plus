import subprocess
from pathlib import Path

from build_support.console import fail


def run(command: list[str], cwd: Path, environment: dict[str, str], label: str) -> None:
    """实时透传输出并在失败时终止，编译日志量大，不做缓冲。"""
    print(f"  $ {' '.join(command)}", flush=True)
    result = subprocess.run(command, cwd=str(cwd), env=environment)
    if result.returncode != 0:
        raise SystemExit(fail(f"{label} failed with exit code {result.returncode}"))
