---
name: commit
description: Use this skill when the user asks to commit changes, write a commit message, amend a commit, or prepare changes for commit, or says phrases like "提交", "commit", "写提交信息", "提交代码" in this AyuGram Desktop project. Do not use for push operations unless the user explicitly mentions pushing.
---

# Commit

## 目标

每次提交是一个原子、可追溯、无夹带的变更单元。

## 提交流程

1. `git status --short --branch` + `git diff` 确认所有改动及归属
2. 按**业务作用域**拆分：同一功能的文件进同一提交；不同作用域分开提交
3. 暂存只 `git add <具体路径>`，**禁止 `git add -A` / `git add .`**
4. `git diff --cached --name-status` 复核暂存内容后才可提交

## 暂存检查

- 工作区的常见无关变动，一律不带入：子模块指针变动（`m` 状态）、`build/` 产物、日志、临时脚本
- 无法归属的改动立即停止并询问用户，不自行猜测
- 新增源文件先 `git check-ignore -v <path>` 确认没有被 `.gitignore` 误排除（`ayu/debug/` 有大小写陷阱）

## 提交消息

标题：

- 英文，`type: 短描述` 格式，祈使句，不超过 70 字符
- type 取值：`feat`（新功能）、`fix`（修复）、`build`（构建系统）、`docs`（文档）、`refactor`（重构）、`chore`（杂务）、`test`（测试）

正文：

- **全英文**，与标题一致；提交消息里不出现中文
- 说明**为什么改**，其次才是怎么改；不复述 diff
- 有副作用、约束或关键决策时单独成行写明，方便后人定位

示例：

```
fix: destroy debug server before QApplication teardown

Closing a Debug build raised a CRT abort dialog. The QTcpServer was
owned by a file-level static unique_ptr, so its destructor ran after
QApplication teardown, which is undefined behaviour.

StartServer now destroys it on aboutToQuit. Verified all three exit
paths are clean.
```

## 推送

- 默认只创建本地提交，不推送
- 用户明确要求推送时，先报告待推送提交、远端和分支，再执行 `git push`
- 禁止强推已公开分支

## 停止条件

- 发现无法归属的改动、暂存了意外文件，或任一 git 命令失败
- 停止时保留当前状态，说明已完成步骤和需要用户决定的事项，不自行绕过
