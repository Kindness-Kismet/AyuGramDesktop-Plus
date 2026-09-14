#ifdef _DEBUG
#include "ayu/debug/commands/commands_internal.h"

#include "core/application.h"
#include "main/main_session.h"

namespace AyuDebug::Commands {

Main::Session *ActiveSession() {
	return Core::App().maybePrimarySession();
}

} // namespace AyuDebug::Commands

namespace AyuDebug {
namespace {

// 合并八个域的注册表；各域内部 static，这里只做拼接。
[[nodiscard]] const Commands::HandlerMap &Handlers() {
	static const auto result = [] {
		auto all = Commands::HandlerMap{};
		for (const auto *part : {
			&Commands::AppHandlers(),
			&Commands::SessionHandlers(),
			&Commands::SettingsHandlers(),
			&Commands::GhostHandlers(),
			&Commands::StorageHandlers(),
			&Commands::ScreenshotHandlers(),
			&Commands::ControlHandlers(),
			&Commands::MessageHandlers(),
			&Commands::WindowHandlers(),
		}) {
			all.insert(part->begin(), part->end());
		}
		return all;
	}();
	return result;
}

} // namespace

Result Execute(const QString &command, const QStringList &args) {
	const auto &handlers = Handlers();
	const auto i = handlers.find(command);
	if (i == handlers.end()) {
		return Result::Err(u"unknown command "_q + command);
	}
	// 处理函数会碰 nlohmann 与 Qt 的解析路径，任何一处抛出都不该带走监听。
	try {
		return i->second(args);
	} catch (const std::exception &e) {
		return Result::Err(QString::fromUtf8(e.what()));
	}
}

QStringList CommandNames() {
	auto result = QStringList();
	for (const auto &[name, handler] : Handlers()) {
		result.push_back(name);
	}
	return result;
}

} // namespace AyuDebug
#endif // _DEBUG
