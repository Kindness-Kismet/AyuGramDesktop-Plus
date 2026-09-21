#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

#include <vector>

namespace Ayu::EmojiPacks {

struct Pack {
	int id = 0;
	QString name;
	QString previewPath;
	QString hash;
};

struct Preset {
	QString id;
	QString name;
	QString path;
	QString hash;
};

enum class ImportError {
	None,
	Busy,
	Read,
	Font,
	Full,
	Write,
};

struct ImportResult {
	Pack pack;
	ImportError error = ImportError::None;
};

[[nodiscard]] bool isCustom(int id);
[[nodiscard]] std::vector<Pack> installed();
[[nodiscard]] std::vector<Preset> presets();
// 主线程发起；转换在后台执行，回调回到主线程。
void importFont(const QString &path, Fn<void(ImportResult)> done,
	const QString &presetName = {});

} // namespace Ayu::EmojiPacks
