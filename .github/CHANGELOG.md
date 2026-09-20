# Changelog

## 7.2.9.1

- 跟进官方 7.2.9，修复部分动画贴纸和自定义表情的显示问题。
- 修复投票、分享、个人资料等界面中的崩溃和内存泄漏问题。
- 修复视频变速播放、长按加速和部分键盘布局下的快捷键问题。
- 改善资料页滚动位置恢复和会话文件夹切换体验。
- 修复中文界面中“发送、编辑、接收时自动加空格”设置仍显示英文的问题。
- macOS 与 Linux 现在可以从本仓库接收自动更新；macOS 为 Intel 和 Apple 芯片分别提供安装包和更新包。

## 7.2.9

Project-specific changes:

- Automatic updates now work: new versions are downloaded and installed from this repository's releases.
- Adaptive layout for wide screens is off by default, and its switch is always visible in Settings.
- Ghost mode switches are hidden by default.
- Message bubble outlines follow the real corner shape.
- The chat list drawer closes when the archive is opened from it.

---

本项目自身的改动：

- 自动更新可用：新版本会从本仓库的发布页下载并安装。
- 宽屏自适应布局默认关闭，开关在设置里常驻显示。
- 幽灵模式的开关默认隐藏。
- 消息气泡的描边沿真实圆角绘制。
- 从聊天列表抽屉打开归档时会自动收起抽屉。

## 7.2.8

Includes all upstream Telegram Desktop 7.2.8 changes, most notably from the 7.2 series:

- Text tool with fonts, alignment and styles in the image editor.
- Drop folders and file sets into a chat to send them as one archive.
- Edit GIFs before sending them with a caption.
- Rate a finished call with stars right in the call panel.
- Make custom emoji out of video stickers.

Project-specific changes:

- Automatic spacing between CJK and Latin text in outgoing messages.
- Simplified Chinese translation built in.
- UI customization options: wide message scale, avatar corners, switch styles.

---

包含官方 Telegram Desktop 7.2.8 的全部更新，其中 7.2 系列的主要变化：

- 图片编辑器新增文字工具，支持字体、对齐与样式。
- 把文件夹或文件集拖进聊天即可作为单个压缩包发送。
- 发送 GIF 前可以先编辑并附上说明文字。
- 通话结束后可以直接在通话面板中用星标评分。
- 可以把视频贴纸制作成自定义表情。

本项目自身的改动：

- 发送消息时自动在中文与英文之间补空格。
- 内置简体中文翻译。
- 界面定制选项：宽消息倍率、头像圆角、开关样式。
