---
name: commit
description: Use this skill when the user asks to commit changes, write a commit message, amend a commit, or prepare changes for commit, or says phrases like "提交", "commit", "写提交信息", "提交代码" in this AyuGram Desktop project. Do not use for push operations unless the user explicitly mentions pushing.
---

# 本地提交

每完成一个可独立验证的小步就提交，一个提交只处理一件事。

1. 用 `git status --short --branch` 和 `git diff` 核对改动与任务范围。
2. 按功能分组，逐个 `git add <具体路径>`。构建产物、账号数据和本地过程文档保留在工作区。
3. 新源码先用 `git check-ignore -v <路径>` 确认可被追踪；调试目录有大小写规则。
4. 用 `git diff --cached` 复核内容与验证结果，再提交。
5. 记录提交号和完成内容，继续下一步。

标题使用英文 `type: 简短祈使句`，最多 70 字符。类型可用
`feat`、`fix`、`build`、`docs`、`refactor`、`chore`、`test`。
正文使用英文，说明改动原因、最终行为、验证结果及相关限制。

默认创建本地提交。推送前说明待推送内容、远端与分支，收到明确确认后执行。
公开分支保留已有历史。

遇到归属不明的改动或命令失败，保留现场并说明具体原因；先完成可确认范围内的工作。
