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

void Widget::addFieldBlockFormatActions(not_null<QMenu*> menu) {
	const auto formattingText = tr::lng_menu_formatting(tr::now);
	const auto baseText = [](const QString &text) {
		const auto tab = text.indexOf(QChar('\t'));
		return (tab >= 0) ? text.left(tab) : text;
	};
	auto submenu = (QMenu*)nullptr;
	for (const auto action : menu->actions()) {
		if (const auto candidate = action->menu()) {
			if (baseText(action->text()) == formattingText) {
				submenu = candidate;
				break;
			}
		}
	}
	if (!submenu) {
		return;
	}
	const auto textWithShortcut = [](QString text, const QKeySequence &sequence) {
		const auto shortcut = sequence.toString(QKeySequence::NativeText);
		return shortcut.isEmpty()
			? text
			: text + QChar('\t') + shortcut;
	};
	const auto shortcutText = [](const QString &text) {
		const auto tab = text.indexOf(QChar('\t'));
		return (tab >= 0) ? text.mid(tab + 1) : QString();
	};
	const auto monospaceText = tr::lng_menu_formatting_monospace(tr::now);
	const auto monospaceShortcut = Ui::kMonospaceSequence.toString(
		QKeySequence::NativeText);
	for (const auto action : submenu->actions()) {
		if (!action->isSeparator()
			&& baseText(action->text()) == monospaceText
			&& shortcutText(action->text()) == monospaceShortcut) {
			submenu->removeAction(action);
			delete action;
			break;
		}
	}
	const auto before = [&] {
		for (const auto action : submenu->actions()) {
			if (action->isSeparator()) {
				return action;
			}
		}
		return (QAction*)nullptr;
	}();
	const auto add = [&](QAction *action) {
		if (before) {
			submenu->insertAction(before, action);
		} else {
			submenu->addAction(action);
		}
	};
	if (!activeLeafIsTableCell()) {
		const auto blockquote = new QAction(
			textWithShortcut(
				tr::lng_menu_formatting_blockquote(tr::now),
				Ui::kBlockquoteSequence),
			submenu);
		connect(blockquote, &QAction::triggered, this, [=] {
			insertBlockquote();
		});
		add(blockquote);
	}

	const auto monospace = new QAction(
		textWithShortcut(monospaceText, Ui::kMonospaceSequence),
		submenu);
	connect(monospace, &QAction::triggered, this, [=] {
		applyFieldMonospaceAction();
	});
	add(monospace);
}

