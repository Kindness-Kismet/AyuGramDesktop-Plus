#include "ayu/ui/settings/settings_debug.h"

#include "lang_auto.h"
#include "logs.h"
#include "settings.h"
#include "ayu/ui/settings/settings_main.h"
#include "boxes/abstract_box.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "core/version.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "storage/storage_domain.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/boxes/confirm_box.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <QClipboard>
#include <QGuiApplication>

namespace Settings {

using namespace Builder;

namespace {

[[nodiscard]] QString BuildConfiguration() {
#ifdef _DEBUG
	return u"Debug"_q;
#else
	return u"Release"_q;
#endif
}

void BuildEnvironment(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.addSkip();
	builder.addSubsectionTitle(tr::ayu_DebugEnvironment());

	// 只读条目，点击复制值，排查时省去手抄。
	const auto copyable = [&](
			const QString &id,
			const rpl::producer<QString> &title,
			const QString &value,
			const style::icon *icon) {
		builder.addButton({
			.id = id,
			.title = title,
			.icon = { icon },
			.label = rpl::single(value),
			.onClick = [=] {
				QGuiApplication::clipboard()->setText(value);
				controller->showToast(tr::ayu_DebugCopied(tr::now));
			},
		});
	};

	copyable(
		u"ayu/debug/version"_q,
		tr::ayu_DebugVersion(),
		QString::fromLatin1(AppVersionStr) + u" ("_q
			+ BuildConfiguration() + u")"_q,
		&st::menuIconInfo);
	copyable(
		u"ayu/debug/workingDir"_q,
		tr::ayu_DebugWorkingDirectory(),
		cWorkingDir(),
		&st::menuIconShowInFolder);
	copyable(
		u"ayu/debug/session"_q,
		tr::ayu_DebugSessionUserId(),
		QString::number(builder.session()->userId().bare),
		&st::menuIconProfile);
}

void BuildDiagnostics(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle(tr::ayu_DebugDiagnostics());

	// switchDebugMode() 只在关闭时重启应用，开启时进程继续存活，因此这里自己
	// 维护状态流，否则开启后开关会停在旧值。
	// variable 的引用由按钮的订阅与回调持有，页面销毁时随最后一个引用释放。
	const auto enabled = std::make_shared<rpl::variable<bool>>(
		Logs::DebugEnabled());

	builder.addButton({
		.id = u"ayu/debug/toggleLogs"_q,
		.title = tr::ayu_DebugVerboseNetworkLogs(),
		.icon = { &st::menuIconStats },
		.toggled = enabled->value(),
		.onClick = [=] {
			controller->show(Ui::MakeConfirmBox({
				.text = (Logs::DebugEnabled()
					? tr::ayu_DebugDisableLogsConfirm(tr::now)
					: tr::ayu_DebugEnableLogsConfirm(tr::now)),
				.confirmed = [=](Fn<void()> &&close) {
					Core::App().switchDebugMode();
					// 关闭分支已经触发重启，走到这里的只有开启分支。
					*enabled = Logs::DebugEnabled();
					close();
				},
			}));
		},
	});

	builder.addButton({
		.id = u"ayu/debug/openLogs"_q,
		.title = tr::ayu_DebugShowLogFile(),
		.icon = { &st::menuIconShowInFolder },
		.onClick = [=] {
			File::ShowInFolder(cWorkingDir() + u"log.txt"_q);
		},
	});

	builder.addSkip();
	builder.addDividerText(tr::ayu_DebugVerboseLogsNote());
}

void BuildDangerZone(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.addSkip();
	builder.addSubsectionTitle(tr::ayu_DebugCrashReporting());

	// 故意崩溃用于验证崩溃上报链路，双重确认避免误触。
	builder.addButton({
		.id = u"ayu/debug/crash"_q,
		.title = tr::ayu_DebugTriggerTestCrash(),
		.icon = { &st::menuIconReport },
		.onClick = [=] {
			controller->show(Ui::MakeConfirmBox({
				.text = tr::ayu_DebugCrashConfirmText(tr::now),
				.confirmed = [=](Fn<void()> &&close) {
					close();
					Unexpected("Crashed from the debug section.");
				},
				.confirmText = tr::ayu_DebugCrashConfirmButton(tr::now),
			}));
		},
	});

	builder.addSkip();
	builder.addDividerText(tr::ayu_DebugCrashReportingNote());
	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = AyuDebug::Id(),
	.parentId = AyuMain::Id(),
	.title = u"Debug"_q,
	.icon = &st::menuIconStats,
}, [](SectionBuilder &builder) {
	BuildEnvironment(builder);
	BuildDiagnostics(builder);
	BuildDangerZone(builder);
});

} // namespace

bool DebugEntryVisible() {
#ifdef _DEBUG
	return true;
#else
	return Logs::DebugEnabled();
#endif
}

rpl::producer<QString> AyuDebug::title() {
	return rpl::single(u"Debug"_q);
}

AyuDebug::AyuDebug(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuDebug::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuDebugId() {
	return AyuDebug::Id();
}

} // namespace Settings
