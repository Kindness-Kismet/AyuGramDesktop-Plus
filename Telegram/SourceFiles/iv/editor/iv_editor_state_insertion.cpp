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

State::InsertionAnchor State::resolveActiveInsertionTarget() const {
	auto result = InsertionAnchor{
		.container = BlockContainerPath(),
		.blockIndex = int(_richPage->blocks.size()) - 1,
	};
	if (const auto descriptor = textNode(_activeTextOrdinal)) {
		result = descriptor->insertionAnchor;
	}
	return blockContainer(result.container)
		? result
		: InsertionAnchor{
			.container = BlockContainerPath(),
			.blockIndex = int(_richPage->blocks.size()) - 1,
		};
}

std::optional<int> State::normalizeTextOnlyListItemForInsertion(
		const BlockContainerPath &container) {
	if (container.steps.empty()) {
		return std::nullopt;
	}
	const auto &step = container.steps.back();
	if (step.kind != BlockContainerKind::ListItemChildren) {
		return std::nullopt;
	}
	auto parent = container;
	parent.steps.pop_back();
	auto itemPath = BlockPath{
		.container = parent,
		.index = step.blockIndex,
	};
	auto item = listItem(itemPath, step.listItemIndex);
	if (!item || (item->anchorId.isEmpty() && RichTextIsEmpty(item->text))) {
		return std::nullopt;
	}
	auto paragraph = MakeParagraphBlock();
	paragraph.anchorId = std::move(item->anchorId);
	paragraph.text = std::move(item->text);
	item->anchorId.clear();
	item->text = RichText();
	if (!BlockIsEmpty(paragraph)) {
		clearTemporaryDownParagraph();
		item->blocks.insert(item->blocks.begin(), std::move(paragraph));
		return 0;
	}
	return -1;
}

std::optional<int> State::normalizeTextOnlyQuoteSurface(
		const BlockContainerPath &container,
		bool keepEmptyParagraph) {
	if (container.steps.empty()) {
		return std::nullopt;
	}
	const auto &step = container.steps.back();
	if (step.kind != BlockContainerKind::BlockChildren) {
		return std::nullopt;
	}
	auto parent = container;
	parent.steps.pop_back();
	auto owner = block({
		.container = parent,
		.index = step.blockIndex,
	});
	if (!owner
		|| owner->kind != BlockKind::Quote
		|| !owner->blocks.empty()) {
		return std::nullopt;
	}
	auto paragraph = MakeParagraphBlock();
	paragraph.text = std::move(owner->text);
	owner->text = RichText();
	clearTemporaryDownParagraph();
	if (keepEmptyParagraph || !BlockIsEmpty(paragraph)) {
		owner->blocks.insert(owner->blocks.begin(), std::move(paragraph));
		return 0;
	}
	return -1;
}

std::optional<int> State::normalizeTextOnlyQuoteForInsertion(
		const BlockContainerPath &container) {
	return normalizeTextOnlyQuoteSurface(container, false);
}

bool State::normalizeTextOnlyContainerForInsertion(
		const BlockContainerPath &container,
		int *insertAt) {
	if (!insertAt || *insertAt < 0) {
		return false;
	}
	if (const auto normalized = normalizeTextOnlyListItemForInsertion(
			container); normalized && (*normalized >= 0)) {
		++*insertAt;
	}
	if (const auto normalized = normalizeTextOnlyQuoteForInsertion(
			container); normalized && (*normalized >= 0)) {
		++*insertAt;
	}
	const auto blocks = blockContainer(container);
	return blocks && *insertAt <= int(blocks->size());
}

bool State::shouldReplaceActiveTextOnlyBlock(
		const TextNodeDescriptor &descriptor,
		const std::vector<Block> &blocks) const {
	if (descriptor.leaf.kind != LeafKind::BlockText
		|| descriptor.removalTarget.kind != RemovalKind::Block) {
		return false;
	}
	const auto owner = block(descriptor.removalTarget.block);
	if (!owner || !BlockIsEmpty(*owner)) {
		return false;
	}
	if (owner->kind == BlockKind::Paragraph) {
		return true;
	}
	return owner->kind == BlockKind::Heading
		&& blocks.size() == 1
		&& blocks.front().kind == BlockKind::Heading;
}

