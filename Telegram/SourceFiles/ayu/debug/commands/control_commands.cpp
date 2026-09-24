#ifdef _DEBUG
#include "ayu/debug/commands/commands_internal.h"

#include "core/application.h"
#include "ui/abstract_button.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/fields/input_field.h"
#include "window/window_controller.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEnterEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
#include <QWidget>

namespace AyuDebug::Commands {
namespace {

using json = nlohmann::json;

// ---- UI 探查与合成交互 ----

// list 的 #index 与 click 的序号寻址共用这份先序 DFS 顺序，改遍历顺序等于
// 让所有序号失效，勿动。
struct WidgetInfo {
	QPointer<QWidget> widget;
	QWidget *root = nullptr; // 所属顶层窗口，visible/enabled 判定基准
	int depth = 0;
};

[[nodiscard]] std::vector<WidgetInfo> CollectWidgets(bool allTopLevels) {
	auto result = std::vector<WidgetInfo>();
	auto stack = std::vector<std::pair<QWidget*, int>>();
	const auto pushRoot = [&](QWidget *root) {
		stack.emplace_back(root, 0);
	};
	if (allTopLevels) {
		const auto tops = QApplication::topLevelWidgets();
		for (auto i = tops.rbegin(); i != tops.rend(); ++i) {
			pushRoot(*i);
		}
	} else {
		const auto window = Core::App().activeWindow();
		if (window) {
			pushRoot(window->widget());
		}
	}
	while (!stack.empty()) {
		const auto [widget, depth] = stack.back();
		stack.pop_back();
		if (!widget) {
			continue;
		}
		result.push_back({ widget, widget->window(), depth });
		const auto children = widget->findChildren<QWidget*>(
			QString(),
			Qt::FindDirectChildrenOnly);
		// 逆序入栈，出栈即恢复先序。
		for (auto i = children.rbegin(); i != children.rend(); ++i) {
			stack.emplace_back(*i, depth + 1);
		}
	}
	return result;
}

[[nodiscard]] QString WidgetText(const QWidget *widget) {
	if (const auto button = qobject_cast<const QAbstractButton*>(widget)) {
		return button->text();
	} else if (const auto label = qobject_cast<const QLabel*>(widget)) {
		return label->text();
	}
	return QString();
}

[[nodiscard]] json DescribeWidget(const WidgetInfo &info) {
	const auto widget = info.widget.data();
	const auto topLeft = widget->mapToGlobal(QPoint(0, 0));
	return json{
		{ "index", nullptr }, // 由 ControlList 填全树序号
		{ "depth", info.depth },
		{ "class", widget->metaObject()->className() },
		{ "name", widget->objectName().toStdString() },
		{ "accessible", widget->accessibleName().toStdString() },
		{ "text", WidgetText(widget).toStdString() },
		{ "rect", json{ widget->x(), widget->y(), widget->width(), widget->height() } },
		{ "globalRect", json{ topLeft.x(), topLeft.y(), widget->width(), widget->height() } },
		{ "visible", widget->isVisibleTo(info.root) },
		{ "enabled", widget->isEnabledTo(info.root) },
	};
}

// 过滤只作用于展示，序号始终是全树序号，保证 click #index 与 list 对得上。
[[nodiscard]] bool MatchesFilter(const WidgetInfo &info, const QString &filter) {
	if (filter.isEmpty()) {
		return true;
	}
	const auto widget = info.widget.data();
	if (filter == u"@scroll"_q) {
		return dynamic_cast<Ui::ScrollArea*>(widget)
			|| dynamic_cast<Ui::ElasticScroll*>(widget);
	}
	return widget->objectName().contains(filter, Qt::CaseInsensitive)
		|| QString::fromLatin1(widget->metaObject()->className())
			.contains(filter, Qt::CaseInsensitive)
		|| widget->accessibleName().contains(filter, Qt::CaseInsensitive)
		|| WidgetText(widget).contains(filter, Qt::CaseInsensitive);
}

[[nodiscard]] Result ControlList(const QStringList &args) {
	auto filter = QString();
	auto all = false;
	for (const auto &arg : args) {
		if (arg == u"--all"_q) {
			all = true;
		} else if (filter.isEmpty()) {
			filter = arg;
		} else {
			return Result::Err(u"usage: control.list [filter] [--all]"_q);
		}
	}
	const auto widgets = CollectWidgets(all);
	if (widgets.empty()) {
		return Result::Err(u"no active window"_q);
	}
	constexpr auto kMaxShown = 500;
	auto nodes = json::array();
	auto shown = 0;
	auto truncated = false;
	for (auto i = 0; i < int(widgets.size()); ++i) {
		if (!MatchesFilter(widgets[i], filter)) {
			continue;
		}
		if (shown >= kMaxShown) {
			truncated = true;
			break;
		}
		auto node = DescribeWidget(widgets[i]);
		node["index"] = i;
		nodes.push_back(std::move(node));
		++shown;
	}
	return Result::Ok(Compact(json{
		{ "total", widgets.size() },
		{ "shown", shown },
		{ "truncated", truncated },
		{ "widgets", std::move(nodes) },
	}));
}

// 合成鼠标点击：在目标控件中心命中子控件，按 Enter → Press → Release →
// Leave 的顺序 sendEvent，走与真实点击相同的事件分发路径。不依赖窗口前台
// 与真实光标，少数依赖 QCursor::pos() 的代码路径不适用。
[[nodiscard]] QWidget *FindByAccessibleName(const QString &name) {
	for (const auto &info : CollectWidgets(true)) {
		const auto widget = info.widget.data();
		if (widget && widget->accessibleName() == name) {
			return widget;
		}
	}
	return nullptr;
}

[[nodiscard]] Result ControlClick(const QStringList &args) {
	auto selector = QString();
	auto all = false;
	auto mouse = false;
	for (const auto &arg : args) {
		if (arg == u"--all"_q) {
			all = true;
		} else if (arg == u"--mouse"_q) {
			mouse = true;
		} else if (selector.isEmpty()) {
			selector = arg;
		} else {
			return Result::Err(
				u"usage: control.click <objectName | #index> [--all] [--mouse]"_q);
		}
	}
	if (selector.isEmpty()) {
		return Result::Err(
			u"usage: control.click <objectName | #index> [--all] [--mouse]"_q);
	}
	auto target = (QWidget*)nullptr;
	if (selector.startsWith(u'#')) {
		auto ok = false;
		const auto index = selector.mid(1).toInt(&ok);
		// #index 与 control.list 同模式的序号对应；默认活动窗口，--all 全部顶层。
		// UI 变化后序号会漂移，跨指令使用需先重新 list。
		const auto widgets = CollectWidgets(all);
		if (!ok || index < 0 || index >= int(widgets.size())) {
			return Result::Err(u"bad index, run control.list first"_q);
		}
		target = widgets[index].widget.data();
	} else {
		if (const auto window = Core::App().activeWindow()) {
			target = window->widget()->findChild<QWidget*>(selector);
		}
		// 菜单、弹层是独立顶层窗口，活动窗口树里找不到时扫全部顶层。
		if (!target) {
			for (const auto top : QApplication::topLevelWidgets()) {
				if ((target = top->findChild<QWidget*>(selector))) {
					break;
				}
			}
		}
		// objectName 之外兜底 accessibleName 精确匹配：菜单外的标题栏按钮等
		// 只有无障碍名，本地化文本随语言变化，固定语言环境下稳定。
		if (!target) {
			target = FindByAccessibleName(selector);
		}
	}
	if (!target) {
		return Result::Err(u"widget not found: "_q
			+ selector
			+ u", run control.list to list"_q);
	}
	if (!target->isVisible()) {
		return Result::Err(u"widget is not visible: "_q + selector);
	}
	if (!target->isEnabled()) {
		return Result::Err(u"widget is disabled: "_q + selector);
	}
	// 语义触发优先：AbstractButton 走 clicked()，直接执行回调与信号流，
	// 与真实点击的最终出口等价，不受命中偏移和子控件遮挡影响。
	// lib_ui 不挂 Q_OBJECT，qobject_cast 不可用，dynamic_cast 走 RTTI。
	if (const auto button = dynamic_cast<Ui::AbstractButton*>(target)
		; button && !mouse) {
		// 回调可能销毁按钮自身（菜单项点击后 PopupMenu 整体销毁），
		// 返回字段必须先拷值，触发后不再访问 target。
		const auto className = QString::fromLatin1(
			target->metaObject()->className()).toStdString();
		const auto objectName = target->objectName().toStdString();
		button->clicked({}, Qt::LeftButton);
		return Result::Ok(Compact(json{
			{ "class", className },
			{ "name", objectName },
			{ "mode", "semantic" },
		}));
	}
	// 命中测试找最深子控件，模拟真实分发：事件先给子控件，不消费再冒泡。
	const auto center = target->rect().center();
	const auto root = mouse ? target->window() : target;
	const auto hit = root->childAt(root->mapFromGlobal(
		target->mapToGlobal(center)));
	if (mouse && (!hit || (hit != target && !target->isAncestorOf(hit)))) {
		return Result::Err(u"widget is covered at its center"_q);
	}
	const auto receiver = hit ? static_cast<QWidget*>(hit) : target;
	const auto local = QPointF(receiver->mapFromGlobal(target->mapToGlobal(center)));
	const auto windowPos = QPointF(
		receiver->window()->mapFromGlobal(receiver->mapToGlobal(local.toPoint())));
	const auto global = QPointF(receiver->mapToGlobal(local.toPoint()));
	// 事件处理（release 触发点击回调）可能销毁 receiver/target（如菜单项
	// 点击后 PopupMenu 整体销毁），返回字段先拷值，每步发送前用 QPointer 验活。
	const auto targetClass = QString::fromLatin1(
		target->metaObject()->className()).toStdString();
	const auto targetName = target->objectName().toStdString();
	const auto receiverClass = QString::fromLatin1(
		receiver->metaObject()->className()).toStdString();
	const auto receiverName = receiver->objectName().toStdString();
	const auto alive = QPointer<QWidget>(receiver);

	auto hover = QEnterEvent(local, windowPos, global);
	QApplication::sendEvent(receiver, &hover);
	auto press = QMouseEvent(
		QEvent::MouseButtonPress,
		local,
		windowPos,
		global,
		Qt::LeftButton,
		Qt::LeftButton,
		Qt::NoModifier);
	const auto handled = QApplication::sendEvent(receiver, &press);
	if (alive) {
		auto release = QMouseEvent(
			QEvent::MouseButtonRelease,
			local,
			windowPos,
			global,
			Qt::LeftButton,
			Qt::NoButton,
			Qt::NoModifier);
		QApplication::sendEvent(receiver, &release);
	}
	if (alive) {
		auto leave = QEvent(QEvent::Leave);
		QApplication::sendEvent(receiver, &leave);
	}
	return Result::Ok(Compact(json{
		{ "class", targetClass },
		{ "name", targetName },
		{ "receiver", receiverClass },
		{ "receiverName", receiverName },
		{ "point", json{ global.x(), global.y() } },
		{ "handled", handled },
		{ "destroyed", !alive },
	}));
}

// 直接修改输入控件，供多行布局验证使用，不触发发送动作。
[[nodiscard]] Result controlSetText(const QStringList &args) {
	if (args.size() != 2 || !args[1].startsWith(u"b64:"_q)) {
		return Result::Err(u"usage: control.set-text <objectName> <base64>"_q);
	}
	const auto window = Core::App().activeWindow();
	const auto target = window
		? window->widget()->findChild<QWidget*>(args.front())
		: nullptr;
	const auto field = dynamic_cast<Ui::InputField*>(target);
	if (!field || !field->isVisible() || !field->isEnabled()) {
		return Result::Err(u"editable input not found"_q);
	}
	const auto previous = field->getLastText();
	const auto text = QString::fromUtf8(
		QByteArray::fromBase64(args[1].mid(4).toLatin1()));
	field->setTextWithTags({ text, {} });
	return Result::Ok(Compact(json{
		{ "previousText", previous.toStdString() },
		{ "length", text.size() },
		{ "height", field->height() },
	}));
}

[[nodiscard]] QWidget *findControl(const QString &selector) {
	if (selector.startsWith(u'#')) {
		auto ok = false;
		const auto index = selector.mid(1).toInt(&ok);
		const auto widgets = CollectWidgets(false);
		return (ok && index >= 0 && index < int(widgets.size()))
			? widgets[index].widget.data()
			: nullptr;
	}
	const auto window = Core::App().activeWindow();
	return window ? window->widget()->findChild<QWidget*>(selector) : nullptr;
}

[[nodiscard]] Result controlHover(const QStringList &args) {
	if (args.size() != 2 || (args[1] != u"on"_q && args[1] != u"off"_q)) {
		return Result::Err(u"usage: control.hover <objectName | #index> <on|off>"_q);
	}
	const auto button = dynamic_cast<Ui::AbstractButton*>(findControl(args[0]));
	if (!button || !button->isVisible() || button->isDisabled()) {
		return Result::Err(u"visible enabled button not found"_q);
	}
	// 仅设置绘制状态，不点击，也不移动系统光标。
	button->setSynteticOver(args[1] == u"on"_q);
	return Result::Ok(Compact(json{ { "hovered", button->isOver() } }));
}

[[nodiscard]] Result controlScroll(const QStringList &args) {
	if (args.isEmpty() || args.size() > 2) {
		return Result::Err(u"usage: control.scroll <objectName | #index> [top]"_q);
	}
	const auto target = findControl(args.front());
	const auto scroll = dynamic_cast<Ui::ElasticScroll*>(target);
	const auto area = dynamic_cast<Ui::ScrollArea*>(target);
	if ((!scroll && !area) || !target->isVisible()) {
		return Result::Err(u"visible scroll area not found"_q);
	}
	if (args.size() == 2) {
		auto ok = false;
		const auto top = args[1].toInt(&ok);
		if (!ok) {
			return Result::Err(u"expected integer scroll position"_q);
		}
		if (scroll) {
			scroll->scrollToY(top);
		} else {
			area->scrollToY(top);
		}
	}
	return Result::Ok(Compact(json{
		{ "top", scroll ? scroll->scrollTop() : area->scrollTop() },
		{ "maximum", scroll ? scroll->scrollTopMax() : area->scrollTopMax() },
		{ "height", target->height() },
	}));
}

} // namespace

const HandlerMap &ControlHandlers() {
	static const auto result = HandlerMap{
		{ u"control.list"_q, &ControlList },
		{ u"control.click"_q, &ControlClick },
		{ u"control.set-text"_q, &controlSetText },
		{ u"control.scroll"_q, &controlScroll },
		{ u"control.hover"_q, &controlHover },
	};
	return result;
}

} // namespace AyuDebug::Commands
#endif // _DEBUG
