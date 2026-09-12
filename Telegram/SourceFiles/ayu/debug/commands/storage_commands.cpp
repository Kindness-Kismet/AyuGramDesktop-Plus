#ifdef _DEBUG
#include "ayu/debug/commands/commands_internal.h"

#include "ayu/ayu_settings.h"
#include "settings.h"

#include <QFileInfo>

namespace AyuDebug::Commands {
namespace {

using json = nlohmann::json;

[[nodiscard]] Result StorageStats(const QStringList &) {
	const auto &settings = AyuSettings::getInstance();
	const auto path = cWorkingDir() + u"tdata/ayudata.db"_q;
	const auto info = QFileInfo(path);
	return Result::Ok(Compact(json{
		{ "saveDeletedMessages", settings.saveDeletedMessages() },
		{ "saveMessagesHistory", settings.saveMessagesHistory() },
		{ "saveForBots", settings.saveForBots() },
		{ "databasePath", path.toStdString() },
		{ "databaseExists", info.exists() },
		{ "databaseBytes", info.exists() ? info.size() : 0 },
	}));
}

} // namespace

const HandlerMap &StorageHandlers() {
	static const auto result = HandlerMap{
		{ u"storage.stats"_q, &StorageStats },
	};
	return result;
}

} // namespace AyuDebug::Commands
#endif // _DEBUG
