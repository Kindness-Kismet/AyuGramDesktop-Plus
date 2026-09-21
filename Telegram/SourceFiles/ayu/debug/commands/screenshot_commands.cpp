#ifdef _DEBUG
#include "ayu/debug/commands/commands_internal.h"

#include "core/application.h"
#include "window/window_controller.h"

#include <QDir>
#include <QFileInfo>
#include <QPixmap>

namespace AyuDebug::Commands {
namespace {

using json = nlohmann::json;

[[nodiscard]] Result ScreenshotTake(const QStringList &args) {
	if (args.size() != 1) {
		return Result::Err(u"usage: screenshot.take <path>"_q);
	}
	const auto window = Core::App().activeWindow();
	if (!window) {
		return Result::Err(u"no active window"_q);
	}
	const auto widget = window->widget();
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
