---
name: pull-request
description: Use this skill when the user asks to create, prepare, review, or describe a pull request, or says phrases like "提 PR", "发 PR", "创建 PR", "审查 PR", "review PR", "pull request" in this AyuGram Desktop project.
---

# Pull Request

## 目标

PR 是单一主题的完整交付：改动可读、决策可追溯、结果可验证。

## 分支

- 基于 `merge` 分支创建：`feature/<name>` 或 `fix/<name>`
- PR 目标分支始终是 `merge`（对应上游 tdesktop 的 `dev`）
- 分支内提交遵守 commit skill（原子提交、type 前缀、无夹带）

## 标题

英文一句话概括最终结果，祈使句，不带 type 前缀（PR 标题描述整体，提交各自带前缀）。

## 描述模板

```markdown
## Why

问题的背景与动机，为什么要做这个改动。

## What

改了什么，按用户可感知的行为列出。涉及上游文件的改动注明
已登记 `.github/upstream.json`。

## How

关键实现决策、取舍、约束。不逐文件复述 diff。

## Tested

验证方式与结果。本仓库标准路径：

- `python scripts/build.py --dev` 构建通过
- app-debug skill 快速验证（`app.ensure` → 具体指令 → `app.stop`）
- 涉及 UI 的改动附 `build/screenshots/` 截图

## Risks

副作用、已知局限、后续工作。
```

## 审查要点

审查 PR 时按此清单过：

- [ ] 新代码放对了目录（见 CLAUDE.md"改哪里决策表"）
- [ ] 新文件已登记 `Telegram/CMakeLists.txt` 的 `ayugram_files`
- [ ] 调试代码有 `#ifdef _DEBUG` 包裹
- [ ] 新设置项在 `ayu_settings` 成员 / `to_json` / `from_json` 三处同步
- [ ] rpl 订阅绑定 `lifetime()`
- [ ] 跨线程调用走 `dispatchToMainThread`
- [ ] 卫语句优先，无深嵌套，无过度防御，无异常处理
- [ ] 注释中文、不超两行、不写黑话
- [ ] 改上游文件或 `lib_ui` / `lib_tl` 已登记 upstream.json
- [ ] 提交历史干净（无夹带、无 fixup 残留）

## 规模控制

- 一个 PR 一个主题；重构与功能不混
- 超过约 20 个文件或有跨层改动时，说明拆分理由或拆成多个 PR

## 停止条件

- 无法验证改动效果、描述不了改动动机、或审查发现未登记的定制路径时，先停下问清楚再动
