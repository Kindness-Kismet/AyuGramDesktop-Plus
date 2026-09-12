"""配方内的下载与解压工具，替代 PowerShell 以免受执行策略限制。

作为子命令被 bat 配方调用：python fetch.py download URL PATH / python fetch.py unzip ZIP DIR
"""
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

_USER_AGENT = "AyuGramDesktop-build"


def download(url: str, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    request = urllib.request.Request(url, headers={"User-Agent": _USER_AGENT})
    with urllib.request.urlopen(request, timeout=300) as response, target.open("wb") as handle:
        shutil.copyfileobj(response, handle)
    print(f"downloaded {target.name} ({target.stat().st_size} bytes)")


def unzip(archive: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as zipped:
        zipped.extractall(destination)
    print(f"extracted {archive.name} -> {destination}")


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: fetch.py download URL PATH | fetch.py unzip ZIP DIR", file=sys.stderr)
        return 2

    action = argv[0]
    if action == "download" and len(argv) == 3:
        download(argv[1], Path(argv[2]))
    elif action == "unzip" and len(argv) == 3:
        unzip(Path(argv[1]), Path(argv[2]))
    else:
        print(f"unknown or malformed action: {' '.join(argv)}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
