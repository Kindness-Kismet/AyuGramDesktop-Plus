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

bool Widget::handleHardcodedBlockShortcut(QKeyEvent *e) {
	const auto type = e->type();
	if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress) {
		return false;
	}
	const auto perform = (type == QEvent::KeyPress);
	if (MatchesKeySequence(e, kEditorHeading1Sequence)) {
		if (perform) {
			insertBlock({
				.type = State::InsertBlockType::Heading,
				.headingLevel = 1,
			});
		}
	} else if (MatchesKeySequence(e, kEditorHeading2Sequence)) {
		if (perform) {
			insertBlock({
				.type = State::InsertBlockType::Heading,
				.headingLevel = 2,
			});
		}
	} else if (MatchesKeySequence(e, kEditorTableSequence)) {
		if (perform) {
			insertBlock({ .type = State::InsertBlockType::Table });
		}
	} else if (MatchesKeySequence(e, kEditorBodyTextSequence)) {
		if (perform) {
			applyToolbarFormatAction(ToolbarFormatAction::PlainText);
		}
	} else {
		return false;
	}
	e->accept();
	return true;
}

bool Widget::handleFieldBlockInsertShortcut(QKeyEvent *e) {
	if (_fieldMode != State::FieldMode::Rich || _field->isHidden()) {
		return false;
	}
	if (handleHardcodedBlockShortcut(e)) {
		return true;
	}
	const auto type = e->type();
	if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress) {
		return false;
	}
	const auto blockquote = MatchesKeySequence(e, Ui::kBlockquoteSequence);
	const auto monospace = MatchesKeySequence(e, Ui::kMonospaceSequence);
	if (!blockquote && !monospace) {
		return false;
	}
	if (blockquote) {
		if (type == QEvent::KeyPress) {
			insertBlockquote();
		}
		e->accept();
		return true;
	}
	if (type == QEvent::KeyPress) {
		applyFieldMonospaceAction();
	}
	e->accept();
	return true;
}

bool Widget::handleStructuralBlockInsertShortcut(QKeyEvent *e) {
	if (!hasStructuralSelection()) {
		return false;
	}
	if (handleHardcodedBlockShortcut(e)) {
		return true;
	}
	const auto type = e->type();
	if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress) {
		return false;
	}
	const auto blockquote = MatchesKeySequence(e, Ui::kBlockquoteSequence);
	const auto monospace = MatchesKeySequence(e, Ui::kMonospaceSequence);
	if (!blockquote && !monospace) {
		return false;
	}
	if (blockquote) {
		if (type == QEvent::KeyPress) {
			insertBlockquote();
		}
		e->accept();
		return true;
	}
	if (type == QEvent::KeyPress) {
		applyStructuralMonospaceAction();
	}
	e->accept();
	return true;
}

bool Widget::handleBroaderFormatShortcut(QKeyEvent *e) {
	if (inlineToolbarModeActive()) {
		return false;
	}
	const auto type = e->type();
	if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress) {
		return false;
	}
	const auto action = [&]() -> std::optional<ToolbarFormatAction> {
		if (e == QKeySequence::Bold) {
			return ToolbarFormatAction::Bold;
		} else if (e == QKeySequence::Italic) {
			return ToolbarFormatAction::Italic;
		} else if (e == QKeySequence::Underline) {
			return ToolbarFormatAction::Underline;
		} else if (MatchesKeySequence(e, Ui::kStrikeOutSequence)) {
			return ToolbarFormatAction::StrikeOut;
		} else if (MatchesKeySequence(e, Ui::kSpoilerSequence)) {
			return ToolbarFormatAction::Spoiler;
		} else if (MatchesKeySequence(e, Ui::kClearFormatSequence)) {
			return ToolbarFormatAction::PlainText;
		}
		return std::nullopt;
	}();
	if (!action || !toolbarActionState(*action).enabled) {
		return false;
	}
	if (type == QEvent::KeyPress) {
		applyToolbarFormatAction(*action);
	}
	e->accept();
	return true;
}

