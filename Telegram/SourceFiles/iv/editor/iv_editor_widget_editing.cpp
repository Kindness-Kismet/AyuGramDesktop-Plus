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

PreparedMediaPasteTarget Widget::preparedMediaPasteTarget() const {
	auto result = PreparedMediaPasteTarget();
	const auto leaf = _state->activeLeafPath();
	if (!leaf) {
		return result;
	}
	result.leaf = leaf;
	const auto ordinal = _state->textOrdinalForLeafPath(*leaf);
	if (ordinal >= 0) {
		result.anchor = _state->textNodes()[ordinal].insertionAnchor;
	}
	switch (leaf->kind) {
	case StateLeafKind::BlockText:
	case StateLeafKind::ListItemText:
		result.context = ClipboardPasteInsertContext(
			activeTextInsertContext());
		break;
	case StateLeafKind::BlockCaption:
	case StateLeafKind::TableCellText:
	case StateLeafKind::MathFormula:
		break;
	}
	return result;
}

Widget::PreparedMediaPasteActivation
Widget::activatePreparedMediaPasteTarget(PreparedMediaPasteTarget target) {
	if (!target.leaf) {
		return {};
	}
	const auto ordinal = _state->textOrdinalForLeafPath(*target.leaf);
	if (ordinal < 0) {
		return {};
	}
	const auto &nodes = _state->textNodes();
	if (ordinal >= int(nodes.size())) {
		return {};
	}
	if (target.anchor
		&& ((nodes[ordinal].insertionAnchor.blockIndex
				!= target.anchor->blockIndex)
			|| !(nodes[ordinal].insertionAnchor.container
				== target.anchor->container))) {
		return {};
	}
	activateTextOrdinal(ordinal, 0);
	return {
		.resolved = true,
		.context = std::move(target.context),
	};
}

std::optional<Widget::MathEditRequest> Widget::activeMathEditRequest() const {
	if (_settingField
		|| (_activeSegmentIndex < 0)) {
		return std::nullopt;
	}
	if (_state->activeFieldMode() == State::FieldMode::Raw) {
		const auto leaf = _state->activeLeafPath();
		if (!leaf || leaf->kind != StateLeafKind::MathFormula) {
			return std::nullopt;
		}
		return MathEditRequest{
			.source = _state->activeRawText(),
			.displayMathOrdinal = _state->activeTextOrdinal(),
			.editingExisting = true,
			.allowSeparateLine = true,
			.separateLine = true,
		};
	}
	if (_field->isHidden()) {
		return std::nullopt;
	}
	if (_state->activeFieldMode() != State::FieldMode::Rich) {
		return std::nullopt;
	}
	const auto cursor = _field->textCursor();
	const auto selection = Ui::InputFieldTextRange{
		.from = cursor.selectionStart(),
		.till = cursor.selectionEnd(),
	};
	auto request = MathEditRequest{
		.range = selection,
		.allowSeparateLine = _state->activeSurfaceAllowsSeparateLineFormula(),
	};
	if (!selection.empty()) {
		request.source = _field->getTextWithTagsPart(
			selection.from,
			selection.till).text;
		return request;
	}
	request.range = _field->selectionEditMarkdownTagRange(
		selection,
		Ui::InputField::kTagIvMath);
	if (!request.range.empty()) {
		request.source = _field->getTextWithTagsPart(
			request.range.from,
			request.range.till).text;
		request.editingExisting = true;
	}
	return request;
}

Widget::MathEditRequest Widget::newDisplayMathRequest() const {
	return MathEditRequest{
		.allowSeparateLine = true,
		.separateLine = true,
		.insertNewDisplayBlock = true,
	};
}

auto Widget::inlineButtonEditRequestFromArticleHit(
		const Markdown::MarkdownArticleHitTestResult &hit) const
-> std::optional<ButtonEditRequest> {
	if (!hit.valid()
		|| !hit.direct
		|| (hit.forcedOffset >= 0)
		|| !hit.state.uponSymbol
		|| !_article->segmentIsEditable(hit.segmentIndex)) {
		return std::nullopt;
	}
	const auto ordinal = editableOrdinalForSegment(hit.segmentIndex);
	if (ordinal < 0) {
		return std::nullopt;
	}
	const auto offset = int(hit.state.symbol);
	const auto button = _state->inlineButtonAt(ordinal, offset);
	if (!button
		|| (button->type == HistoryMessageMarkupButton::Type::Disabled)) {
		return std::nullopt;
	}
	return MakeInlineButtonEditRequest(ordinal, offset, *button);
}

