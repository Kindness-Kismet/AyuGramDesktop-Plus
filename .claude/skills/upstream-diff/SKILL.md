---
name: upstream-diff
description: "Find what changed upstream since AyuGram last adapted, and sync the tracked baseline once adaptation is done. Use when the user asks whether Telegram Desktop has new changes, how far behind upstream the fork is, what needs adapting, or asks to record a completed upstream sync."
---

# Upstream Diff

## 目标

回答两个问题并保持基线可信：

- 官方从上次适配之后改了什么，其中哪些落在 AyuGram 定制过的文件上
- 适配完成后，把 `.github/upstream.json` 的基线推进到新的提交

`.github/upstream.json` 是唯一的基线来源。它记录的不是"官方最新版本"，而是"AyuGram 已经适配到哪里"。两者的差集就是待办工作。

## 权限与停止条件

- 查询和比较随时可做，不需要确认。
- 推进基线只在用户确认适配完成、并且构建通过之后执行。
- 不自行合并上游代码、不改子模块指针、不推送。
- `gh` 未登录或 API 失败时停止并报告，不用缓存或猜测的数字充当结果。
- 基线与仓库实际状态矛盾时停止并询问，不直接改写。

## 追踪的对象

`.github/upstream.json` 覆盖四类，各自的基线字段不同：

- `tdesktop` — 主仓库，基线是 `commit`，来自上一次合并的官方父提交
- `submodules.Telegram/lib_ui` — fork 子模块（Kindness-Kismet/lib_ui，分支 master-ui），基线是 `fork_point`
- `submodules.Telegram/lib_tl`、`submodules.Telegram/codegen` — fork 的子模块，基线是 `fork_point`
- `submodules.cmake` — 官方子模块，无 fork，`fork_point` 登记跟随的官方提交；定制由 `scripts/build_support/cmake_patch.py` 在构建时动态 patch，`customised_paths` 列 patch 目标，上游动到这些文件时锚点可能漂移
- `submodules.Telegram/lib_icu` — AyuGram 自建，`repository` 为 `null`，不参与比较
- `deferred` — 有意识不跟随的上游改动及原因，prepare 时标注到清单里

带 `customised_paths` 的条目会在文件比较时标出重叠，那是冲突的预警。

## 1. 查看落后情况

```
python .claude/skills/upstream-diff/scripts/upstream.py status
```

输出每个对象落后多少提交，以及最近若干条提交标题。`--limit N` 控制列出的条数。

判读要点：

- `up to date` 表示基线已经等于上游头部
- `N new` 是相对基线的新增提交数，不是版本号差距
- `unreachable` 表示 API 失败，此时不要把它当成"已同步"

## 2. 生成适配材料

适配的起点。

```
python .claude/skills/upstream-diff/scripts/upstream.py prepare
```

锚点是登记的 tdesktop 基线，目标自动取上游 dev 最新的 `Version X.Y.Z` 提交
（不追 dev 分支最新提交）；只提取 `Telegram/` 范围的改动，`.gitmodules` 归入子模块组。
首次在新机器上运行会自动 fetch 上游历史。

材料输出到 `build/upstream-adapt/`（可再生，不进 git）：

| 文件 | 用途 |
|---|---|
| `MANIFEST.md` | take / merge / add / del / submodule 分类清单，含双方 numstat、冲突块预演、DEFERRED 标注 |
| `conflict_preview.txt` | 每个冲突文件的三方冲突块（diff3 格式） |
| `batch_take_add.txt` | 可直接取上游的文件名单 |
| `batch_merge_clean.txt` | 三方合并无冲突的文件名单 |
| `batch_merge_conflict.txt` | 需人工解冲突的文件（冲突块数 + 路径） |
| `upstream_name_status.txt` / `upstream_log.txt` / `upstream_full.diff` | 原始清单 / 提交列表 / 完整 diff |

`upstream.json` 的 `deferred` 段记录有意识不跟随的上游改动，prepare 会按路径
前缀在清单里标注 `[DEFERRED: 原因]`；新决策应及时登记，防止清单与实际状态脱节。

使用顺序：`batch_take_add.txt` 逐个 `git checkout <head> -- <path>` →
`batch_merge_clean.txt` 走 `git merge-file` → `batch_merge_conflict.txt` 对照
`conflict_preview.txt` 人工解 → 编译验证 → `bump`。

## 3. 估算适配范围

```
python .claude/skills/upstream-diff/scripts/upstream.py files <target>
```

`<target>` 用 `status` 输出里的名字，例如 `tdesktop`、`Telegram/lib_ui`、`Telegram/lib_tl`。

输出改动文件列表，落在 `customised_paths` 上的会标 `<-- customised here`，并在末尾汇总数量。这些文件必然产生合并冲突，优先查看。

`tdesktop` 的改动量通常很大，先按目录归类再判断哪些与 AyuGram 定制相关，不要逐条读完。

## 4. 报告

向用户说明时给出：

- 每个对象落后多少提交
- 哪些改动落在定制文件上，这决定工作量
- 与 AyuGram 功能直接相关的上游改动
- 明确区分"官方已发布新版本"和"我们尚未适配"，前者不构成必须跟进

不要在这一步给出适配方案之外的承诺，也不要修改任何文件。

## 5. 推进基线

只有用户确认适配完成并且构建通过后才执行：

```
python .claude/skills/upstream-diff/scripts/upstream.py bump <target>
```

默认推进到上游头部；需要停在中间某个提交时用 `--commit <sha>`。

脚本只替换基线 SHA，因此还要手工更新同一条目的：

- `commit_date` — 新基线提交的日期
- `synced_at` — 完成适配的日期
- `tdesktop` 的 `version` 和 `merge_commit`，如果本次跟进了版本号
- `Telegram/lib_ui` 的 `customised_paths`，如果定制文件集合发生变化

`Telegram/lib_ui` 的定制文件集合可以这样重新确认：

```
curl -sSL https://codeload.github.com/desktop-app/lib_ui/tar.gz/<sha> | tar xz -C <tmp> --strip-components=1
diff -rq <tmp> Telegram/lib_ui
```

## 6. 提交

基线推进单独提交，不与适配代码混在一起：

- 标题：`chore: track upstream <target> at <short-sha>`
- 正文说明本次适配范围，以及仍然搁置的上游改动

工作区若有其他未提交改动，先按作用域分开提交，再提交基线。
