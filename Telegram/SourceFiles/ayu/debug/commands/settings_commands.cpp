#ifdef _DEBUG
#include "ayu/debug/commands/commands_internal.h"

#include "ayu/ayu_settings.h"
#include "ayu/ui/settings/settings_main.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_search.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "window/themes/window_theme.h"

namespace AyuDebug::Commands {
namespace {

using json = nlohmann::json;

// 设置的读写统一走 to_json/from_json：AyuSettings 有 96 个键，逐个写指令无法
// 维护，而 from_json 直接给 rpl::variable 赋值，与 setter 的通知路径一致。
[[nodiscard]] json SettingsJson() {
	return json(AyuSettings::getInstance());
}

void ApplySettingsJson(const json &patched) {
	auto &settings = AyuSettings::getInstance();
	from_json(patched, settings);
	settings.validate();
	AyuSettings::save();
}

// 命令行只能给出字符串，这里按目标键的既有类型解释，避免把 bool 写成字符串。
[[nodiscard]] Result ParseInto(json &slot, const QString &raw) {
	const auto text = raw.trimmed();
	if (slot.is_boolean()) {
		if (text == u"true"_q) {
			slot = true;
		} else if (text == u"false"_q) {
			slot = false;
		} else {
			return Result::Err(u"expected true or false"_q);
		}
	} else if (slot.is_number_integer()) {
		auto ok = false;
		const auto value = text.toLongLong(&ok);
		if (!ok) {
			return Result::Err(u"expected an integer"_q);
		}
		slot = value;
	} else if (slot.is_number_float()) {
		auto ok = false;
		const auto value = text.toDouble(&ok);
		if (!ok) {
			return Result::Err(u"expected a number"_q);
		}
		slot = value;
	} else if (slot.is_string()) {
		slot = text.toStdString();
	} else {
		return Result::Err(u"unsupported value type"_q);
	}
	return Result::Ok();
}

[[nodiscard]] Result SettingsKeys(const QStringList &) {
	// items() 返回的代理持有 json 的引用，range-for 的生命周期延长不覆盖它，
	// 直接迭代临时对象会在循环里读已释放内存，必须先落到具名变量。
	const auto all = SettingsJson();
	auto names = json::array();
	for (const auto &[key, value] : all.items()) {
		names.push_back(key);
	}
	return Result::Ok(Compact(names));
}

[[nodiscard]] Result SettingsDump(const QStringList &) {
	return Result::Ok(Compact(SettingsJson()));
}

[[nodiscard]] Result SettingsGet(const QStringList &args) {
	if (args.size() != 1) {
		return Result::Err(u"usage: settings.get <key>"_q);
	}
	const auto key = args.front().toStdString();
	const auto all = SettingsJson();
	if (!all.contains(key)) {
		return Result::Err(u"unknown key "_q + args.front());
	}
	return Result::Ok(Compact(all.at(key)));
}

[[nodiscard]] Result SettingsSet(const QStringList &args) {
	if (args.size() != 2) {
		return Result::Err(u"usage: settings.set <key> <value>"_q);
	}
	const auto key = args.front().toStdString();
	auto all = SettingsJson();
	if (!all.contains(key)) {
		return Result::Err(u"unknown key "_q + args.front());
	}
	auto slot = all.at(key);
	if (const auto parsed = ParseInto(slot, args.at(1)); !parsed.ok) {
		return parsed;
	}
	all[key] = slot;
	ApplySettingsJson(all);

	// 回读实际生效值，validate() 可能把越界输入夹到合法区间。
	return Result::Ok(Compact(SettingsJson().at(key)));
}

[[nodiscard]] Result openPage(const QStringList &args) {
	if (args.size() != 1) {
		return Result::Err(u"usage: page.open <settings|ayu|search>"_q);
	}
	const auto window = Core::App().activeWindow();
	if (!window) {
		return Result::Err(u"no active window"_q);
	}
	const auto controller = window->sessionController();
	if (!controller) {
		return Result::Err(u"no session"_q);
	}
	const auto section = args.front();
	const auto type = (section == u"settings"_q)
		? ::Settings::MainId()
		: (section == u"ayu"_q)
		? ::Settings::AyuMainId()
		: (section == u"search"_q)
		? ::Settings::Search::Id()
		: ::Settings::Type{};
	if (!type) {
		return Result::Err(u"unknown section: "_q + section);
	}
	controller->showSettings(type);
	return Result::Ok();
}

[[nodiscard]] Result ResetBackground(const QStringList &) {
	const auto background = Window::Theme::Background();
	background->reset();
	const auto &paper = background->paper();
	const auto &image = background->prepared();
	return Result::Ok(Compact(json{
		{ "themePath", background->themeObject().pathAbsolute.toStdString() },
		{ "isPattern", paper.isPattern() },
		{ "intensity", paper.patternIntensity() },
		{ "imageWidth", image.width() },
		{ "imageHeight", image.height() },
	}));
}

[[nodiscard]] Result setTheme(const QStringList &args) {
	if (args.size() != 1 || (args.front() != u"dark"_q && args.front() != u"light"_q)) {
		return Result::Err(u"usage: theme.set <dark|light>"_q);
	}
	// 手动选择主题时解除跟随系统，与主菜单行为一致。
	Core::App().settings().setSystemDarkModeEnabled(false);
	Core::App().saveSettingsDelayed();
	// 通过主题切换流程同时更新颜色与界面。
	if (Window::Theme::IsNightMode() != (args.front() == u"dark"_q)) {
		Window::Theme::ToggleNightMode();
		// 保存已应用的主题。
		Window::Theme::KeepApplied();
	}
	return Result::Ok(Window::Theme::IsNightMode() ? u"dark"_q : u"light"_q);
}

} // namespace

const HandlerMap &SettingsHandlers() {
	static const auto result = HandlerMap{
		{ u"settings.keys"_q, &SettingsKeys },
		{ u"settings.dump"_q, &SettingsDump },
		{ u"settings.get"_q, &SettingsGet },
		{ u"settings.set"_q, &SettingsSet },
		{ u"page.open"_q, &openPage },
		{ u"theme.reset-background"_q, &ResetBackground },
		{ u"theme.set"_q, &setTheme },
	};
	return result;
}

} // namespace AyuDebug::Commands
#endif // _DEBUG