std::optional<int> State::activateRebuiltLeaf(const LeafPath &path) {
	const auto ordinal = textNodeOrdinal(path);
	if (setActiveTextByOrdinal(ordinal)) {
		return _activeTextOrdinal;
	}
	ensureActiveTextOrdinal();
	return (_activeTextOrdinal >= 0)
		? std::make_optional(_activeTextOrdinal)
		: std::nullopt;
}

std::optional<State::ParagraphTarget> State::reuseOrInsertParagraph(
		const BlockContainerPath &containerPath,
		int index) {
	const auto blocks = blockContainer(containerPath);
	if (!blocks) {
		return std::nullopt;
	}
	const auto insertAt = std::clamp(index, 0, int(blocks->size()));
	if (insertAt < int(blocks->size())
		&& (*blocks)[insertAt].kind == BlockKind::Paragraph) {
		return ParagraphTarget{
			.leaf = {
				.kind = LeafKind::BlockText,
				.block = {
					.container = containerPath,
					.index = insertAt,
				},
			},
		};
	}
	clearTemporaryDownParagraph();
	blocks->insert(blocks->begin() + insertAt, MakeParagraphBlock());
	return ParagraphTarget{
		.leaf = {
			.kind = LeafKind::BlockText,
			.block = {
				.container = containerPath,
				.index = insertAt,
			},
		},
		.inserted = true,
	};
}

auto State::resolveActiveTextInsertTarget()
-> std::optional<State::ActiveTextInsertTarget> {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	if (descriptor->leaf.kind == LeafKind::ListItemText) {
		const auto surface = normalizeActiveListItemSurface();
		if (!surface) {
			return std::nullopt;
		}
		const auto container = ListItemChildrenContainer(
			surface->path,
			surface->itemIndex);
		return ActiveTextInsertTarget{
			.leaf = {
				.kind = LeafKind::BlockText,
				.block = {
					.container = container,
					.index = 0,
				},
			},
			.anchor = {
				.container = container,
				.blockIndex = 0,
			},
		};
	}
	if (descriptor->leaf.kind == LeafKind::BlockText) {
		if (const auto owner = block(descriptor->leaf.block);
			owner
			&& owner->kind == BlockKind::Quote
			&& !owner->pullquote
			&& owner->blocks.empty()) {
			const auto container = BlockChildrenContainer(
				descriptor->leaf.block);
			if (!normalizeTextOnlyQuoteSurface(container, true)) {
				return std::nullopt;
			}
			return ActiveTextInsertTarget{
				.leaf = {
					.kind = LeafKind::BlockText,
					.block = {
						.container = container,
						.index = 0,
					},
				},
				.anchor = {
					.container = container,
					.blockIndex = 0,
				},
			};
		}
	}
	return ActiveTextInsertTarget{
		.leaf = descriptor->leaf,
		.anchor = descriptor->insertionAnchor,
	};
}

auto State::activeQuote(bool pullquote) const
-> std::optional<State::ActiveQuote> {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	const auto direct = block(descriptor->leaf.block);
	if (direct && direct->kind == BlockKind::Quote) {
		return (direct->pullquote == pullquote)
			? std::make_optional(ActiveQuote{
				.path = descriptor->leaf.block,
			})
			: std::nullopt;
	}
	auto container = descriptor->leaf.block.container;
	while (!container.steps.empty()) {
		const auto step = container.steps.back();
		container.steps.pop_back();
		if (step.kind != BlockContainerKind::BlockChildren) {
			continue;
		}
		const auto path = BlockPath{
			.container = container,
			.index = step.blockIndex,
		};
		const auto owner = block(path);
		if (!owner || owner->kind != BlockKind::Quote) {
			continue;
		}
		if (owner->pullquote != pullquote) {
			return std::nullopt;
		}
		const auto body = BlockChildrenContainer(path);
		auto lastBodyLeaf = false;
		for (auto i = textNodeCount(); i != 0; --i) {
			const auto &candidate = _textNodes[i - 1].leaf;
			if (ContainerHasPrefix(candidate.block.container, body)) {
				lastBodyLeaf = (candidate == descriptor->leaf);
				break;
			}
		}
		return ActiveQuote{
			.path = path,
			.activeLeafIsLastEditableBodyLeaf = lastBodyLeaf,
		};
	}
	return std::nullopt;
}

