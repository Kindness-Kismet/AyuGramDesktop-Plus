---
name: version-bump
description: 更新或校验本项目的发布版本、品牌信息和更新日志，并分别核对官方适配基线。用于升级版本号、准备新版本或检查版本一致性。
---

# 版本更新

发布版本前三段对应 `.github/upstream.json` 中已适配的官方版本。
首次发布可用 `x.y.z`，后续修订依次使用 `x.y.z.1`、`x.y.z.2`。

1. 查询本仓库最新稳定发布，确认当前官方版本下最大的修订号。
   查询失败时说明原因，待获得确切版本后继续。
2. 官方前三段变化时，先完成上游适配与构建，再登记实际基线。
3. 选择比已发布版本更高的修订号，同一轮发布使用同一个目标版本。
4. 通过现有工具统一修改品牌和版本文件：

   ```bash
   python scripts/build_support/version.py <官方版本>[.<本库修订号>]
   ```

`AppVersion` 使用官方整数编码，负责数据兼容；
`AppUpdateVersion` 负责打包、版本比较和更新索引。第四段用于本项目修订。

## 更新说明

范围为上一个已发布稳定版本到目标版本之间的实际变化。
只写用户能直接看到或使用的功能、界面及问题修复，内容须有代码和验证依据。
完成官方适配时写“适配官方 Telegram Desktop x.y.z”。

默认将 `.github/CHANGELOG.md` 更新为目标版本的唯一小节，
同步 `changelog.txt` 的当前版本条目，后者更简短。
构建、测试、重构和内部实现写在提交或本地过程记录中；隐藏功能信息保留在内部。

用户明确要求维护旧日志时，按指定版本和范围处理。

## 校验

```bash
python .codex/skills/version-bump/scripts/version_sync.py
python scripts/release_notes.py .github/CHANGELOG.md <本项目版本> build/release-notes.md
```

检查版本文件和 Windows 资源一致，发布正文只包含目标版本及对应变化。
上游适配完成后可用 `version_sync.py --write` 补全已登记提交的基线元数据。
此操作保持发布版本和基线提交号不变。校验失败时修正报告中的具体差异。
