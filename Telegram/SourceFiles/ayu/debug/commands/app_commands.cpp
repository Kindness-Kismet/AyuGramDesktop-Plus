#ifdef _DEBUG
#include "ayu/debug/commands/commands_internal.h"

#include "core/application.h"
#include "core/version.h"
#include "logs.h"
#include "main/main_session.h"
#include "window/window_controller.h"

namespace AyuDebug::Commands {
namespace {

using json = nlohmann::json;

[[nodiscard]] Result AppInfo(const QStringList &) {
	const auto session = ActiveSession();
	const auto window = Core::App().activeWindow();
	auto info = json{
		{ "version", AppVersionStr },
		{ "versionCode", AppVersion },
		{ "configuration", "Debug" },
		{ "workingDir", cWorkingDir().toStdString() },
		{ "debugLogs", Logs::DebugEnabled() },
		{ "hasSession", session != nullptr },
		{ "hasWindow", window != nullptr },
	};
	if (session) {
		info["userId"] = session->userId().bare;
	}
	return Result::Ok(Compact(info));
}

[[nodiscard]] Result Ping(const QStringList &) {
	return Result::Ok(u"pong"_q);
}

[[nodiscard]] Result Quit(const QStringList &) {
	// 立即退出会让 OK 还没写出去就断链，客户端读到的是连接重置。排到事件循环
	// 尾部，等 Reply 把响应刷进 socket 之后再退。
	crl::on_main([] {
		Core::Quit();
	});
	return Result::Ok();
}

[[nodiscard]] Result Help(const QStringList &) {
	auto names = json::array();
	for (const auto &name : CommandNames()) {
		names.push_back(name.toStdString());
	}
	return Result::Ok(Compact(names));
}

} // namespace

const HandlerMap &AppHandlers() {
	static const auto result = HandlerMap{
		{ u"app.ping"_q, &Ping },
		{ u"app.quit"_q, &Quit },
		{ u"app.info"_q, &AppInfo },
		{ u"app.help"_q, &Help },
	};
	return result;
}

} // namespace AyuDebug::Commands
#endif // _DEBUG
