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

void Widget::setTopContentPadding(int value) {
	if (_topContentPadding == value) {
		return;
	}
	_topContentPadding = value;
	updateSearchBarGeometry();
	resizeToWidth(width());
	update();
}

void Widget::setBottomContentPadding(int value) {
	if (_bottomContentPadding == value) {
		return;
	}
	_bottomContentPadding = value;
	resizeToWidth(width());
	update();
}

void Widget::setContentMaxWidth(int value) {
	if (_contentMaxWidth == value) {
		return;
	}
	_contentMaxWidth = value;
	update();
}

rpl::producer<int> Widget::searchSlideHeightValue() const {
	return _searchSlideHeight.value();
}

int Widget::resizeGetHeight(int newWidth) {
	if (!_article) {
		return 1;
	}
	const auto width = std::max(newWidth, 1);
	const auto padding = effectiveBodyPadding();
	if (articleRelayoutDeferralActive()) {
		requestDeferredArticleRelayout();
		if (!_field->isHidden()) {
			requestDeferredInlineFieldGeometry();
			requestDeferredInlineFieldHeightOverride();
		}
		const auto fieldBottom = !_field->isHidden()
			? (_field->y() + _field->height())
			: 0;
		return std::max(
			std::max(
				_articleHeight + padding.top() + padding.bottom(),
				fieldBottom),
			st::ivEditorMinHeight);
	}
	_articleHeight = _article->resizeGetHeight(articleWidth(width));
	syncArticleVisibleTopBottom();
	ensurePendingActivation();
	syncInlineFieldGeometry(width);
	const auto fieldBottom = !_field->isHidden()
		? (_field->y() + _field->height())
		: 0;
	return std::max(
		std::max(
			_articleHeight + padding.top() + padding.bottom(),
			fieldBottom),
		st::ivEditorMinHeight);
}

void Widget::visibleTopBottomUpdated(int visibleTop, int visibleBottom) {
	_visibleRange = Ui::VisibleRange{
		.top = visibleTop,
		.bottom = visibleBottom,
	};
	syncArticleVisibleTopBottom();
	_insertSuggestions->updatePosition();
}

void Widget::paintEvent(QPaintEvent *e) {
	if (!_article) {
		return;
	}
	auto p = Painter(this);
	p.setTextPalette(st::inTextPalette);
	const auto topLeft = articleTopLeft();
	p.save();
	p.translate(topLeft);
	_article->paint(
		p,
		textPaintContext(e->rect().translated(-topLeft.x(), -topLeft.y())));
	p.restore();
	paintMediaControls(p, topLeft);
	paintButtonRowControls(p, topLeft);
	_insertSuggestions->paintQuery(p);
	if (!_articleSelectionDrag.indicatorRect.isEmpty()) {
		auto color = st::windowActiveTextFg->c;
		color.setAlphaF(color.alphaF() * 0.7);
		auto rect = _articleSelectionDrag.indicatorRect.translated(topLeft);
		rect.setHeight(std::max(rect.height(), st::lineWidth));
		p.fillRect(rect, color);
	}
	if (!_externalMediaDrag.indicatorRect.isEmpty()) {
		auto color = st::windowActiveTextFg->c;
		color.setAlphaF(color.alphaF() * 0.7);
		auto rect = _externalMediaDrag.indicatorRect.translated(topLeft);
		rect.setHeight(std::max(rect.height(), st::lineWidth));
		p.fillRect(rect, color);
	}
}

void Widget::resizeEvent(QResizeEvent *e) {
	Ui::RpWidget::resizeEvent(e);
	syncInlineFieldGeometry();
}

void Widget::requestRepaint(QRect articleRect) {
	crl::on_main(this, [=] {
		if (!_article) {
			return;
		} else if (articleRect.isEmpty()) {
			update();
		} else {
			update(articleRect.translated(articleTopLeft()));
		}
	});
}

void Widget::requestRelayout(QRect articleRect) {
	crl::on_main(this, [=] {
		if (!_article) {
			return;
		}
		relayoutCurrentContent();
		if (articleRect.isEmpty()) {
			update();
		} else {
			update(articleRect.translated(articleTopLeft()));
		}
	});
}

void Widget::setDocument(const Markdown::MarkdownArticleContent &prepared) {
	_article->setContent(prepared);
}

