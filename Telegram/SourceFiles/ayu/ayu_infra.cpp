#include "ayu/ayu_infra.h"

#include "ayu/ayu_lang.h"
#include "ayu/ayu_settings.h"
#include "ayu/ayu_ui_settings.h"
#include "ayu/ayu_worker.h"
#include "ayu/data/ayu_database.h"
#ifdef _DEBUG
#include "ayu/debug/debug_server.h"
#endif
#include "ayu/ui/ayu_logo.h"
#include "features/translator/ayu_translator.h"
#include "lang/lang_instance.h"
#include "ui/chat/chat_style_radius.h"
#include "utils/rc_manager.h"

#ifdef Q_OS_WIN
#include "ayu/utils/windows_utils.h"
#endif

namespace AyuInfra {

void initLang() {
	QString id = Lang::GetInstance().id();
	if (id.isEmpty()) {
		LOG(("Language is not loaded"));
		return;
	}
	AyuLanguage::init();
}

void initUiSettings() {
	const auto &settings = AyuSettings::getInstance();

	AyuUiSettings::setMonoFont(settings.monoFont());
	AyuUiSettings::setWideMultiplier(settings.wideMultiplier());
	AyuUiSettings::setMaterialSwitches(settings.materialSwitches());
	AyuUiSettings::setAvatarCorners(settings.avatarCorners());
	Ui::SetAppliedBubbleRadius(settings.messageBubbleRadius());
}

void initDatabase() {
	AyuDatabase::initialize();
}

void initWorker() {
	AyuWorker::initialize();
}

void initRCManager() {
	RCManager::getInstance().start();
}

void initTranslator() {
	Ayu::Translator::TranslateManager::init();
}

void initIcon() {
#ifdef Q_OS_WIN
	AyuAssets::loadAppIco();
	reloadAppIconFromTaskBar();
#endif
}

void initDebugServer() {
#ifdef _DEBUG
	AyuDebug::StartServer();
#endif
}

void init() {
	initLang();
	initDatabase();
	initUiSettings();
	initIcon();
	initWorker();
	initRCManager();
	initTranslator();
	initDebugServer();
}

}