std::optional<LeafPath> State::leafAfterUnwrappingBlockChildren(
		const LeafPath &leaf,
		const BlockPath &wrapper) const {
	const auto body = BlockChildrenContainer(wrapper);
	if (!ContainerHasPrefix(leaf.block.container, body)) {
		return std::nullopt;
	}
	auto result = leaf;
	if (leaf.block.container == body) {
		result.block.container = wrapper.container;
		result.block.index += wrapper.index;
		return result;
	}
	auto suffix = leaf.block.container.steps;
	suffix.erase(suffix.begin(), suffix.begin() + body.steps.size());
	if (suffix.empty()) {
		return std::nullopt;
	}
	suffix.front().blockIndex += wrapper.index;
	result.block.container = wrapper.container;
	result.block.container.steps.insert(
		result.block.container.steps.end(),
		suffix.begin(),
		suffix.end());
	return result;
}

bool State::unwrapActiveCodeBlockUnchecked(
		const ActiveTextInsertContext &context,
		ActiveTextSelectionTarget *target) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || descriptor->leaf.kind != LeafKind::BlockText) {
		return false;
	}
	const auto owner = block(descriptor->leaf.block);
	if (!owner || owner->kind != BlockKind::Code) {
		return false;
	}
	auto *container = blockContainer(descriptor->leaf.block.container);
	const auto index = descriptor->leaf.block.index;
	if (!container || index < 0 || index >= int(container->size())) {
		return false;
	}
	auto paragraph = MakeParagraphBlock();
	paragraph.anchorId = owner->anchorId;
	paragraph.text = owner->text;
	paragraph.text.text = JoinText(
		context.before,
		context.selected,
		context.after);
	(*container)[index] = std::move(paragraph);
	clearTemporaryDownParagraph();
	rebuild();
	if (!activateRebuiltLeaf(descriptor->leaf)) {
		return false;
	}
	if (target) {
		const auto selectionFrom = int(context.before.text.size());
		*target = {
			.leaf = descriptor->leaf,
			.selectionFrom = selectionFrom,
			.selectionTo = selectionFrom + int(context.selected.text.size()),
		};
	}
	return true;
}

bool State::unwrapActiveQuoteUnchecked(
		bool pullquote,
		const ActiveTextInsertContext &context,
		ActiveTextSelectionTarget *target) {
	const auto descriptor = textNode(_activeTextOrdinal);
	const auto quote = activeQuote(pullquote);
	if (!descriptor || !quote) {
		return false;
	}
	switch (descriptor->leaf.kind) {
	case LeafKind::BlockCaption:
	case LeafKind::TableCellText:
	case LeafKind::MathFormula:
		return false;
	case LeafKind::BlockText:
	case LeafKind::ListItemText:
		break;
	}
	auto *owner = block(quote->path);
	if (!owner
		|| owner->kind != BlockKind::Quote
		|| owner->pullquote != pullquote) {
		return false;
	}
	auto *activeText = richText(descriptor->leaf);
	if (!activeText) {
		return false;
	}
	const auto selectionFrom = int(context.before.text.size());
	const auto selectionTo = selectionFrom + int(context.selected.text.size());
	if (owner->blocks.empty()) {
		if (descriptor->leaf.kind != LeafKind::BlockText
			|| descriptor->leaf.block != quote->path
			|| !RichTextIsEmpty(owner->caption)) {
			return false;
		}
		auto *container = blockContainer(quote->path.container);
		const auto index = quote->path.index;
		if (!container || index < 0 || index >= int(container->size())) {
			return false;
		}
		auto paragraph = MakeParagraphBlock();
		paragraph.anchorId = owner->anchorId;
		paragraph.text = owner->text;
		paragraph.text.text = JoinText(
			context.before,
			context.selected,
			context.after);
		(*container)[index] = std::move(paragraph);
		clearTemporaryDownParagraph();
		rebuild();
		if (!activateRebuiltLeaf(descriptor->leaf)) {
			return false;
		}
		if (target) {
			*target = {
				.leaf = descriptor->leaf,
				.selectionFrom = selectionFrom,
				.selectionTo = selectionTo,
			};
		}
		return true;
	}
	if (!owner->anchorId.isEmpty()
		|| !RichTextIsEmpty(owner->text)
		|| !RichTextIsEmpty(owner->caption)) {
		return false;
	}
	const auto destinationLeaf = leafAfterUnwrappingBlockChildren(
		descriptor->leaf,
		quote->path);
	if (!destinationLeaf) {
		return false;
	}
	auto *container = blockContainer(quote->path.container);
	const auto index = quote->path.index;
	if (!container || index < 0 || index >= int(container->size())) {
		return false;
	}
	activeText->text = JoinText(
		context.before,
		context.selected,
		context.after);
	auto blocks = std::move(owner->blocks);
	container->erase(container->begin() + index);
	container->insert(
		container->begin() + index,
		std::make_move_iterator(blocks.begin()),
		std::make_move_iterator(blocks.end()));
	clearTemporaryDownParagraph();
	rebuild();
	if (!activateRebuiltLeaf(*destinationLeaf)) {
		return false;
	}
	if (target) {
		*target = {
			.leaf = *destinationLeaf,
			.selectionFrom = selectionFrom,
			.selectionTo = selectionTo,
		};
	}
	return true;
}

