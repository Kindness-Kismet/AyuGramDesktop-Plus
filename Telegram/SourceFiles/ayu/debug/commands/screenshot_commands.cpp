#ifdef _DEBUG
#include "ayu/debug/commands/commands_internal.h"

#include "core/application.h"
#include "window/window_controller.h"

#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QtWidgets/QApplication>

namespace AyuDebug::Commands {
namespace {

using json = nlohmann::json;

[[nodiscard]] Result ScreenshotTake(const QStringList &args) {
	if (args.size() != 1
		&& (args.size() != 2 || args[1] != u"popup"_q)) {
		return Result::Err(u"usage: screenshot.take <path> [popup]"_q);
	}
	const auto popup = (args.size() == 2);
	const auto window = Core::App().activeWindow();
	const auto widget = popup
		? QApplication::activePopupWidget()
		: window ? window->widget().get() : nullptr;
	if (!widget) {
		return Result::Err(popup ? u"no active popup"_q : u"no active window"_q);
	}
	const auto image = widget->grab();
	if (image.isNull()) {
		return Result::Err(u"grab returned an empty image"_q);
	}
	const auto target = QFileInfo(args.front()).absoluteFilePath();
	QDir().mkpath(QFileInfo(target).absolutePath());
	if (!image.save(target, "JPG", 80)) {
		return Result::Err(u"cannot write "_q + target);
	}
	return Result::Ok(Compact(json{
		{ "path", target.toStdString() },
		{ "width", image.width() },
		{ "height", image.height() },
	}));
}

} // namespace

const HandlerMap &ScreenshotHandlers() {
	static const auto result = HandlerMap{
		{ u"screenshot.take"_q, &ScreenshotTake },
	};
	return result;
}

} // namespace AyuDebug::Commands
#endif // _DEBUG
