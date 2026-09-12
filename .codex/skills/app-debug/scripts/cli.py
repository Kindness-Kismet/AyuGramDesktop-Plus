#!/usr/bin/env python3
"""app-debug CLI，用 domain.action 指令控制正在运行的 AyuGram Debug 构建。"""
import argparse
import os
import re
import shlex
import socket
import subprocess
import sys
import time
from contextlib import contextmanager
from pathlib import Path

sys.dont_write_bytecode = True
os.environ["PYTHONDONTWRITEBYTECODE"] = "1"

# 应用回传的 payload 含中文和表情，cp936 控制台会抛 UnicodeEncodeError。
for _stream in (sys.stdout, sys.stderr):
    _stream.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(os.environ.get("AYUGRAM_DEBUG_ROOT", Path(__file__).resolve().parents[4])).resolve()
PORT = 20100
COMMAND_TIMEOUT_SECONDS = 180
SCREENSHOT_DIR = ROOT / "build" / "screenshots"
COMMAND_LOCK_PATH = ROOT / "build" / "app-debug-cli.lock"
COMMAND_SEPARATOR = "+"
BOOLEAN_CHOICES = ("true", "false")

VERSION_FILE = ROOT / "Telegram" / "build" / "version"


def read_app_version() -> str:
    for line in VERSION_FILE.read_text(encoding="utf-8").splitlines():
        match = re.search(r"AppVersionOriginal\s+(\S+)", line)
        if match:
            return match.group(1)
    raise SystemExit("AppVersionOriginal not found in Telegram/build/version")


def current_platform() -> str:
    if sys.platform == "win32":
        return "win-x64"
    return "mac" if sys.platform == "darwin" else "linux"


def debug_dir() -> Path:
    # 产物目录带版本号，与 scripts/build.py 的 output_dir 保持一致
    return ROOT / "build" / f"AyuGram-v{read_app_version()}-{current_platform()}-dev"


def app_exe() -> Path:
    return debug_dir() / ("AyuGram.exe" if sys.platform == "win32" else "AyuGram")


def is_debug_app_exe(path: Path) -> bool:
    # 前后缀匹配而非精确路径：版本升级后旧目录里的进程同样能被识别
    suffix = f"-{current_platform()}-dev"
    resolved = path.resolve()
    directory = resolved.parent.name
    return (
        resolved.name == ("AyuGram.exe" if sys.platform == "win32" else "AyuGram")
        and resolved.parent.parent.resolve() == (ROOT / "build").resolve()
        and directory.startswith("AyuGram-v")
        and directory.endswith(suffix)
        and len(directory) > len("AyuGram-v") + len(suffix)
    )

# 启动到监听之间要过 Qt 初始化、账号加载和主窗口构造，冷启动比较慢。
PORT_WAIT_SECONDS = 60

COMMAND_CATEGORY_LABELS = {
    "app": "应用生命周期",
    "debug": "登录绕过、环境与消息",
    "settings": "设置读写",
    "ghost": "幽灵模式",
    "storage": "已删除消息与编辑历史",
    "screenshot": "截图",
    "control": "控件树与合成交互",
}


def print_command_help(subparsers) -> None:
    # argparse 只在子命令 action 上保留注册时的帮助文本。
    categories = {}
    for action in subparsers._choices_actions:
        category = action.dest.partition(".")[0]
        categories.setdefault(category, []).append((action.dest, action.help))

    print("用法：app-debug <命令> [参数]")
    print("查看某个命令的参数：app-debug <命令> --help")
    print("多条指令用裸 + 分隔，按顺序执行，任一条失败即中止。")
    print("\n可用命令（按功能分组）：")
    for category in sorted(categories):
        label = COMMAND_CATEGORY_LABELS.get(category, category)
        print(f"\n{label}（{category}）")
        for command, description in sorted(categories[category]):
            print(f"  {command:<44} {description}")