void Widget::handleFieldContextMenuRequest(
		Ui::InputField::ContextMenuRequest request) {
	addFieldBlockFormatActions(request.menu);
	if (!SingleRootPlainTextFieldSelectAllPassthrough(
			_state->richPage(),
			_state->activeLeafPath(),
			_field->isHidden())) {
		const auto selectAllShortcut = QKeySequence(
			QKeySequence::SelectAll).toString(QKeySequence::NativeText);
		const auto shortcutText = [](const QString &text) {
			const auto tab = text.indexOf(QChar('\t'));
			return (tab >= 0) ? text.mid(tab + 1) : QString();
		};
		for (const auto action : request.menu->actions()) {
			if (!action->isSeparator()
				&& !selectAllShortcut.isEmpty()
				&& shortcutText(action->text()) == selectAllShortcut) {
				QObject::disconnect(
					action,
					&QAction::triggered,
					nullptr,
					nullptr);
				connect(action, &QAction::triggered, this, [this] {
					selectWholeDocument();
				});
				break;
			}
		}
	}
	const auto activeLeaf = _state->activePreparedLeafSource();
	const auto listSource = activeLeaf
		? ListItemSourceFromLeaf(*activeLeaf)
		: std::optional<PreparedEditListItemSource>();
	const auto listBlock = activeLeaf
		? std::make_optional(activeLeaf->block)
		: std::optional<PreparedEditBlockPath>();
	const auto listRange = effectiveListRangeForSource(listSource, listBlock);
	const auto listSources = ListContextSources(listSource, listBlock);
	const auto listMenuRange = !listSources.empty()
		? fullListRangeForSource(listSources.front())
		: std::optional<PreparedListItemRange>();
	const auto cell = activeTableCellSourceAt(
		_field->rawTextEdit().get(),
		*request.event);
	const auto tableRange = cell
		? effectiveTableRangeForCell(*cell)
		: PreparedEditTableCellRange();
	const auto listInfo = listMenuRange
		? _state->listSelectionInfo(*listMenuRange)
		: State::ListSelectionInfo();
	const auto listItemInfo = listRange
		? _state->listSelectionInfo(*listRange)
		: State::ListSelectionInfo();
	const auto tableInfo = !tableRange.empty()
		? _state->tableSelectionInfo(tableRange)
		: State::TableSelectionInfo();
	const auto hasListMenu = listMenuRange && listInfo.valid;
	const auto hasListItemMenu = listRange
		&& listItemInfo.valid
		&& (listItemInfo.listKind == RichPage::ListKind::Ordered)
		&& !listItemInfo.taskList;
	const auto hasTableMenu = !tableRange.empty() && tableInfo.valid;
	if (!hasListMenu && !hasListItemMenu && !hasTableMenu) {
		return;
	}
	request.customizePopupMenu([=](not_null<Ui::PopupMenu*> popup) {
		const auto popupMenu = popup->menu();
		auto position = 0;
		const auto addSubmenu = [&](const QString &text, const auto &fill) {
			const auto action = new QAction(text, popupMenu.get());
			action->setMenu(new QMenu(popupMenu.get()));
			popup->insertAction(
				position++,
				base::make_unique_q<Ui::Menu::Action>(
					popupMenu,
					popupMenu->st(),
					action,
					nullptr,
					nullptr));
			const auto submenu = popup->ensureSubmenu(
				action,
				st::popupMenuWithIcons);
			fill(submenu);
		};
		if (hasListMenu) {
			addSubmenu(
				tr::lng_article_list_change(tr::now),
				[=](not_null<Ui::PopupMenu*> submenu) {
					fillListChangeMenu(submenu, *listMenuRange);
				});
		}
		if (hasListItemMenu) {
			addSubmenu(
				tr::lng_article_list_item_change(tr::now),
				[=](not_null<Ui::PopupMenu*> submenu) {
					fillListItemChangeMenu(submenu, *listRange);
				});
		}
		if (hasTableMenu) {
			addSubmenu(
				tr::lng_article_table_change(tr::now),
				[=](not_null<Ui::PopupMenu*> submenu) {
					fillTableChangeMenu(submenu, tableRange);
				});
		}
		if (position > 0) {
			const auto separator = new QAction(popupMenu.get());
			separator->setSeparator(true);
			popup->insertAction(
				position,
				base::make_unique_q<Ui::Menu::Separator>(
					popupMenu,
					popupMenu->st(),
					popupMenu->st().separator,
					separator));
		}
	});
}

std::optional<PreparedListItemRange> Widget::effectiveListRangeForSource(
		const std::optional<PreparedEditListItemSource> &source,
		const std::optional<PreparedEditBlockPath> &block) {
	auto fallback = std::optional<PreparedListItemRange>();
	for (const auto &candidate : ListContextSources(source, block)) {
		const auto single = ListRangeFromItem(candidate);
		if (single.empty()) {
			continue;
		}
		if (!fallback) {
			fallback = single;
		}
		if (const auto selected = _state->listContextRangeForSelection(
				_structuralSelection,
				candidate)) {
			return *selected;
		}
	}
	return fallback;
}

