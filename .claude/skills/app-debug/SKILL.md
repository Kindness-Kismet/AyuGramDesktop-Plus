---
name: app-debug
description: Use this skill when the user asks to debug, test, or verify AyuGram functionality, take a screenshot, read or change settings, inspect ghost mode, check deleted-message storage, control the running Debug app, restart or stop the app, or says phrases like "调试", "测试", "验证", "截图", "看一下设置", "改个设置", "幽灵模式", "已删除消息", "重启应用", "停掉应用", "观察界面" in this AyuGram Desktop project.
---

# App Debug

实时控制正在运行的 AyuGram Debug 构建：启动、重启、查看与修改设置、查询幽灵模式、查询存储、截图、停止。

服务端只在 `_DEBUG` 下编译，Release 二进制里完全不存在这段代码。应用运行时常驻监听
`127.0.0.1:20100`，一条指令一条连接，服务端回写 `OK[ payload]\n` 或 `ERR <reason>\n` 后立即断开。
payload 是紧凑 JSON，不含换行。

指令在主线程的 Qt 事件循环里同步执行，所以处理函数可以直接访问设置和界面对象，不需要额外加锁；
反过来，耗时指令会阻塞界面。

## 触发与验证策略

用户表达"调试、测试、验证某项功能"时，应读取本 skill。

- 验证业务状态（设置值、幽灵模式、存储开关与库大小）用 `settings.*`、`ghost.status`、`storage.stats`，直接读状态不需要界面配合。
- 验证 UI 状态、控件绑定、视觉变化、布局时，用 `control.list`、`control.click`、`screenshot.take`；验证真实鼠标命中和弹层路由必须人工点击。
- 改设置后验证界面反应：`settings.set <key> <value> + screenshot.take` 一次串联。
- 消息气泡等自绘内容不在控件树里，视觉验证只能靠截图；导航到目标页面用 `control.click`。
- 边界模糊时按验证目标选路径：验证"开关有没有生效"用 `settings.get`；验证"开关改了界面长什么样"用 `screenshot.take`。

## 入口

在项目根目录运行，PowerShell 与 Git Bash 写法相同：

```bash
python .claude/skills/app-debug/scripts/cli.py <command> [args...]
```

一次调用可串多条指令，用裸 `+` 分隔，按书写顺序依次执行；任一条失败即中止：

```bash
python .claude/skills/app-debug/scripts/cli.py settings.set streamerMode true + screenshot.take
```

全部指令先整体解析再开始执行，拼写错误不会让前几条已经生效。

验证其它工作树时设置 `AYUGRAM_DEBUG_ROOT` 为对应项目根目录，进程校验和产物路径会随之切换。

所有指令必须串行；CLI 通过 `build/app-debug-cli.lock` 自动排队，手动编排也要保持顺序。

## 先读哪份指引

指令统一使用"领域.动词 宾语 参数"结构，复合词使用连字符。只阅读当前任务需要的 guide，不要一次性阅读全部表格。

| 任务 | 指引 |
|---|---|
| 应用启动/重启/停止、存活检查、应用信息、指令清单 | `guides/runtime.md` |
| 假会话、假消息、拉黑/影子拉黑验证、打开聊天、测试数据中心切换 | `guides/session.md` |
| 设置键名/导出/读写 | `guides/settings.md` |
| 幽灵模式状态 | `guides/ghost.md` |
| 已删除消息存储统计 | `guides/storage.md` |
| 控件树列举、合成点击 | `guides/controls.md` |
| 截图 | `guides/screenshot.md` |

## 命令一览

完整参数、返回字段和错误信息见对应 guide。

| 命令 | 说明 |
|---|---|
| `app.ensure` / `app.restart` / `app.stop` | 生命周期，CLI 本地执行，不进服务端 |
| `app.ping` / `app.info` / `app.help` | 存活检查、应用信息、指令清单 |
| `app.quit` | 让应用自行退出，`app.stop` 内部用它，一般不直接调 |
| `app.check-update` | 触发一次更新检查，异步，结果看工作目录 `tupdates/` 与日志 |
| `debug.fake-session [userId]` | 创建本地假会话绕过登录，直接进主界面 |
| `debug.reset-background` | 重置聊天背景到默认壁纸，验证默认壁纸改动 |
| `debug.fake-message <text> [--from <userId>] [--blocked] [--shadow-ban]` | 往 Saved Messages 插入本地文本消息，验证渲染与隐藏逻辑 |
| `debug.chats [filter]` | 列出已加载对话的 peerId 与名称，供定位目标 |
| `debug.send-message <peerId> <text>` | 真实发送文本消息到指定对话，需已登录，仅发往自己掌控的测试对话 |
| `debug.open-chat [peerId\|userId]` | 打开指定对话，缺省 Saved Messages |
| `debug.open-archive` | 直接打开归档文件夹，不走抽屉入口 |
| `debug.testmode` | 在生产环境与官方测试数据中心之间切换 |
| `settings.keys` / `settings.dump` | 96 个设置键的键名与全量导出 |
| `settings.get <key>` / `settings.set <key> <value>` | 读写单个设置，写入返回实际生效值 |
| `settings.open <main\|ayu\|search>` | 直接打开设置页：上游主页 / AyuGram 偏好 / 设置搜索页 |
| `ghost.status` | 全局与当前账号的幽灵模式状态，需已登录 |
| `storage.stats` | 保存开关、`ayudata.db` 路径与大小 |
| `screenshot.take` | 截活动窗口到 `build/screenshots/shot-<时间戳>.png` |
| `control.list [filter] [--all]` | 列控件树：序号、objectName、类名、几何、可见性 |
| `control.click <objectName \| #序号>` | 合成鼠标点击，进程内走真实事件分发路径 |