def register_commands(sub) -> None:
    sub.add_parser("app.ensure", help="确保 Debug 应用在运行，未运行则拉起并等到端口就绪")
    sub.add_parser("app.restart", help="重启 Debug 应用")
    sub.add_parser("app.stop", help="停止 Debug 应用，只认端口 PID 或本仓库 dev 产物路径")
    sub.add_parser("app.ping", help="探活，返回 pong")
    sub.add_parser("app.info", help="读版本、配置、工作目录、会话与窗口状态")
    sub.add_parser("app.help", help="列出服务端已注册的全部指令名")

    sub.add_parser("settings.keys", help="列出全部设置键名")
    sub.add_parser("settings.dump", help="导出全部设置为 JSON")
    command = sub.add_parser("settings.get", help="读单个设置键")
    command.add_argument("key")
    command = sub.add_parser("settings.set", help="写单个设置键，按键的既有类型解释取值")
    command.add_argument("key")
    command.add_argument("value")
    command = sub.add_parser("settings.open", help="打开设置页：main / ayu / search")
    command.add_argument("section")
    sub.add_parser("debug.reset-background", help="重置聊天背景到默认壁纸")
    command = sub.add_parser("debug.theme-night", help="切换夜模式主题")
    command.add_argument("state", choices=["on", "off"])

    command = sub.add_parser("debug.fake-session", help="造本地假会话绕过登录，直接进主界面")
    command.add_argument("userId", nargs="?")
    sub.add_parser("debug.testmode", help="在生产环境与官方测试数据中心之间切换")

    command = sub.add_parser("debug.fake-message", help="往假会话的 Saved Messages 塞本地文本消息，验证渲染与隐藏逻辑")
    command.add_argument("text", help="消息文本")
    command.add_argument("--from", dest="from_user", metavar="USER_ID", help="指定另一个假用户作为发送者")
    command.add_argument("--blocked", action="store_true", help="把发送者标记为已拉黑（真拉黑）")
    command.add_argument("--shadow-ban", action="store_true", help="把发送者加入 AyuGram 影子拉黑名单")
    command = sub.add_parser("debug.open-chat", help="打开指定对话并清空导航栈；参数取 debug.chats 的 peerId，正数兼容旧 userId，缺省 Saved Messages")
    command.add_argument("peerId", nargs="?")
    command = sub.add_parser("debug.chats", help="列出已加载对话的 peerId 与名称，filter 为名称子串")
    command.add_argument("filter", nargs="?")
    command = sub.add_parser("debug.send-message", help="真实发送文本消息到指定对话，需已登录，仅限本人测试群")
    command.add_argument("peerId")
    command.add_argument("text", help="消息文本，多个参数以空格拼接")
    command.add_argument("extraText", nargs="*", help=argparse.SUPPRESS)
    command = sub.add_parser("debug.history-stats", help="报告 Saved Messages 里指定 id 消息的存在/隐藏/视图状态，诊断断点")
    command.add_argument("msgIds", nargs="+", metavar="MSG_ID")

    sub.add_parser("ghost.status", help="读全局与当前账号的幽灵模式状态")

    sub.add_parser("storage.stats", help="读保存开关与数据库文件大小")

    sub.add_parser("screenshot.take", help="截图当前活动窗口到 build/screenshots/")

    command = sub.add_parser("control.list", help="列出活动窗口的控件树：标识、类名、几何、可见性")
    command.add_argument("filter", nargs="?", help="子串过滤，匹配 objectName/类名/accessibleName/文本")
    command.add_argument("--all", action="store_true", help="扫全部顶层窗口（含菜单、弹层）")
    command = sub.add_parser("control.click", help="合成鼠标点击：按 objectName/accessibleName 或 #序号 寻址，进程内分发")
    command.add_argument("target", help="objectName（如 mainMenuButton）、accessibleName 或 #序号")
    command.add_argument("--all", action="store_true", help="#序号 按 control.list --all 的全顶层序号寻址")


def main() -> int:
    parser = argparse.ArgumentParser(prog="app-debug", add_help=False)
    parser.add_argument("-h", "--help", action="store_true", dest="show_help")
    sub = parser.add_subparsers(dest="command")
    register_commands(sub)

    argv = sys.argv[1:]
    if not argv or argv[0] in ("-h", "--help"):
        print_command_help(sub)
        return 0

    # 先整体解析所有分段，拼写错误不会让前几条已经生效。
    try:
        segments = [parser.parse_args(segment) for segment in split_command_segments(argv)]
    except SystemExit:
        return 2

    for args in segments:
        try:
            execute_command(args)
        except Exception as exception:
            print(f"错误: {exception}", file=sys.stderr)
            return 1
    return 0