bool Widget::activeLeafIsTableCell() const {
	const auto leaf = _state->activeLeafPath();
	return leaf && (leaf->kind == StateLeafKind::TableCellText);
}

bool Widget::fieldMonospaceShortcutUsesCodeBlock() const {
	// A cell holds text, not blocks, so monospace there is the inline tag.
	return (_fieldMode == State::FieldMode::Rich)
		&& _field
		&& _field->isVisible()
		&& !activeLeafIsTableCell()
		&& (_field->selectionMarkdownTagForToggle(
			Ui::InputField::kTagCode) != Ui::InputField::kTagCode);
}

bool Widget::structuralMonospaceShortcutTargetsCodeBlock() const {
	if (_structuralSelection.kind != PreparedSelectionKind::Blocks) {
		return false;
	}
	const auto &range = _structuralSelection.blocks;
	if (range.from + 1 != range.till) {
		return false;
	}
	const auto block = BlockFromPath(_state->richPage(), ToStateBlockPath({
		.container = range.container,
		.index = range.from,
	}));
	return block
		&& ((block->kind == RichPage::BlockKind::Paragraph)
			|| (block->kind == RichPage::BlockKind::Code));
}

void Widget::applyFieldMonospaceAction() {
	if (!_field) {
		return;
	} else if (fieldMonospaceShortcutUsesCodeBlock()) {
		insertCodeBlock();
		return;
	}
	const auto cursor = _field->textCursor();
	const auto wholeCell = !cursor.hasSelection() && activeLeafIsTableCell();
	if (wholeCell) {
		auto selecting = cursor;
		selecting.select(QTextCursor::Document);
		if (selecting.hasSelection()) {
			_field->setTextCursor(selecting);
		}
	}
	if (activeLeafIsTableCell()) {
		toggleFieldMonospaceLineByLine();
	} else {
		_field->toggleCurrentMarkdownTag(Ui::InputField::kTagCode);
	}
	if (wholeCell) {
		_field->setTextCursor(cursor);
	}
	notifyToolbarStateChanged();
}

void Widget::toggleFieldMonospaceLineByLine() {
	// A multiline selection would toggle a code block, which a cell can't keep.
	const auto tag = Ui::InputField::kTagCode;
	const auto raw = _field->rawTextEdit();
	const auto cursor = raw->textCursor();
	const auto from = cursor.selectionStart();
	const auto till = cursor.selectionEnd();
	if (from >= till) {
		_field->toggleCurrentMarkdownTag(tag);
		return;
	}
	const auto document = raw->document();
	auto lines = std::vector<std::pair<int, int>>();
	auto lineFrom = from;
	for (auto position = from; position != till; ++position) {
		if (!IsFieldLineBreak(document->characterAt(position))) {
			continue;
		}
		if (lineFrom < position) {
			lines.push_back({ lineFrom, position });
		}
		lineFrom = position + 1;
	}
	if (lineFrom < till) {
		lines.push_back({ lineFrom, till });
	}
	const auto remove = _field->isMarkdownTagActive(tag);
	for (auto i = int(lines.size()); i != 0;) {
		const auto line = lines[--i];
		auto lineCursor = raw->textCursor();
		lineCursor.setPosition(line.first);
		lineCursor.setPosition(line.second, QTextCursor::KeepAnchor);
		_field->setTextCursor(lineCursor);
		if (_field->isMarkdownTagActive(tag) == remove) {
			_field->toggleCurrentMarkdownTag(tag);
		}
	}
}

