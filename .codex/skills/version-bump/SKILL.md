---
name: version-bump
description: 更新本应用的发布版本号并编写更新说明。用于升级版本号、准备新版本或查看当前版本。
---

# 版本更新

本应用版本号只保存在 `Telegram/build/version`。`version.h` 与 Windows 资源里的版本数值
在 CMake 配置阶段从它生成，不需要手动修改。

版本号前三段等于 `.github/upstream.json` 登记的官方版本，第四段是本项目修订号：
首次发布用 `x.y.z`，之后依次用 `x.y.z.1`、`x.y.z.2`。官方版本的变化走 upstream-sync。

1. 查询本仓库最新稳定发布，确认当前官方版本下最大的修订号。
   查询失败时说明原因，待获得确切版本后继续。
2. 选择比已发布版本更高的修订号，同一轮发布使用同一个目标版本。
3. 写好更新说明后修改版本号：

   ```bash
   python scripts/build_support/version.py <官方版本>[.<本库修订号>]
   ```

   脚本只改 `Telegram/build/version`，并检查前三段与已登记的官方版本一致。

`AppVersion` 使用官方整数编码，负责数据兼容；
`AppUpdateVersion` 负责打包、版本比较和更新索引。

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
python scripts/build_support/version.py
python scripts/release_notes.py .github/CHANGELOG.md <本项目版本> build/release-notes.md
```

第一条显示当前版本，第二条确认发布正文只包含目标版本及对应变化。
推送到 `main` 且版本文件有变化时，发布工作流会自动打标签、构建并公开发布。