def split_command_segments(argv: list[str]) -> list[list[str]]:
    segments: list[list[str]] = [[]]
    for token in argv:
        if token == COMMAND_SEPARATOR:
            segments.append([])
        else:
            segments[-1].append(token)
    if any(not segment for segment in segments):
        raise SystemExit(f"{COMMAND_SEPARATOR} 两侧都必须有指令。")
    return segments


def execute_command(args: argparse.Namespace) -> None:
    command = args.command

    # 生命周期指令由 CLI 自己完成，不进服务端。
    if command == "app.ensure":
        ensure_debug_app()
        print("应用已就绪。")
        return
    if command == "app.restart":
        restart_debug_app()
        print("应用已重启。")
        return
    if command == "app.stop":
        stop_debug_app_if_running()
        print("应用已停止。")
        return

    if command == "screenshot.take":
        ensure_debug_app()
        SCREENSHOT_DIR.mkdir(parents=True, exist_ok=True)
        target = SCREENSHOT_DIR / time.strftime("shot-%Y%m%d-%H%M%S.png")
        print(send_command(f"screenshot.take {quote_arg(str(target))}"))
        return

    ensure_debug_app()
    payload = send_command(build_server_command(args))
    if payload:
        print(payload)


def build_server_command(args: argparse.Namespace) -> str:
    command = args.command
    if command == "settings.get":
        return f"settings.get {quote_arg(args.key)}"
    if command == "settings.set":
        return f"settings.set {quote_arg(args.key)} {quote_arg(args.value)}"
    if command == "settings.open":
        return f"settings.open {quote_arg(args.section)}"
    if command == "debug.reset-background":
        return "debug.reset-background"
    if command == "debug.theme-night":
        return f"debug.theme-night {quote_arg(args.state)}"
    if command == "control.list":
        parts = ["control.list"]
        if args.filter:
            parts.append(quote_arg(args.filter))
        if args.all:
            parts.append("--all")
        return " ".join(parts)
    if command == "control.click":
        parts = ["control.click", quote_arg(args.target)]
        if args.all:
            parts.append("--all")
        return " ".join(parts)
    if command == "debug.fake-session":
        return ("debug.fake-session" if args.userId is None
                else f"debug.fake-session {quote_arg(args.userId)}")
    if command == "debug.fake-message":
        parts = ["debug.fake-message", quote_arg(args.text)]
        if args.from_user:
            parts.extend(["--from", args.from_user])
        if args.blocked:
            parts.append("--blocked")
        if args.shadow_ban:
            parts.append("--shadow-ban")
        return " ".join(parts)
    if command == "debug.open-chat":
        return ("debug.open-chat" if args.peerId is None
                else f"debug.open-chat {quote_arg(args.peerId)}")
    if command == "debug.chats":
        return ("debug.chats" if args.filter is None
                else f"debug.chats {quote_arg(args.filter)}")
    if command == "debug.send-message":
        text = " ".join([args.text, *args.extraText])
        return f"debug.send-message {args.peerId} {quote_arg(text)}"
    if command == "debug.history-stats":
        return "debug.history-stats " + " ".join(args.msgIds)
    # 其余都是无参指令，名字与服务端一一对应。
    return command


def quote_arg(value: str) -> str:
    return f'"{value}"' if (" " in value or not value) else value


def ensure_debug_app() -> None:
    if port_owner_pid() is not None:
        return
    if not app_exe().is_file():
        raise SystemExit(
            f"Debug 产物不存在：{app_exe()}\n"
            f"先运行 python scripts/build.py --dev"
        )
    launch_app()
    wait_for_port()


def launch_app() -> None:
    if sys.platform == "win32":
        import subprocess
        subprocess.Popen([str(app_exe())], cwd=debug_dir(), creationflags=subprocess.DETACHED_PROCESS)
    else:
        os.spawnl(os.P_NOWAIT, app_exe())
    print(f"已启动 {app_exe().name}，等待调试端口就绪...", flush=True)


