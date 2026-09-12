# 控件树与合成交互

## `control.list [filter] [--all]`

列出活动窗口的控件树。每项含：全树序号（`index`）、层级（`depth`）、类名（`class`）、
objectName（`name`）、accessibleName（`accessible`）、文本（`text`）、相对父级与全局几何
（`rect` / `globalRect`）、`visible` / `enabled`。

```bash
python .claude/skills/app-debug/scripts/cli.py control.list
python .claude/skills/app-debug/scripts/cli.py control.list sendButton
python .claude/skills/app-debug/scripts/cli.py control.list --all
# {"total":386,"shown":386,"truncated":false,"widgets":[{"index":0,"depth":0,...}]}
```

- 过滤参数是子串匹配（objectName / 类名 / accessibleName / 文本），只影响展示；
  `index` 始终是全树序号，与 `control.click #序号` 一致。
- `--all` 扫全部顶层窗口（菜单、弹层是独立顶层）。此时序号语义与默认模式不同，
  混用时优先 objectName。
- 最多返回 500 项，超出时 `truncated: true`。

自带 objectName 的常用控件：`mainMenuButton`（主菜单）、`sendButton`（发送）、
`messageInput`（消息输入框）、`menu.*`（主菜单项）、`ayu/*`（AyuGram 设置按钮，
id 即 objectName，如 `ayu/search`、`ayu/cat/ghost`）。没有 objectName 的控件用 `control.list`
查类名/accessibleName 定位，或用 `#序号` 寻址。

## `control.click <objectName | #序号> [--all]`

合成鼠标点击：在目标控件中心做命中测试，找到最深子控件，沿 Enter → Press → Release →
Leave 顺序 `sendEvent`，走与真实点击相同的事件分发路径。不依赖窗口前台与真实光标。

```bash
python .claude/skills/app-debug/scripts/cli.py control.click mainMenuButton
python .claude/skills/app-debug/scripts/cli.py control.click "#42"
# {"class":"Ui::IconButton","name":"mainMenuButton","receiver":"...","point":[44,66],"handled":true}
```

- 寻址：objectName（先活动窗口，再全部顶层）或 `#序号`（对应默认 `control.list` 的
  `index`，UI 变化后序号会漂移，需重新 list）。objectName 找不到时按 accessibleName
  精确匹配作为备选。
- 语义触发优先：目标是 AbstractButton 时直接调 `clicked()`（等价真实点击的最终出口）；
  其余控件改用合成鼠标事件。
- 目标不可见或被禁用时报错；被隐藏页里的控件先导航到对应页面。
- 回调可能销毁控件自身（如菜单项点击后 PopupMenu 整体销毁），返回前已拷贝取值并确认控件存活，
  点击后 `control.list` 可能因控件销毁找不到目标，属正常。
- 少数依赖 `QCursor::pos()` 真实光标位置的代码路径不会触发。
- Windows 自绘标题栏（最小化/最大化/关闭）不是 QWidget，无法寻址；退出用 `app.quit`。
