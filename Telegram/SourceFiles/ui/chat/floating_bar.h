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

#include <QtWidgets/QGraphicsEffect>

namespace Ui {

class ChatControlSurface final : public QGraphicsEffect {
public:
	ChatControlSurface(int radius, bool outline)
	: _radius(radius)
	, _outline(outline) {
	}

protected:
	void draw(QPainter *p) override {
		auto offset = QPoint();
		const auto source = sourcePixmap(Qt::LogicalCoordinates, &offset, NoPad);
		if (source.isNull()) {
			return;
		}
		auto surface = QPixmap(source.size());
		surface.setDevicePixelRatio(source.devicePixelRatio());
		surface.fill(Qt::transparent);
		const auto rect = QRectF(QPointF(),
			QSizeF(source.size()) / source.devicePixelRatio());
		const auto radius = std::min(qreal(_radius), rect.height() / 2.);
		{
			auto painter = QPainter(&surface);
			painter.setRenderHint(QPainter::Antialiasing);
			painter.setPen(Qt::NoPen);
			painter.setBrush(Qt::white);
			painter.drawRoundedRect(rect, radius, radius);
			painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
			painter.drawPixmap(0, 0, source);
			if (_outline) {
				painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
				const auto halfStroke = st::lineWidth / 2.;
				painter.setPen(QPen(st::windowDividerFg->c, st::lineWidth));
				painter.setBrush(Qt::NoBrush);
				painter.drawRoundedRect(rect.adjusted(
					halfStroke, halfStroke, -halfStroke, -halfStroke),
					radius - halfStroke, radius - halfStroke);
			}
		}
		p->drawPixmap(offset, surface);
	}

private:
	const int _radius;
	const bool _outline;
};

inline void ApplyChatControlSurface(
		not_null<QWidget*> widget,
		int radius,
		bool outline = true) {
	widget->setGraphicsEffect(new ChatControlSurface(radius, outline));
}

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