auto Widget::inlineButtonEditRequestFromFieldPoint(QPoint globalPoint) const
-> std::optional<ButtonEditRequest> {
	if (_settingField
		|| _field->isHidden()
		|| (_state->activeFieldMode() != State::FieldMode::Rich)) {
		return std::nullopt;
	}
	const auto ordinal = _state->activeTextOrdinal();
	if (ordinal < 0) {
		return std::nullopt;
	}
	const auto raw = _field->rawTextEdit();
	const auto local = raw->viewport()->mapFromGlobal(globalPoint);
	const auto cursor = raw->cursorForPosition(local);
	const auto boundary = raw->cursorRect(cursor).x();
	const auto index = (local.x() >= boundary)
		? cursor.position()
		: (cursor.position() - 1);
	if (index < 0) {
		return std::nullopt;
	}
	const auto part = _field->getTextWithTagsPart(index, index + 1);
	if ((part.text.size() != 1)
		|| (part.text[0] != QChar::ObjectReplacementCharacter)
		|| (part.tags.size() != 1)) {
		return std::nullopt;
	}
	const auto &id = part.tags.front().id;
	auto button = std::optional<Markdown::InlineTextObjectButtonData>();
	for (const auto &component : TextUtilities::SplitTags(id)) {
		if (!Ui::InputField::IsCustomEmojiLink(component)) {
			continue;
		}
		button = ButtonDataFromEntity(
			Ui::InputField::CustomEmojiEntityData(component));
		if (button) {
			break;
		}
	}
	if (!button
		|| (button->type == HistoryMessageMarkupButton::Type::Disabled)) {
		return std::nullopt;
	}
	const auto text = ConvertEditorTagsToRichText(
		_field->getTextWithAppliedMarkdown());
	return MakeInlineButtonEditRequest(
		ordinal,
		richOffsetForFieldOffset(text, index),
		*button);
}

Widget::ButtonEditRequest Widget::MakeInlineButtonEditRequest(
		int ordinal,
		int offset,
		const Markdown::InlineTextObjectButtonData &button) {
	return ButtonEditRequest{
		.target = ButtonEditRequest::Target::InlineToken,
		.data = {
			.label = button.label,
			.payload = button.data,
			.type = button.type,
			.color = button.color,
		},
		.ordinal = ordinal,
		.offset = offset,
		.editingExisting = true,
	};
}

std::optional<Widget::ButtonEditRequest> Widget::rowButtonEditRequest(
		const Markdown::PreparedEditBlockSource &block,
		int index) const {
	const auto button = _state->rowButtonAt(block, index);
	if (!button
		|| (button->button.type
			== HistoryMessageMarkupButton::Type::Disabled)) {
		return std::nullopt;
	}
	return ButtonEditRequest{
		.target = ButtonEditRequest::Target::RowButton,
		.data = {
			.label = button->text.text,
			.payload = button->button.data,
			.type = button->button.type,
			.color = button->button.visual.color,
		},
		.block = block,
		.buttonIndex = index,
		.editingExisting = true,
	};
}

