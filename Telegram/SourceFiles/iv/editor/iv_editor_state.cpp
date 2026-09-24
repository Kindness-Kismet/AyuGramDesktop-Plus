/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_state.h"
#include "iv/editor/iv_editor_page_media.h"
#include "iv/editor/iv_editor_page_table_grid.h"
#include "iv/editor/iv_editor_text_entities.h"
#include "iv/markdown/iv_markdown_prepare_serialize.h"
#include "lang/lang_keys.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"

#include <algorithm>
#include <utility>
#include "iv/editor/iv_editor_state_internal.h"

namespace Iv::Editor {
using namespace StateDetails;

State::State()
: State(std::make_shared<RichPage>(), nullptr, RichMessageLimits()) {
}

State::State(
	std::shared_ptr<RichPage> richPage,
	std::shared_ptr<Markdown::MediaRuntime> mediaRuntime,
	RichMessageLimits limits)
: _richPage(richPage ? std::move(richPage) : std::make_shared<RichPage>())
, _mediaRuntime(std::move(mediaRuntime))
, _limits(std::move(limits)) {
	if (_richPage->blocks.empty()) {
		_richPage->blocks.push_back(MakeParagraphBlock());
	}
	StripEditModeWrapperEntities(_richPage->blocks);
	DegradeEditModeButtons(_richPage->blocks);
	rebuild();
}

const RichPage &State::richPage() const {
	return *_richPage;
}

bool State::articleEmpty() const {
	return ranges::all_of(_richPage->blocks, [](const auto &block) {
		return BlockIsEmpty(block);
	});
}

const Markdown::MarkdownArticleContent &State::prepared() const {
	return _prepared;
}

auto State::tableRenderLimits() const
-> Markdown::MarkdownPrepareTableRenderLimits {
	return Markdown::PrepareTableRenderLimitsForRichMessage(_limits);
}

const RichMessageLimits &State::limits() const {
	return _limits;
}

void State::commitCheckedMutation(State state) {
	_richPage = std::move(state._richPage);
	_prepared = std::move(state._prepared);
	_textNodes = std::move(state._textNodes);
	_activeTextOrdinal = state._activeTextOrdinal;
	_lastPreparedMutationKind = (
		state._lastPreparedMutationKind == PreparedMutationKind::FullRebuild)
		? state._lastPreparedMutationKind
		: PreparedMutationKind::FullRebuild;
	_lastLimitError = std::nullopt;
	_temporaryDownParagraph = std::move(state._temporaryDownParagraph);
}

const std::vector<TextNodeDescriptor> &State::textNodes() const {
	return _textNodes;
}

State::Snapshot State::snapshot() const {
	return {
		.richPage = *_richPage,
		.activeLeaf = activeLeafPath(),
		.temporaryDownParagraph = _temporaryDownParagraph,
	};
}

void State::restoreSnapshot(Snapshot snapshot) {
	_richPage = std::make_shared<RichPage>(std::move(snapshot.richPage));
	_activeTextOrdinal = -1;
	_lastLimitError = std::nullopt;
	_temporaryDownParagraph = std::move(snapshot.temporaryDownParagraph);
	rebuild();
	if (snapshot.activeLeaf && (textNodeOrdinal(*snapshot.activeLeaf) >= 0)) {
		const auto activated = activateRebuiltLeaf(*snapshot.activeLeaf);
		Assert(activated);
	} else {
		ensureActiveTextOrdinal();
	}
}

std::optional<LeafPath> State::activeLeafPath() const {
	if (const auto descriptor = textNode(_activeTextOrdinal)) {
		return descriptor->leaf;
	}
	return std::nullopt;
}

int State::textOrdinalForLeafPath(const LeafPath &path) const {
	return textNodeOrdinal(path);
}

void State::clearTemporaryDownParagraph() {
	_temporaryDownParagraph = std::nullopt;
}

void State::clearTemporaryDownParagraphIfInvalid() {
	if (!_temporaryDownParagraph
		|| _temporaryDownParagraph->kind != LeafKind::BlockText
		|| (textNodeOrdinal(*_temporaryDownParagraph) < 0)) {
		clearTemporaryDownParagraph();
		return;
	}
	const auto owner = block(_temporaryDownParagraph->block);
	if (!owner
		|| owner->kind != BlockKind::Paragraph
		|| !BlockIsEmpty(*owner)) {
		clearTemporaryDownParagraph();
	}
}

int State::textOrdinalForLeaf(
		const Markdown::PreparedEditLeafSource &source) const {
	const auto leaf = convertLeafPath(source);
	return leaf ? textOrdinalForLeafPath(*leaf) : -1;
}

std::optional<PreparedEditLeafSource> State::preparedLeafSourceForOrdinal(
		int ordinal) const {
	const auto descriptor = textNode(ordinal);
	return descriptor ? convertPreparedLeafSource(*descriptor) : std::nullopt;
}

PreparedMutationKind State::lastPreparedMutationKind() const {
	return _lastPreparedMutationKind;
}

std::optional<PreparedEditLeafSource> State::activePreparedLeafSource() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	return descriptor ? convertPreparedLeafSource(*descriptor) : std::nullopt;
}