def restart_debug_app() -> None:
    stop_debug_app_if_running()
    launch_app()
    wait_for_port()


def stop_debug_app_if_running() -> None:
    pid = port_owner_pid()
    if pid is not None:
        if not is_expected_process(pid):
            raise RuntimeError(
                f"端口 {PORT} 被意外 PID {pid} 占用，拒绝停止。\n"
                f"手动核对路径：wmic process where \"ProcessId={pid}\" get ExecutablePath"
            )
        try:
            send_command("app.quit")
            wait_for_port_closed()
            wait_for_process_exit(pid)
        except Exception:
            if process_exists(pid):
                kill_pid(pid)
                wait_for_port_closed()
                wait_for_process_exit(pid)

    # 端口尚未建立时，仅按当前 Debug 可执行文件绝对路径清理启动中的进程。
    for remaining_pid in debug_app_pids():
        kill_pid(remaining_pid)
        wait_for_process_exit(remaining_pid)


def debug_app_pids() -> list[int]:
    expected_path = app_exe().resolve()
    if sys.platform == "win32":
        escaped_path = str(expected_path).replace("'", "''")
        command = [
            "powershell",
            "-NoProfile",
            "-Command",
            f"$expected = '{escaped_path}'; "
            "Get-CimInstance Win32_Process | "
            "Where-Object { $_.ExecutablePath -eq $expected } | "
            "ForEach-Object { $_.ProcessId }",
        ]
        result = subprocess.run(command, cwd=ROOT, text=True, errors="replace", capture_output=True, check=False)
        return [int(line.strip()) for line in result.stdout.splitlines() if line.strip().isdigit()]

    proc_root = Path("/proc")
    if proc_root.is_dir():
        pids = []
        for entry in proc_root.iterdir():
            if not entry.name.isdigit():
                continue
            try:
                process_path = Path(os.readlink(entry / "exe")).resolve()
            except OSError:
                continue
            if process_path == expected_path:
                pids.append(int(entry.name))
        return pids

    result = subprocess.run(
        ["ps", "-axo", "pid=,command="],
        cwd=ROOT,
        text=True,
        errors="replace",
        capture_output=True,
        check=False,
    )
    pids = []
    for line in result.stdout.splitlines():
        parts = line.strip().split(maxsplit=1)
        if len(parts) != 2 or not parts[0].isdigit():
            continue
        try:
            executable = shlex.split(parts[1])[0]
        except (ValueError, IndexError):
            continue
        process_path = Path(executable)
        if not process_path.is_absolute():
            process_path = PACKAGE_DIR / process_path
        if process_path.resolve() == expected_path:
            pids.append(int(parts[0]))
    return pids


def port_owner_pid() -> int | None:
    if sys.platform == "win32":
        # netstat 输出窄且不需要 admin，只要解析 127.0.0.1:PORT 那一行的 PID。
        result = subprocess.run(
            ["netstat", "-ano", "-p", "TCP"],
            text=True,
            errors="replace",
            capture_output=True,
            check=False,
        )
        for line in result.stdout.splitlines():
            if f"127.0.0.1:{PORT}" in line and "LISTENING" in line:
                parts = line.split()
                if len(parts) >= 5 and parts[-1].isdigit():
                    return int(parts[-1])
        return None

    # POSIX: lsof / ss / netstat，按可用性顺序试探。
    for cmd in (
        ["lsof", "-iTCP", "-sTCP:LISTEN", "-n", "-P"],
        ["ss", "-tlnp"],
        ["netstat", "-tlnp"],
    ):
        result = subprocess.run(cmd, text=True, errors="replace", capture_output=True, check=False)
        if result.returncode != 0:
            continue
        for line in result.stdout.splitlines():
            if f":{PORT}" not in line or "LISTEN" not in line:
                continue
            # lsof: COMMAND PID USER FD TYPE ... NAME
            # ss/netstat: ... 127.0.0.1:PORT ... users:(("name",pid=<pid>,...))
            parts = line.split()
            if len(parts) > 1 and parts[1].isdigit():
                return int(parts[1])
            match = __import__("re").search(r"pid=(\d+)", line)
            if match:
                return int(match.group(1))
    return None