Markdown::MarkdownArticleTextLeafStyle Widget::inlineFieldStyleForSegment(
		int segmentIndex) const {
	return _article
		? _article->editableStyleForSegment(segmentIndex)
		: Markdown::MarkdownArticleTextLeafStyle();
}

const Widget::CachedInlineFieldStyle &Widget::inlineFieldStyleFor(
		const Markdown::MarkdownArticleTextLeafStyle &leafStyle) {
	return inlineFieldStyleFor(normalizedInlineFieldStyle(leafStyle));
}

const Widget::CachedInlineFieldStyle &Widget::inlineFieldStyleFor(
		const InlineFieldStyleData &data) {
	auto key = inlineFieldStyleKey(data);
	auto textFg = data.textFg;
	auto ownedTextFg = std::shared_ptr<style::owned_color>();
	auto ownedTextMarkBg = std::make_shared<style::owned_color>(
		data.textMarkBg);
	auto textMarkBg = ownedTextMarkBg->color();
	if (_inlineFieldTextColorOverride
		&& data.textFg.get() == _inlineFieldTextColorOverride->color().get()) {
		ownedTextFg = std::make_shared<style::owned_color>(data.textFg->c);
		textFg = ownedTextFg->color();
		key.textFg = textFg;
	}
	for (const auto &cached : _fieldStyles) {
		if (cached.key == key) {
			cached.ownedTextMarkBg->update(data.textMarkBg);
			return cached;
		}
	}
	auto fieldStyle = std::make_shared<style::InputField>(
		st::ivEditorInputField);
	fieldStyle->style = *data.textStyle;
	fieldStyle->style.font = data.italic
		? data.textStyle->font->italic()
		: data.textStyle->font;
	fieldStyle->style.lineHeight = data.lineHeight;
	fieldStyle->textFg = textFg;
	fieldStyle->textMarkBg = textMarkBg;
	fieldStyle->textAlign = data.align;
	fieldStyle->placeholderFont = data.quoteCaptionPlaceholder
		? fieldStyle->style.font->bold(false)
		: fieldStyle->style.font;
	fieldStyle->placeholderAlign = data.align;
	_fieldStyles.push_back({
		.key = key,
		.style = std::move(fieldStyle),
		.ownedTextFg = std::move(ownedTextFg),
		.ownedTextMarkBg = std::move(ownedTextMarkBg),
	});
	return _fieldStyles.back();
}

std::optional<QColor> Widget::activeQuoteCaptionColor() {
	if (!_state->activeLeafUsesQuoteCaptionColor()) {
		return std::nullopt;
	}
	return Markdown::NonPullquoteQuoteCaptionColor(
		textPaintContext(QRect()),
		*_articleStyle);
}

std::optional<QColor> Widget::activeQuotePlaceholderColor() {
	if (!_state->activeLeafUsesQuotePlaceholderColor()) {
		return std::nullopt;
	}
	return Markdown::NonPullquoteQuoteCaptionColor(
		textPaintContext(QRect()),
		*_articleStyle);
}

void Widget::refreshInlineFieldTextColorOverride() {
	const auto color = activeQuoteCaptionColor();
	if (!color) {
		if (_inlineFieldTextColorOverride) {
			_activeFieldStyleKey = std::nullopt;
			_inlineFieldTextColorOverride.reset();
		}
		return;
	}
	if (_inlineFieldTextColorOverride) {
		_inlineFieldTextColorOverride->update(*color);
	} else {
		_inlineFieldTextColorOverride.emplace(*color);
	}
}

Widget::InlineFieldStyleData Widget::normalizedInlineFieldStyle(
		const Markdown::MarkdownArticleTextLeafStyle &leafStyle) const {
	const auto valid = leafStyle.valid();
	const auto textStyle = valid
		? leafStyle.textStyle
		: &_articleStyle->body;
	const auto lineHeight = (valid && leafStyle.lineHeight > 0)
		? leafStyle.lineHeight
		: std::max(textStyle->lineHeight, textStyle->font->height);
	return {
		.textStyle = textStyle,
		.lineHeight = lineHeight,
		.textFg = _inlineFieldTextColorOverride
			? _inlineFieldTextColorOverride->color()
			: (valid ? leafStyle.textColor : _articleStyle->textColor),
		.textMarkBg = valid
			? leafStyle.markBg
			: _articleStyle->textPalette.markBg->c,
		.align = valid ? leafStyle.align : style::al_left,
		.italic = valid ? leafStyle.italic : false,
		.quoteCaptionPlaceholder = _state->activeLeafUsesQuoteCaptionColor(),
	};
}

