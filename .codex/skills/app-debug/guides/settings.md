# 设置管理

## `settings.keys`

列出所有设置键的 JSON 数组。

```bash
python .claude/skills/app-debug/scripts/cli.py settings.keys
# ["ghostModeSettings","useGlobalGhostMode","saveDeletedMessages",...]
```

## `settings.dump`

导出完整设置 JSON，包含所有键值。

```bash
python .claude/skills/app-debug/scripts/cli.py settings.dump > backup.json
```

## `settings.get <key>`

读取单个设置的当前值：

```bash
python .claude/skills/app-debug/scripts/cli.py settings.get streamerMode
# false

python .claude/skills/app-debug/scripts/cli.py settings.get messageBubbleRadius
# 12
```

## `settings.set <key> <value>`

修改设置并返回实际生效值。`value` 按目标键的类型解释：

- `bool` — 只接受 `true` / `false`
- `int` / `float` — 数字字符串
- `string` — 原样保留

CLI 对含空格的参数自动加引号。

```bash
python .claude/skills/app-debug/scripts/cli.py settings.set streamerMode true
# true

python .claude/skills/app-debug/scripts/cli.py settings.set messageBubbleRadius 18
# 18
```

**注意**：`validate()` 会夹紧越界值，例如半径上限是 23，设为 50 会实际变成 23。写入走
`from_json`，与设置页 setter 的通知路径一致，界面会同步刷新。

## 主题与页面

- `theme.set <dark|light>`：手动切换主题并保存，解除跟随系统。
- `theme.reset-background`：恢复默认聊天壁纸。
- `page.open <settings|ayu|search>`：打开设置主页、AyuGram 偏好或设置搜索。