bool State::convertActiveHeadingOrFooterUnchecked(
		InsertAction action,
		const ActiveTextInsertContext &context,
		ActiveTextSelectionTarget *target) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || descriptor->leaf.kind != LeafKind::BlockText) {
		return false;
	}
	auto *owner = block(descriptor->leaf.block);
	if (!owner) {
		return false;
	}
	const auto heading = (action.type == InsertBlockType::Heading);
	const auto required = heading ? BlockKind::Heading : BlockKind::Footer;
	if (owner->kind != required) {
		return false;
	}
	const auto level = std::clamp(action.headingLevel, 1, 6);
	if (heading && std::clamp(owner->headingLevel, 1, 6) != level) {
		owner->headingLevel = level;
	} else {
		owner->kind = BlockKind::Paragraph;
		owner->headingLevel = 0;
	}
	owner->text.text = JoinText(
		context.before,
		context.selected,
		context.after);
	clearTemporaryDownParagraph();
	rebuild();
	if (!activateRebuiltLeaf(descriptor->leaf)) {
		return false;
	}
	if (target) {
		const auto selectionFrom = int(context.before.text.size());
		*target = {
			.leaf = descriptor->leaf,
			.selectionFrom = selectionFrom,
			.selectionTo = selectionFrom + int(context.selected.text.size()),
		};
	}
	return true;
}

auto State::activeListItemSurface() const
-> std::optional<State::ActiveListItemSurface> {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	auto path = std::optional<BlockPath>();
	if (descriptor->leaf.kind == LeafKind::ListItemText) {
		path = descriptor->leaf.block;
	} else if (descriptor->leaf.kind == LeafKind::BlockText) {
		const auto owner = block(descriptor->leaf.block);
		if (!owner || owner->kind != BlockKind::Paragraph) {
			return std::nullopt;
		}
		const auto &container = descriptor->leaf.block.container;
		if (container.steps.empty()) {
			return std::nullopt;
		}
		const auto &step = container.steps.back();
		if (step.kind != BlockContainerKind::ListItemChildren) {
			return std::nullopt;
		}
		auto parent = container;
		parent.steps.pop_back();
		path = BlockPath{
			.container = parent,
			.index = step.blockIndex,
		};
	} else {
		return std::nullopt;
	}
	const auto owner = block(*path);
	if (!owner || owner->kind != BlockKind::List) {
		return std::nullopt;
	}
	const auto itemIndex = ListItemIndexForLeaf(descriptor->leaf, *path);
	if (!itemIndex) {
		return std::nullopt;
	}
	if (descriptor->leaf.kind == LeafKind::BlockText
		&& descriptor->leaf.block.container
			!= ListItemChildrenContainer(*path, *itemIndex)) {
		return std::nullopt;
	}
	return ActiveListItemSurface{
		.path = *path,
		.itemIndex = *itemIndex,
	};
}

