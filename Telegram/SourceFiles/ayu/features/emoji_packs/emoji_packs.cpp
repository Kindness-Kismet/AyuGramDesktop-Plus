#include "ayu/features/emoji_packs/emoji_packs.h"

#include "ayu/features/emoji_packs/emoji_font.h"
#include "ui/emoji_config.h"

#include <crl/crl_async.h>
#include <crl/crl_on_main.h>

#include <QtCore/QCryptographicHash>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QTemporaryDir>
#include <QtGui/QPainter>

namespace Ayu::EmojiPacks {
namespace {

// 与 ui/emoji_config.cpp 的图集格式一致；自定义编号避开官方表情包。
constexpr auto kFirstId = 100;
constexpr auto kLastId = 255;
constexpr auto kSetVersion = 7;
constexpr auto kSize = 72;
constexpr auto kColumns = 32;
constexpr auto kPerSprite = 512;
constexpr auto kFontSizeLimit = 64 * 1024 * 1024;

bool Importing = false;

QJsonObject readConfig(const QString &folder) {
	auto file = QFile(folder + u"/config.json"_q);
	if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) {
		return {};
	}
	return QJsonDocument::fromJson(file.readAll()).object();
}

Pack packFromConfig(const QString &folder, const QJsonObject &config) {
	return {
		config[u"id"_q].toInt(),
		config[u"name"_q].toString(),
		folder + u"/preview.png"_q,
		config[u"fontHash"_q].toString(),
	};
}

ImportResult convertFont(
		const QString &path,
		const QString &root,
		const std::vector<QString> &emojis,
		const QString &presetName) {
	auto file = QFile(path);
	const auto sizeLimit = presetName.isEmpty() ? kFontSizeLimit : 4 * kFontSizeLimit;
	if (!file.open(QIODevice::ReadOnly)
		|| file.size() <= 0 || file.size() > sizeLimit) {
		return { .error = ImportError::Read };
	}
	const auto bytes = file.readAll();
	if (bytes.size() != file.size()) {
		return { .error = ImportError::Read };
	}
	const auto hash = QString::fromLatin1(
		QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
	auto id = 0;
	for (auto i = kFirstId; i <= kLastId; ++i) {
		const auto folder = root + u"/set%1"_q.arg(i);
		if (!QFileInfo::exists(folder)) {
			if (!id) id = i;
			continue;
		}
		const auto config = readConfig(folder);
		if (config[u"fontHash"_q].toString() == hash
			&& config[u"version"_q].toInt() == kSetVersion
			&& config[u"id"_q].toInt() == i) {
			auto images = Ui::Emoji::UniversalImages(i);
			if (images.ensureLoaded()) {
				return { packFromConfig(folder, config) };
			}
		}
	}
	if (!id) {
		return { .error = ImportError::Full };
	}
	auto font = EmojiFont(bytes);
	if (!font.valid()) {
		return { .error = ImportError::Font };
	}
	if (!QDir().mkpath(root)) {
		return { .error = ImportError::Write };
	}
	auto temporary = QTemporaryDir(root + u"/import-XXXXXX"_q);
	if (!temporary.isValid()) {
		return { .error = ImportError::Write };
	}
	const auto count = int(emojis.size());
	const auto spriteCount = (count + kPerSprite - 1) / kPerSprite;
	auto supported = 0;
	auto previews = std::vector<QImage>();
	const auto previewTexts = { u"😀"_q, u"😉"_q, u"😔"_q, u"😨"_q };
	for (auto spriteIndex = 0; spriteIndex < spriteCount; ++spriteIndex) {
		const auto filename = u"emoji_%1.webp"_q.arg(spriteIndex + 1);
		auto sprite = QImage(u":/gui/emoji/"_q + filename, "WEBP")
			.convertToFormat(QImage::Format_ARGB32_Premultiplied);
		const auto start = spriteIndex * kPerSprite;
		const auto end = std::min(start + kPerSprite, count);
		const auto rows = (end - start + kColumns - 1) / kColumns;
		if (sprite.size() != QSize(kColumns * kSize, rows * kSize)) {
			return { .error = ImportError::Write };
		}
		{
			auto painter = QPainter(&sprite);
			for (auto i = start; i < end; ++i) {
				const auto image = font.render(emojis[i], kSize);
				if (image.isNull()) {
					continue;
				}
				const auto x = (i % kColumns) * kSize;
				const auto y = ((i - start) / kColumns) * kSize;
				painter.setCompositionMode(QPainter::CompositionMode_Source);
				painter.fillRect(x, y, kSize, kSize, Qt::transparent);
				painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
				painter.drawImage(x + (kSize - image.width()) / 2,
					y + (kSize - image.height()) / 2, image);
				++supported;
			}
		}
		if (!sprite.save(temporary.path() + '/' + filename, "WEBP", 100)) {
			return { .error = ImportError::Write };
		}
		for (const auto &text : previewTexts) {
			const auto i = std::find(emojis.begin(), emojis.end(), text) - emojis.begin();
			if (i >= start && i < end) {
				previews.push_back(sprite.copy((i % kColumns) * kSize,
					((i - start) / kColumns) * kSize, kSize, kSize));
			}
		}
	}
	if (!supported) {
		return { .error = ImportError::Font };
	}
	auto preview = QImage(kSize * 4, kSize, QImage::Format_ARGB32_Premultiplied);
	preview.fill(Qt::transparent);
	{
		auto painter = QPainter(&preview);
		for (auto i = 0; i < int(previews.size()); ++i) {
			painter.drawImage(i * kSize, 0, previews[i]);
		}
	}
	if (!preview.save(temporary.path() + u"/preview.png"_q)) {
		return { .error = ImportError::Write };
	}
	const auto config = QJsonObject{
		{ u"id"_q, id },
		{ u"version"_q, kSetVersion },
		{ u"name"_q, presetName.isEmpty()
			? QFileInfo(path).completeBaseName().left(128) : presetName },
		{ u"fontHash"_q, hash },
		{ u"supported"_q, supported },
		{ u"total"_q, count },
	};
	const auto data = QJsonDocument(config).toJson();
	auto output = QSaveFile(temporary.path() + u"/config.json"_q);
	if (!output.open(QIODevice::WriteOnly)
		|| output.write(data) != data.size() || !output.commit()) {
		return { .error = ImportError::Write };
	}
	const auto folder = root + u"/set%1"_q.arg(id);
	if (!QDir().rename(temporary.path(), folder)) {
		return { .error = ImportError::Write };
	}
	temporary.setAutoRemove(false);
	return { packFromConfig(folder, config) };
}

} // namespace

bool isCustom(int id) {
	return id >= kFirstId && id <= kLastId;
}

std::vector<Pack> installed() {
	auto result = std::vector<Pack>();
	for (auto id = kFirstId; id <= kLastId; ++id) {
		const auto folder = Ui::Emoji::internal::SetDataPath(id);
		const auto config = readConfig(folder);
		if (config[u"id"_q].toInt() == id
			&& !config[u"fontHash"_q].toString().isEmpty()) {
			result.push_back(packFromConfig(folder, config));
		}
	}
	return result;
}

std::vector<Preset> presets() {
	const auto root = QCoreApplication::applicationDirPath() + u"/emoji-presets/"_q;
	auto file = QFile(root + u"presets.json"_q);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	const auto list = QJsonDocument::fromJson(file.readAll())
		.object()[u"presets"_q].toArray();
	auto result = std::vector<Preset>();
	for (const auto &value : list) {
		const auto preset = value.toObject();
		const auto name = preset[u"file"_q].toString();
		if (name.isEmpty() || QFileInfo(name).fileName() != name) {
			continue;
		}
		result.push_back({
			preset[u"id"_q].toString(),
			preset[u"name"_q].toString(),
			root + name,
			preset[u"sha256"_q].toString(),
		});
	}
	return result;
}

void importFont(const QString &path, Fn<void(ImportResult)> done,
		const QString &presetName) {
	if (Importing) {
		done({ .error = ImportError::Busy });
		return;
	}
	Importing = true;
	const auto root = Ui::Emoji::internal::CacheFileFolder();
	auto emojis = std::vector<QString>();
	const auto count = Ui::Emoji::internal::FullCount();
	emojis.reserve(count);
	for (auto i = 0; i < count; ++i) {
		emojis.push_back(Ui::Emoji::internal::ByIndex(i)->text());
	}
	crl::async([=, emojis = std::move(emojis)] {
		const auto result = convertFont(path, root, emojis, presetName);
		crl::on_main([=] {
			Importing = false;
			done(result);
		});
	});
}

} // namespace Ayu::EmojiPacks
