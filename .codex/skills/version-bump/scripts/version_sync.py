#!/usr/bin/env python3
"""校验并补全官方版本跟进后的版本号登记。

版本号跟随 tdesktop，由上游适配流程更新四处文件；本脚本核对一致性、
品牌保留与 upstream.json 登记，--write 时补全可自动确定的字段。
"""
import argparse
import datetime
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
VERSION_FILE = ROOT / "Telegram" / "build" / "version"
VERSION_H = ROOT / "Telegram" / "SourceFiles" / "core" / "version.h"
WINRC = ROOT / "Telegram" / "Resources" / "winrc"
TRACKING = ROOT / ".github" / "upstream.json"

# 品牌基线：适配解冲突时必须保留的 AyuGram 标识，被官方值覆盖视为解错
BRAND_VERSION_H = {
    "AppId": '"{53F49750-6209-4FBF-9CA8-7A333C87D666}"_cs',
    "AppNameOld": '"AyuGram for Windows"_cs',
    "AppName": '"AyuGram Desktop"_cs',
    "AppFile": '"AyuGram"_cs',
}
BRAND_RC = {
    "Telegram.rc": {
        "CompanyName": "Radolyn Labs",
        "FileDescription": "AyuGram Desktop",
        "ProductName": "AyuGram Desktop",
    },
    "Updater.rc": {
        "CompanyName": "Radolyn Labs",
        "FileDescription": "AyuGram Desktop Updater",
        "ProductName": "AyuGram Desktop",
    },
}


def read_bytes(path: Path) -> bytes:
    return path.read_bytes()


def git(*args: str) -> str:
    done = subprocess.run(
        ["git", *args], capture_output=True, text=True,
        errors="replace", cwd=ROOT,
    )
    if done.returncode != 0:
        raise SystemExit(f"git {' '.join(args)} failed: {done.stderr.strip()}")
    return done.stdout


def parse_version_file() -> dict[str, str]:
    values = {}
    for line in read_bytes(VERSION_FILE).decode("utf-8").splitlines():
        parts = line.split()
        if len(parts) == 2:
            values[parts[0]] = parts[1]
    return values


