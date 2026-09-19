#!/usr/bin/env python3
"""分别校验本项目发布版本和官方适配基线。"""
import argparse
import datetime
import json
import re
import subprocess
import sys
from pathlib import Path
from xml.etree import ElementTree

ROOT = Path(__file__).resolve().parents[4]
BRAND_VERSION_H = {
    "AppId": '"{53F49750-6209-4FBF-9CA8-7A333C87D666}"_cs',
    "AppNameOld": '"AyuGram for Windows"_cs',
    "AppName": '"AyuGram Desktop"_cs',
    "AppFile": '"AyuGram"_cs',
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true", help="按已登记的官方提交补全基线元数据")
    args = parser.parse_args()
    sys.path.insert(0, str(ROOT / "scripts"))
    from build_support.version import parse_version

    telegram = ROOT / "Telegram"
    values = dict(
        line.split() for line in (telegram / "build/version").read_text(encoding="utf-8").splitlines()
        if line.strip()
    )
    version = parse_version(values["AppVersionOriginal"])
    failures = []

    def check(label, actual, expected):
        if actual != expected:
            failures.append(f"{label}：期望 {expected!r}，实际 {actual!r}")

    def match(text, pattern):
        found = re.search(pattern, text, re.MULTILINE)
        return found.group(1) if found else None

    for key, expected in {
        "AppVersion": str(version.full),
        "AppVersionStr": version.text,
        "AppVersionStrSmall": version.text_small,
        "AppVersionStrMajor": f"{version.major}.{version.minor}",
        "BetaChannel": "1" if version.beta else "0",
        "AlphaVersion": str(version.full_alpha),
    }.items():
        check(f"build/version {key}", values.get(key), expected)

    header = (telegram / "SourceFiles/core/version.h").read_text(encoding="utf-8")
    for key, expected in {
        **BRAND_VERSION_H,
        "AppVersion": str(version.full),
        "AppVersionStr": f'"{version.text_small}"',
        "AppBetaVersion": "true" if version.beta else "false",
    }.items():
        check(f"version.h {key}", match(header, rf"constexpr auto {key} = (.*?);"), expected)
    check(
        "version.h 内测版本",
        match(header, r"#define TDESKTOP_REQUESTED_ALPHA_VERSION \((\d+)ULL\)"),
        str(version.full_alpha),
    )

    parts = (version.major, version.minor, version.patch, version.alpha)
    dotted = ".".join(map(str, parts))
    comma = ",".join(map(str, parts))
    for name, description in (
        ("Telegram.rc", "AyuGram Desktop"),
        ("Updater.rc", "AyuGram Desktop Updater"),
    ):
        resource = (telegram / "Resources/winrc" / name).read_text(encoding="utf-8")
        for key, expected in {
            "CompanyName": "Radolyn Labs",
            "FileDescription": description,
            "ProductName": "AyuGram Desktop",
            "FileVersion": dotted,
            "ProductVersion": dotted,
        }.items():
            check(f"{name} {key}", match(resource, rf'VALUE "{key}", "([^"]*)"'), expected)
        for key in ("FILEVERSION", "PRODUCTVERSION"):
            check(f"{name} {key}", match(resource, rf"^\s*{key}\s+([\d,]+)"), comma)

    manifest = telegram / "Resources/uwp/AppX/AppxManifest.xml"
    identity = ElementTree.fromstring(manifest.read_bytes()).find("{*}Identity")
    check("应用包版本", identity.get("Version") if identity is not None else None, dotted)

    tracking = ROOT / ".github/upstream.json"
    raw = tracking.read_text(encoding="utf-8")
    data = json.loads(raw)
    upstream = data["tdesktop"]
    commit = upstream["commit"]
    result = subprocess.run(
        ["git", "show", "-s", "--format=%s%n%cs", commit],
        capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=ROOT,
    )
    if result.returncode:
        print(f"无法查询官方基线：{result.stderr.strip()}")
        return 1
    subject, commit_date = result.stdout.strip().splitlines()
    upstream_version = match(subject, r"^Version (\d+\.\d+\.\d+)\.$")
    if not upstream_version:
        failures.append(f"官方基线不是稳定版提交：{commit[:10]} {subject}")

    # 元数据从官方提交推导，本项目发布版本不参与基线判断。
    if args.write and upstream_version:
        upstream.update(
            version=upstream_version,
            commit_date=commit_date,
            synced_at=datetime.date.today().isoformat(),
        )
    check("官方基线版本", upstream.get("version"), upstream_version)
    check("官方基线日期", upstream.get("commit_date"), commit_date)
    synced = upstream.get("synced_at", "")
    if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", synced):
        failures.append("缺少有效的适配日期")

    adaptation = upstream.get("merge_commit", "")
    probe = subprocess.run(
        ["git", "merge-base", "--is-ancestor", adaptation, "HEAD"],
        capture_output=True, cwd=ROOT,
    )
    if probe.returncode:
        failures.append(f"适配提交不在当前历史中：{adaptation or '缺失'}")

    if failures:
        print("\n".join(f"未通过：{failure}" for failure in failures))
        return 1
    if args.write:
        updated = json.dumps(data, ensure_ascii=False, indent=2) + "\n"
        if updated != raw:
            tracking.write_bytes(updated.encode("utf-8"))
            print("已补全官方适配基线元数据。")
    print(f"校验通过：本项目 {version.original}，官方适配基线 {upstream_version}。")
    return 0


if __name__ == "__main__":
    sys.dont_write_bytecode = True
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(main())