服务端响应格式：成功 `OK [payload]`，失败 `ERR <reason>`；CLI 将 `ERR` 写入 stderr 并返回退出码 1。
常见错误：`unknown command`（拼写）、`usage: ...`（参数数量）、`unknown key`（设置键不存在）、
`expected true or false`（类型不符）、`no active session`（需登录）、`no active window`（需窗口）。

## 构建

本 skill 不负责打包。C++ 改动后运行：

```bash
python scripts/build.py --dev
```

产物保存在 `build/AyuGram-v<版本>-win-x64-dev/`（版本取自 `Telegram/build/version`），
含 `AyuGram.exe` 和 `AyuGram.pdb`。**编译会覆盖 exe，必须先
`cli.py app.stop`**，否则链接器报文件占用。

Debug 应用的 `tdata` 建在产物目录旁边，与正式安装版的账号数据互不影响，
首次启动需要单独登录。

## 典型工作流

串联步骤优先写成一次调用，用 `+` 分隔。

- **改完 C++ 后验证**：`cli.py app.stop` → `python scripts/build.py --dev` → `cli.py app.ensure + screenshot.take`
- **找某个设置的键名**：`cli.py settings.keys`，再 `cli.py settings.get <key>`
- **改设置并看界面反应**：`cli.py settings.set materialSwitches true + screenshot.take`
- **找可点控件**：`cli.py control.list send`，再 `cli.py control.click <objectName>`
- **点击后看效果**：`cli.py control.click mainMenuButton + screenshot.take`
- **查幽灵模式**：`cli.py ghost.status`
- **验证忽略用户（拉黑/影子拉黑隐藏）**：`cli.py debug.fake-session` → `debug.fake-message ... --from <id> --blocked/--shadow-ban` → `settings.set filtersEnabled true` → `debug.open-chat + screenshot.take`，详见 `guides/session.md`
- **查已删除消息有没有在存**：`cli.py storage.stats`
- **结束**：`cli.py app.stop`

## 限制

- 端口在 `AyuInfra::init()` 里启动，晚于 Qt 初始化和账号加载；启动期间连接会被拒，`app.ensure`
  和 `app.restart` 会轮询直至就绪，最多 60 秒。
- Release 构建不挂调试端口，本 skill 仅对 Debug 产物有效。
- 需要 session 的指令（`ghost.status`、`app.info` 的 `userId`）在未登录时报错或缺字段。
- `control.list` / `control.click` 只覆盖有 QWidget 实体的控件；消息气泡等自绘内容不在树里，
  这类验证仍靠 `screenshot.take`。
- Windows 自绘标题栏（最小化/最大化/关闭）不是 QWidget，不在控件树里，
  `control.click` 无法寻址；退出用 `app.quit`，不要试图点击关闭按钮。
- 截图走 `QWidget::grab()`，抓的是窗口自身渲染内容而非屏幕像素；OpenGL 渲染的区域可能抓不全。
- 布尔参数仅接受小写 `true` / `false`。

## 失败处理

脚本失败时优先报告失败原因，不要自行删除 `build/` 或清理用户的其他进程。

### 严禁绕过 cli.py 结束进程

`cli.py app.stop` 先校验端口 PID 属于本仓库 `build/AyuGram-v*-win-x64-dev/AyuGram.exe`，再让应用退出。
端口尚未建立时，仅枚举路径匹配同一形态的进程。

**禁止**用 `taskkill`、`Stop-Process`、`wmic process delete` 按进程名清理 `AyuGram.exe`——
同名进程极可能是用户的正式安装版，强制结束会丢失工作状态。

唯一允许的链路：

1. 端口被占且 `cli.py` 报"被意外 PID 占用"。
2. 先核对路径：`wmic process where "ProcessId=<pid>" get ExecutablePath`。
3. **仅当路径匹配本仓库 `build/AyuGram-v*-win-x64-dev/AyuGram.exe` 时**，才结束它；否则报告给用户决定。

## Skill 维护

命令新增、删除、重命名，或入口、参数、返回值、行为发生变化时，按以下顺序处理：

1. **修改前先备份**：将要修改的每个文件（`SKILL.md`、`scripts/cli.py`、每个实际修改的
   `guides/*.md`）先复制一份，备份命名统一为 `原名.bak.<YYYYMMDD-HHMMSS>`，备份保留，不主动删除。
2. **三层同步**：服务端 `Telegram/SourceFiles/ayu/debug/commands/` 对应域文件
   （新增域要建新文件并在 `debug_commands.cpp` 注册表与 `CMakeLists.txt` 登记）、
   `scripts/cli.py` 的 `register_commands()` 和 `build_server_command()`、
   对应 `guides/*.md` 与本文的命令一览表；未受影响的层不做机械修改。
3. 新增或删除指令域时，同步更新本文"先读哪份指引"索引。
4. **双侧同步**：`.claude/skills/app-debug/` 与 `.agents/skills/app-debug/` 内容保持一致
   （Codex 从 `.agents/skills/` 加载），改完一侧立即复制到另一侧。
5. 服务端改动必须 `python scripts/build.py --dev` 重新编译才生效。

注意 `.gitignore` 有 `Debug/` 规则，Windows 大小写不敏感会连带忽略 `ayu/debug/`，
已用 `!/Telegram/SourceFiles/ayu/debug/` 显式放行，新增该目录下的文件前先确认没有被忽略。

写新指令时注意 nlohmann 的临时对象陷阱：`SettingsJson().items()` 这类写法会让代理持有
已析构对象的引用，range-for 的生命周期延长不覆盖它，必须先存入具名变量：

```cpp
const auto all = SettingsJson();
for (const auto &[key, value] : all.items()) { ... }
```