std::optional<PreparedListItemRange> Widget::fullListRangeForSource(
		const PreparedEditListItemSource &source) const {
	const auto path = _state->convertBlockPath(source.block);
	const auto block = path ? BlockFromPath(_state->richPage(), *path) : nullptr;
	if (!path
		|| !block
		|| block->kind != RichPage::BlockKind::List
		|| source.listItemIndex < 0
		|| source.listItemIndex >= int(block->listItems.size())) {
		return std::nullopt;
	}
	return PreparedListItemRange{
		.block = source.block,
		.from = 0,
		.till = int(block->listItems.size()),
	};
}

std::optional<Markdown::PreparedEditBlockPath> Widget::selectedBlockPath() const {
	switch (_structuralSelection.kind) {
	case PreparedEditSelectionKind::Blocks: {
		const auto &range = _structuralSelection.blocks;
		if (range.empty() || (range.till - range.from != 1)) {
			return std::nullopt;
		}
		return PreparedEditBlockPath{
			.container = range.container,
			.index = range.from,
		};
	}
	case PreparedEditSelectionKind::ListItems:
		return _structuralSelection.listItems.empty()
			? std::nullopt
			: std::make_optional(_structuralSelection.listItems.block);
	case PreparedEditSelectionKind::TableRows:
		return _structuralSelection.tableRows.empty()
			? std::nullopt
			: std::make_optional(_structuralSelection.tableRows.block);
	case PreparedEditSelectionKind::TableCells:
		return _structuralSelection.tableCells.empty()
			? std::nullopt
			: std::make_optional(_structuralSelection.tableCells.block);
	case PreparedEditSelectionKind::None:
		return std::nullopt;
	}
	return std::nullopt;
}

Widget::ActiveBlockInfo Widget::activeBlockInfo() const {
	if (const auto path = selectedBlockPath()) {
		const auto statePath = _state->convertBlockPath(*path);
		const auto block = statePath
			? BlockFromPath(_state->richPage(), *statePath)
			: nullptr;
		if (block) {
			return {
				.kind = block->kind,
				.pullquote = block->pullquote,
				.headingLevel = block->headingLevel,
			};
		}
	}
	const auto leaf = _state->activeLeafPath();
	if (!leaf) {
		return {};
	}
	const auto block = BlockFromPath(_state->richPage(), leaf->block);
	if (!block) {
		return {};
	}
	return {
		.kind = block->kind,
		.pullquote = block->pullquote,
		.headingLevel = block->headingLevel,
	};
}

std::optional<PreparedListItemRange> Widget::currentListRangeAtCaret() const {
	if (_structuralSelection.kind == PreparedEditSelectionKind::ListItems
		&& !_structuralSelection.listItems.empty()
		&& _state->listSelectionInfo(_structuralSelection.listItems).valid) {
		const auto source = PreparedEditListItemSource{
			.block = _structuralSelection.listItems.block,
			.listItemIndex = _structuralSelection.listItems.from,
		};
		if (const auto selected = _state->listContextRangeForSelection(
				_structuralSelection,
				source)) {
			if (!selected->empty()
				&& _state->listSelectionInfo(*selected).valid) {
				return *selected;
			}
		}
	}
	const auto activeLeaf = _state->activePreparedLeafSource();
	const auto listSource = activeLeaf
		? ListItemSourceFromLeaf(*activeLeaf)
		: std::optional<PreparedEditListItemSource>();
	const auto listBlock = activeLeaf
		? std::make_optional(activeLeaf->block)
		: std::optional<PreparedEditBlockPath>();
	const auto sources = ListContextSources(listSource, listBlock);
	if (sources.empty()) {
		return std::nullopt;
	}
	const auto range = fullListRangeForSource(sources.front());
	if (!range || !_state->listSelectionInfo(*range).valid) {
		return std::nullopt;
	}
	return range;
}

std::optional<PreparedListItemRange> Widget::currentListItemRangeAtCaret() {
	const auto activeLeaf = _state->activePreparedLeafSource();
	const auto listSource = activeLeaf
		? ListItemSourceFromLeaf(*activeLeaf)
		: std::optional<PreparedEditListItemSource>();
	const auto listBlock = activeLeaf
		? std::make_optional(activeLeaf->block)
		: std::optional<PreparedEditBlockPath>();
	const auto range = effectiveListRangeForSource(listSource, listBlock);
	if (!range || !_state->listSelectionInfo(*range).valid) {
		return std::nullopt;
	}
	return range;
}

