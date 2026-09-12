# 绕过登录

全新 Debug 构建启动后停在登录界面，而 AyuGram 的设置页（含 Debug 入口）挂在登录后的
`AyuMain` 里，不登录就到不了。下面两条指令解决这个问题，用途不同。

## `debug.fake-session [userId]`

创建一个只在本地存在的 self 用户，直接进主界面。不传 `userId` 时默认 `999999999`。

走的是 tdesktop 恢复本地会话的同一条路径，所以界面联动自然成立：`createSession` 赋值
`_sessionValue`，`window_controller.cpp:175` 收到非空会话就建 SessionController 并
`setupMain()`。

**能测什么**：界面布局、全部设置项、Debug 入口可达性、主题、各 AyuGram 设置页。

**不能测什么**：聊天列表是空的，任何联网操作都会失败。已删除消息、编辑历史、幽灵模式的
实际收发行为都测不了——那些需要真实消息流。

命令内部会把 MTP 的全局失败处理换成空实现。不这么做的话，假密钥发出的第一个授权请求就会
401，`main_account.cpp:459` 会立刻把会话登出。

前置条件：`domain` 已启动、当前账号无会话。已登录时返回 `ERR session already exists`。

假会话只存在于内存，不写入 `tdata`，重启后自动消失，不会在本地留下坏账号。需要时重新调一次。

副作用：有了会话之后 tdesktop 会开始发授权请求，每次都是 401，日志里会持续出现
`AUTH_KEY_UNREGISTERED`（约每 10 秒一条）。这是假密钥的必然结果，不影响界面，但会让 `log.txt`
持续增大，长时间运行要注意。

## `debug.testmode`

切到官方测试数据中心，再在登录界面用测试号登录。拿到的是**真实会话**，有真实消息、真实的
删除和编辑事件。

等价于在登录界面输入 `testmode`（`settings_codes.cpp:147`）。再执行一次切回生产环境。

测试服的账号与生产环境完全隔离，登录方式见 Telegram 官方 API 文档的 test accounts 一节。

前置条件：`domain` 已启动、无会话、且恰好只有一个账号。`addActivated` 会新建账号，多账号时
切换会留下多余的空账号，官方 `testmode` 也是这个前提。

## `debug.fake-message <text> [--from <userId>] [--blocked] [--shadow-ban]`

往假会话的 Saved Messages 插入一条本地构造的文本消息，走 `addNewMessage` 官方路径，
渲染行为与真实消息一致。只存在内存，重启即消失，不触发任何网络请求。
用于无网络观测渲染与隐藏逻辑（如被拉黑/影子拉黑用户的消息隐藏）。

- `--from` 缺省时发送者是 self（Saved Messages 里表现为 out 消息，不会被隐藏链过滤）。
- `--blocked` 走 `hideFromBlocked` 真拉黑路线：需同时开 `filtersEnabled` + `hideFromBlocked`。
- `--shadow-ban` 走影子拉黑路线：只需 `filtersEnabled`，名单可用 `settings.set` 独立维护。

## `debug.chats [filter]`

列出当前账号已加载对话的 peerId 与名称，`filter` 为名称子串、忽略大小写。
`send-message` / `open-chat` 的 peerId 均以本指令输出为准。

peerId 是内部 64 位标识（高位带类型掩码，不是客户端里的 -100 拼接格式），
原样回传即可。只覆盖已加载进内存的对话（主列表）；未出现在结果里的对话先在
应用里点开一次再查。

## `debug.send-message <peerId> <text>`

真实发送文本消息，走官方发送链路（`session->api().sendMessage`），发送侧钩子
（如 auto_space）均生效。需要已登录的真实会话，与 fake-session 不兼容。
仅向自己掌控的测试对话发送，避免打扰真实联系人。

- 返回只表示请求已提交，服务器确认是异步的，验证效果稍等片刻再 `screenshot.take`。
- 不清除目标对话的草稿，不影响输入框。

## `debug.open-chat [peerId|userId]`

打开指定对话并清空导航栈；参数取 `debug.chats` 输出的 peerId，正数也兼容旧
userId 写法，缺省 self（Saved Messages）。配合 `screenshot.take` 做 UI 观测，
免去找列表项点击的不稳定。

## 验证忽略用户（拉黑 / 影子拉黑）

组合用法：`debug.fake-session` 创建会话，`debug.fake-message` 的 `--from <id>` 配合
`--blocked` / `--shadow-ban` 构造被隐藏的消息，`settings.set filtersEnabled true`
（真拉黑路线还需 `hideFromBlocked`）开启过滤，`debug.open-chat` + `screenshot.take`
观测效果，关闭开关对比消息是否恢复。

## 该用哪个

| 目标 | 用 |
|---|---|
| 看界面、改设置、验证 Debug 入口 | `debug.fake-session` |
| 测已删除消息、编辑历史、幽灵模式、过滤器 | `debug.testmode` + 测试号登录 |
| 发文本验证发送链路 | `debug.chats` 定位 + `debug.send-message` |
