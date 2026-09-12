---
name: version-bump
description: "Verify and record the official version bump after adapting upstream tdesktop changes: the version number always follows upstream, this skill checks the four version locations, brand preservation, and upstream.json bookkeeping. Use when the user asks to sync or verify the version, or says phrases like \"跟进版本号\", \"升版本\", \"更新版本号\", \"校验版本\" in this AyuGram Desktop project."
---

# Version Bump（官方版本跟进）

## 语义

版本号跟随官方 tdesktop，不自行发明。上游适配（upstream-diff skill）完成时，
`Telegram/build/version`、`version.h`、两个 `.rc` 已随适配更新为官方版本号并保留
AyuGram 品牌。本 skill 是适配流程的收尾环节：核对版本一致性，补全登记字段。

不存在独立的发版提交与更新日志；版本号变化包含在上游适配提交里。

## 触发时机

- 上游适配完成、构建通过之后（紧跟 `upstream.py bump` 执行）
- 用户要求校验版本一致性

## 一条命令

```bash
python .claude/skills/version-bump/scripts/version_sync.py           # 校验并报告
python .claude/skills/version-bump/scripts/version_sync.py --write   # 校验 + 补全 upstream.json
```

脚本核对的清单（全部 PASS 才算收尾完成）：

| 位置 | 校验内容 |
|---|---|
| `Telegram/build/version` | `AppVersion` = major×1000000 + minor×1000 + patch；`AppVersionStr` / `StrSmall` / `StrMajor` 与数值自洽 |
| `Telegram/SourceFiles/core/version.h` | `AppVersion` / `AppVersionStr` 与 build/version 一致；`AppId`（`…D666` 结尾 GUID）、`AppName`、`AppNameOld`、`AppFile` 保持 AyuGram 值 |
| `Telegram/Resources/winrc/Telegram.rc` | `FileVersion` / `ProductVersion` 为 `{版本}.0`；`CompanyName` = Radolyn Labs、`ProductName` / `FileDescription` = AyuGram Desktop |
| `Telegram/Resources/winrc/Updater.rc` | 同上，`FileDescription` 为 AyuGram Desktop Updater |
| `.github/upstream.json` | 基线提交的标题以 `Version {版本}` 开头；`version`、`commit_date`（基线提交日期）一致；`merge_commit` 可达；`synced_at` 存在 |

`--write` 只自动补全 `version` / `commit_date` / `synced_at` 三个字段（字节级替换，
保留 JSON 注释与排版）；`merge_commit` 需按适配提交人工确认后写入。

## 停止条件

- 版本文件互相不一致或品牌字段被官方值覆盖：说明适配时解决冲突出错，
  报告差异并停止，不自动改版本文件
- `upstream.json` 基线提交的版本标题与 `build/version` 矛盾：可能是基线未 bump
  或版本文件漏更新，先回到 upstream-diff 流程核对，再重跑本脚本
- 任何位置出现低于当前生效版本的回退迹象时停止并询问

## 与其他 skill 的边界

- `upstream-diff` 的 `bump` 只替换基线 SHA；本 skill 负责其余登记字段的补全与校验
- 版本号涉及构建产物路径（`build/AyuGram-v{版本}-win-x64-dev`）与 `app-debug`
  的产物定位，适配完成后的首次 `app.ensure` 会自动使用新目录，旧目录的 `tdata`
  需要人工迁移一次

## 提交

`--write` 改动了 `upstream.json` 时单独提交，标题：

```
chore: sync version tracking to <版本>
```

版本文件本身不产生额外提交（已包含在上游适配提交里）。