State::ListSelectionInfo Widget::listSelectionInfo(
		const PreparedListItemRange &range) const {
	return _state->listSelectionInfo(range);
}

std::optional<PreparedEditTableCellRange>
Widget::currentTableRangeAtCaret() const {
	if (_structuralSelection.kind == PreparedEditSelectionKind::TableCells
		&& !_structuralSelection.tableCells.empty()
		&& _state->tableSelectionInfo(_structuralSelection.tableCells).valid) {
		return _structuralSelection.tableCells;
	}
	const auto activeLeaf = _state->activePreparedLeafSource();
	if (!activeLeaf
		|| activeLeaf->kind != PreparedEditLeafKind::TableCellText
		|| activeLeaf->tableRowIndex < 0
		|| activeLeaf->tableCellIndex < 0) {
		return std::nullopt;
	}
	const auto path = _state->convertBlockPath(activeLeaf->block);
	const auto block = path
		? BlockFromPath(_state->richPage(), *path)
		: nullptr;
	if (!block || block->kind != RichPage::BlockKind::Table) {
		return std::nullopt;
	}
	const auto grid = BuildTableGrid(*block);
	for (const auto &cell : grid.cells) {
		if (cell.rowIndex != activeLeaf->tableRowIndex
			|| cell.cellIndex != activeLeaf->tableCellIndex) {
			continue;
		}
		auto range = PreparedEditTableCellRange{
			.block = activeLeaf->block,
			.rowFrom = cell.rowFrom,
			.rowTill = cell.rowTill,
			.columnFrom = cell.columnFrom,
			.columnTill = cell.columnTill,
		};
		if (range.empty() || !_state->tableSelectionInfo(range).valid) {
			return std::nullopt;
		}
		const auto source = Markdown::PreparedEditTableCellSource{
			.block = activeLeaf->block,
			.tableRowIndex = cell.rowFrom,
			.tableCellIndex = cell.cellIndex,
			.column = cell.columnFrom,
			.colspan = cell.columnTill - cell.columnFrom,
			.rowspan = cell.rowTill - cell.rowFrom,
		};
		if (const auto selected = _state->tableContextRangeForSelection(
				_structuralSelection,
				source)) {
			if (!selected->empty()
				&& _state->tableSelectionInfo(*selected).valid) {
				return *selected;
			}
		}
		return range;
	}
	return std::nullopt;
}

void Widget::showListContextMenu(
		const PreparedListItemRange &range,
		QPoint globalPos) {
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	fillListChangeMenu(menu, range);
	if (menu->empty()) {
		menu->deleteLater();
		return;
	}
	menu->popup(globalPos);
}

PreparedEditTableCellRange Widget::effectiveTableRangeForCell(
		const PreparedEditTableCellSource &source) {
	const auto single = TableRangeFromCell(source);
	if (single.empty()) {
		return {};
	}
	if (const auto selected = _state->tableContextRangeForSelection(
			_structuralSelection,
			source)) {
		return *selected;
	}
	clearSelection();
	return single;
}

void Widget::fillListChangeMenu(
		not_null<Ui::PopupMenu*> menu,
		const PreparedListItemRange &range) {
	FillListChangeMenu(menu, _state.get(), range, [=](Fn<bool()> change) {
		applyListChange(std::move(change));
	});
}

void Widget::fillListItemChangeMenu(
		not_null<Ui::PopupMenu*> menu,
		const PreparedListItemRange &range) {
	FillListItemChangeMenu(menu, _state.get(), range, [=](Fn<bool()> change) {
		applyListChange(std::move(change));
	});
}

