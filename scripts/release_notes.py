#!/usr/bin/env python3
"""从累计更新日志中提取单个版本的 GitHub Release 正文。"""

import argparse
import re
from pathlib import Path


_VERSION_HEADING = re.compile(r"^##[ \t]+([^\r\n]+?)[ \t]*$", re.MULTILINE)


def extract_release_notes(changelog: str, version: str) -> str:
    """按二级版本标题提取唯一小节，避免把旧版本历史发布给用户。"""
    headings = list(_VERSION_HEADING.finditer(changelog))
    matches = [heading for heading in headings if heading.group(1) == version]
    if not matches:
        raise ValueError(f"更新日志中缺少版本 {version} 的二级标题")
    if len(matches) > 1:
        raise ValueError(f"更新日志中版本 {version} 的二级标题重复")

    heading = matches[0]
    following = next(
        (candidate for candidate in headings if candidate.start() > heading.start()),
        None,
    )
    end = following.start() if following else len(changelog)
    notes = changelog[heading.start():end].strip()
    if not notes[len(heading.group(0)):].strip():
        raise ValueError(f"版本 {version} 的发布说明为空")
    return notes + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("changelog", type=Path)
    parser.add_argument("version")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    changelog = args.changelog.read_text(encoding="utf-8")
    notes = extract_release_notes(changelog, args.version)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(notes, encoding="utf-8")
    print(f"已生成 {args.version} 的发布说明：{args.output}")


if __name__ == "__main__":
    main()
