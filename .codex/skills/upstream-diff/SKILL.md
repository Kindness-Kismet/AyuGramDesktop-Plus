---
name: upstream-diff
description: "Find what changed upstream since AyuGram last adapted, and sync the tracked baseline once adaptation is done. Use when the user asks whether Telegram Desktop has new changes, how far behind upstream the fork is, what needs adapting, or asks to record a completed upstream sync."
---

# 上游变化与适配记录

`.github/upstream.json` 记录当前已经适配的上游状态。查询结果以实际仓库和接口响应为准，
报告包含新增提交数、涉及的定制文件及需要适配的功能。

## 查询与材料

```bash
python .codex/skills/upstream-diff/scripts/upstream.py status
python .codex/skills/upstream-diff/scripts/upstream.py files tdesktop
python .codex/skills/upstream-diff/scripts/upstream.py prepare
```

`status --limit N` 控制提交列表长度。`files` 的对象名称以 `status` 输出为准，
例如 `tdesktop`、`Telegram/lib_ui`。定制路径重叠表示需要重点检查，是否冲突由实际合并结果决定。

`prepare` 从已登记基线比较到上游 dev 最新稳定版本提交，提取 `Telegram/` 范围及
子模块变化。首次运行会获取上游历史；材料保存在 `build/upstream-adapt/`：

- `MANIFEST.md`：分类、改动量、冲突预演和已延后项目。
- `batch_take_add.txt`：可直接采用的文件。
- `batch_merge_clean.txt`、`batch_merge_conflict.txt`：需合并的文件。
- `conflict_preview.txt`：三方差异，辅助处理冲突。
- `upstream_name_status.txt`、`upstream_log.txt`、`upstream_full.diff`：原始证据。

按清单完成适配、构建和验证；新出现的延后事项记入 `deferred`。
查询失败时报告对象和原因，恢复访问后继续。基线与实际代码不一致时先查提交历史。

## 子模块

- `lib_ui`、`lib_tl`、`codegen` 是定制仓库，各自记录上游起点和定制路径。
- `cmake` 跟随官方提交，定制由构建脚本应用，检查补丁位置是否仍匹配。
- `lib_icu` 是本项目维护的库，按配置中的仓库信息决定比较范围。

子模块指针随已验证的改动更新。查询任务保持只读；合并、基线更新和推送按本次授权范围执行。

## 更新基线

用户确认适配完成且构建通过后，明确指定已经适配的提交：

```bash
python .codex/skills/upstream-diff/scripts/upstream.py bump <对象> --commit <提交号>
```

同步核对提交日期、适配日期、官方版本、合并提交与定制路径。
基线更新单独提交，正文说明完成范围和仍然延后的事项。