def numeric(major: str, minor: str, patch: str) -> int:
    return int(major) * 1_000_000 + int(minor) * 1_000 + int(patch)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--write", action="store_true",
        help="补全 upstream.json 中可自动确定的 version / commit_date / synced_at",
    )
    args = parser.parse_args()

    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8", errors="replace")

    vf = parse_version_file()
    ver = vf.get("AppVersionOriginal", "")
    m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", ver)
    checks: list[tuple[str, bool, str]] = []
    if not m:
        print(f"FAIL Telegram/build/version: AppVersionOriginal {ver!r} 不是三段式版本")
        return 1
    major, minor, patch = m.groups()
    code = str(numeric(major, minor, patch))
    small = f"{major}.{minor}.{patch}"
    checks.append(("build/version AppVersion", vf.get("AppVersion") == code,
                   f"期望 {code}，实际 {vf.get('AppVersion')}"))
    checks.append(("build/version AppVersionStr", vf.get("AppVersionStr") == small,
                   f"期望 {small}，实际 {vf.get('AppVersionStr')}"))
    checks.append(("build/version AppVersionStrSmall", vf.get("AppVersionStrSmall") == small,
                   f"期望 {small}，实际 {vf.get('AppVersionStrSmall')}"))
    checks.append(("build/version AppVersionStrMajor", vf.get("AppVersionStrMajor") == f"{major}.{minor}",
                   f"期望 {major}.{minor}，实际 {vf.get('AppVersionStrMajor')}"))

    vh = read_bytes(VERSION_H).decode("utf-8")
    for key, expected in BRAND_VERSION_H.items():
        got = re.search(rf"constexpr auto {key} = (.*?);", vh)
        checks.append((f"version.h {key}", got and got.group(1) == expected,
                       f"期望 {expected}，实际 {got.group(1) if got else '缺失'}"))
    checks.append(("version.h AppVersion",
                   f"constexpr auto AppVersion = {code};" in vh,
                   f"期望 {code}"))
    checks.append(("version.h AppVersionStr",
                   f'constexpr auto AppVersionStr = "{small}";' in vh,
                   f"期望 {small}"))

    for name, brand in BRAND_RC.items():
        rc = read_bytes(WINRC / name).decode("utf-8", errors="replace")
        for key, expected in brand.items():
            got = re.search(rf'VALUE "{key}", "([^"]*)"', rc)
            checks.append((f"{name} {key}", got and got.group(1) == expected,
                           f"期望 {expected}，实际 {got.group(1) if got else '缺失'}"))
        for key in ("FileVersion", "ProductVersion"):
            got = re.search(rf'VALUE "{key}", "([^"]*)"', rc)
            checks.append((f"{name} {key}", got and got.group(1) == f"{small}.0",
                           f"期望 {small}.0，实际 {got.group(1) if got else '缺失'}"))

    raw = read_bytes(TRACKING).decode("utf-8")
    td_commit = re.search(r'"commit": "([0-9a-f]{40})"', raw)
    td_commit = td_commit.group(1) if td_commit else ""
    subject = git("show", "-s", "--format=%s", td_commit).strip() if td_commit else ""
    checks.append(("upstream.json 基线提交是 Version 提交",
                   subject.startswith(f"Version {small}"),
                   f"{td_commit[:9]}: {subject or '不可达'}"))
    td_date = git("show", "-s", "--format=%cd", "--date=short", td_commit).strip() if td_commit else ""
    got_date = re.search(r'"commit_date": "([^"]*)"', raw)
    checks.append(("upstream.json commit_date", got_date and got_date.group(1) == td_date,
                   f"期望 {td_date}，实际 {got_date.group(1) if got_date else '缺失'}"))
    got_version = re.search(r'"version": "([^"]*)"', raw)
    checks.append(("upstream.json version", got_version and got_version.group(1) == small,
                   f"期望 {small}，实际 {got_version.group(1) if got_version else '缺失'}"))
    merge = re.search(r'"merge_commit": "([0-9a-f]{40})"', raw)
    merge_ok = False
    if merge:
        probe = subprocess.run(
            ["git", "cat-file", "-e", merge.group(1)],
            capture_output=True, cwd=ROOT,
        )
        merge_ok = probe.returncode == 0
    checks.append(("upstream.json merge_commit 可达", merge_ok,
                   merge.group(1)[:9] if merge else "缺失"))
    synced = re.search(r'"synced_at": "([^"]*)"', raw)
    today = datetime.date.today().isoformat()
    checks.append(("upstream.json synced_at 是近期日期", bool(synced),
                   synced.group(1) if synced else "缺失"))

    failed = [(name, detail) for name, ok, detail in checks if not ok]
    for name, ok, detail in checks:
        print(f"{'PASS' if ok else 'FAIL'}  {name}" + ("" if ok else f"  ({detail})"))

    if failed:
        print(f"\n{len(failed)} 项未通过。")
        if not args.write:
            print("版本文件不一致属于适配解冲突错误，需人工修复；")
            print("upstream.json 字段可用 --write 自动补全。")
        return 1

    if args.write:
        updated = raw
        for key, value in (
            ("version", small),
            ("commit_date", td_date),
            ("synced_at", today),
        ):
            updated = re.sub(
                rf'("{key}": ")[^"]+(")',
                rf"\g<1>{value}\g<2>",
                updated,
                count=1,
            )
        if updated != raw:
            TRACKING.write_bytes(updated.encode("utf-8"))
            print(f"\nupstream.json 已补全：version={small} "
                  f"commit_date={td_date} synced_at={today}")
            print("merge_commit 仍需按适配提交人工确认。")
        else:
            print("\nupstream.json 字段已是最新，未改动。")
    else:
        print(f"\n全部通过：{small} (AppVersion {code})。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