Widget::InlineFieldStyleKey Widget::inlineFieldStyleKey(
		const InlineFieldStyleData &data) const {
	const auto textStyle = data.textStyle
		? data.textStyle
		: &_articleStyle->body;
	return {
		.font = data.italic
			? textStyle->font->italic()
			: textStyle->font,
		.lineHeight = data.lineHeight,
		.textFg = data.textFg,
		.textMarkBg = data.textMarkBg,
		.align = data.align,
		.quoteCaptionPlaceholder = data.quoteCaptionPlaceholder,
	};
}

void Widget::ensureInlineFieldForSegment(int segmentIndex) {
	_revivedRetainedField = false;
	refreshInlineFieldTextColorOverride();
	auto leafStyle = inlineFieldStyleForSegment(segmentIndex);
	if (!leafStyle.valid()) {
		ensureArticleLayoutForInlineField(widthNoMargins());
		leafStyle = inlineFieldStyleForSegment(segmentIndex);
	}
	const auto data = normalizedInlineFieldStyle(leafStyle);
	const auto key = inlineFieldStyleKey(data);
	const auto mode = _state->activeFieldMode();
	const auto leaf = _state->activeLeafPath();
	const auto fieldLeafMismatch = leaf
		&& _fieldLeaf
		&& (*_fieldLeaf != *leaf);
	if (_activeFieldStyleKey
		&& leaf
		&& _fieldLeaf
		&& (*_fieldLeaf == *leaf)
		&& *_activeFieldStyleKey == key
		&& _fieldMode == mode) {
		return;
	}
	if (leaf) {
		if (auto revived = reviveRetainedLeafField(
				_historyIndex,
				*leaf,
				mode,
				key)) {
			const auto wasHidden = _field->isHidden();
			const auto hadFocus = _field->hasFocus();
			_field = std::move(revived);
			_activeFieldStyleKey = key;
			_fieldMode = mode;
			_fieldLeaf = *leaf;
			refreshInlineFieldPlaceholderColor();
			_fieldUndoAvailable = _field->isUndoAvailable();
			_fieldRedoAvailable = _field->isRedoAvailable();
			_revivedRetainedField = true;
			clearFieldUndoRedoNoopState();
			if (!wasHidden) {
				_field->show();
				_field->raise();
				if (hadFocus) {
					_field->setFocusFast();
				}
			}
			return;
		}
	}
	const auto needsRecreate = !_activeFieldStyleKey
		|| (*_activeFieldStyleKey != key)
		|| (_fieldMode != mode)
		|| fieldLeafMismatch;
	if (!needsRecreate) {
		_activeFieldStyleKey = key;
		_fieldMode = mode;
		return;
	}
	const auto &cached = inlineFieldStyleFor(data);
	_activeFieldStyleKey = cached.key;
	_fieldMode = mode;
	recreateInlineField(*cached.style);
}

void Widget::refreshPalette() {
	_theme = Markdown::CreateStandaloneChatTheme();
	_style->apply(_theme.get());
	_highlightColors = Markdown::HighlightColors(_style.get());
	*_articleStyle = CreateEditorMarkdownStyle();
	if (_article) {
		_article->invalidatePaletteCache();
	}
	_fieldStyles.clear();
	_retainedLeafFields.clear();
	_activeFieldStyleKey = std::nullopt;
	_inlineFieldTextColorOverride.reset();
	_inlineFieldPlaceholderColorOverride.reset();
	if (_field && !_field->isHidden()) {
		refreshInlineFieldTextColorOverride();
		const auto &cached = inlineFieldStyleFor(
			inlineFieldStyleForSegment(_activeSegmentIndex));
		_activeFieldStyleKey = cached.key;
		recreateInlineField(*cached.style);
	}
	relayoutCurrentContent();
	update();
}

void Widget::ensureInlineFieldCreated() {
	if (_field) {
		return;
	}
	const auto &fieldStyle = inlineFieldStyleFor(
		Markdown::MarkdownArticleTextLeafStyle());
	_activeFieldStyleKey = fieldStyle.key;
	_fieldMode = State::FieldMode::Rich;
	_field = base::make_unique_q<Ui::InputField>(
		this,
		*fieldStyle.style,
		Ui::InputField::Mode::MultiLine,
		rpl::single(QString()));
	setupInlineField();
	clearFieldUndoRedoNoopState();
}