std::vector<TextNodeSpan> State::resolveTextSpansForPreparedLeafRange(
		const PreparedEditLeafSource &source,
		int from,
		int till) const {
	if (from < 0 || till <= from) {
		return {};
	}
	const auto firstLeaf = convertLeafPath(source);
	if (!firstLeaf) {
		return {};
	}
	const auto firstOrdinal = textOrdinalForLeafPath(*firstLeaf);
	if (firstOrdinal < 0) {
		return {};
	}
	auto result = std::vector<TextNodeSpan>();
	auto consumed = 0;
	for (auto i = firstOrdinal, count = textNodeCount()
		; i != count && consumed < till
		; ++i) {
		const auto current = richText(_textNodes[i].leaf);
		if (!current) {
			return {};
		}
		const auto length = int(current->text.text.size());
		const auto spanFrom = std::max(from - consumed, 0);
		const auto spanTo = std::min(till - consumed, length);
		if (spanFrom < spanTo) {
			result.push_back(TextNodeSpan{
				.leaf = _textNodes[i].leaf,
				.from = spanFrom,
				.till = spanTo,
			});
		}
		consumed += length;
	}
	return (consumed >= till) ? result : std::vector<TextNodeSpan>();
}

int State::textNodeCount() const {
	return int(_textNodes.size());
}

int State::activeTextOrdinal() const {
	return _activeTextOrdinal;
}

bool State::setActiveTextByOrdinal(int ordinal) {
	if (ordinal < 0 || ordinal >= textNodeCount()) {
		return false;
	}
	if (_temporaryDownParagraph
		&& !(_textNodes[ordinal].leaf == *_temporaryDownParagraph)) {
		clearTemporaryDownParagraph();
	}
	_activeTextOrdinal = ordinal;
	return true;
}

TextWithEntities State::activeText() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return TextWithEntities();
	}
	if (const auto current = richText(descriptor->leaf)) {
		return StripEditModeWrapperEntities(current->text);
	}
	if (const auto current = rawText(descriptor->leaf)) {
		return MakeText(*current);
	}
	return TextWithEntities();
}

ApplyResult State::applyActiveText(TextWithEntities text) {
	_lastLimitError = std::nullopt;
	_lastPreparedMutationKind = PreparedMutationKind::None;
	DegradeEditModeInlineButtons(text);
	if (const auto descriptor = textNode(_activeTextOrdinal)) {
		if (descriptor->leaf.kind == LeafKind::TableCellText) {
			DegradeBlockOnlyEntities(text);
		}
	}
	return applyActiveTextWithLocalLimit(std::move(text));
}

ApplyResult State::applyActiveTextUnchecked(TextWithEntities text) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return ApplyResult::Failed;
	}
	if (auto current = richText(descriptor->leaf)) {
		if (current->text == text) {
			return ApplyResult::Unchanged;
		}
		current->text = std::move(text);
		if (leafMutationKeepsTextNodes(*descriptor)) {
			if (_temporaryDownParagraph
				&& (descriptor->leaf == *_temporaryDownParagraph)
				&& !RichTextIsEmpty(*current)) {
				clearTemporaryDownParagraph();
			}
			if (updatePreparedActiveLeaf(*descriptor)) {
				_lastPreparedMutationKind = PreparedMutationKind::LeafOnly;
			} else {
				rebuildPrepared();
			}
		} else {
			rebuild();
		}
		return ApplyResult::Changed;
	}
	if (auto current = rawText(descriptor->leaf)) {
		if (*current == text.text) {
			return ApplyResult::Unchanged;
		}
		*current = std::move(text.text);
		if (leafMutationKeepsTextNodes(*descriptor)) {
			if (updatePreparedActiveLeaf(*descriptor)) {
				_lastPreparedMutationKind = PreparedMutationKind::LeafOnly;
			} else {
				rebuildPrepared();
			}
		} else {
			rebuild();
		}
		return ApplyResult::Changed;
	}
	return ApplyResult::Failed;
}

