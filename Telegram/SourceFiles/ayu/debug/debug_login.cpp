#ifdef _DEBUG
#include "ayu/debug/debug_login.h"

#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session_settings.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/mtproto_dc_options.h"

namespace AyuDebug {

// 走的是 tdesktop 恢复本地会话的同一条路径（main_account.cpp:145 也是这样拼
// MTPUser），所以界面联动天然成立：createSession 赋值 _sessionValue，
// window_controller.cpp:175 收到非空会话就建 SessionController 并 setupMain。
//
// 代价是这个会话没有任何服务端数据，聊天列表是空的，任何联网操作都会失败。
// 只适合验证界面、设置项和入口可达性，测消息级功能要用测试服。
QString CreateFakeSession(int64 userId) {
	// mtp() 直接解引用 _mtp，没有公开的就绪查询；domain.started() 是 tdesktop
	// 自己在 settings_codes.cpp:152 用的同一前提。
	if (!Core::App().domain().started()) {
		return u"domain is not started yet"_q;
	}
	auto &account = Core::App().activeAccount();
	if (account.sessionExists()) {
		return u"session already exists"_q;
	}
	if (userId <= 0) {
		return u"expected a positive userId"_q;
	}

	// 一旦有了会话，tdesktop 会开始发需要授权的请求；假密钥必然 401，而
	// main_account.cpp:459 的全局失败处理会把会话直接登出。换成空实现留住它。
	account.mtp().setGlobalFailHandler(nullptr);

	using Flag = MTPDuser::Flag;
	account.createSession(MTP_user(
		MTP_flags(Flag::f_self | Flag::f_first_name),
		MTP_long(userId),
		MTPlong(), // access_hash
		MTP_string("Debug"),
		MTPstring(), // last_name
		MTPstring(), // username
		MTPstring(), // phone
		MTPUserProfilePhoto(),
		MTPUserStatus(),
		MTPint(), // bot_info_version
		MTPVector<MTPRestrictionReason>(),
		MTPstring(), // bot_inline_placeholder
		MTPstring(), // lang_code
		MTPEmojiStatus(),
		MTPVector<MTPUsername>(),
		MTPRecentStory(),
		MTPPeerColor(), // color
		MTPPeerColor(), // profile_color
		MTPint(), // bot_active_users
		MTPlong(), // bot_verification_icon
		MTPlong(), // send_paid_messages_stars
		MTPlong())); // linked_community_id

	return QString();
}

QString SwitchTestEnvironment() {
	auto &domain = Core::App().domain();
	if (!domain.started()) {
		return u"domain is not started"_q;
	}
	if (domain.active().sessionExists()) {
		return u"already logged in; log out before switching environment"_q;
	}
	// addActivated 会新建账号，多账号时切换会留下多余的空账号，官方 testmode
	// 也是这个前提（settings_codes.cpp:150）。
	if (domain.accounts().size() != 1) {
		return u"expected exactly one account"_q;
	}
	const auto was = domain.active().mtp().environment();
	domain.addActivated((was == MTP::Environment::Production)
		? MTP::Environment::Test
		: MTP::Environment::Production);
	return QString();
}

} // namespace AyuDebug
#endif // _DEBUG
