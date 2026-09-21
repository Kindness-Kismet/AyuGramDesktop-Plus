---
name: version-bump
description: 更新或校验本项目的发布版本、品牌信息和更新日志，并分别核对官方适配基线。用于升级版本号、准备新版本或检查版本一致性。
---

# 本项目版本更新

## 版本规则

- 发布版本的前三段必须等于 `.github/upstream.json` 记录的官方适配版本。首次发布可用 `x.y.z`；同一官方版本的后续发布依次使用 `x.y.z.1`、`x.y.z.2`，不能把第四段当作 closed alpha。
- 从本项目发布页确认同一官方版本已发布的最大修订号，查询失败时不要猜测。新发布的修订号必须更大，同一轮发布不重复升号。
- `AppVersion` 保留官方整数编码，供存储格式和上游迁移阈值使用；`AppUpdateVersion` 才用于 Packer、更新比较和 `current6`。不要互换两者。
- `.github/upstream.json` 只记录实际适配的官方版本与提交。没有完成上游适配时不能改前三段，也不能伪造基线。

## 更新日志

默认只写**上一版本号 → 目标版本号**这一段差。上一版本取本仓库已发布的最新稳定标签，不要把更早版本的累计变化再写一遍，也不要从上游完整历史复制条目。

只写用户能看见、能用到的净变化：新功能、界面变化、使用时会碰到的问题修复。真正完成官方适配时，写「适配官方 Telegram Desktop x.y.z」。

不写：

- 构建、CI、测试、脚本、重构、调试信息和内部实现
- 提交标题、文件名、函数名、协议名、格式名，以及用户界面上看不到的说法
- 隐藏功能本身、解锁方式或解锁提示
- 上一版本及更早已写过的内容
- 没有代码、行为或验证证据的条目，也不为凑条数推测

默认整文件覆写 `.github/CHANGELOG.md`：只留目标版本这一个小节，不保留更早版本。除非用户明确要求改旧日志，否则不要追加、插入或改写旧条目。

`changelog.txt` 同步写入当前版本条目，写得更短，供构建脚本和 Linux 软件源说明校验。未要求时不要把本次变化补记到旧版本下面。

## 操作

1. 先从 GitHub Release 查询上一个已发布的稳定标签；查询失败时不要猜测。用上一版本号与目标版本号框定更新日志，不要扩大范围。
2. 把 `.github/CHANGELOG.md` 覆写成目标版本的唯一小节，并同步 `changelog.txt` 的当前版本条目。
3. 若前三段要升级，先按 `upstream-diff` 流程完成适配并推进 `tdesktop` 基线，同时核对 `version`、`commit`、`commit_date`、`synced_at` 和 `merge_commit`。版本工具会拒绝与已登记基线不一致的前三段。
4. 用现有工具统一更新版本文件，保留 AyuGram 品牌：

   ```bash
   python scripts/build_support/version.py <官方版本>[.<本库修订号>]
   ```

   覆盖 `Telegram/build/version`、`core/version.h`、两个 Windows 资源文件和应用包清单，不能遗漏数值版本或应用包版本。
5. 校验版本一致性，并生成将要发布的单版本正文人工复核：

   ```bash
   python .codex/skills/version-bump/scripts/version_sync.py
   python scripts/release_notes.py .github/CHANGELOG.md <本项目版本> build/release-notes.md
   ```

   `build/release-notes.md` 必须只包含目标版本小节，且每条都能追溯到上一版本号到目标版本号之间的真实用户变化。发布流程也使用同一提取脚本，不会把历史小节带入 Release。

上游适配构建通过后，按适配流程登记基线及实际适配提交，再补全官方版本、提交日期和同步日期：

```bash
python .codex/skills/version-bump/scripts/version_sync.py --write
```

校验失败时修正具体差异。`--write` 只从已登记的官方提交补全基线元数据，不修改发布版本、不推进基线。
