/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_filters_menu.h"

#include "ayu/ayu_settings.h"
#include "boxes/peer_list_controllers.h"
#include "calls/calls_box_controller.h"
#include "data/data_session.h"
#include "data/data_unread_value.h"
#include "data/data_user.h"
#include "dialogs/dialogs_common.h"
#include "dialogs/dialogs_key.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_window.h"

#include <QtGui/QPainterPath>

namespace Window {

FiltersMenu::FiltersMenu(
		not_null<Ui::RpWidget*> parent,
		not_null<SessionController*> session)
: _session(session)
, _outer(parent)
, _menu(&_outer, TextWithEntities{ tr::lng_main_menu(tr::now) },
	st::windowFiltersMainMenu)
, _scroll(&_outer)
, _container(_scroll.setOwnedWidget(
	object_ptr<Ui::VerticalLayout>(&_scroll))) {
	_outer.setObjectName(u"navigationRail"_q);
	_outer.setVisualTabOrder(true);
	_container->setVisualTabOrder(true);
	_menu.setObjectName(u"navigation.menu"_q);
	_menu.setIsMenuButton(true);
	_menu.setAccessibleName(tr::lng_main_menu(tr::now));
	_menu.setShowText(false);
	_menu.setIconOverride(&st::windowNavigationLogo);
	_menu.setClickedCallback([=] {
		_session->widget()->showMainMenu();
	});

	const auto add = [&](
			const QString &name,
			const QString &title,
			const style::icon &icon,
			const style::icon &active,
			Fn<void()> callback) {
		const auto button = _container->add(object_ptr<Ui::SideBarButton>(
			_container,
			TextWithEntities{ title },
			st::windowNavigationButton));
		button->setObjectName(name);
		button->setAccessibleName(title);
		button->setIconOverride(&icon, &active);
		button->setClickedCallback(std::move(callback));
		return button;
	};
	const auto chats = add(
		u"navigation.chats"_q,
		tr::lng_filters_edit_chats(tr::now),
		st::windowNavigationChats,
		st::windowNavigationChatsActive,
		[=] {
			_session->hideLayer();
			_session->closeFolder();
			_session->closeForum();
			_session->closeCommunity();
			_session->setActiveChatsFilter(FilterId(0));
		});
	add(u"navigation.contacts"_q,
		tr::lng_menu_contacts(tr::now),
		st::windowNavigationContacts,
		st::windowNavigationContacts,
		[=] { _session->show(PrepareContactsBox(_session)); });
	add(u"navigation.calls"_q,
		tr::lng_menu_calls(tr::now),
		st::windowNavigationCalls,
		st::windowNavigationCalls,
		[=] { Calls::ShowCallsBox(_session); });
	const auto saved = add(
		u"navigation.saved"_q,
		tr::lng_saved_messages(tr::now),
		st::windowNavigationSaved,
		st::windowNavigationSavedActive,
		[=] { _session->showPeerHistory(_session->session().user()); });
	add(u"navigation.settings"_q,
		tr::lng_menu_settings(tr::now),
		st::windowNavigationSettings,
		st::windowNavigationSettings,
		[=] { _session->showSettings(); });

	_session->activeChatValue(
	) | rpl::on_next([=](Dialogs::Key key) {
		const auto peer = key.peer();
		const auto isSaved = peer && peer->isSelf();
		chats->setActive(!isSaved);
		saved->setActive(isSaved);
	}, _outer.lifetime());
	rpl::combine(
		Data::UnreadStateValue(&_session->session(), FilterId(0)),
		Data::IncludeMutedCounterFoldersValue(),
		AyuSettings::getInstance().hideNotificationCountersValue()
	) | rpl::on_next([=](
			const Dialogs::UnreadState &state,
			bool includeMuted,
			bool hideCounters) {
		const auto muted = state.chatsMuted + state.marksMuted;
		const auto count = hideCounters ? 0 : state.chats + state.marks
			- (includeMuted ? 0 : muted);
		const auto badge = (count <= 0) ? QString()
			: (count > 999) ? u"99+"_q : QString::number(count);
		chats->setBadge(badge, includeMuted && count == muted);
	}, _outer.lifetime());

	const auto gap = st::windowCardGap;
	parent->heightValue() | rpl::on_next([=](int height) {
		const auto width = st::windowFiltersWidth;
		const auto innerWidth = width - gap;
		_outer.setGeometry(0, 0, width, height);
		_menu.resizeToWidth(innerWidth);
		_menu.move(gap, gap);
		_scroll.setGeometry(gap, gap + _menu.height(), innerWidth,
			std::max(0, height - gap * 2 - _menu.height()));
		_container->resizeToWidth(innerWidth);
	}, _outer.lifetime());
	_outer.paintRequest() | rpl::on_next([=](QRect clip) {
		auto p = QPainter(&_outer);
		p.fillRect(clip, st::windowShellBg);
		p.fillRect(QRect(gap, gap, _outer.width() - gap,
			_outer.height() - 2 * gap), st::windowBg);
	}, _outer.lifetime());

	const auto overlay = Ui::CreateChild<Ui::RpWidget>(&_outer);
	overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
	_outer.sizeValue() | rpl::on_next([=](QSize size) {
		overlay->resize(size);
		overlay->raise();
	}, overlay->lifetime());
	overlay->paintRequest() | rpl::on_next([=] {
		const auto card = QRect(gap, gap, _outer.width() - gap,
			_outer.height() - 2 * gap);
		auto p = QPainter(overlay);
		p.setRenderHint(QPainter::Antialiasing);
		auto square = QPainterPath();
		square.addRect(card);
		auto rounded = QPainterPath();
		rounded.addRoundedRect(card, st::windowCardRadius, st::windowCardRadius);
		// 只裁外侧两个圆角，让导航与会话列表共用连续白底。
		p.setClipRect(QRect(card.x(), card.y(), card.width() / 2, card.height()));
		p.fillPath(square.subtracted(rounded), st::windowShellBg);
		p.setClipping(false);
		p.fillRect(_outer.width() - st::lineWidth, gap, st::lineWidth,
			card.height(), st::windowDividerFg);
	}, overlay->lifetime());
	_outer.show();
	_menu.show();
	_scroll.show();
	overlay->show();
}

FiltersMenu::~FiltersMenu() = default;

} // namespace Window