void Widget::fillTableChangeMenu(
		not_null<Ui::PopupMenu*> menu,
		const PreparedEditTableCellRange &range) {
	FillTableChangeMenu(menu, _state.get(), range, [=](Fn<bool()> change) {
		applyTableChange(std::move(change));
	});
}

void Widget::applyListChange(Fn<bool()> change) {
	recordMutationTransaction([&] {
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
		if (_article) {
			_article->clearTextLeafHeightOverride();
			_article->clearEditableTextEmptyOverride();
			_article->clearEditableMaxLineWidthOverride();
		}
		clearSelection();
		setFocus();
		if (!change()) {
			refreshAfterInlineFieldCommit(committed);
			showLastLimitToast();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		refreshPreparedContent();
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
}

void Widget::showTableContextMenu(
		const PreparedEditTableCellRange &range,
		QPoint globalPos) {
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	fillTableChangeMenu(menu, range);
	if (menu->empty()) {
		menu->deleteLater();
		return;
	}
	menu->popup(globalPos);
}

void Widget::applyTableChange(Fn<bool()> change) {
	recordMutationTransaction([&] {
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
		if (_article) {
			_article->clearTextLeafHeightOverride();
		}
		clearSelection();
		setFocus();
		if (!change()) {
			refreshAfterInlineFieldCommit(committed);
			showLastLimitToast();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		refreshPreparedContent();
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
}

void Widget::showRowButtonMenu(
		const Markdown::PreparedEditBlockSource &block,
		int index,
		bool disabled,
		QPoint globalPos) {
	if (!_state->rowButtonAt(block, index)) {
		return;
	}
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	if (!disabled) {
		menu->addAction(
			tr::lng_article_button_edit(tr::now),
			[=] {
				if (const auto request = rowButtonEditRequest(block, index)) {
					showButtonEditBox(*request);
				}
			},
			&st::menuIconEdit);
	}
	Ui::Menu::CreateAddActionCallback(menu)({
		.text = tr::lng_box_remove(tr::now),
		.handler = [=] {
			removeRowButton(block, index);
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

void Widget::removeRowButton(
		const Markdown::PreparedEditBlockSource &block,
		int index) {
	auto removal = State::RowButtonRemoveResult();
	[[maybe_unused]] const auto applied = applyMutationWithFieldCommit([&] {
		removal = _state->removeRowButtonAt(block, index);
		return removal.result;
	}, [&] {
		if (!removal.removedRow) {
			return;
		} else if (removal.caretOrdinal) {
			activateTextOrdinal(*removal.caretOrdinal, 0);
		} else {
			activateInitialNode();
		}
	});
}

void Widget::showButtonRowMenu(
		const Markdown::PreparedEditBlockSource &block,
		QPoint globalPos) {
	const auto current = _state->rowAlignment(block);
	if (!current) {
		return;
	}
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	menu->addAction(
		tr::lng_article_button_row_add(tr::now),
		[=] {
			showButtonEditBox({
				.target = ButtonEditRequest::Target::AppendToRow,
				.block = block,
			});
		},
		&st::ivEditorToolbarButtonIcon);
	const auto applyAlignment = [=](RichPage::ButtonAlignment alignment) {
		[[maybe_unused]] const auto applied = applyMutationWithFieldCommit([=] {
			return _state->setRowAlignment(block, alignment);
		}, [] {
		});
	};
	const auto addAlignment = [&](
			const QString &text,
			RichPage::ButtonAlignment alignment,
			const style::icon *icon) {
		Menu::AddCheckedAction(
			menu,
			text,
			[=] { applyAlignment(alignment); },
			icon,
			(*current == alignment));
	};
	addAlignment(
		tr::lng_article_button_row_stretch(tr::now),
		RichPage::ButtonAlignment::Stretch,
		&st::ivEditorButtonAlignStretchIcon);
	addAlignment(
		tr::lng_article_button_row_align_left(tr::now),
		RichPage::ButtonAlignment::Left,
		&st::ivEditorTableAlignLeftIcon);
	addAlignment(
		tr::lng_article_button_row_align_center(tr::now),
		RichPage::ButtonAlignment::Center,
		&st::ivEditorTableAlignCenterIcon);
	addAlignment(
		tr::lng_article_button_row_align_right(tr::now),
		RichPage::ButtonAlignment::Right,
		&st::ivEditorTableAlignRightIcon);
	menu->popup(globalPos);
}

void Widget::showStructuralPhotoVideoMenu(QPoint globalPos) {
	if (!structuralPhotoVideoSelectionAvailable()) {
		return;
	}
	const auto selection = _structuralSelection;
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	menu->addAction(
		tr::lng_article_media_collage(tr::now),
		[=] {
			[[maybe_unused]] const auto changed = applyMediaBlockChange([=] {
				return _state->groupPhotoVideoBlocks(
					selection,
					RichPage::GroupedMediaIntent::Collage);
			});
		},
		&st::menuIconShowAll);
	menu->addAction(
		tr::lng_article_media_slideshow(tr::now),
		[=] {
			[[maybe_unused]] const auto changed = applyMediaBlockChange([=] {
				return _state->groupPhotoVideoBlocks(
					selection,
					RichPage::GroupedMediaIntent::Slideshow);
			});
		},
		&st::menuIconPhotoSet);
	if (_state->canUngroupGroupedMediaBlocks(selection)) {
		menu->addAction(
			tr::lng_article_media_ungroup(tr::now),
			[=] {
				[[maybe_unused]] const auto changed = applyMediaBlockChange([=] {
					return _state->ungroupGroupedMediaBlocks(selection);
				});
			},
			&st::menuIconExpand);
	}
	Ui::Menu::CreateAddActionCallback(menu)({
		.text = tr::lng_box_remove(tr::now),
		.handler = [=] {
			if (structuralPhotoVideoSelectionAvailable()) {
				removeStructuralSelectionAndReposition(true);
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

bool Widget::showMediaMenuFromHit(
		const PreparedEditHit &hit,
		const Markdown::MarkdownArticleHitTestResult &articleHit,
		QPoint globalPos,
		MediaClickKind clickKind) {
	if (clickHitsStructuralPhotoVideoSelection(hit)) {
		showStructuralPhotoVideoMenu(globalPos);
		return true;
	} else if (const auto path = simpleMediaBlockPathFromHit(hit)) {
		if (documentRowBlockPathFromHit(hit)) {
			if ((clickKind != MediaClickKind::ContextMenu)
				|| !articleHit.direct) {
				return false;
			}
			showSimpleMediaMenu(*path, globalPos);
			return true;
		}
		if (articleHit.mediaActivation.kind
			== Markdown::MediaActivationKind::None) {
			return false;
		}
		if (clickKind == MediaClickKind::Left) {
			const auto block = BlockFromPath(_state->richPage(), *path);
			if (block && (block->kind == RichPage::BlockKind::Photo)) {
				editPhotoBlock(*path);
				return true;
			}
		}
		showSimpleMediaMenu(*path, globalPos);
		return true;
	} else if (const auto path = groupedMediaBlockPathFromHit(hit)) {
		if (articleHit.mediaActivation.kind
			== Markdown::MediaActivationKind::None) {
			return false;
		}
		const auto itemIndex = articleHit.mediaActivation.itemIndex;
		if (clickKind == MediaClickKind::Left) {
			const auto block = BlockFromPath(_state->richPage(), *path);
			const auto photoItem = block
				&& (itemIndex >= 0)
				&& (itemIndex < int(block->mediaItems.size()))
				&& (block->mediaItems[itemIndex].kind
					== RichPage::BlockKind::Photo);
			if (photoItem) {
				editGroupedItemPhoto(*path, itemIndex);
				return true;
			}
		}
		showGroupedMediaMenu(*path, itemIndex, globalPos);
		return true;
	}
	return false;
}

} // namespace Iv::Editor
