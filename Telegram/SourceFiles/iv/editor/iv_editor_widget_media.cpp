/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_widget.h"

#include "base/event_filter.h"
#include "base/qthelp_url.h"
#include "base/qt/qt_common_adapters.h"
#include "base/random.h"
#include "base/weak_qptr.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/rich_paste_toast.h"
#include "core/mime_type.h"
#include "data/data_msg_id.h"
#include "data/data_types.h"
#include "editor/editor_layer_widget.h"
#include "editor/photo_editor.h"
#include "editor/photo_editor_common.h"
#include "iv/editor/iv_editor_article_style.h"
#include "iv/editor/iv_editor_auto_pair.h"
#include "iv/editor/iv_editor_clipboard_import.h"
#include "iv/editor/iv_editor_commands.h"
#include "iv/editor/iv_editor_math_box.h"
#include "iv/editor/iv_editor_page_media.h"
#include "iv/editor/iv_editor_page_path.h"
#include "iv/editor/iv_editor_page_table_grid.h"
#include "iv/editor/iv_editor_prepared_selection.h"
#include "iv/editor/iv_editor_session.h"
#include "iv/editor/iv_editor_structure_menu.h"
#include "iv/editor/iv_editor_text_entities.h"
#include "iv/editor/iv_editor_window.h"
#include "iv/markdown/iv_markdown_article_paint.h"
#include "iv/markdown/iv_markdown_article_selection.h"
#include "iv/markdown/iv_markdown_article_text.h"
#include "iv/markdown/iv_markdown_microtex.h"
#include "iv/markdown/iv_markdown_prepare_links.h"
#include "iv/markdown/iv_markdown_prepare_native_richtext.h"
#include "iv/markdown/iv_markdown_prepare_serialize.h"
#include "iv/markdown/iv_markdown_slideshow_chrome.h"
#include "iv/markdown/iv_markdown_theme.h"
#include "iv/iv_search_bar.h"
#include "iv/iv_search_controller.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "menu/menu_checked_action.h"
#include "platform/platform_file_utilities.h"
#include "spellcheck/spellcheck_highlight_syntax.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/click_handler.h"
#include "ui/delayed_activation.h"
#include "ui/image/image.h"
#include "ui/image/image_location.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_html_tags.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/menu/menu_separator.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"

#include "styles/palette.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_iv.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QEvent>
#include <QtCore/QMimeData>
#include <QtCore/QPointer>
#include <QtGui/QClipboard>
#include <QtGui/qevent.h>
#include <QtGui/QCursor>
#include <QtGui/QTextBlock>
#include <QtGui/QTextLayout>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QTextEdit>
#include <QShortcut>
#include <QAction>

#include <algorithm>
#include <array>
#include "iv/editor/iv_editor_widget_internal.h"

