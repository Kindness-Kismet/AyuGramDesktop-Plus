#!/usr/bin/env python3
"""比较 .github/upstream.json 记录的基线与上游当前状态。

只读取仓库状态并调用 GitHub API，不修改任何文件；同步版本号由 bump 子命令
显式完成。
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
TRACKING = ROOT / ".github" / "upstream.json"


def load() -> dict:
    if not TRACKING.is_file():
        raise SystemExit(f"{TRACKING} not found.")
    return json.loads(TRACKING.read_text(encoding="utf-8"))


def gh(endpoint: str, jq: str) -> str:
    """gh 未登录或 API 失败时返回空串，由调用方判定，避免半截结果被当成事实。"""
    try:
        done = subprocess.run(
            ["gh", "api", endpoint, "--jq", jq],
            capture_output=True, text=True, errors="replace", timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        print(f"  ! gh api {endpoint} failed: {error}", file=sys.stderr)
        return ""
    if done.returncode != 0:
        print(f"  ! gh api {endpoint}: {done.stderr.strip()}", file=sys.stderr)
        return ""
    return done.stdout.strip()


def compare(repository: str, branch: str, baseline: str) -> dict | None:
    """用 compare 接口拿领先提交数，ahead_by 是相对 baseline 的新增提交。"""
    raw = gh(
        f"repos/{repository}/compare/{baseline}...{branch}",
        "{ahead: .ahead_by, behind: .behind_by, head: .commits[-1].sha, "
        "date: .commits[-1].commit.author.date}",
    )
    if not raw:
        return None
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return None


def commits(repository: str, branch: str, baseline: str, limit: int) -> list[str]:
    raw = gh(
        f"repos/{repository}/compare/{baseline}...{branch}",
        ".commits | reverse | .[0:%d] | .[] | .sha[0:10] + \"  \" "
        "+ .commit.author.date[0:10] + \"  \" "
        "+ (.commit.message | split(\"\\n\")[0])" % limit,
    )
    return raw.splitlines() if raw else []


def targets(data: dict) -> list[tuple[str, str, str, str]]:
    """展开成 (标签, 仓库, 分支, 基线) 四元组，跳过无上游的条目。"""
    found = []
    td = data["tdesktop"]
    found.append(("tdesktop", td["repository"], td["branch"], td["commit"]))
    for name, entry in data.get("vendored", {}).items():
        if entry.get("repository"):
            found.append((name, entry["repository"], entry["branch"], entry["commit"]))
    for path, entry in data.get("submodules", {}).items():
        if entry.get("repository"):
            found.append(
                (path, entry["repository"], entry["branch"], entry["fork_point"])
            )
    return found


def status(args: argparse.Namespace) -> int:
    data = load()
    stale = 0
    unreachable = 0
    for label, repository, branch, baseline in targets(data):
        result = compare(repository, branch, baseline)
        if result is None:
            print(f"{label:20} unreachable  ({repository})")
            unreachable += 1
            continue
        ahead = result["ahead"]
        if ahead:
            stale += 1
            print(
                f"{label:20} {ahead:4} new  ->  {result['head'][:10]}"
                f"  {result['date'][:10]}  ({repository}@{branch})"
            )
            for line in commits(repository, branch, baseline, args.limit):
                print(f"  {line}")
        else:
            print(f"{label:20}   up to date  ({repository}@{branch})")
    print()
    print(f"{stale} target(s) behind upstream, {unreachable} unreachable.")
    return 1 if unreachable else 0


def files(args: argparse.Namespace) -> int:
    """列出基线到上游之间改动的文件，用于估算适配面。"""
    data = load()
    picked = dict((label, item) for label, *item in
                  ((t[0], t[1], t[2], t[3]) for t in targets(data)))
    if args.target not in picked:
        print(f"Unknown target {args.target!r}. Available: {', '.join(picked)}")
        return 2
    repository, branch, baseline = picked[args.target]
    raw = gh(
        f"repos/{repository}/compare/{baseline}...{branch}",
        ".files | .[] | .status[0:1] + \"  \" + .filename",
    )
    if not raw:
        print("No file list available.")
        return 1
    lines = raw.splitlines()
    print(f"{len(lines)} file(s) changed between {baseline[:10]} and {branch}:")
    entry = (data.get("vendored", {}).get(args.target)
             or data.get("submodules", {}).get(args.target)
             or {})
    custom = {p.rstrip("/") for p in entry.get("customised_paths", [])}
    clashes = 0
    for line in lines:
        name = line.split("  ", 1)[-1]
        hit = any(name == c or name.startswith(c + "/") for c in custom)
        clashes += hit
        print(f"  {line}{'   <-- customised here' if hit else ''}")
    if clashes:
        print()
        print(f"{clashes} upstream change(s) land on customised files; "
              "expect conflicts.")
    return 0


def bump(args: argparse.Namespace) -> int:
    """适配完成后同步基线。改的是文本而非 json.dump，以保留注释和排版。"""
    data = load()
    picked = dict((label, item) for label, *item in
                  ((t[0], t[1], t[2], t[3]) for t in targets(data)))
    if args.target not in picked:
        print(f"Unknown target {args.target!r}. Available: {', '.join(picked)}")
        return 2
    repository, branch, baseline = picked[args.target]
    result = compare(repository, branch, baseline)
    if result is None:
        print("Cannot reach upstream, refusing to bump.")
        return 1
    if not result["ahead"]:
        print(f"{args.target} already at {baseline[:10]}, nothing to bump.")
        return 0

    new_sha = args.commit or result["head"]
    text = TRACKING.read_text(encoding="utf-8")
    if baseline not in text:
        print(f"Baseline {baseline} not found verbatim in {TRACKING.name}.")
        return 1
    TRACKING.write_text(text.replace(baseline, new_sha, 1), encoding="utf-8")
    print(f"{args.target}: {baseline[:10]} -> {new_sha[:10]}")
    print(f"Remember to update commit_date / synced_at in {TRACKING.name}.")
    return 0


def git_text(*args: str, check: bool = True) -> str:
    done = subprocess.run(
        ["git", *args], capture_output=True, text=True,
        errors="replace", cwd=ROOT,
    )
    if check and done.returncode != 0:
        raise SystemExit(f"git {' '.join(args)} failed:\n{done.stderr.strip()}")
    return done.stdout


def git_bytes(*args: str) -> bytes:
    done = subprocess.run(
        ["git", *args], capture_output=True, cwd=ROOT,
    )
    if done.returncode != 0:
        raise SystemExit(f"git {' '.join(args)} failed")
    return done.stdout


def stable_anchor() -> tuple[str, str]:
    """取上游 dev 最新的 Version 提交作为稳定版锚点，不追 dev 尖端。"""
    line = git_text(
        "log", "upstream/dev", "-1", "--grep", "^Version ",
        "--format=%H %s",
    ).strip()
    if not line:
        raise SystemExit("No 'Version ...' commit found on upstream/dev.")
    sha, _, subject = line.partition(" ")
    return sha, subject


def merge_rehearsal(path: str, base: str, head: str) -> tuple[int, bytes]:
    """三方试算一个 merge 文件，返回 (冲突块数, 带标记的合并全文)。"""
    parts = []
    try:
        ours = Path(ROOT / path).read_bytes()
        # autocrlf 下工作树可能是 CRLF，而 blob 是 LF；归一到 LF 才不产生伪冲突
        if b"\r\n" in ours:
            ours = ours.replace(b"\r\n", b"\n")
        for tag, data in (
            ("ours", ours),
            ("base", git_bytes("show", f"{base}:{path}")),
            ("theirs", git_bytes("show", f"{head}:{path}")),
        ):
            f = tempfile.NamedTemporaryFile(delete=False, suffix=f"_{tag}")
            f.write(data)
            f.close()
            parts.append(f.name)
        done = subprocess.run(
            ["git", "merge-file", "-p", "--diff3", *parts],
            capture_output=True, cwd=ROOT,
        )
    finally:
        for name in parts:
            os.unlink(name)
    return done.stdout.count(b"<<<<<<<"), done.stdout


def write_out(name: str, text: str, out: Path) -> None:
    # newline 固定 LF，避免 Windows 默认 CRLF 污染下游 bash 消费
    (out / name).write_text(text, encoding="utf-8", newline="\n")


def prepare(args: argparse.Namespace) -> int:
    """生成到最新稳定版的全套适配材料，只看 Telegram/ 与子模块结构。"""
    data = load()
    td = data["tdesktop"]
    base = args.base or td["commit"]

    remotes = git_text("remote").split()
    if "upstream" not in remotes:
        git_text(
            "remote", "add", "upstream",
            f"https://github.com/{td['repository']}.git",
        )
    print("Fetching upstream/dev ...")
    git_text("fetch", "--no-tags", "upstream", "dev")
    probe = subprocess.run(
        ["git", "cat-file", "-e", base], capture_output=True, cwd=ROOT,
    )
    if probe.returncode != 0:
        print("Baseline unreachable, fetching full history ...")
        git_text("fetch", "--no-tags", "--unshallow", "upstream")

    head, subject = stable_anchor()
    if head == base:
        print(f"Baseline {base[:10]} is already the latest stable anchor:")
        print(f"  {subject}")
        return 0
    version_new = subject.removeprefix("Version ").split(":")[0].strip(" .")
    # 版本号一律从提交反查，避免登记值与实际基线不一致
    base_subject = git_text(
        "log", base, "-1", "--grep", "^Version ", "--format=%s",
    ).strip()
    version_old = (base_subject.removeprefix("Version ").split(":")[0].strip(" .")
                   or td["version"])

    # 只取 Telegram/；.gitmodules 一并拉出但归入子模块组单独提示
    status_lines = git_text(
        "diff", "--name-status", base, head, "--", "Telegram/", ".gitmodules",
    ).splitlines()
    submodule_paths = {
        line.split()[1] for line in git_text(
            "config", "-f", ".gitmodules", "--get-regexp",
            r"^submodule\..*\.path$",
        ).splitlines() if line.strip()
    }
    ours = set(git_text(
        "diff", "--name-only", base, "--", "Telegram/",
    ).splitlines())

    take: list[str] = []
    merge: list[str] = []
    add: list[str] = []
    dele: list[str] = []
    subm: list[tuple[str, str]] = []
    for line in status_lines:
        st, path = line.split("\t", 1)
        if path in submodule_paths:
            subm.append((st, path))
        elif st == "A":
            add.append(path)
        elif st == "D":
            dele.append(path)
        elif path in ours and (ROOT / path).is_file():
            merge.append(path)
        else:
            take.append(path)

    deferred = data.get("deferred", {})

    def note(path: str) -> str:
        for key, reason in deferred.items():
            if path == key or path.startswith(key.rstrip("/") + "/"):
                return f"   [DEFERRED: {reason}]"
        return ""

    numstat: dict[str, str] = {}
    for line in git_text(
        "diff", "--numstat", base, head, "--", "Telegram/",
    ).splitlines():
        added, removed, path = line.split("\t", 2)
        numstat[path] = f"+{added}/-{removed}"

    clean: list[str] = []
    conflict: list[tuple[int, str]] = []
    previews = []
    for path in merge:
        blocks, merged = merge_rehearsal(path, base, head)
        if blocks:
            conflict.append((blocks, path))
            preview = merged.decode("utf-8", errors="replace")
            header = f"\n{'=' * 70}\n{path} ({blocks} 冲突块)\n{'=' * 70}"
            # 只保留冲突块本身（含标记与三方内容）
            keep, inside = [], False
            for line in preview.splitlines():
                if line.startswith("<<<<<<<"):
                    inside = True
                if inside:
                    keep.append(line)
                if line.startswith(">>>>>>>"):
                    inside = False
            previews.append(header + "\n" + "\n".join(keep))
        else:
            clean.append(path)
    conflict.sort(key=lambda item: (-item[0], item[1]))

    total = len(take) + len(merge) + len(add) + len(dele) + len(subm)
    log = git_text("log", "--oneline", f"{base}..{head}", "--", "Telegram/")
    commits = len(log.splitlines())

    lines = []
    lines.append(f"# tdesktop {version_old} -> {version_new} 适配清单\n")
    lines.append(
        f"基线 `{base[:9]}` ({version_old}) -> 目标 `{head[:9]}` ({subject})，"
        f"{commits} 提交，{total} 文件。"
    )
    lines.append(
        "材料可再生：`upstream.py prepare`。冲突详情见 `conflict_preview.txt`。\n"
    )
    lines.append("\n## 概览\n")
    lines.append("| 类别 | 数量 | 动作 |")
    lines.append("|---|---|---|")
    lines.append(f"| take 直接取上游 | {len(take)} | `git checkout {head[:9]} -- <path>` |")
    lines.append(f"| merge 三方合并 | {len(merge)} | `git merge-file`（{len(clean)} 自动 / {len(conflict)} 人工） |")
    lines.append(f"| add 上游新增 | {len(add)} | 从上游 checkout |")
    lines.append(f"| del 上游删除 | {len(dele)} | 需要决策 |")
    lines.append(f"| submodule 指针/结构 | {len(subm)} | 单独处理，不 checkout |")

    lines.append(f"\n## 一、take（直接取上游，{len(take)} 个）\n")
    for path in sorted(take):
        lines.append(f"- `{path}` {numstat.get(path, '')}{note(path)}")

    lines.append(f"\n## 二、merge（三方合并，{len(merge)} 个）\n")
    if merge:
        lines.append("格式：`路径` 上游改动量 / 冲突块数\n")
    for path in sorted(merge):
        blocks = dict(conflict).get(path, 0)
        lines.append(
            f"- `{path}` {numstat.get(path, '')} / "
            f"{blocks} 块{note(path)}"
        )

    lines.append(f"\n## 三、add（上游新增，{len(add)} 个）\n")
    for path in sorted(add):
        lines.append(f"- `{path}`{note(path)}")

    lines.append(f"\n## 四、del（上游删除，{len(dele)} 个）\n")
    for path in dele:
        lines.append(f"- `{path}`{note(path)}")

    lines.append(f"\n## 五、submodule（{len(subm)} 个）\n")
    for st, path in sorted(subm):
        lines.append(f"- `{path}` ({st}){note(path)}")
    lines.append("\n指针 bump 在子模块仓库内完成（合并上游后主仓库登记 gitlink）；")
    lines.append("`.gitmodules` 变化意味着子模块增删，需人工确认是否跟随。")

    out = ROOT / args.out
    out.mkdir(parents=True, exist_ok=True)
    write_out("MANIFEST.md", "\n".join(lines) + "\n", out)
    write_out("conflict_preview.txt", "\n".join(previews), out)
    write_out("batch_take_add.txt", "\n".join(sorted(take) + sorted(add)) + "\n", out)
    write_out("batch_merge_clean.txt", "\n".join(sorted(clean)) + "\n", out)
    write_out(
        "batch_merge_conflict.txt",
        "\n".join(f"{n}\t{p}" for n, p in conflict) + "\n",
        out,
    )
    write_out("upstream_name_status.txt", "\n".join(status_lines) + "\n", out)
    write_out("upstream_log.txt", log, out)
    git_full = subprocess.run(
        ["git", "diff", base, head, "--", "Telegram/"],
        capture_output=True, cwd=ROOT,
    )
    (out / "upstream_full.diff").write_bytes(git_full.stdout)

    print(f"Materials for {version_old} -> {version_new} written to {out}:")
    print(f"  {total} files ({len(take)} take / {len(merge)} merge / {len(add)} add / "
          f"{len(dele)} del / {len(subm)} submodule)")
    print(f"  merge rehearsal: {len(clean)} auto, {len(conflict)} manual")
    for n, path in conflict:
        print(f"    {n:2d} blocks: {path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("status", help="Show how far every target lags upstream")
    p.add_argument("--limit", type=int, default=10, help="Commits to list per target")
    p.set_defaults(run=status)

    p = sub.add_parser("files", help="List files changed upstream for one target")
    p.add_argument("target", help="Target name from status output")
    p.set_defaults(run=files)

    p = sub.add_parser("bump", help="Move one baseline forward after adapting")
    p.add_argument("target", help="Target name from status output")
    p.add_argument("--commit", help="Pin an explicit commit instead of the head")
    p.set_defaults(run=bump)

    p = sub.add_parser(
        "prepare",
        help="Generate adaptation materials up to the latest stable version",
    )
    p.add_argument("--base", help="Base commit, defaults to the tracked baseline")
    p.add_argument(
        "--out", default="build/upstream-adapt",
        help="Output directory relative to the repository root",
    )
    p.set_defaults(run=prepare)

    args = parser.parse_args()
    return args.run(args)


if __name__ == "__main__":
    sys.exit(main())
