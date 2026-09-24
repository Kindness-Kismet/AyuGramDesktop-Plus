# 固定会话场景

先在独立配置中创建假会话，再生成固定列表：

```bash
AYUGRAM_DEBUG_PROFILE=scenarios python .codex/skills/app-debug/scripts/cli.py app.ensure + debug.fake-session + scenario.seed
AYUGRAM_DEBUG_PROFILE=scenarios python .codex/skills/app-debug/scripts/cli.py scenario.open discussion + screenshot.take
```

- `scenario.seed`：向本进程创建的假会话注入固定数据，重复调用复用同一列表。
- `scenario.list`：返回场景的 `key`、`name`、`peerId`，可在初始化前查看。
- `scenario.open <key>`：打开列表中的场景，话题场景直接进入真实话题输入区。
- `app.info` 的 `fakeSession` 表示当前会话是否由本进程创建，重启后失效。

| 键名 | 展示内容 |
|---|---|
| private | 普通私聊与输入区 |
| contact | 陌生人添加／屏蔽提示 |
| blocked | 已屏蔽用户的底部动作 |
| bot | 机器人启动按钮 |
| channel | 只读频道、置顶及通知按钮 |
| discussion | 频道底部讨论入口 |
| join | 未加入频道的加入按钮 |
| restricted | 群聊发送限制 |
| translate | 翻译与置顶组合 |
| requests | 加入申请与置顶组合 |
| topic | 话题输入区 |
| call | 计划通话、申请与置顶堆叠 |
| business | 商业机器人提示 |
| paid | 付费消息提示 |

这些场景使用正式界面的数据与控件路径，数据存在内存中；按钮仍保留原有业务行为。
布局检查使用截图、控件树和内部悬停，涉及发送、加入、通话等业务操作时单独安排测试。
修改输入文字可用于多行布局验证，测试数据只写入独立配置。