namespace Iv::Editor {
using namespace WidgetDetails;

std::optional<State::BlockPath> Widget::simpleMediaBlockPathFromHit(
		const PreparedEditHit &hit) const {
	if (hit.kind != PreparedEditHitKind::Block || !hit.block) {
		return std::nullopt;
	}
	const auto path = _state->convertBlockPath(*hit.block);
	if (!path) {
		return std::nullopt;
	}
	const auto block = BlockFromPath(_state->richPage(), *path);
	if (!block || !IsSimpleMediaBlockKind(block->kind)) {
		return std::nullopt;
	}
	return path;
}

std::optional<State::BlockPath> Widget::documentRowBlockPathFromHit(
		const PreparedEditHit &hit) const {
	const auto path = simpleMediaBlockPathFromHit(hit);
	if (!path) {
		return std::nullopt;
	}
	const auto block = BlockFromPath(_state->richPage(), *path);
	return (block && RichBlockIsDocumentRow(block->kind))
		? path
		: std::nullopt;
}

std::optional<State::BlockPath> Widget::groupedMediaBlockPathFromHit(
		const PreparedEditHit &hit) const {
	if (hit.kind != PreparedEditHitKind::Block || !hit.block) {
		return std::nullopt;
	}
	const auto path = _state->convertBlockPath(*hit.block);
	if (!path) {
		return std::nullopt;
	}
	const auto block = BlockFromPath(_state->richPage(), *path);
	if (!block || block->kind != RichPage::BlockKind::GroupedMedia) {
		return std::nullopt;
	}
	return path;
}

bool Widget::structuralPhotoVideoSelectionAvailable() const {
	return _state->canGroupPhotoVideoBlocks(_structuralSelection);
}

bool Widget::clickHitsStructuralPhotoVideoSelection(
		const PreparedEditHit &hit) const {
	if (!structuralPhotoVideoSelectionAvailable()
		|| _structuralSelection.kind != PreparedEditSelectionKind::Blocks
		|| hit.kind != PreparedEditHitKind::Block
		|| !hit.block) {
		return false;
	}
	const auto path = _state->convertBlockPath(*hit.block);
	if (!path || !BlockFromPath(_state->richPage(), *path)) {
		return false;
	}
	return PreparedPathInBlockRange(
		hit.block->path,
		_structuralSelection.blocks);
}

void Widget::showSimpleMediaMenu(
		const State::BlockPath &path,
		QPoint globalPos) {
	const auto block = BlockFromPath(_state->richPage(), path);
	if (!block || !IsSimpleMediaBlockKind(block->kind)) {
		return;
	}
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	menu->addAction(
		tr::lng_attach_replace(tr::now),
		[=] {
			requestReplaceMedia(path);
		},
		&st::menuIconReplace);
	if (IsPhotoVideoBlockKind(block->kind)) {
		addReplaceFromClipboardAction(menu, path, -1);
	}
	if (block->kind == RichPage::BlockKind::Photo) {
		menu->addAction(
			tr::lng_context_draw(tr::now),
			[=] {
				editPhotoBlock(path);
			},
			&st::menuIconPalette);
	}
	if (IsPhotoVideoBlockKind(block->kind)) {
		const auto currentSpoiler = block->spoiler;
		Menu::AddCheckedAction(
			menu,
			tr::lng_context_spoiler_effect(tr::now),
			[=] {
				[[maybe_unused]] const auto changed = applyMediaBlockChange([=] {
					const auto current = BlockFromPath(
						_state->richPage(),
						path);
					if (!current || !IsPhotoVideoBlockKind(current->kind)) {
						return false;
					}
					return _state->toggleSpoilerOnBlocks(
						std::vector<State::BlockPath>{ path },
						!currentSpoiler);
				});
			},
			&st::menuIconSpoiler,
			currentSpoiler);
	}
	const auto removeText = RichBlockIsDocumentRow(block->kind)
		? tr::lng_context_delete_msg(tr::now)
		: tr::lng_box_remove(tr::now);
	Ui::Menu::CreateAddActionCallback(menu)({
		.text = removeText,
		.handler = [=] {
			auto target = std::optional<int>();
			const auto changed = applyMediaBlockChange([=, &target] {
				const auto current = BlockFromPath(
					_state->richPage(),
					path);
				if (!current || !IsSimpleMediaBlockKind(current->kind)) {
					return false;
				}
				target = _state->removeBlock(path, true);
				return true;
			});
			if (!changed) {
				return;
			} else if (target) {
				activateTextOrdinal(*target, 0);
			} else {
				activateInitialNode();
			}
		},
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
	if (menu->empty()) {
		menu->deleteLater();
		return;
	}
	menu->popup(globalPos);
}

void Widget::showGroupedMediaMenu(
		const State::BlockPath &path,
		int itemIndex,
		QPoint globalPos) {
	const auto block = BlockFromPath(_state->richPage(), path);
	if (!block || block->kind != RichPage::BlockKind::GroupedMedia) {
		return;
	}
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	const auto hasItem = (itemIndex >= 0)
		&& (itemIndex < int(block->mediaItems.size()))
		&& IsPhotoVideoBlockKind(block->mediaItems[itemIndex].kind);
	const auto currentSpoiler = hasItem
		? block->mediaItems[itemIndex].spoiler
		: GroupedPhotoVideoItemsHaveSpoiler(*block);
	if (hasItem) {
		menu->addAction(
			tr::lng_attach_replace(tr::now),
			[=] {
				requestReplaceGroupedItem(path, itemIndex);
			},
			&st::menuIconReplace);
		addReplaceFromClipboardAction(menu, path, itemIndex);
		if (block->mediaItems[itemIndex].kind
			== RichPage::BlockKind::Photo) {
			menu->addAction(
				tr::lng_context_draw(tr::now),
				[=] {
					editGroupedItemPhoto(path, itemIndex);
				},
				&st::menuIconPalette);
		}
	}
	menu->addAction(
		tr::lng_article_media_ungroup(tr::now),
		[=] {
			[[maybe_unused]] const auto changed = applyMediaBlockChange([=] {
				const auto current = BlockFromPath(
					_state->richPage(),
					path);
				if (!current
					|| current->kind != RichPage::BlockKind::GroupedMedia) {
					return false;
				}
				return _state->ungroupGroupedMediaBlock(path);
			});
		},
		&st::menuIconExpand);
	const auto toSlideshow = (block->mediaIntent
		!= RichPage::GroupedMediaIntent::Slideshow);
	menu->addAction(
		toSlideshow
			? tr::lng_article_media_slideshow(tr::now)
			: tr::lng_article_media_collage(tr::now),
		[=] {
			toggleGroupedMediaIntent(path);
		},
		toSlideshow ? &st::menuIconPhotoSet : &st::menuIconShowAll);
	if (GroupedMediaHasPhotoVideoItems(*block)) {
		Menu::AddCheckedAction(
			menu,
			tr::lng_context_spoiler_effect(tr::now),
			[=] {
				[[maybe_unused]] const auto changed
					= applyGroupedMediaChangePreservingActiveIndex(path, [=] {
						const auto current = BlockFromPath(
							_state->richPage(),
							path);
						if (!current
							|| !GroupedMediaHasPhotoVideoItems(*current)) {
							return false;
						}
						return hasItem
							? _state->toggleSpoilerOnGroupedItem(
								path,
								itemIndex,
								!currentSpoiler)
							: _state->toggleSpoilerOnBlocks(
								std::vector<State::BlockPath>{ path },
								!currentSpoiler);
					});
			},
			&st::menuIconSpoiler,
			currentSpoiler);
	}
	Ui::Menu::CreateAddActionCallback(menu)({
		.text = tr::lng_box_remove(tr::now),
		.handler = [=] {
			auto target = std::optional<int>();
			const auto changed = applyGroupedMediaChangePreservingActiveIndex(
				path,
				[=, &target] {
					const auto current = BlockFromPath(
						_state->richPage(),
						path);
					if (!current
						|| current->kind != RichPage::BlockKind::GroupedMedia) {
						return false;
					}
					if (hasItem) {
						return _state->removeGroupedItem(path, itemIndex);
					}
					target = _state->removeBlock(path, true);
					return true;
				});
			if (!changed) {
				return;
			} else if (target) {
				activateTextOrdinal(*target, 0);
			} else {
				activateInitialNode();
			}
		},
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
	if (menu->empty()) {
		menu->deleteLater();
		return;
	}
	menu->popup(globalPos);
}

bool Widget::activateMediaBlockLinkFromHit(
		const PreparedEditHit &hit,
		const Markdown::MarkdownArticleHitTestResult &articleHit,
		Qt::MouseButton button) {
	if (!articleHit.state.link
		|| (articleHit.mediaActivation.kind
			!= Markdown::MediaActivationKind::None)
		|| (!groupedMediaBlockPathFromHit(hit)
			&& !documentRowBlockPathFromHit(hit))) {
		return false;
	}
	ActivateClickHandler(this, articleHit.state.link, button);
	return true;
}

int Widget::groupedActiveIndexForPath(const State::BlockPath &path) const {
	for (const auto &geo : _article->mediaBlockGeometries()) {
		if (!geo.grouped) {
			continue;
		}
		const auto geoPath = _state->convertBlockPath(geo.block);
		if (geoPath && (*geoPath == path)) {
			return geo.activeItemIndex;
		}
	}
	return -1;
}

void Widget::restoreGroupedActiveIndexForPath(
		const State::BlockPath &path,
		int activeIndex) {
	if (activeIndex < 0) {
		return;
	}
	for (const auto &geo : _article->mediaBlockGeometries()) {
		if (!geo.grouped) {
			continue;
		}
		const auto geoPath = _state->convertBlockPath(geo.block);
		if (geoPath && (*geoPath == path)) {
			_article->setGroupedActiveIndex(geo.block, activeIndex);
			return;
		}
	}
}

bool Widget::applyGroupedMediaChangePreservingActiveIndex(
		const State::BlockPath &path,
		Fn<bool()> change) {
	const auto savedActiveIndex = groupedActiveIndexForPath(path);
	const auto changed = applyMediaBlockChange(std::move(change));
	if (changed) {
		restoreGroupedActiveIndexForPath(path, savedActiveIndex);
	}
	return changed;
}

bool Widget::applyMediaBlockChange(Fn<bool()> change) {
	const auto hadVisibleField = !_field->isHidden();
	auto changed = false;
	const auto result = recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		}
		_pendingOrdinal = -1;
		_pendingCursorOffset = 0;
		hideInlineField();
		clearInlineFieldEditSession();
		changed = change();
		if (changed) {
			refreshPreparedContent();
		} else if (hadVisibleField) {
			refreshAfterInlineFieldCommit(committed);
		}
		clearTextSelection();
		clearStructuralSelection();
		setFocus();
		notifyToolbarStateChanged();
		return MutationTransactionResult{
			.committed = committed,
			.changed = (committed == ApplyResult::Changed) || changed,
		};
	});
	return !result.failed && changed;
}

std::optional<State::ReplaceTarget> Widget::replaceTargetForMedia(
		const State::BlockPath &path,
		int itemIndex) const {
	if (itemIndex < 0) {
		return _state->replaceTargetForBlock(path);
	} else if (mediaUploadStateForGroupedItem(path, itemIndex).uploading) {
		return std::nullopt;
	}
	return _state->replaceTargetForGroupedItem(path, itemIndex);
}

void Widget::requestReplaceMedia(State::BlockPath path) {
	auto target = replaceTargetForMedia(path, -1);
	if (!target) {
		return;
	}
	const auto type = (target->kind == RichPage::BlockKind::File)
		? RequestMediaType::File
		: RequestMediaType::PhotoVideoAudio;
	requestMedia(std::move(target), type);
}

void Widget::requestReplaceGroupedItem(
		State::BlockPath path,
		int itemIndex) {
	auto target = replaceTargetForMedia(path, itemIndex);
	if (!target) {
		return;
	}
	requestMedia(std::move(target), RequestMediaType::PhotoVideoAudio);
}

void Widget::addReplaceFromClipboardAction(
		not_null<Ui::PopupMenu*> menu,
		State::BlockPath path,
		int itemIndex) {
	const auto data = QApplication::clipboard()->mimeData();
	if (!_replacePhotoWithList || !data || !data->hasImage()) {
		return;
	}
	menu->addAction(
		tr::lng_profile_photo_from_clipboard(tr::now),
		[=] {
			replaceMediaFromClipboard(path, itemIndex);
		},
		&st::menuIconPhoto);
}

void Widget::replaceMediaFromClipboard(
		State::BlockPath path,
		int itemIndex) {
	const auto data = QApplication::clipboard()->mimeData();
	if (!_replacePhotoWithList || !data) {
		return;
	}
	auto list = PreparedMediaFromClipboard(
		not_null<const QMimeData*>(data),
		SessionPremium(_session));
	if (!list) {
		return;
	}
	auto target = replaceTargetForMedia(path, itemIndex);
	if (!target) {
		return;
	}
	_replacePhotoWithList(
		not_null<Widget*>(this),
		std::move(*list),
		std::move(*target));
}

void Widget::editPhotoBlock(State::BlockPath path) {
	const auto block = BlockFromPath(_state->richPage(), path);
	if (!block || block->kind != RichPage::BlockKind::Photo) {
		return;
	}
	auto target = _state->replaceTargetForBlock(path);
	if (!target) {
		return;
	}
	openPhotoEditor(block->photoId, block->spoiler, std::move(*target));
}

void Widget::openPhotoEditor(
		uint64 photoId,
		bool spoiler,
		State::ReplaceTarget target) {
	if (!_requestPhotoEditSource) {
		return;
	}
	const auto weak = base::make_weak(this);
	_requestPhotoEditSource(photoId, [=, target = std::move(target)](
			QImage source) {
		const auto strong = weak.get();
		if (!strong || source.isNull()) {
			return;
		}
		strong->showPhotoEditor(std::move(source), spoiler, target);
	});
}

void Widget::showPhotoEditor(
		QImage source,
		bool spoiler,
		State::ReplaceTarget target) {
	const auto previewWidth = st::sendMediaPreviewSize;
	const auto sourceShared = std::make_shared<QImage>(std::move(source));
	const auto replaceTarget = std::make_shared<State::ReplaceTarget>(
		std::move(target));
	auto fileImage = std::make_shared<Image>(QImage(*sourceShared));
	auto editor = base::make_unique_q<::Editor::PhotoEditor>(
		_outer,
		_show,
		nullptr,
		std::move(fileImage),
		::Editor::PhotoModifications());
	const auto raw = editor.get();
	auto layer = std::make_unique<::Editor::LayerWidget>(
		_outer,
		std::move(editor));
	const auto weak = base::make_weak(this);
	::Editor::InitEditorLayer(layer.get(), raw, [=](
			::Editor::PhotoModifications mods) {
		const auto strong = weak.get();
		if (!strong || !mods || !strong->_replacePhotoWithList) {
			return;
		}
		auto copy = QImage(*sourceShared);
		auto list = Storage::PrepareMediaFromImage(
			std::move(copy),
			QByteArray(),
			previewWidth);
		if (list.files.empty()) {
			return;
		}
		using ImageInfo = Ui::PreparedFileInformation::Image;
		auto &file = list.files.front();
		file.spoiler = spoiler;
		if (const auto image = std::get_if<ImageInfo>(
				&file.information->media)) {
			image->modifications = std::move(mods);
		}
		Storage::ApplyModifications(list);
		strong->_replacePhotoWithList(
			not_null<Widget*>(strong),
			std::move(list),
			*replaceTarget);
	});
	_show->showLayer(std::move(layer), Ui::LayerOption::KeepOther);
}

void Widget::editGroupedItemPhoto(State::BlockPath path, int itemIndex) {
	const auto block = BlockFromPath(_state->richPage(), path);
	if (!block
		|| block->kind != RichPage::BlockKind::GroupedMedia
		|| itemIndex < 0
		|| itemIndex >= int(block->mediaItems.size())) {
		return;
	}
	const auto &item = block->mediaItems[itemIndex];
	if (item.kind != RichPage::BlockKind::Photo
		|| mediaUploadStateForGroupedItem(path, itemIndex).uploading) {
		return;
	}
	auto target = _state->replaceTargetForGroupedItem(path, itemIndex);
	if (!target) {
		return;
	}
	openPhotoEditor(item.photoId, item.spoiler, std::move(*target));
}

MediaUploadState Widget::mediaUploadStateForBlock(
		const State::BlockPath &path) const {
	const auto block = BlockFromPath(_state->richPage(), path);
	if (!block) {
		return {};
	}
	const auto mediaId = MediaIdForBlock(*block);
	return _mediaUploadState ? _mediaUploadState(mediaId) : MediaUploadState();
}

MediaUploadState Widget::mediaUploadStateForGroupedItem(
		const State::BlockPath &path,
		int itemIndex) const {
	const auto block = BlockFromPath(_state->richPage(), path);
	if (!block
		|| block->kind != RichPage::BlockKind::GroupedMedia
		|| itemIndex < 0
		|| itemIndex >= int(block->mediaItems.size())) {
		return {};
	}
	const auto mediaId = MediaIdForGroupedItem(block->mediaItems[itemIndex]);
	return _mediaUploadState ? _mediaUploadState(mediaId) : MediaUploadState();
}

Widget::MediaControlLayout Widget::mediaControlLayout(
		QRect mediaRect) const {
	const auto d = st::ivEditorMediaCornerSize;
	const auto skip = st::ivEditorMediaCornerSkip;
	const auto &r = mediaRect;
	const auto threeDots = QRect(r.left() + skip, r.top() + skip, d, d);
	const auto plus = QRect(r.right() - skip - d + 1, r.top() + skip, d, d);
	const auto layoutSwitch = plus.translated(-(d + skip), 0);
	const auto rs = st::ivEditorMediaUploadRadialSize;
	const auto radial = QRect(
		r.center().x() - rs / 2,
		r.center().y() - rs / 2,
		rs,
		rs);
	return { threeDots, plus, radial, layoutSwitch };
}

void Widget::paintMediaControls(Painter &p, QPoint topLeft) {
	for (const auto &geo : _article->mediaBlockGeometries()) {
		if (geo.visibleMediaRect.isEmpty()) {
			continue;
		}
		const auto path = _state->convertBlockPath(geo.block);
		if (!path) {
			continue;
		}
		const auto block = BlockFromPath(_state->richPage(), *path);
		if (!block) {
			continue;
		}
		const auto paintCircleIcon = [&](QRect circle, const style::icon &icon) {
			Markdown::PaintRoundButton(
				p,
				circle.translated(topLeft),
				st::roundedBg,
				icon);
		};
		if (block->kind == RichPage::BlockKind::GroupedMedia) {
			const auto active = geo.activeItemIndex;
			for (auto i = 0, count = int(geo.itemRects.size())
				; i != count
				; ++i) {
				const auto &itemRect = geo.itemRects[i];
				if (itemRect.isEmpty()) {
					continue;
				}
				const auto itemIndex = (active >= 0) ? active : i;
				const auto layout = mediaControlLayout(itemRect);
				if (!mediaUploadStateForGroupedItem(*path, itemIndex).uploading) {
					paintCircleIcon(
						layout.threeDots,
						st::sendBoxAlbumButtonMediaMore);
				}
			}
			const auto group = mediaControlLayout(geo.visibleMediaRect);
			const auto switchIcon = (block->mediaIntent
					== RichPage::GroupedMediaIntent::Slideshow)
				? &st::ivEditorMediaToCollageIcon
				: &st::ivEditorMediaToSlideshowIcon;
			paintCircleIcon(group.layoutSwitch, *switchIcon);
			paintCircleIcon(group.plus, st::ivEditorMediaAddIcon);
			continue;
		}
		if (!IsPhotoVideoBlockKind(block->kind)) {
			continue;
		}
		const auto layout = mediaControlLayout(geo.visibleMediaRect);
		if (!mediaUploadStateForBlock(*path).uploading) {
			paintCircleIcon(layout.threeDots, st::sendBoxAlbumButtonMediaMore);
			paintCircleIcon(layout.plus, st::ivEditorMediaAddIcon);
		}
	}
}

void Widget::paintButtonRowControls(Painter &p, QPoint topLeft) {
	for (const auto &rect : _article->buttonRowControlRects()) {
		Markdown::PaintRoundButton(
			p,
			rect.translated(topLeft),
			st::roundedBg,
			st::sendBoxAlbumButtonMediaMore);
	}
}

Widget::PressedMediaControl Widget::mediaControlHitTest(
		QPoint articlePoint) const {
	for (const auto &geo : _article->mediaBlockGeometries()) {
		if (geo.visibleMediaRect.isEmpty()) {
			continue;
		}
		const auto path = _state->convertBlockPath(geo.block);
		if (!path) {
			continue;
		}
		const auto block = BlockFromPath(_state->richPage(), *path);
		if (!block) {
			continue;
		}
		if (block->kind == RichPage::BlockKind::GroupedMedia) {
			const auto active = geo.activeItemIndex;
			for (auto i = 0, count = int(geo.itemRects.size())
				; i != count
				; ++i) {
				const auto &itemRect = geo.itemRects[i];
				if (itemRect.isEmpty()) {
					continue;
				}
				const auto itemIndex = (active >= 0) ? active : i;
				const auto layout = mediaControlLayout(itemRect);
				if (mediaUploadStateForGroupedItem(*path, itemIndex).uploading) {
					if (layout.radial.contains(articlePoint)) {
						return { MediaControl::UploadRadial, *path, itemIndex };
					}
				} else if (layout.threeDots.contains(articlePoint)) {
					return { MediaControl::ThreeDots, *path, itemIndex };
				}
			}
			const auto group = mediaControlLayout(geo.visibleMediaRect);
			if (group.layoutSwitch.contains(articlePoint)) {
				return { MediaControl::LayoutSwitch, *path };
			} else if (group.plus.contains(articlePoint)) {
				return { MediaControl::Plus, *path };
			}
			continue;
		}
		if (!IsSimpleMediaBlockKind(block->kind)) {
			continue;
		}
		const auto layout = mediaControlLayout(geo.visibleMediaRect);
		if (mediaUploadStateForBlock(*path).uploading) {
			if (layout.radial.contains(articlePoint)) {
				return { MediaControl::UploadRadial, *path };
			}
		} else if (RichBlockIsDocumentRow(block->kind)) {
			continue;
		} else if (layout.threeDots.contains(articlePoint)) {
			return { MediaControl::ThreeDots, *path };
		} else if (layout.plus.contains(articlePoint)) {
			return { MediaControl::Plus, *path };
		}
	}
	return {};
}

void Widget::addToCollageFromBlock(const State::BlockPath &path) {
	if (_addMediaAndGroupWithBlock) {
		_addMediaAndGroupWithBlock(
			this,
			path,
			QPointer<QWidget>(_outer.get()));
	}
}

void Widget::toggleGroupedMediaIntent(const State::BlockPath &path) {
	[[maybe_unused]] const auto changed = applyMediaBlockChange([=] {
		const auto current = BlockFromPath(_state->richPage(), path);
		if (!current
			|| current->kind != RichPage::BlockKind::GroupedMedia) {
			return false;
		}
		const auto next = (current->mediaIntent
				== RichPage::GroupedMediaIntent::Slideshow)
			? RichPage::GroupedMediaIntent::Collage
			: RichPage::GroupedMediaIntent::Slideshow;
		return _state->setGroupedMediaIntent(path, next);
	});
}

void Widget::groupBlocksIntoGroup(
		State::BlockPath anchor,
		int insertedCount) {
	if (insertedCount < 1) {
		return;
	}
	const auto block = BlockFromPath(_state->richPage(), anchor);
	if (block && block->kind == RichPage::BlockKind::GroupedMedia) {
		[[maybe_unused]] const auto changed
			= applyGroupedMediaChangePreservingActiveIndex(anchor, [&] {
				return _state->addItemsToGroupedMedia(anchor, insertedCount);
			});
		return;
	}
	auto selection = _state->preparedSelectionForBlock(anchor);
	selection.blocks.till += insertedCount;
	if (!_state->canGroupPhotoVideoBlocks(selection)) {
		return;
	}
	[[maybe_unused]] const auto changed = applyMediaBlockChange([&] {
		return _state->groupPhotoVideoBlocks(
			selection,
			RichPage::GroupedMediaIntent::Collage);
	});
}

void Widget::cancelMediaUploadForBlock(const State::BlockPath &path) {
	const auto block = BlockFromPath(_state->richPage(), path);
	if (!block) {
		return;
	}
	const auto mediaId = MediaIdForBlock(*block);
	if (_cancelMediaUpload) {
		_cancelMediaUpload(this, mediaId);
	}
	auto target = std::optional<int>();
	const auto changed = applyMediaBlockChange([=, &target] {
		const auto current = BlockFromPath(_state->richPage(), path);
		if (!current || !IsSimpleMediaBlockKind(current->kind)) {
			return false;
		}
		target = _state->removeBlock(path, true);
		return true;
	});
	if (!changed) {
		return;
	} else if (target) {
		activateTextOrdinal(*target, 0);
	} else {
		activateInitialNode();
	}
}

void Widget::cancelMediaUploadForGroupedItem(
		const State::BlockPath &path,
		int itemIndex) {
	const auto block = BlockFromPath(_state->richPage(), path);
	if (!block
		|| block->kind != RichPage::BlockKind::GroupedMedia
		|| itemIndex < 0
		|| itemIndex >= int(block->mediaItems.size())) {
		return;
	}
	const auto mediaId = MediaIdForGroupedItem(block->mediaItems[itemIndex]);
	if (_cancelMediaUpload) {
		_cancelMediaUpload(this, mediaId);
	}
	[[maybe_unused]] const auto changed = applyMediaBlockChange([=] {
		const auto current = BlockFromPath(_state->richPage(), path);
		if (!current
			|| current->kind != RichPage::BlockKind::GroupedMedia) {
			return false;
		}
		return _state->removeGroupedItem(path, itemIndex);
	});
}

} // namespace Iv::Editor