void Widget::applyStructuralMonospaceAction() {
	if (!structuralMonospaceShortcutTargetsCodeBlock()) {
		return;
	}
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
		clearInlineFieldEditSession();
		if (!_state->toggleCodeBlockForStructuralSelection(
				_structuralSelection)) {
			showLastLimitToast();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		clearSelection();
		refreshPreparedContent();
		activateTextOrdinal(_state->activeTextOrdinal(), 0);
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
}

void Widget::notifyToolbarStateChanged() {
	updateHasSelection();
	_toolbarStateChanges.fire_copy(toolbarStateValue());
}

bool Widget::inlineToolbarModeActive() const {
	return !_field->isHidden()
		&& (_state->activeFieldMode() == State::FieldMode::Rich);
}

Widget::ToolbarLinkMode Widget::toolbarLinkMode() const {
	return inlineToolbarModeActive() && _field->hasCurrentMarkdownLink()
		? ToolbarLinkMode::Edit
		: ToolbarLinkMode::Create;
}

Widget::ToolbarActionState Widget::toolbarActionState(
		ToolbarFormatAction action) const {
	const auto inlineActive = inlineToolbarModeActive();
	const auto activeDisplayMath = !inlineActive
		&& [&] {
			const auto leaf = _state->activeLeafPath();
			return leaf && (leaf->kind == StateLeafKind::MathFormula);
		}();
	const auto broaderTextSelected = !inlineActive
		&& broaderSelectionHasSelectedText();
	const auto broaderMediaSelected = !inlineActive
		&& (action == ToolbarFormatAction::Spoiler)
		&& !broaderSelectionMediaBlocks().empty();
	switch (action) {
	case ToolbarFormatAction::Undo:
		return {
			.shown = true,
			.enabled = canPerformFieldUndoRedo(false)
				|| canPerformHistoryUndoRedo(false),
		};
	case ToolbarFormatAction::Redo: {
		const auto enabled = canPerformFieldUndoRedo(true)
			|| canPerformHistoryUndoRedo(true);
		return {
			.shown = enabled,
			.enabled = enabled,
		};
	}
	case ToolbarFormatAction::Link:
		return {
			.shown = true,
			.enabled = inlineActive,
		};
	case ToolbarFormatAction::Count:
		return {};
	case ToolbarFormatAction::Bold:
	case ToolbarFormatAction::Italic:
	case ToolbarFormatAction::Underline:
	case ToolbarFormatAction::StrikeOut:
	case ToolbarFormatAction::Subscript:
	case ToolbarFormatAction::Superscript:
	case ToolbarFormatAction::Marked:
	case ToolbarFormatAction::PlainText:
		return {
			.shown = true,
			.enabled = inlineActive || broaderTextSelected,
			.active = inlineActive
				&& (action != ToolbarFormatAction::PlainText)
				&& ToolbarActionTag(action)
				&& _field->isMarkdownTagActive(*ToolbarActionTag(action)),
		};
	case ToolbarFormatAction::Spoiler:
		return {
			.shown = true,
			.enabled = inlineActive
				|| broaderTextSelected
				|| broaderMediaSelected,
			.active = inlineActive
				&& _field->isMarkdownTagActive(Ui::InputField::kTagSpoiler),
		};
	case ToolbarFormatAction::Math:
		return {
			.shown = true,
			.enabled = inlineActive || activeDisplayMath,
			.active = activeDisplayMath || (inlineActive
				&& _field->isMarkdownTagActive(Ui::InputField::kTagIvMath)),
		};
	}
	return {};
}

void Widget::clearFieldUndoRedoNoopState() {
	_fieldUndoNoopState = std::nullopt;
	_fieldRedoNoopState = std::nullopt;
}

bool Widget::escapeActiveBlockBodyFromToolbar() {
	if (_field->isHidden()
		|| _field->textCursor().hasSelection()
		|| !_state->activeBlockBodyCanEscape()) {
		return false;
	}
	auto handled = false;
	recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		} else if (const auto target = _state->escapeActiveBlockBody()) {
			refreshPreparedContent();
			activateTextOrdinal(*target, 0);
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.changed = true,
			};
		} else if (_state->lastLimitError()) {
			showLastLimitToast();
			handled = true;
		}
		return MutationTransactionResult{
			.committed = committed,
			.changed = (committed == ApplyResult::Changed),
		};
	});
	return handled;
}

Fn<void()> Widget::captureScrollTopRestorer() const {
	for (auto parent = parentWidget(); parent; parent = parent->parentWidget()) {
		if (const auto scroll = dynamic_cast<Ui::ScrollArea*>(parent)) {
			const auto weak = QPointer<Ui::ScrollArea>(scroll);
			const auto top = scroll->scrollTop();
			return [=] {
				if (weak) {
					weak->scrollToY(top);
				}
			};
		}
		if (const auto scroll = dynamic_cast<Ui::ElasticScroll*>(parent)) {
			const auto weak = QPointer<Ui::ElasticScroll>(scroll);
			const auto top = scroll->scrollTop();
			return [=] {
				if (weak) {
					weak->scrollToY(top);
				}
			};
		}
	}
	return nullptr;
}