void Widget::refreshInlineFieldPlaceholder() {
	_field->setPlaceholder(rpl::single(_state->activePlaceholderText()));
	refreshInlineFieldPlaceholderColor();
}

void Widget::refreshInlineFieldPlaceholderColor() {
	auto color = activeQuotePlaceholderColor().value_or(
		_articleStyle->supplementaryTextColor->c);
	color.setAlphaF(color.alphaF() * 0.5);
	if (_inlineFieldPlaceholderColorOverride) {
		_inlineFieldPlaceholderColorOverride->update(color);
	} else {
		_inlineFieldPlaceholderColorOverride.emplace(color);
	}
	_field->setPlaceholderColorOverride(
		_inlineFieldPlaceholderColorOverride->color());
}

int Widget::inlineFieldMaxVisualLineWidth() const {
	if (_field->isHidden()) {
		return 0;
	}
	return MaxVisualLineWidth(_field->rawTextEdit()->document());
}

void Widget::refreshInlineFieldTextEmptyOverride() {
	if (!_article) {
		return;
	}
	if (!_field || _field->isHidden() || _settingField) {
		_article->clearEditableTextEmptyOverride();
		return;
	}
	const auto source = _state->activePreparedLeafSource();
	if (source) {
		_article->setEditableTextEmptyOverride(
			*source,
			_field->getLastText().isEmpty());
	} else {
		_article->clearEditableTextEmptyOverride();
	}
}

void Widget::refreshInlineFieldMaxLineWidthOverride() {
	if (!_article || _refreshingInlineFieldMaxLineWidthOverride) {
		return;
	}
	_refreshingInlineFieldMaxLineWidthOverride = true;
	const auto guard = gsl::finally([&] {
		_refreshingInlineFieldMaxLineWidthOverride = false;
	});
	for (auto pass = 0; pass != 2; ++pass) {
		refreshInlineFieldTextEmptyOverride();
		const auto livePullquoteWidthRelevant = !_field->isHidden()
			&& (_activeSegmentIndex >= 0)
			&& !_settingField
			&& (inlineFieldStyleForSegment(_activeSegmentIndex).italic
				|| _state->activeLeafUsesQuoteCaptionColor());
		auto source = livePullquoteWidthRelevant
			? _state->activePreparedLeafSource()
			: std::optional<Markdown::PreparedEditLeafSource>();
		if (source) {
			const auto maxWidth = _article
				->pullquoteAvailableTextWidthForEditableLeaf(*source);
			const auto width = (maxWidth > 0)
				? MaxVisualLineWidthForWidth(
					_field->rawTextEdit()->document(),
					maxWidth)
				: inlineFieldMaxVisualLineWidth();
			if (width > 0) {
				_article->setEditableMaxLineWidthOverride(*source, width);
			} else {
				_article->clearEditableMaxLineWidthOverride();
			}
		} else {
			_article->clearEditableMaxLineWidthOverride();
		}
		relayoutCurrentContent();
		if (_field->isHidden()) {
			break;
		}
		syncInlineFieldGeometry();
	}
	if (!_field->isHidden()) {
		updateInlineFieldHeightOverride();
	}
}

void Widget::updateInlineFieldHeightOverride() {
	if (_settingField
		|| _field->isHidden()
		|| _activeOrdinal < 0
		|| !_article) {
		return;
	} else if (_syncingInlineFieldGeometry) {
		_pendingHeightOverrideUpdate = true;
		return;
	} else if (articleRelayoutDeferralActive()) {
		requestDeferredInlineFieldGeometry();
		requestDeferredInlineFieldHeightOverride();
		return;
	}
	if (_article->editableIndexForSegment(_activeSegmentIndex) < 0) {
		clearArticleEditableHeightOverride();
		return;
	}
	const auto segmentRect = fieldOuterRectForSegment(_activeSegmentIndex);
	auto height = segmentRect.isEmpty()
		? _field->height()
		: std::max(_field->geometry().bottom() + 1 - segmentRect.y(), 1);
	if (_activeSegmentIsDisplayMath) {
		const auto blockRect = _article->displayMathBlockRect(
			_activeSegmentIndex).translated(articleTopLeft());
		if (!blockRect.isEmpty()) {
			height = std::max(
				_field->geometry().bottom() + 1 - blockRect.y(),
				1);
		}
		height = std::max(height, _activeDisplayMathBaselineHeight);
	}
	_article->setEditableHeightOverrideForSegment(_activeSegmentIndex, height);
	relayoutCurrentContent();
	update();
}

