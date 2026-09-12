# 截图

## `screenshot.take`

截取活动窗口并保存为 PNG。CLI 自动生成路径 `build/screenshots/shot-<时间戳>.png`，
不接受路径参数。

```bash
python .claude/skills/app-debug/scripts/cli.py screenshot.take
# {"height":938,"path":"<repo>/build/screenshots/shot-20260901-190750.png","width":1200}
```

**前提**：必须有活动窗口（`hasWindow: true`）。

截图走 `QWidget::grab()`，抓窗口自身渲染内容而非屏幕像素，被其他窗口遮挡时也能正常截图；
但 OpenGL 渲染的区域可能抓不全。

消息气泡等自绘内容不在控件树里，验证这类视觉效果只能靠截图，配合
`control.list` / `control.click` 做前置导航。
