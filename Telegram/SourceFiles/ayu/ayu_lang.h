#pragma once

#include <QJsonDocument>

// AyuGram 自有文案的语言覆盖，全部从 qrc 内置资源加载，不走网络。
class AyuLanguage {
public:
	static void init();

private:
	AyuLanguage() = default;

	static AyuLanguage *instance;

	bool loadBundledLanguage();
	void applyLanguageJson(QJsonDocument doc);
};