void Widget::clearDisplayMathEditSession() {
	_activeSegmentIsDisplayMath = false;
	_activeDisplayMathBaselineHeight = 0;
}

void Widget::clearInlineFieldEditSession(
		bool keepRetainedFieldOnCurrentHistoryEntry) {
	clearDisplayMathEditSession();
	if (_article) {
		clearArticleEditableHeightOverride();
		_article->clearEditableMaxLineWidthOverride();
	}
	if (!_field->isHidden()
		|| !_fieldLeaf) {
		return;
	}
	const auto activeLeaf = _state->activeLeafPath();
	if (!activeLeaf || (*activeLeaf != *_fieldLeaf)) {
		const auto &fieldStyle = inlineFieldStyleFor(
			Markdown::MarkdownArticleTextLeafStyle());
		_activeFieldStyleKey = fieldStyle.key;
		_fieldMode = State::FieldMode::Rich;
		recreateInlineField(*fieldStyle.style);
		return;
	}
	retainActiveLeafField(keepRetainedFieldOnCurrentHistoryEntry);
	const auto &fieldStyle = inlineFieldStyleFor(
		Markdown::MarkdownArticleTextLeafStyle());
	_activeFieldStyleKey = fieldStyle.key;
	_fieldMode = State::FieldMode::Rich;
	recreateInlineField(*fieldStyle.style);
}

void Widget::ensureArticleLayoutForInlineField(int width) {
	if (!_article || width <= 0) {
		return;
	} else if (articleRelayoutDeferralActive()) {
		requestDeferredArticleRelayout();
		requestDeferredInlineFieldGeometry();
		return;
	}
	_articleHeight = _article->resizeGetHeight(articleWidth(width));
}

void Widget::syncArticleVisibleTopBottom() {
	if (!_article) {
		return;
	}
	const auto articleTop = articleTopLeft().y();
	_article->setVisibleTopBottom(
		_visibleRange.top - articleTop,
		_visibleRange.bottom - articleTop);
}

void Widget::syncInlineFieldGeometry(int width) {
	if (_field->isHidden() || width <= 0) {
		return;
	} else if (articleRelayoutDeferralActive()) {
		requestDeferredInlineFieldGeometry();
		return;
	}
	ensureArticleLayoutForInlineField(width);
	if (_activeSegmentIndex >= 0) {
		ensureInlineFieldForSegment(_activeSegmentIndex);
	}
	const auto segmentRect = fieldOuterRectForSegment(_activeSegmentIndex);
	if (segmentRect.isEmpty()) {
		_pendingOrdinal = _activeOrdinal;
		_pendingCursorOffset = _field->textCursor().position();
		hideInlineField();
		clearArticleEditableHeightOverride();
		_article->clearEditableMaxLineWidthOverride();
		const auto pendingOrdinal = _pendingOrdinal;
		ensureArticleLayoutForInlineField(width);
		if (_pendingOrdinal == pendingOrdinal && pendingOrdinal >= 0) {
			const auto segmentIndex = segmentIndexForEditableOrdinal(
				pendingOrdinal);
			if (segmentIndex >= 0
				&& !_article->logicalSegmentRect(segmentIndex).isEmpty()) {
				ensurePendingActivation();
			}
		}
		return;
	}
	const auto margins = _field->fullTextMargins();
	const auto left = segmentRect.x() - margins.left();
	const auto top = segmentRect.y() - margins.top();
	const auto fieldWidth = std::max(
		segmentRect.width() + margins.left() + margins.right(),
		1);
	_syncingInlineFieldGeometry = true;
	_field->resizeToWidth(fieldWidth);
	const auto fieldHeight = FieldNaturalHeight(_field.get());
	_field->setGeometryToLeft(left, top, fieldWidth, fieldHeight, width);
	_field->raise();
	_syncingInlineFieldGeometry = false;
	if (_pendingHeightOverrideUpdate) {
		_pendingHeightOverrideUpdate = false;
		updateInlineFieldHeightOverride();
	}
	refreshInlineFieldMaxLineWidthOverride();
}

