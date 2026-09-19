---
name: version-bump
description: 更新或校验本项目的发布版本、品牌信息和更新日志，并分别核对官方适配基线。用于升级版本号、准备新版本或检查版本一致性。
---

# 本项目版本更新

## 版本规则

- 发布版本以本项目为准。新一轮发布从当前版本与已发布稳定版中的较高者递增，默认修订号加一；用户指定的目标版本优先，同一轮发布不重复升号。
- 从本项目发布页确认已发布版本，查询失败时不要猜测。新发布的版本必须高于已发布版本，才能触发客户端自动更新。
- `.github/upstream.json` 只记录实际适配的官方版本与提交，允许与本项目发布版本不同；升级发布版本不代表完成上游适配。

## 操作

1. 更新 `.github/CHANGELOG.md`，在顶部添加本项目目标版本，写用户能感知的变化；涉及上游适配时注明官方版本。同步 `changelog.txt` 的版本条目，供构建脚本校验。
2. 用现有工具统一更新版本文件，保留 AyuGram 品牌：

   ```bash
   python scripts/build_support/version.py <本项目版本>
   ```

   覆盖 `Telegram/build/version`、`core/version.h`、两个 Windows 资源文件和应用包清单，不能遗漏数值版本或应用包版本。
3. 校验：

   ```bash
   python .codex/skills/version-bump/scripts/version_sync.py
   ```

上游适配构建通过后，按适配流程登记基线及实际适配提交，再补全官方版本、提交日期和同步日期：

```bash
python .codex/skills/version-bump/scripts/version_sync.py --write
```

校验失败时修正具体差异，不把本项目版本改成官方版本来消除报错。`--write` 只补全基线元数据，不修改发布版本、不推进基线。