void Widget::insertBlockquote() {
	insertBlock({ .type = State::InsertBlockType::Blockquote });
}

void Widget::insertCodeBlock() {
	insertBlock({ .type = State::InsertBlockType::Code });
}

void Widget::insertEmoji(EmojiPtr emoji) {
	if (!emoji || !prepareFieldForInput()) {
		return;
	}
	_field->setFocusFast();
	Ui::InsertEmojiAtCursor(_field->textCursor(), emoji);
}

void Widget::insertCustomEmoji(not_null<DocumentData*> document) {
	if (!prepareFieldForInput()) {
		return;
	}
	_field->setFocusFast();
	InsertCustomEmoji(_field.get(), document);
}

Widget::ToolbarState Widget::toolbarStateValue() const {
	auto result = ToolbarState();
	result.linkMode = toolbarLinkMode();
	for (auto i = 0; i != int(ToolbarFormatAction::Count); ++i) {
		const auto action = ToolbarFormatAction(i);
		result[action] = toolbarActionState(action);
	}
	return result;
}

rpl::producer<Widget::ToolbarState> Widget::toolbarStateChanges() const {
	return _toolbarStateChanges.events_starting_with(toolbarStateValue());
}

rpl::producer<Widget::AutosaveEvent> Widget::autosaveEvents() const {
	return _autosaveEvents.events();
}

void Widget::performToolbarUndoRedo(bool redo) {
	if (!canPerformFieldUndoRedo(redo) && !canPerformHistoryUndoRedo(redo)) {
		return;
	}
	performUndoRedo(redo);
}

