# 运行控制与观察

## 服务端指令

| method | arg | 作用 |
|---|---|---|
| `app.ping` | 空 | 心跳检测，返回 `pong`，验证服务端连通性。 |
| `app.info` | 空 | 返回版本、配置、工作目录、会话与窗口状态 JSON。 |
| `app.help` | 空 | 列出服务端已注册的全部指令名。 |
| `app.quit` | 空 | 让应用走正常退出流程；退出动作排在事件循环尾部，确保 `OK` 写完 socket 再退。 |

`app.quit` 一般不直接调，`app.stop` 内部先发它。

`app.info` 返回字段：`version` / `versionCode` / `configuration` / `workingDir` / `debugLogs` / `hasSession` / `hasWindow` / `userId`（已登录时）。

## CLI 本地指令

不进服务端，由 CLI 自己完成。

| method | arg | 作用 |
|---|---|---|
| `app.ensure` | 空 | 检查 Debug 应用是否运行，未运行时启动并等待端口就绪（最多 60 秒）。 |
| `app.restart` | 空 | 先停止再启动，等到端口就绪。改完 C++ 重新编译后用它启动新构建。 |
| `app.stop` | 空 | 校验端口 PID 的可执行文件路径必须匹配本仓库 `build/AyuGram-v*-win-x64-dev/AyuGram.exe`，再让应用自行退出；失败才回落到强制结束。端口未建立时只枚举路径匹配同一形态的进程。 |

```bash
python .claude/skills/app-debug/scripts/cli.py app.ensure
# 应用已就绪。
```

单条指令的读超时是 180 秒，长操作不会被 CLI 提前中断。PID 路径不匹配时直接报错拒绝操作，不会错误结束正式安装版。

## 崩溃排查

Debug 构建装了未处理异常过滤器：进程崩溃时把异常码与符号化调用栈写进
工作目录 `crash.log`（工作目录见 `app.info` 的 workingDir）。

```bash
python .claude/skills/app-debug/scripts/cli.py settings.open ayu   # 复现操作
cat build/AyuGram-v*-win-x64-dev/crash.log                     # 读调用栈定位
```

复现崩溃后先查看 crash.log 的栈帧，直接给到文件与行号，不要靠猜。