ApplyResult Widget::applyMathEditResult(
		const MathEditRequest &request,
		MathEditResult result) {
	const auto source = result.source.trimmed();
	if (source.isEmpty()) {
		return ApplyResult::Unchanged;
	}
	if (_settingField) {
		return ApplyResult::Unchanged;
	}
	if (request.insertNewDisplayBlock) {
		auto block = RichPage::Block();
		block.kind = RichPage::BlockKind::Math;
		block.formula = source;
		insertPreparedBlock(std::move(block));
		return ApplyResult::Changed;
	}
	if (request.displayMathOrdinal >= 0) {
		if (!_state->setActiveTextByOrdinal(request.displayMathOrdinal)) {
			return ApplyResult::Failed;
		}
		_activeOrdinal = request.displayMathOrdinal;
		_activeSegmentIndex = segmentIndexForEditableOrdinal(_activeOrdinal);
	}
	const auto displayMathEdit
		= (_state->activeFieldMode() == State::FieldMode::Raw);
	if (!displayMathEdit && _field->isHidden()) {
		return ApplyResult::Unchanged;
	}
	if (displayMathEdit) {
		auto displayMathResult = State::DisplayMathEditResult();
		const auto committed = recordMutationTransaction([&] {
			displayMathResult = _state->editActiveDisplayMath(
				source,
				result.separateLine);
			return displayMathResult.result;
		});
		if (committed == ApplyResult::Failed) {
			showLastLimitToast();
			return committed;
		}
		if (committed != ApplyResult::Changed) {
			return committed;
		}
		refreshPreparedContent();
		if (displayMathResult.inlineLeaf) {
			const auto ordinal = _state->textOrdinalForLeafPath(
				*displayMathResult.inlineLeaf);
			activateTextOrdinal(
				(ordinal >= 0) ? ordinal : _state->activeTextOrdinal(),
				displayMathResult.selectionFrom,
				displayMathResult.selectionTo);
		} else {
			activateTextOrdinal(_state->activeTextOrdinal(), 0);
		}
		return committed;
	}
	if (_state->activeFieldMode() != State::FieldMode::Rich) {
		return ApplyResult::Unchanged;
	}
	if (result.separateLine) {
		auto cursor = _field->textCursor();
		cursor.setPosition(request.range.from);
		cursor.setPosition(request.range.till, QTextCursor::KeepAnchor);
		_field->setTextCursor(cursor);
		auto block = RichPage::Block();
		block.kind = RichPage::BlockKind::Math;
		block.formula = source;
		insertPreparedBlock(std::move(block));
		return ApplyResult::Changed;
	}
	auto restoreOrdinal = -1;
	auto restoreOffset = 0;
	const auto committed = recordMutationTransaction([&] {
		_field->commitMarkdownTagEdit(
			request.range,
			Ui::InputField::kTagIvMath,
			source);
		restoreOrdinal = _state->activeTextOrdinal();
		restoreOffset = richOffsetForFieldPosition(
			request.range.from + int(source.size()));
		const auto committed = commitInlineField();
		if (committed != ApplyResult::Failed) {
			_pendingOrdinal = -1;
			_pendingCursorOffset = 0;
			hideInlineField();
			clearInlineFieldEditSession();
		}
		return committed;
	});
	if (committed != ApplyResult::Failed) {
		refreshAfterInlineFieldCommit(committed);
		if (restoreOrdinal >= 0) {
			activateTextOrdinal(restoreOrdinal, restoreOffset);
		}
	}
	return committed;
}

ApplyResult Widget::applyButtonEditResult(
		const ButtonEditRequest &request,
		RichButtonEditResult result) {
	if (_settingField) {
		return ApplyResult::Unchanged;
	}
	auto data = std::move(result.data);
	const auto makeRowButton = [&] {
		auto button = RichPage::Button();
		button.text.text = data.label;
		button.button = HistoryMessageMarkupButton(
			data.type,
			data.label.text,
			HistoryMessageMarkupButton::Visual{ .color = data.color },
			data.payload);
		return button;
	};
	const auto makeInlineObject = [&] {
		return Markdown::InlineTextObjectButtonData{
			.label = data.label,
			.data = data.payload,
			.type = data.type,
			.color = data.color,
		};
	};
	if (request.target == ButtonEditRequest::Target::RowButton) {
		return applyMutationWithFieldCommit([&] {
			return _state->editRowButtonAt(
				request.block,
				request.buttonIndex,
				makeRowButton());
		}, [] {
		});
	} else if (request.target == ButtonEditRequest::Target::AppendToRow) {
		return applyMutationWithFieldCommit([&] {
			return _state->addRowButton(request.block, makeRowButton());
		}, [] {
		});
	} else if (request.target == ButtonEditRequest::Target::InlineToken) {
		return applyMutationWithFieldCommit([&] {
			return _state->editInlineButtonAt(
				request.ordinal,
				request.offset,
				makeInlineObject());
		}, [&] {
			activateTextOrdinal(request.ordinal, 0);
		});
	}
	if (result.separateLine) {
		auto block = RichPage::Block();
		block.kind = RichPage::BlockKind::ButtonRow;
		block.buttonAlignment = RichPage::ButtonAlignment::Stretch;
		block.buttons.push_back(makeRowButton());
		insertPreparedBlock(std::move(block));
		return ApplyResult::Changed;
	}
	if (_field->isHidden()
		|| (_state->activeFieldMode() != State::FieldMode::Rich)) {
		return ApplyResult::Unchanged;
	}
	const auto serialized = Markdown::SerializeInlineTextObjectEntity({
		.kind = Markdown::InlineTextObjectKind::Button,
		.data = makeInlineObject(),
	});
	if (serialized.isEmpty()) {
		return ApplyResult::Unchanged;
	}
	auto restoreOrdinal = -1;
	auto restoreOffset = 0;
	const auto committed = recordMutationTransaction([&] {
		const auto cursor = _field->textCursor();
		const auto position = cursor.selectionStart() + 1;
		Ui::InsertCustomEmojiAtCursor(
			_field.get(),
			cursor,
			QString(QChar::ObjectReplacementCharacter),
			Ui::InputField::CustomEmojiLink(serialized));
		restoreOrdinal = _state->activeTextOrdinal();
		restoreOffset = richOffsetForFieldPosition(position);
		const auto committed = commitInlineField();
		if (committed != ApplyResult::Failed) {
			_pendingOrdinal = -1;
			_pendingCursorOffset = 0;
			hideInlineField();
			clearInlineFieldEditSession();
		}
		return committed;
	});
	if (committed != ApplyResult::Failed) {
		refreshAfterInlineFieldCommit(committed);
		if (restoreOrdinal >= 0) {
			activateTextOrdinal(restoreOrdinal, restoreOffset);
		}
	}
	return committed;
}

