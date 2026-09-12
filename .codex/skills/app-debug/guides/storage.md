# 存储统计

## `storage.stats`

返回已删除消息和编辑历史的存储状态 JSON：

- `saveDeletedMessages` / `saveMessagesHistory` / `saveForBots` — 三个开关
- `databasePath` — 数据库绝对路径
- `databaseExists` — 是否存在
- `databaseBytes` — 文件大小

```bash
python .claude/skills/app-debug/scripts/cli.py storage.stats
# {"saveDeletedMessages":true,"databasePath":"<repo>/.../ayudata.db","databaseBytes":32768,...}
```

判断已删除消息是否正在保存：先看三个开关，再看 `databaseExists` / `databaseBytes` 是否
随收消息增长。开关本身也可以用 `settings.set` 修改。
