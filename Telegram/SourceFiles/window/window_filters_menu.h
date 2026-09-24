/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/side_bar_button.h"
#include "ui/widgets/scroll_area.h"

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Window {

class SessionController;

// 全局入口固定在侧栏，账号文件夹由会话列表上方的标签承载。
class FiltersMenu final {
public:
	FiltersMenu(
		not_null<Ui::RpWidget*> parent,
		not_null<SessionController*> session);
	~FiltersMenu();

private:
	const not_null<SessionController*> _session;
	Ui::RpWidget _outer;
	Ui::SideBarButton _menu;
	Ui::ScrollArea _scroll;
	not_null<Ui::VerticalLayout*> _container;

};

} // namespace Window