auto State::normalizeActiveListItemSurface()
-> std::optional<State::ActiveListItemSurface> {
	const auto descriptor = textNode(_activeTextOrdinal);
	const auto surface = activeListItemSurface();
	if (!descriptor
		|| !surface
		|| descriptor->leaf.kind != LeafKind::ListItemText) {
		return surface;
	}
	const auto item = listItem(surface->path, surface->itemIndex);
	if (!item) {
		return std::nullopt;
	}
	auto paragraph = MakeParagraphBlock();
	paragraph.anchorId = std::move(item->anchorId);
	paragraph.text = std::move(item->text);
	item->anchorId.clear();
	item->text = RichText();
	clearTemporaryDownParagraph();
	item->blocks.insert(item->blocks.begin(), std::move(paragraph));
	return surface;
}

void State::seedInsertedBlocks(
		std::vector<Block> &blocks,
		TextWithEntities text) {
	for (auto &block : blocks) {
		if (auto target = seedInsertedBlock(block)) {
			if (!text.text.isEmpty()) {
				auto combined = std::move(text);
				combined.append(target->text);
				target->text = std::move(combined);
			}
			return;
		}
	}
}

RichText *State::seedInsertedBlock(Block &block) {
	switch (block.kind) {
	case BlockKind::Heading:
	case BlockKind::Paragraph:
	case BlockKind::Footer:
	case BlockKind::Code:
	case BlockKind::Table:
	case BlockKind::Details:
		return &block.text;
	case BlockKind::Quote:
		if (block.blocks.empty()) {
			return &block.text;
		}
		for (auto &child : block.blocks) {
			if (const auto result = seedInsertedBlock(child)) {
				return result;
			}
		}
		return &block.caption;
	case BlockKind::List:
		for (auto &item : block.listItems) {
			if (!RichTextIsEmpty(item.text) || item.blocks.empty()) {
				return &item.text;
			}
			for (auto &child : item.blocks) {
				if (const auto result = seedInsertedBlock(child)) {
					return result;
				}
			}
		}
		return nullptr;
	case BlockKind::Photo:
	case BlockKind::Video:
	case BlockKind::Audio:
	case BlockKind::File:
	case BlockKind::Map:
		return &block.caption;
	default:
		return nullptr;
	}
}

bool State::appendInsertedTrailingText(
		const BlockContainerPath &container,
		int insertAt,
		int count,
		TextWithEntities text) {
	if (text.text.isEmpty()) {
		return true;
	}
	const auto blocks = blockContainer(container);
	if (!blocks
		|| insertAt < 0
		|| count < 0
		|| insertAt + count > int(blocks->size())) {
		return false;
	}
	const auto paragraphIndex = insertAt + count;
	if (paragraphIndex < int(blocks->size())
		&& (*blocks)[paragraphIndex].kind == BlockKind::Paragraph
		&& !BlockIsEmpty((*blocks)[paragraphIndex])) {
		clearTemporaryDownParagraph();
		blocks->insert(
			blocks->begin() + paragraphIndex,
			MakeParagraphBlock());
	}
	const auto paragraph = reuseOrInsertParagraph(container, paragraphIndex);
	if (!paragraph) {
		return false;
	}
	const auto target = richText(paragraph->leaf);
	if (!target) {
		return false;
	}
	target->text = std::move(text);
	return true;
}