def is_expected_process(pid: int) -> bool:
    if sys.platform == "win32":
        result = subprocess.run(
            ["powershell", "-NoProfile", "-Command", f"(Get-Process -Id {pid} -ErrorAction Ignore).Path"],
            text=True,
            errors="replace",
            capture_output=True,
            check=False,
        )
        path = result.stdout.strip()
        return bool(path) and is_debug_app_exe(Path(path))

    proc_root = Path("/proc")
    if proc_root.is_dir():
        try:
            process_path = Path(os.readlink(proc_root / str(pid) / "exe")).resolve()
        except OSError:
            return False
        return is_debug_app_exe(process_path)

    result = subprocess.run(
        ["ps", "-p", str(pid), "-o", "command="],
        text=True,
        errors="replace",
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        return False
    try:
        executable = shlex.split(result.stdout.strip())[0]
    except (ValueError, IndexError):
        return False
    process_path = Path(executable)
    if not process_path.is_absolute():
        process_path = debug_dir() / process_path
    return is_debug_app_exe(process_path.resolve())


def kill_pid(pid: int) -> None:
    if sys.platform == "win32":
        subprocess.run(["taskkill", "/F", "/PID", str(pid)], check=False, capture_output=True)
    else:
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass


def wait_for_process_exit(pid: int) -> None:
    for _ in range(50):
        if not process_exists(pid):
            return
        time.sleep(0.1)


def process_exists(pid: int) -> bool:
    if sys.platform == "win32":
        result = subprocess.run(
            ["powershell", "-NoProfile", "-Command", f"Get-Process -Id {pid} -ErrorAction Ignore"],
            capture_output=True,
            check=False,
        )
        return result.returncode == 0

    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def wait_for_port() -> None:
    deadline = time.time() + PORT_WAIT_SECONDS
    while time.time() < deadline:
        if port_owner_pid() is not None:
            print(f"调试端口 {PORT} 已就绪。")
            return
        time.sleep(0.2)
    raise TimeoutError(f"调试端口 {PORT_WAIT_SECONDS} 秒内未就绪，应用可能启动失败。")


def wait_for_port_closed() -> None:
    for _ in range(50):
        if port_owner_pid() is None:
            return
        time.sleep(0.1)
    raise TimeoutError("调试端口在停止后仍被占用。")


@contextmanager
def serialized_debug_command():
    COMMAND_LOCK_PATH.parent.mkdir(parents=True, exist_ok=True)
    with COMMAND_LOCK_PATH.open("a+b") as lock_file:
        if sys.platform == "win32":
            import msvcrt

            deadline = time.time() + COMMAND_TIMEOUT_SECONDS + 30
            while True:
                try:
                    lock_file.seek(0)
                    msvcrt.locking(lock_file.fileno(), msvcrt.LK_NBLCK, 1)
                    break
                except OSError as exception:
                    if time.time() >= deadline:
                        raise TimeoutError("等待调试 CLI 串行锁超时。") from exception
                    time.sleep(0.1)

            try:
                yield
            finally:
                lock_file.seek(0)
                msvcrt.locking(lock_file.fileno(), msvcrt.LK_UNLCK, 1)
            return

        import fcntl

        fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(lock_file.fileno(), fcntl.LOCK_UN)


# 服务端在收到完整一行后关闭连接，payload 可能多行。
def send_command(line: str) -> str:
    with serialized_debug_command():
        with socket.create_connection(("127.0.0.1", PORT), timeout=10) as client:
            client.settimeout(COMMAND_TIMEOUT_SECONDS)
            client.sendall((line + "\n").encode("utf-8"))
            response = client.makefile("r", encoding="utf-8").read().rstrip("\n")

    if response == "OK":
        return ""

    if response.startswith("OK "):
        return response[3:]

    if response.startswith("ERR "):
        raise RuntimeError(response[4:])

    raise RuntimeError(f"未知响应格式: {response}")


if __name__ == "__main__":
    sys.exit(main())
