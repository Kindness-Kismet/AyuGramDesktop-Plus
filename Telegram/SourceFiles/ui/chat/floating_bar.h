/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/painter.h"
#include "styles/palette.h"
#include "styles/style_window.h"

namespace Ui {

// 通知条与聊天标题连续排列，仅在底边分隔。
inline void PaintChatBar(
		QPainter &p,
		const QRect &rect,
		const QColor &fill) {
	if (rect.isEmpty()) {
		return;
	}
	p.fillRect(rect, fill);
	p.fillRect(rect.x(), rect.bottom(), rect.width(), st::lineWidth,
		st::windowDividerFg);
}

} // namespace Ui