std::optional<int> State::ensureTrailingParagraphActive() {
	return applyCheckedMutation(std::optional<int>(), [](State &candidate) {
		const auto result = candidate.ensureTrailingParagraphActiveUnchecked();
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::ensureTrailingParagraphActiveUnchecked() {
	if (_richPage->blocks.empty()
		|| _richPage->blocks.back().kind != BlockKind::Paragraph) {
		clearTemporaryDownParagraph();
		_richPage->blocks.push_back(MakeParagraphBlock());
	}
	const auto path = BlockPath{
		.container = BlockContainerPath(),
		.index = int(_richPage->blocks.size()) - 1,
	};
	rebuild();
	const auto ordinal = textNodeOrdinal({
			.kind = LeafKind::BlockText,
			.block = path,
		});
	if (!setActiveTextByOrdinal(ordinal)) {
		ensureActiveTextOrdinal();
	}
	return (_activeTextOrdinal >= 0)
		? std::make_optional(_activeTextOrdinal)
		: std::nullopt;
}

std::optional<int> State::insertLeadingParagraphActive(bool focusInserted) {
	return applyCheckedMutation(std::optional<int>(), [=](State &candidate) {
		const auto result = candidate.insertLeadingParagraphActiveUnchecked(
			focusInserted);
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::insertLeadingParagraphActiveUnchecked(
		bool focusInserted) {
	auto restore = std::optional<LeafPath>();
	if (!focusInserted) {
		if (const auto descriptor = textNode(_activeTextOrdinal)) {
			restore = descriptor->leaf;
			// The paragraph is prepended to the top-level blocks list,
			// so the active leaf's root-level index shifts by one.
			if (restore->block.container.steps.empty()) {
				++restore->block.index;
			} else {
				++restore->block.container.steps.front().blockIndex;
			}
		}
	}
	clearTemporaryDownParagraph();
	_richPage->blocks.insert(_richPage->blocks.begin(), MakeParagraphBlock());
	rebuild();
	const auto inserted = LeafPath{
		.kind = LeafKind::BlockText,
		.block = {
			.container = BlockContainerPath(),
			.index = 0,
		},
	};
	const auto target = restore.value_or(inserted);
	if (!setActiveTextByOrdinal(textNodeOrdinal(target))) {
		ensureActiveTextOrdinal();
	}
	return (_activeTextOrdinal >= 0)
		? std::make_optional(_activeTextOrdinal)
		: std::nullopt;
}

void State::resyncAfterExternalRichPageMutation() {
	clearTemporaryDownParagraph();
	const auto activeLeaf = [&]() -> std::optional<LeafPath> {
		if (const auto descriptor = textNode(_activeTextOrdinal)) {
			return descriptor->leaf;
		}
		return std::nullopt;
	}();
	rebuild();
	if (activeLeaf && (textNodeOrdinal(*activeLeaf) >= 0)) {
		const auto activated = activateRebuiltLeaf(*activeLeaf);
		Assert(activated);
	} else {
		ensureActiveTextOrdinal();
	}
}

bool State::insertBlocksAfterActiveWithContextUnchecked(
		std::vector<Block> &blocks,
		const ActiveTextInsertContext &context) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || descriptor->leaf.kind == LeafKind::TableCellText) {
		return false;
	}
	const auto target = resolveActiveTextInsertTarget();
	if (!target) {
		return false;
	}
	auto container = target->anchor.container;
	auto insertAt = target->anchor.blockIndex + 1;
	auto removeSource = false;
	if (target->leaf.kind == LeafKind::BlockText) {
		if (const auto owner = block(target->leaf.block);
			owner
			&& context.before.text.isEmpty()
			&& ((owner->kind == BlockKind::Paragraph)
				|| (owner->kind == BlockKind::Heading)
				|| (owner->kind == BlockKind::Footer))) {
			removeSource = true;
			container = target->leaf.block.container;
			insertAt = target->leaf.block.index;
		}
	}
	auto *destination = blockContainer(container);
	if (!destination) {
		return false;
	}
	if (removeSource) {
		if (insertAt < 0 || insertAt >= int(destination->size())) {
			return false;
		}
	} else {
		const auto current = richText(target->leaf);
		if (!current) {
			return false;
		}
		current->text = context.before;
	}
	seedInsertedBlocks(blocks, context.selected);
	if (removeSource) {
		destination->erase(destination->begin() + insertAt);
	}
	const auto count = int(blocks.size());
	if (insertAt < 0 || insertAt > int(destination->size())) {
		return false;
	}
	destination->insert(
		destination->begin() + insertAt,
		std::make_move_iterator(blocks.begin()),
		std::make_move_iterator(blocks.end()));
	if (!appendInsertedTrailingText(
			container,
			insertAt,
			count,
			context.after)) {
		return false;
	}
	rebuild();
	focusInsertedBlocks(container, insertAt, count);
	return true;
}

bool State::insertBlocksAfterActiveUnchecked(
		std::vector<Block> blocks,
		std::optional<ActiveTextInsertContext> context) {
	if (blocks.empty()) {
		return false;
	}
	clearTemporaryDownParagraph();
	NormalizeInsertedOrderedListMetadata(&blocks);
	normalizeInsertedBlockAnchors(blocks);
	if (context) {
		if (insertBlocksAfterActiveWithContextUnchecked(blocks, *context)) {
			return true;
		}
		if (applyActiveTextUnchecked(JoinText(
				context->before,
				context->selected,
				context->after)) == ApplyResult::Failed) {
			return false;
		}
	}
	const auto descriptor = textNode(_activeTextOrdinal);
	if (descriptor && shouldReplaceActiveTextOnlyBlock(*descriptor, blocks)) {
		const auto path = descriptor->removalTarget.block;
		const auto container = blockContainer(path.container);
		if (container
			&& path.index >= 0
			&& path.index < int(container->size())) {
			const auto insertAt = path.index;
			const auto count = int(blocks.size());
			container->erase(container->begin() + insertAt);
			container->insert(
				container->begin() + insertAt,
				std::make_move_iterator(blocks.begin()),
				std::make_move_iterator(blocks.end()));
			rebuild();
			focusInsertedBlocks(path.container, insertAt, count);
			return true;
		}
	}
	auto anchor = resolveActiveInsertionTarget();
	if (const auto normalized = normalizeTextOnlyListItemForInsertion(
			anchor.container)) {
		anchor.blockIndex = *normalized;
	} else if (const auto normalized = normalizeTextOnlyQuoteForInsertion(
			anchor.container)) {
		anchor.blockIndex = *normalized;
	}
	auto *container = blockContainer(anchor.container);
	if (!container) {
		anchor = InsertionAnchor{
			.container = BlockContainerPath(),
			.blockIndex = int(_richPage->blocks.size()) - 1,
		};
		container = &_richPage->blocks;
	}
	const auto insertAt = std::clamp(
		anchor.blockIndex + 1,
		0,
		int(container->size()));
	const auto count = int(blocks.size());
	container->insert(
		container->begin() + insertAt,
		std::make_move_iterator(blocks.begin()),
		std::make_move_iterator(blocks.end()));
	rebuild();
	focusInsertedBlocks(anchor.container, insertAt, count);
	return true;
}

bool State::insertPreparedBlocksAtExplicitPosition(
		std::vector<Block> blocks,
		const BlockContainerPath &container,
		int *insertAt) {
	if (!normalizeTextOnlyContainerForInsertion(container, insertAt)) {
		return false;
	}
	auto *destination = blockContainer(container);
	if (!destination || *insertAt > int(destination->size())) {
		return false;
	}
	NormalizeInsertedOrderedListMetadata(&blocks);
	destination->insert(
		destination->begin() + *insertAt,
		std::make_move_iterator(blocks.begin()),
		std::make_move_iterator(blocks.end()));
	return true;
}

bool State::insertPreparedBlocksAtRemovedBlockRange(
		std::vector<Block> blocks,
		const StructuralBlockRange &range) {
	if (blocks.empty()) {
		return false;
	}
	return applyCheckedMutation(false, [
			blocks = std::move(blocks),
			range](State &candidate) mutable {
		candidate.normalizeInsertedBlockAnchors(blocks);
		auto insertAt = range.from;
		const auto count = int(blocks.size());
		if (!candidate.insertPreparedBlocksAtExplicitPosition(
				std::move(blocks),
				range.container,
				&insertAt)) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		candidate.rebuild();
		candidate.focusInsertedBlocks(range.container, insertAt, count);
		return CheckedMutationResult<bool>{ .apply = true, .result = true };
	});
}

bool State::insertPreparedBlocksAtDropTarget(
		std::vector<Block> blocks,
		const Markdown::PreparedEditBlockDropTarget &target) {
	if (blocks.empty() || target.insertIndex < 0) {
		return false;
	}
	return applyCheckedMutation(false, [
			blocks = std::move(blocks),
			target](State &candidate) mutable {
		const auto container = candidate.convertBlockContainerPath(
			target.container);
		if (!container || !candidate.blockContainer(*container)) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		candidate.normalizeInsertedBlockAnchors(blocks);
		auto insertAt = target.insertIndex;
		if (!candidate.insertPreparedBlocksAtExplicitPosition(
				std::move(blocks),
				*container,
				&insertAt)) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		candidate.rebuild();
		return CheckedMutationResult<bool>{ .apply = true, .result = true };
	});
}

bool State::insertPreparedListItemsAtExplicitPosition(
		std::vector<ListItem> items,
		const BlockPath &path,
		int insertAt) {
	auto *owner = block(path);
	if (!owner
		|| owner->kind != BlockKind::List
		|| insertAt < 0
		|| insertAt > int(owner->listItems.size())) {
		return false;
	}
	owner->listItems.insert(
		owner->listItems.begin() + insertAt,
		std::make_move_iterator(items.begin()),
		std::make_move_iterator(items.end()));
	return true;
}

void State::focusInsertedBlocks(
		const BlockContainerPath &container,
		int from,
		int count) {
	for (auto blockIndex = from; blockIndex != from + count; ++blockIndex) {
		const auto path = BlockPath{
			.container = container,
			.index = blockIndex,
		};
		for (auto i = 0, textCount = textNodeCount(); i != textCount; ++i) {
			if (descriptorBelongsToBlock(_textNodes[i], path)
				&& setActiveTextByOrdinal(i)) {
				return;
			}
		}
	}
	ensureActiveTextOrdinal();
}

State::BoundaryTarget State::destinationTargetForInsertedBlocks(
		const BlockContainerPath &container,
		int from,
		int count) {
	focusInsertedBlocks(container, from, count);
	if (const auto descriptor = textNode(_activeTextOrdinal)) {
		for (auto blockIndex = from; blockIndex != from + count; ++blockIndex) {
			const auto path = BlockPath{
				.container = container,
				.index = blockIndex,
			};
			if (descriptorBelongsToBlock(*descriptor, path)) {
				return {
					.action = BoundaryTarget::Action::Text,
					.textOrdinal = _activeTextOrdinal,
				};
			}
		}
	}
	for (auto blockIndex = from; blockIndex != from + count; ++blockIndex) {
		const auto path = BlockPath{
			.container = container,
			.index = blockIndex,
		};
		if (const auto owner = block(path); owner && CanEditBlock(*owner)) {
			return {
				.action = BoundaryTarget::Action::StructuralSelection,
				.structuralSelection = preparedSelectionForBlock(path),
			};
		}
	}
	return {};
}

State::BoundaryTarget State::destinationTargetForInsertedListItems(
		const BlockPath &path,
		int from,
		int count) {
	for (auto i = 0, textCount = textNodeCount(); i != textCount; ++i) {
		const auto itemIndex = ListItemIndexForLeaf(_textNodes[i].leaf, path);
		if (!itemIndex
			|| *itemIndex < from
			|| *itemIndex >= from + count
			|| !setActiveTextByOrdinal(i)) {
			continue;
		}
		return {
			.action = BoundaryTarget::Action::Text,
			.textOrdinal = _activeTextOrdinal,
		};
	}
	const auto owner = block(path);
	if (owner
		&& owner->kind == BlockKind::List
		&& from >= 0
		&& from < int(owner->listItems.size())
		&& CanEditBlocks(owner->listItems[from].blocks)) {
		ensureActiveTextOrdinal();
		return {
			.action = BoundaryTarget::Action::StructuralSelection,
			.structuralSelection = preparedSelectionForListItem(path, from),
		};
	}
	ensureActiveTextOrdinal();
	return (_activeTextOrdinal >= 0)
		? BoundaryTarget{
			.action = BoundaryTarget::Action::Text,
			.textOrdinal = _activeTextOrdinal,
		}
		: BoundaryTarget();
}

} // namespace Iv::Editor
