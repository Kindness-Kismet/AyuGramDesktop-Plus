import ctypes
import os
import sys
from contextlib import contextmanager

if sys.platform == "win32":
    os.system("")

_USE_COLOR = sys.stdout.isatty() and not os.environ.get("NO_COLOR")

_UTF8_CODE_PAGE = 65001

CYAN = "\033[36m"
DIM = "\033[2m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
RED = "\033[31m"
BOLD = "\033[1m"
RESET = "\033[0m"


def paint(text: str, *codes: str) -> str:
    if not _USE_COLOR:
        return text
    return "".join(codes) + text + RESET


@contextmanager
def utf8_output():
    """把控制台输出代码页切到 UTF-8，退出时还原。

    MSVC 工具按控制台代码页输出本地化文本，936 下 meson 之类按 UTF-8
    解码的工具会直接崩溃。没有控制台时无从设置，原样放过。
    """
    kernel32 = ctypes.windll.kernel32
    previous = kernel32.GetConsoleOutputCP()
    if not previous or previous == _UTF8_CODE_PAGE:
        yield
        return

    kernel32.SetConsoleOutputCP(_UTF8_CODE_PAGE)
    try:
        yield
    finally:
        kernel32.SetConsoleOutputCP(previous)


def header(text: str) -> str:
    return paint(f"==> {text}", CYAN, BOLD)


def timing(elapsed: float) -> str:
    return paint(f"  · {elapsed:.2f}s", GREEN)


def print_summary(*extras: str) -> None:
    for line in extras:
        print(f"  {line}", flush=True)


def warn(text: str) -> str:
    return paint(text, YELLOW)


def fail(text: str) -> str:
    return paint(text, RED, BOLD)


def format_bytes(num_bytes: int) -> str:
    units = ["B", "KB", "MB", "GB"]
    value = float(num_bytes)
    for unit in units:
        if value < 1024 or unit == units[-1]:
            return f"{int(value)} B" if unit == "B" else f"{value:.1f} {unit}"
        value /= 1024
    return f"{value:.1f} GB"
