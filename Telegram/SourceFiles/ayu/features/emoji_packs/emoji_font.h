#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtGui/QImage>

#include <memory>

namespace Ayu::EmojiPacks {

class EmojiFont final {
public:
	explicit EmojiFont(QByteArray data);
	~EmojiFont();

	[[nodiscard]] bool valid() const;
	[[nodiscard]] QImage render(const QString &text, int size) const;

private:
	struct Private;
	std::unique_ptr<Private> _private;
};

} // namespace Ayu::EmojiPacks