int Widget::editableOrdinalForSegment(int segmentIndex) const {
	if (const auto source = _article->editableLeafForSegment(segmentIndex)) {
		const auto ordinal = _state->textOrdinalForLeaf(*source);
		if (ordinal >= 0) {
			return ordinal;
		}
	}
	return _article->editableIndexForSegment(segmentIndex);
}

int Widget::segmentIndexForEditableOrdinal(int ordinal) const {
	if (const auto source = _state->preparedLeafSourceForOrdinal(ordinal)) {
		return _article->segmentIndexForEditableLeaf(*source);
	}
	return _article->segmentIndexForEditableIndex(ordinal);
}

style::margins Widget::effectiveBodyPadding() const {
	const auto base = EditorBodyPadding();
	return style::margins(
		base.left(),
		base.top() + _topContentPadding + _searchSlideHeight.current(),
		base.right(),
		base.bottom() + _bottomContentPadding);
}

QPoint Widget::articleTopLeft() const {
	const auto padding = effectiveBodyPadding();
	const auto outerWidth = std::max(widthNoMargins(), 1);
	const auto available = std::max(
		outerWidth - padding.left() - padding.right(),
		1);
	const auto bodyWidth = articleWidth(outerWidth);
	return {
		padding.left() + std::max((available - bodyWidth) / 2, 0),
		padding.top()
	};
}

int Widget::articleWidth(int outerWidth) const {
	const auto padding = EditorBodyPadding();
	const auto available = std::max(
		outerWidth - padding.left() - padding.right(),
		1);
	const auto maxWidth = (_contentMaxWidth > 0)
		? _contentMaxWidth
		: (_article ? _article->maxWidth() : available);
	return std::min(available, maxWidth);
}

Widget::ArticleColumn Widget::articleColumnForWidth(int outerWidth) const {
	const auto padding = EditorBodyPadding();
	const auto available = std::max(
		outerWidth - padding.left() - padding.right(),
		1);
	const auto width = articleWidth(outerWidth);
	const auto left = padding.left() + std::max((available - width) / 2, 0);
	return { left, width };
}

QRect Widget::outerEditableSegmentRect(int segmentIndex) const {
	const auto rect = _article->logicalSegmentRect(segmentIndex);
	return rect.isEmpty() ? rect : rect.translated(articleTopLeft());
}

QRect Widget::fieldOuterRectForSegment(int segmentIndex) const {
	if (!_article || segmentIndex < 0) {
		return QRect();
	}
	if (!_activeSegmentIsDisplayMath) {
		return outerEditableSegmentRect(segmentIndex);
	}
	const auto rect = _article->displayMathEditRect(segmentIndex);
	return rect.isEmpty() ? rect : rect.translated(articleTopLeft());
}

Markdown::MarkdownArticlePaintContext Widget::textPaintContext(QRect clip) {
	const auto logicalRect = QRect(QPoint(), QSize(
		articleWidth(std::max(widthNoMargins(), 1)),
		std::max(_articleHeight, 1)));
	auto context = Markdown::MarkdownArticlePaintContext(
		_theme->preparePaintContext(
			_style.get(),
			logicalRect,
			logicalRect,
			clip,
			window() ? !window()->isActiveWindow() : false));
	const auto messageStyle = context.messageStyle();
	context.caches = {
		.pre = messageStyle->preCache.get(),
		.blockquote = context.quoteCache({}, 0),
		.colors = _highlightColors,
		.st = &messageStyle->richPageStyle,
		.repaint = [=] {
			crl::on_main(this, [=] {
				update();
			});
		},
		.repaintRect = [=](QRect rect) {
			crl::on_main(this, [=] {
				if (rect.isEmpty()) {
					update();
				} else {
					update(rect.translated(articleTopLeft()));
				}
			});
		},
	};
	const auto hiddenSegmentIndex = _field->isHidden()
		? -1
		: _activeSegmentIndex;
	context.hiddenTextSegmentIndex = hiddenSegmentIndex;
	context.hiddenSegmentIndex = hiddenSegmentIndex;
	context.selectionState.selection = _selection;
	context.selectionState.endpoints = &_selectionEndpoints;
	if (!_structuralSelection.empty()) {
		context.selectionState.structuralSelection = &_structuralSelection;
	}
	return context;
}

} // namespace Iv::Editor
