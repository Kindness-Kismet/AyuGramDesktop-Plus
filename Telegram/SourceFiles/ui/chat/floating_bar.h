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

// 悬浮条的 2px 描边:亮色暗边,暗色亮边。
inline QColor FloatingBarBorder() {
	const auto base = st::windowBg->c;
	const auto luminance = (base.red() * 299
		+ base.green() * 587
		+ base.blue() * 114) / 1000;
	return (luminance <= 128)
		? QColor(255, 255, 255, 48)
		: QColor(0, 0, 0, 36);
}

// 圆角底加描边。调用方负责关掉 WA_OpaquePaintEvent,四角才能透出父级背景。
inline void PaintFloatingRounded(
		QPainter &p,
		const QRect &rect,
		const QColor &fill,
		int radius) {
	if (rect.isEmpty()) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(fill);
	p.drawRoundedRect(rect, radius, radius);
	p.setPen(QPen(FloatingBarBorder(), 2));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(
		QRectF(rect).adjusted(1, 1, -1, -1),
		radius,
		radius);
}

} // namespace Ui