ApplyResult Widget::applyMutationWithFieldCommit(
		Fn<ApplyResult()> mutate,
		Fn<void()> afterRefresh) {
	const auto hadVisibleField = !_field->isHidden();
	auto mutated = ApplyResult::Unchanged;
	const auto transaction = recordMutationTransaction([&] {
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
		mutated = mutate();
		if (mutated == ApplyResult::Changed) {
			refreshPreparedContent();
		} else if (hadVisibleField) {
			refreshAfterInlineFieldCommit(committed);
		}
		clearTextSelection();
		clearStructuralSelection();
		setFocus();
		if (mutated == ApplyResult::Changed) {
			afterRefresh();
		}
		return MutationTransactionResult{
			.committed = committed,
			.changed = (committed == ApplyResult::Changed)
				|| (mutated == ApplyResult::Changed),
			.failed = (mutated == ApplyResult::Failed),
		};
	});
	if (mutated == ApplyResult::Failed) {
		showLastLimitToast();
	}
	return transaction.failed ? ApplyResult::Failed : mutated;
}

bool Widget::showLastLimitToast() {
	if (_showLimitToast) {
		if (const auto error = _state->lastLimitError()) {
			_showLimitToast(*error);
			return true;
		}
	}
	return false;
}

void Widget::showMathEditBox(MathEditRequest request) {
	if (!_show) {
		return;
	}
	const auto weak = QPointer<Widget>(this);
	_show->showBox(Box(
		EditMathBox,
		request.source,
		request.editingExisting,
		request.allowSeparateLine
			? std::make_optional(request.separateLine)
			: std::nullopt,
		[=](QString source, bool separateLine) {
			if (!weak) {
				return;
			}
			const auto result = weak->applyMathEditResult(request, {
				.source = std::move(source),
				.separateLine = separateLine,
			});
			if (result != ApplyResult::Changed) {
				return;
			}
			weak->syncInlineFieldGeometry();
			weak->updateInlineFieldHeightOverride();
			weak->revealActiveInlineField();
			weak->notifyToolbarStateChanged();
		},
		[=](bool active) {
			if (weak) {
				weak->setInlineFieldExternalInteractionActive(active);
				weak->notifyToolbarStateChanged();
			}
		},
		[=] {
			if (weak && !weak->_field->isHidden()) {
				weak->_field->setFocusFast();
				weak->notifyToolbarStateChanged();
			}
		}));
}

void Widget::showButtonEditBox(ButtonEditRequest request) {
	if (!_show) {
		return;
	}
	const auto weak = QPointer<Widget>(this);
	_show->showBox(Box(
		EditRichButtonBox,
		_show,
		RichButtonEditBoxArgs{
			.data = request.data,
			.validateUrl = ValidateInstantViewEditorLink,
			.separateLine = request.allowSeparateLine
				? std::make_optional(false)
				: std::nullopt,
			.editingExisting = request.editingExisting,
		},
		[=](RichButtonEditResult result) {
			if (!weak) {
				return;
			}
			const auto applied = weak->applyButtonEditResult(
				request,
				std::move(result));
			if (applied != ApplyResult::Changed) {
				return;
			}
			weak->syncInlineFieldGeometry();
			weak->updateInlineFieldHeightOverride();
			weak->revealActiveInlineField();
			weak->notifyToolbarStateChanged();
		},
		[=](bool active) {
			if (weak) {
				weak->setInlineFieldExternalInteractionActive(active);
				weak->notifyToolbarStateChanged();
			}
		},
		[=] {
			if (weak && !weak->_field->isHidden()) {
				weak->_field->setFocusFast();
				weak->notifyToolbarStateChanged();
			}
		}));
}

} // namespace Iv::Editor
