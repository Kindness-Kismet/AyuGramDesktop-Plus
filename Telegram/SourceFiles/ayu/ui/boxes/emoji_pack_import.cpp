#include "ayu/ui/boxes/emoji_pack_import.h"

#include "ayu/features/emoji_packs/emoji_packs.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "styles/style_settings.h"
#include "ui/emoji_config.h"
#include "ui/layers/box_content.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"

namespace Ayu::EmojiPacks {
namespace {

QString errorText(ImportError error) {
	switch (error) {
	case ImportError::Busy: return tr::ayu_EmojiPackImporting(tr::now);
	case ImportError::Read: return tr::ayu_EmojiPackReadError(tr::now);
	case ImportError::Font: return tr::ayu_EmojiPackFontError(tr::now);
	case ImportError::Full: return tr::ayu_EmojiPackFull(tr::now);
	case ImportError::Write: return tr::ayu_EmojiPackWriteError(tr::now);
	case ImportError::None: return {};
	}
	Unexpected("Invalid emoji import error");
}

} // namespace

void addPresetRows(not_null<Ui::VerticalLayout*> content, Fn<void()> refresh) {
	const auto packs = installed();
	for (const auto &preset : presets()) {
		const auto found = std::find_if(packs.begin(), packs.end(), [&](const Pack &pack) {
			return pack.hash == preset.hash;
		});
		const auto id = (found == packs.end()) ? -1 : found->id;
		const auto busy = content->lifetime().make_state<rpl::variable<bool>>(false);
		auto label = rpl::combine(
			busy->value(),
			rpl::single(rpl::empty) | rpl::then(Ui::Emoji::Updated())
		) | rpl::map([=](bool importing, auto) {
			return importing ? tr::ayu_EmojiPackImporting(tr::now)
				: (id == Ui::Emoji::CurrentSetId()) ? tr::lng_emoji_set_active(tr::now)
				: tr::lng_emoji_set_ready(tr::now);
		});
		const auto button = Settings::AddButtonWithLabel(content,
			rpl::single(preset.name), std::move(label), st::settingsButtonNoIcon);
		button->setObjectName(u"emoji/preset/"_q + preset.id);
		busy->value() | rpl::on_next([=](bool value) {
			button->setDisabled(value);
		}, button->lifetime());
		button->addClickHandler([=] {
			*busy = true;
			const auto activate = crl::guard(content, [=](int packId) {
				Ui::Emoji::SwitchToSet(packId, crl::guard(content, [=](bool success) {
					*busy = false;
					if (success) {
						refresh();
					} else {
						Ui::Toast::Show(tr::ayu_EmojiPackSwitchError(tr::now));
					}
				}));
			});
			if (id >= 0) {
				activate(id);
				return;
			}
			importFont(preset.path, crl::guard(content, [=](ImportResult result) {
				if (result.error != ImportError::None) {
					*busy = false;
					Ui::Toast::Show(errorText(result.error));
					return;
				}
				activate(result.pack.id);
			}), preset.name);
		});
	}
}

void addImportButton(not_null<Ui::BoxContent*> box, Fn<void()> refresh) {
	const auto busy = box->lifetime().make_state<rpl::variable<bool>>(false);
	const auto button = box->addLeftButton(tr::ayu_EmojiPackImport(), [=] {
		if (busy->current()) {
			return;
		}
		FileDialog::GetOpenPath(box.get(), tr::ayu_EmojiPackImport(tr::now),
			tr::ayu_EmojiPackFileFilter(tr::now),
			crl::guard(box, [=](FileDialog::OpenResult &&result) {
				if (result.paths.isEmpty()) {
					return;
				}
				*busy = true;
				importFont(result.paths.front(), crl::guard(box, [=](ImportResult result) {
					*busy = false;
					if (result.error != ImportError::None) {
						Ui::Toast::Show(errorText(result.error));
						return;
					}
					refresh();
					Ui::Emoji::SwitchToSet(result.pack.id, crl::guard(box, [](bool success) {
						if (!success) {
							Ui::Toast::Show(tr::ayu_EmojiPackSwitchError(tr::now));
						}
					}));
				}));
			}));
	});
	button->setObjectName(u"emoji/import"_q);
	busy->value() | rpl::on_next([=](bool importing) {
		button->setDisabled(importing);
		button->setText(importing
			? tr::ayu_EmojiPackImporting()
			: tr::ayu_EmojiPackImport());
	}, box->lifetime());
}

} // namespace Ayu::EmojiPacks