void Widget::applyToolbarFormatAction(ToolbarFormatAction action) {
	switch (action) {
	case ToolbarFormatAction::Undo:
		performToolbarUndoRedo(false);
		return;
	case ToolbarFormatAction::Redo:
		performToolbarUndoRedo(true);
		return;
	case ToolbarFormatAction::Link:
		editLinkFromToolbar();
		return;
	case ToolbarFormatAction::Math:
		editMathFromToolbar();
		return;
	case ToolbarFormatAction::Count:
		return;
	case ToolbarFormatAction::Bold:
	case ToolbarFormatAction::Italic:
	case ToolbarFormatAction::Underline:
	case ToolbarFormatAction::StrikeOut:
	case ToolbarFormatAction::Spoiler:
	case ToolbarFormatAction::Subscript:
	case ToolbarFormatAction::Superscript:
	case ToolbarFormatAction::Marked:
	case ToolbarFormatAction::PlainText:
		break;
	}
	if (action == ToolbarFormatAction::PlainText) {
		if (inlineToolbarModeActive() && escapeActiveBlockBodyFromToolbar()) {
			return;
		}
		if (!_settingField
			&& !_field->isHidden()
			&& (_activeSegmentIndex >= 0)
			&& (_state->activeFieldMode() != State::FieldMode::Raw)) {
			const auto leaf = _state->activeLeafPath();
			const auto owner = (leaf && leaf->kind == StateLeafKind::BlockText)
				? BlockFromPath(_state->richPage(), leaf->block)
				: nullptr;
			if (owner && (owner->kind == RichPage::BlockKind::Heading)) {
				insertBlock({
					.type = State::InsertBlockType::Heading,
					.headingLevel = owner->headingLevel,
				});
				return;
			} else if (owner && (owner->kind == RichPage::BlockKind::Footer)) {
				insertBlock({ .type = State::InsertBlockType::Footer });
				return;
			}
		}
	}
	if (inlineToolbarModeActive()) {
		if (action == ToolbarFormatAction::PlainText) {
			_field->clearCurrentMarkdown();
			notifyToolbarStateChanged();
			return;
		}
		if (const auto tag = ToolbarActionTag(action)) {
			_field->toggleCurrentMarkdownTag(*tag);
			notifyToolbarStateChanged();
		}
		return;
	}
	const auto textSpans = broaderSelectionTextSpans();
	const auto mediaBlocks = (action == ToolbarFormatAction::Spoiler)
		? broaderSelectionMediaBlocks()
		: std::vector<State::BlockPath>();
	const auto broaderAction = BroaderFormattingAction(action);
	if ((!broaderAction || textSpans.empty())
		&& mediaBlocks.empty()) {
		return;
	}
	recordMutationTransaction([&] {
		const auto hadVisibleField = !_field->isHidden();
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		}
		if (hadVisibleField) {
			_pendingOrdinal = -1;
			_pendingCursorOffset = 0;
			hideInlineField();
			clearInlineFieldEditSession();
		}
		auto changed = false;
		if (action == ToolbarFormatAction::Spoiler) {
			const auto &page = _state->richPage();
			const auto allTextSpoilered = textSpans.empty()
				|| ranges::all_of(textSpans, [&](const TextNodeSpan &span) {
					const auto current = RichTextFromPath(page, span.leaf);
					if (!current) {
						return true;
					}
					auto before = TextWithEntities();
					auto selected = TextWithEntities();
					auto after = TextWithEntities();
					if (!SplitTextSpan(
							current->text,
							span.from,
							span.till,
							&before,
							&selected,
							&after)) {
						return true;
					}
					return HasFullTextTag(
						ConvertRichTextToEditorTags(std::move(selected)).text,
						Ui::InputField::kTagSpoiler);
				});
			const auto allMediaSpoilered = mediaBlocks.empty()
				|| ranges::all_of(
					mediaBlocks,
					[&](const State::BlockPath &path) {
						const auto block = BlockFromPath(page, path);
						return block && MediaBlockHasSpoiler(*block);
					});
			const auto enableSpoiler = !(allTextSpoilered && allMediaSpoilered);
			if (!textSpans.empty()) {
				const auto result = _state->applyFormattingToTextSpans(
					textSpans,
					TextFormattingAction::Spoiler,
					enableSpoiler);
				if (result == ApplyResult::Failed) {
					return MutationTransactionResult{
						.committed = committed,
						.failed = true,
					};
				}
				changed |= (result == ApplyResult::Changed);
			}
			if (!mediaBlocks.empty()) {
				changed |= _state->toggleSpoilerOnBlocks(
					mediaBlocks,
					enableSpoiler);
			}
		} else if (broaderAction) {
			const auto result = _state->applyFormattingToTextSpans(
				textSpans,
				*broaderAction);
			if (result == ApplyResult::Failed) {
				return MutationTransactionResult{
					.committed = committed,
					.failed = true,
				};
			}
			changed = (result == ApplyResult::Changed);
		}
		if (changed || hadVisibleField || (committed == ApplyResult::Changed)) {
			refreshPreparedContent();
		}
		setFocus();
		notifyToolbarStateChanged();
		return MutationTransactionResult{
			.committed = committed,
			.changed = changed
				|| hadVisibleField
				|| (committed == ApplyResult::Changed),
		};
	});
}

void Widget::editLinkFromToolbar() {
	if (!inlineToolbarModeActive()) {
		return;
	}
	_field->editCurrentMarkdownLink();
}

void Widget::editMathFromToolbar() {
	if (!_field->isHidden()
		&& _state->activeFieldMode() == State::FieldMode::Raw) {
		hideInlineField();
		clearInlineFieldEditSession();
	}
	if (const auto request = activeMathEditRequest()) {
		showMathEditBox(*request);
	} else {
		showMathEditBox(newDisplayMathRequest());
	}
}

void Widget::editButtonFromToolbar() {
	auto request = ButtonEditRequest();
	request.allowSeparateLine
		= _state->activeSurfaceAllowsSeparateLineFormula();
	showButtonEditBox(std::move(request));
}

void Widget::setInlineFieldExternalInteractionActive(bool active) {
	_inlineFieldExternalInteractionActive = active;
}

} // namespace Iv::Editor
