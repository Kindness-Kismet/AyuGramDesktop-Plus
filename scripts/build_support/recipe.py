import re
from dataclasses import dataclass, field


@dataclass
class Stage:
    name: str
    location: str
    commands: str
    version: str = "0"
    dependencies: list[str] = field(default_factory=list)


def remove_dir_command(folder: str) -> str:
    # 目录残留会让重建拿到旧产物，删不掉就直接失败
    return f"if exist {folder} rmdir /Q /S {folder}\nif exist {folder} exit /b 1"


def win_fail_on_each(commands: str) -> str:
    """逐条插入错误检查，$VAR 转成 %VAR%，行尾 ^ 表示续行。"""
    result: list[str] = []
    starting = True
    for command in commands.split("\n"):
        command = re.sub(r"\$([A-Za-z0-9_]+)", r"%\1%", command)
        if re.search(r"\$[^<]", command):
            raise SystemExit(f"Bad command: {command}")
        append_call = starting and not re.match(r"(if|for) ", command)
        result.append(("call " if append_call else "") + command)
        if command.endswith("^"):
            starting = False
        else:
            starting = True
            result.append("\r\nif %errorlevel% neq 0 exit /b %errorlevel%\r\n")
    return "".join(result)
