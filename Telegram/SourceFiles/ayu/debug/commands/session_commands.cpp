#ifdef _DEBUG
#include "ayu/debug/commands/commands_internal.h"

#include "ayu/debug/debug_login.h"

#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/mtproto_dc_options.h"

namespace AyuDebug::Commands {
namespace {

using json = nlohmann::json;

// 造一个只在本地存在的 self 用户，绕过登录直接进主界面。
//
// 走的是 tdesktop 恢复本地会话的同一条路径（main_account.cpp:145 也是这样拼
// MTPUser），所以界面联动天然成立：createSession 赋值 _sessionValue，
// window_controller.cpp:175 收到非空会话就建 SessionController 并 setupMain。
//
// 代价是这个会话没有任何服务端数据，聊天列表是空的，任何联网操作都会失败。
// 只适合验证界面、设置项和入口可达性，测消息级功能要用 debug.testmode 走测试服。
[[nodiscard]] Result FakeSession(const QStringList &args) {
	if (args.size() > 1) {
		return Result::Err(u"usage: debug.fake-session [userId]"_q);
	}
	auto userId = int64(999999999);
	if (args.size() == 1) {
		auto ok = false;
		userId = args.front().toLongLong(&ok);
		if (!ok || userId <= 0) {
			return Result::Err(u"expected a positive integer userId"_q);
		}
	}
	if (const auto error = CreateFakeSession(userId); !error.isEmpty()) {
		return Result::Err(error);
	}
	return Result::Ok(Compact(json{
		{ "userId", userId },
		{ "note", "offline fake session, no server data" },
	}));
}

// 切到官方测试数据中心。测试号无需真手机号，能拿到真实会话和真实消息事件。
// 等价于登录界面输入 testmode（settings_codes.cpp:147）。
[[nodiscard]] Result TestMode(const QStringList &) {
	auto &domain = Core::App().domain();
	const auto was = domain.started()
		? domain.active().mtp().environment()
		: MTP::Environment::Production;
	if (const auto error = SwitchTestEnvironment(); !error.isEmpty()) {
		return Result::Err(error);
	}
	const auto target = (was == MTP::Environment::Production)
		? MTP::Environment::Test
		: MTP::Environment::Production;
	return Result::Ok(Compact(json{
		{ "from", (was == MTP::Environment::Production)
			? "production" : "test" },
		{ "to", (target == MTP::Environment::Production)
			? "production" : "test" },
	}));
}

} // namespace

const HandlerMap &SessionHandlers() {
	static const auto result = HandlerMap{
		{ u"debug.fake-session"_q, &FakeSession },
		{ u"debug.testmode"_q, &TestMode },
	};
	return result;
}

} // namespace AyuDebug::Commands
#endif // _DEBUG