ApplyResult State::applyActiveTextWithLocalLimit(TextWithEntities text) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return ApplyResult::Failed;
	}
	auto chunks = SplitFieldText(std::move(text));
	if (chunks.size() <= 1) {
		return applyActiveTextUnchecked(chunks.empty()
			? TextWithEntities()
			: std::move(chunks.front()));
	}
	auto first = chunks.front();
	if (const auto result = applySplitParagraphText(
			*descriptor,
			std::move(chunks)); result != ApplyResult::Failed) {
		return result;
	}
	return applyActiveTextUnchecked(std::move(first));
}

FieldMode State::activeFieldMode() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	return descriptor ? descriptor->mode : FieldMode::Rich;
}

QString State::activeRawText() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return QString();
	}
	if (const auto current = rawText(descriptor->leaf)) {
		return *current;
	}
	if (const auto current = richText(descriptor->leaf)) {
		return StripEditModeWrapperEntities(current->text).text;
	}
	return QString();
}

QString State::activePlaceholderText() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return QString();
	}
	const auto owner = block(descriptor->leaf.block);
	if (!owner) {
		return QString();
	}
	switch (descriptor->leaf.kind) {
	case LeafKind::BlockText:
		switch (owner->kind) {
		case BlockKind::Quote:
			return tr::lng_article_placeholder_quote(tr::now);
		case BlockKind::Heading:
			return Markdown::HeadingLevelLabel(owner->headingLevel);
		case BlockKind::Footer:
			return tr::lng_article_insert_footer(tr::now);
		case BlockKind::Details:
			return tr::lng_article_table_header(tr::now);
		default:
			return QString();
		}
	case LeafKind::BlockCaption:
		switch (owner->kind) {
		case BlockKind::Quote:
			return tr::lng_article_placeholder_author(tr::now);
		case BlockKind::Photo:
		case BlockKind::Video:
		case BlockKind::Audio:
		case BlockKind::File:
		case BlockKind::Map:
		case BlockKind::GroupedMedia:
			return tr::lng_photo_caption(tr::now);
		default:
			return QString();
		}
	case LeafKind::TableCellText: {
		const auto cell = tableCell(
			descriptor->leaf.block,
			descriptor->leaf.tableRowIndex,
			descriptor->leaf.tableCellIndex);
		return (cell && cell->header)
			? tr::lng_article_table_header(tr::now)
			: tr::lng_article_placeholder_cell(tr::now);
	}
	case LeafKind::MathFormula:
		return u"x^2 + y^2"_q;
	}
	return QString();
}

ApplyResult State::applyActiveRawText(QString text) {
	_lastLimitError = std::nullopt;
	_lastPreparedMutationKind = PreparedMutationKind::None;
	return applyActiveRawTextWithLocalLimit(std::move(text));
}

ApplyResult State::applyActiveRawTextUnchecked(QString text) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return ApplyResult::Failed;
	}
	if (auto current = rawText(descriptor->leaf)) {
		if (*current == text) {
			return ApplyResult::Unchanged;
		}
		*current = std::move(text);
		if (leafMutationKeepsTextNodes(*descriptor)) {
			if (updatePreparedActiveLeaf(*descriptor)) {
				_lastPreparedMutationKind = PreparedMutationKind::LeafOnly;
			} else {
				rebuildPrepared();
			}
		} else {
			rebuild();
		}
		return ApplyResult::Changed;
	}
	if (auto current = richText(descriptor->leaf)) {
		auto updated = MakeText(std::move(text));
		if (current->text == updated) {
			return ApplyResult::Unchanged;
		}
		current->text = std::move(updated);
		if (leafMutationKeepsTextNodes(*descriptor)) {
			if (_temporaryDownParagraph
				&& (descriptor->leaf == *_temporaryDownParagraph)
				&& !RichTextIsEmpty(*current)) {
				clearTemporaryDownParagraph();
			}
			if (updatePreparedActiveLeaf(*descriptor)) {
				_lastPreparedMutationKind = PreparedMutationKind::LeafOnly;
			} else {
				rebuildPrepared();
			}
		} else {
			rebuild();
		}
		return ApplyResult::Changed;
	}
	return ApplyResult::Failed;
}

ApplyResult State::applyActiveRawTextWithLocalLimit(QString text) {
	auto chunks = SplitFieldText(MakeText(std::move(text)));
	return applyActiveRawTextUnchecked(chunks.empty()
		? QString()
		: std::move(chunks.front().text));
}

} // namespace Iv::Editor
