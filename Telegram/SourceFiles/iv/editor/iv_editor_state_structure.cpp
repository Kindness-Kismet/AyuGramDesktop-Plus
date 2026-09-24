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

bool State::wrapStructuralBlockSelection(
		const Markdown::PreparedEditSelection &selection,
		InsertAction action,
		BoundaryTarget *destination) {
	if (selection.kind != PreparedEditSelectionKind::Blocks) {
		return false;
	}
	const auto range = validateBlockRange(selection.blocks);
	auto payload = structuredClipboardDataForSelection(selection);
	auto *data = payload
		? std::get_if<ClipboardBlockData>(&*payload)
		: nullptr;
	if (!range || !data) {
		return false;
	}
	auto container = range->container;
	auto insertAt = range->from;
	if (!removeStructuralSelection(selection, true)) {
		return false;
	}
	if (const auto normalized = normalizeTextOnlyListItemForInsertion(
			container); normalized && (*normalized >= 0)) {
		++insertAt;
	}
	auto wrapper = makeBlock(action);
	switch (action.type) {
	case InsertBlockType::Blockquote:
	case InsertBlockType::Pullquote:
	case InsertBlockType::Details:
		wrapper.blocks = std::move(data->blocks);
		break;
	case InsertBlockType::OrderedList:
	case InsertBlockType::BulletList:
	case InsertBlockType::TaskList: {
		if (wrapper.kind != BlockKind::List || wrapper.listItems.empty()) {
			return false;
		}
		const auto taskState = wrapper.listItems.front().taskState;
		auto items = std::vector<ListItem>();
		items.reserve(data->blocks.size());
		for (auto &block : data->blocks) {
			if (block.kind == BlockKind::List) {
				for (auto &item : block.listItems) {
					item.number = {};
					if (taskState == TaskState::None) {
						item.taskState = TaskState::None;
					} else if (item.taskState == TaskState::None) {
						item.taskState = TaskState::Unchecked;
					}
					items.push_back(std::move(item));
				}
				continue;
			}
			auto item = ListItem();
			item.taskState = taskState;
			item.blocks.push_back(std::move(block));
			adoptLeadingParagraphListItemText(&item);
			items.push_back(std::move(item));
		}
		if (!items.empty()) {
			wrapper.listItems = std::move(items);
		}
		break;
	}
	default:
		return false;
	}
	auto inserted = std::vector<Block>();
	inserted.push_back(std::move(wrapper));
	normalizeInsertedBlockAnchors(inserted);
	if (!insertPreparedBlocksAtExplicitPosition(
			std::move(inserted),
			container,
			&insertAt)) {
		return false;
	}
	const auto joined = joinListWithSiblings(
		BlockPath{ .container = container, .index = insertAt },
		action.orderedStartExplicit);
	rebuild();
	const auto target = joined
		? destinationTargetForInsertedListItems(
			joined->list,
			joined->itemsFrom,
			joined->itemsCount)
		: destinationTargetForInsertedBlocks(container, insertAt, 1);
	if (destination) {
		*destination = target;
	}
	return true;
}

bool State::unwrapMatchingStructuralWrapper(
		const Markdown::PreparedEditSelection &selection,
		InsertBlockType type,
		BoundaryTarget *destination) {
	if (selection.kind != PreparedEditSelectionKind::Blocks) {
		return false;
	}
	const auto range = validateBlockRange(selection.blocks);
	if (!range || range->container.steps.empty()) {
		return false;
	}
	const auto step = range->container.steps.back();
	if (step.kind != BlockContainerKind::BlockChildren) {
		return false;
	}
	auto parentContainer = range->container;
	parentContainer.steps.pop_back();
	auto *parent = blockContainer(parentContainer);
	const auto wrapperPath = BlockPath{
		.container = parentContainer,
		.index = step.blockIndex,
	};
	auto *wrapper = block(wrapperPath);
	const auto matches = [&](const Block &block) {
		switch (type) {
		case InsertBlockType::Blockquote:
			return (block.kind == BlockKind::Quote) && !block.pullquote;
		case InsertBlockType::Pullquote:
			return (block.kind == BlockKind::Quote) && block.pullquote;
		case InsertBlockType::Details:
			return (block.kind == BlockKind::Details);
		default:
			return false;
		}
	};
	if (!parent
		|| !wrapper
		|| !matches(*wrapper)
		|| (range->from != 0)
		|| (range->till != int(wrapper->blocks.size()))) {
		return false;
	}
	if (!wrapper->anchorId.isEmpty()
		|| !RichTextIsEmpty(wrapper->text)
		|| !RichTextIsEmpty(wrapper->caption)) {
		return false;
	}
	clearTemporaryDownParagraph();
	auto blocks = std::move(wrapper->blocks);
	const auto insertedCount = int(blocks.size());
	parent->erase(parent->begin() + wrapperPath.index);
	parent->insert(
		parent->begin() + wrapperPath.index,
		std::make_move_iterator(blocks.begin()),
		std::make_move_iterator(blocks.end()));
	rebuild();
	const auto target = destinationTargetForInsertedBlocks(
		parentContainer,
		wrapperPath.index,
		insertedCount);
	if (destination) {
		*destination = target;
	}
	return true;
}

std::vector<Block> State::takeListItemBlocksForUnwrap(ListItem *item) {
	auto result = std::vector<Block>();
	if (!item) {
		return result;
	}
	if (!item->anchorId.isEmpty() || !RichTextIsEmpty(item->text)) {
		auto paragraph = MakeParagraphBlock();
		paragraph.anchorId = std::move(item->anchorId);
		paragraph.text = std::move(item->text);
		result.push_back(std::move(paragraph));
	}
	result.insert(
		result.end(),
		std::make_move_iterator(item->blocks.begin()),
		std::make_move_iterator(item->blocks.end()));
	item->blocks.clear();
	return result;
}

void State::adoptLeadingParagraphListItemText(ListItem *item) const {
	// List items hold either inline text or a list of blocks, never both,
	// so adopt the paragraph text only if it is the single item block.
	if (!item
		|| (item->blocks.size() != 1)
		|| item->blocks.front().kind != BlockKind::Paragraph) {
		return;
	}
	item->text = std::move(item->blocks.front().text);
	item->anchorId = std::move(item->blocks.front().anchorId);
	item->blocks.erase(item->blocks.begin());
}

bool State::unwrapMatchingListItemWrapper(
		const Markdown::PreparedEditSelection &selection,
		InsertBlockType type,
		BoundaryTarget *destination) {
	if (selection.kind != PreparedEditSelectionKind::ListItems) {
		return false;
	}
	const auto range = validateListItemRange(selection.listItems);
	if (!range) {
		return false;
	}
	auto *owner = block(range->block);
	auto *parent = blockContainer(range->block.container);
	const auto matches = [&](const Block &block) {
		if (block.kind != BlockKind::List) {
			return false;
		}
		const auto style = CurrentListStyle(block);
		switch (type) {
		case InsertBlockType::OrderedList:
			return (style == ListStyle::Ordered);
		case InsertBlockType::BulletList:
			return (style == ListStyle::Bullet);
		case InsertBlockType::TaskList:
			return (style == ListStyle::Task);
		default:
			return false;
		}
	};
	if (!owner || !parent || !matches(*owner)) {
		return false;
	}
	clearTemporaryDownParagraph();
	return unwrapListItemRangeIntoParent(
		range->block,
		range->from,
		range->till,
		false,
		destination);
}

bool State::unwrapMatchingListBlockSelection(
		const Markdown::PreparedEditSelection &selection,
		InsertBlockType type,
		BoundaryTarget *destination) {
	if (selection.kind != PreparedEditSelectionKind::Blocks) {
		return false;
	}
	const auto range = validateBlockRange(selection.blocks);
	if (!range) {
		return false;
	}
	auto *parent = blockContainer(range->container);
	if (!parent
		|| range->from < 0
		|| range->till > int(parent->size())
		|| range->till <= range->from) {
		return false;
	}
	const auto matches = [&](const Block &block) {
		if (block.kind != BlockKind::List || block.listItems.empty()) {
			return false;
		}
		const auto style = CurrentListStyle(block);
		switch (type) {
		case InsertBlockType::OrderedList:
			return (style == ListStyle::Ordered);
		case InsertBlockType::BulletList:
			return (style == ListStyle::Bullet);
		case InsertBlockType::TaskList:
			return (style == ListStyle::Task);
		default:
			return false;
		}
	};
	for (auto i = range->from; i != range->till; ++i) {
		if (!matches((*parent)[i])) {
			return false;
		}
	}
	clearTemporaryDownParagraph();
	auto target = BoundaryTarget();
	for (auto i = range->till - 1; i >= range->from; --i) {
		const auto path = BlockPath{ range->container, i };
		const auto owner = block(path);
		if (!owner
			|| !unwrapListItemRangeIntoParent(
				path,
				0,
				int(owner->listItems.size()),
				false,
				&target)) {
			return false;
		}
	}
	if (destination) {
		*destination = target;
	}
	return true;
}

bool State::unwrapListItemIntoParent(
		const BlockPath &listPath,
		int itemIndex,
		bool materializeEmptyItem,
		BoundaryTarget *destination) {
	return unwrapListItemRangeIntoParent(
		listPath,
		itemIndex,
		itemIndex + 1,
		materializeEmptyItem,
		destination);
}

bool State::unwrapListItemRangeIntoParent(
		const BlockPath &listPath,
		int from,
		int till,
		bool materializeEmptyItem,
		BoundaryTarget *destination,
		StructuralBlockRange *unwrapped) {
	auto *owner = block(listPath);
	auto *parent = blockContainer(listPath.container);
	if (!owner
		|| !parent
		|| from < 0
		|| till <= from
		|| till > int(owner->listItems.size())
		|| listPath.index < 0
		|| listPath.index >= int(parent->size())) {
		return false;
	}
	const auto hasLeading = (from > 0);
	const auto hasTrailing = (till < int(owner->listItems.size()));
	const auto leadingStart = (owner->listKind == ListKind::Ordered
		&& hasLeading
		&& owner->orderedList.reversed)
		? EffectiveOrderedItemValue(*owner, 0)
		: std::optional<int>();
	const auto trailingStart = (owner->listKind == ListKind::Ordered
		&& hasTrailing)
		? EffectiveOrderedItemValue(*owner, till)
		: std::optional<int>();
	auto inserted = std::vector<Block>();
	for (auto i = from; i != till; ++i) {
		auto blocks = takeListItemBlocksForUnwrap(&owner->listItems[i]);
		if (materializeEmptyItem && blocks.empty()) {
			blocks.push_back(MakeParagraphBlock());
		}
		inserted.insert(
			inserted.end(),
			std::make_move_iterator(blocks.begin()),
			std::make_move_iterator(blocks.end()));
	}
	auto trailing = std::optional<Block>();
	if (hasTrailing) {
		trailing = Block();
		trailing->kind = BlockKind::List;
		trailing->listKind = owner->listKind;
		trailing->orderedList = owner->orderedList;
		if (trailingStart.has_value()) {
			trailing->orderedList.start = trailingStart;
		}
		trailing->listItems = std::vector<ListItem>(
			std::make_move_iterator(owner->listItems.begin() + till),
			std::make_move_iterator(owner->listItems.end()));
	}
	if (hasLeading) {
		owner->listItems.erase(
			owner->listItems.begin() + from,
			owner->listItems.end());
		if (leadingStart.has_value()) {
			owner->orderedList.start = leadingStart;
		}
	} else {
		parent->erase(parent->begin() + listPath.index);
	}
	auto insertAt = listPath.index + (hasLeading ? 1 : 0);
	const auto insertedCount = int(inserted.size());
	if (unwrapped) {
		*unwrapped = StructuralBlockRange{
			.container = listPath.container,
			.from = insertAt,
			.till = insertAt + insertedCount,
		};
	}
	parent->insert(
		parent->begin() + insertAt,
		std::make_move_iterator(inserted.begin()),
		std::make_move_iterator(inserted.end()));
	if (trailing.has_value()) {
		NormalizeInsertedOrderedListMetadata(&*trailing);
		parent->insert(
			parent->begin() + insertAt + insertedCount,
			std::move(*trailing));
	}
	rebuild();
	const auto target = destinationTargetForInsertedBlocks(
		listPath.container,
		insertAt,
		insertedCount);
	if (destination) {
		*destination = target;
	}
	return true;
}

bool State::wrapStructuralListItemSelection(
		const Markdown::PreparedEditSelection &selection,
		InsertAction action,
		BoundaryTarget *destination) {
	if (selection.kind != PreparedEditSelectionKind::ListItems) {
		return false;
	}
	const auto range = validateListItemRange(selection.listItems);
	if (!range) {
		return false;
	}
	const auto owner = block(range->block);
	if (!owner || owner->kind != BlockKind::List) {
		return false;
	}
	clearTemporaryDownParagraph();
	auto unwrapped = StructuralBlockRange();
	if (!unwrapListItemRangeIntoParent(
			range->block,
			range->from,
			range->till,
			true,
			nullptr,
			&unwrapped)) {
		return false;
	}
	return wrapStructuralBlockSelection(
		preparedSelectionForBlockRange(unwrapped),
		action,
		destination);
}

bool State::replaceStructuralSelectionWithBlock(
		const Markdown::PreparedEditSelection &selection,
		InsertAction action,
		std::optional<ActiveTextInsertContext> context,
		BoundaryTarget *destination) {
	_lastLimitError = std::nullopt;
	if (destination) {
		*destination = {};
	}
	auto candidate = State(
		std::make_shared<RichPage>(*_richPage),
		_mediaRuntime,
		_limits);
	candidate._activeTextOrdinal = _activeTextOrdinal;
	candidate._lastLimitError = std::nullopt;
	candidate._temporaryDownParagraph = _temporaryDownParagraph;
	const auto commitValidatedCandidate = [&](State &&candidate) {
		const auto error = ValidateRichMessage(
			*candidate._richPage,
			_limits);
		if (error) {
			_lastLimitError = error;
			return false;
		}
		commitCheckedMutation(std::move(candidate));
		return true;
	};
	const auto structuralTextBlockConversion = [&]()
	-> std::optional<LeafPath> {
		const auto allowed = [](InsertBlockType type) {
			switch (type) {
			case InsertBlockType::Heading:
			case InsertBlockType::Code:
			case InsertBlockType::Footer:
				return true;
			default:
				return false;
			}
		};
		if (selection.kind != PreparedEditSelectionKind::Blocks
			|| !allowed(action.type)) {
			return std::nullopt;
		}
		const auto range = candidate.validateBlockRange(selection.blocks);
		if (!range || (range->from + 1 != range->till)) {
			return std::nullopt;
		}
		const auto leaf = LeafPath{
			.kind = LeafKind::BlockText,
			.block = {
				.container = range->container,
				.index = range->from,
			},
		};
		const auto owner = candidate.block(leaf.block);
		if (!owner
			|| ((owner->kind != BlockKind::Paragraph)
				&& (owner->kind != BlockKind::Heading)
				&& (owner->kind != BlockKind::Footer))
			|| (candidate.textNodeOrdinal(leaf) < 0)) {
			return std::nullopt;
		}
		return leaf;
	};
	if (const auto leaf = structuralTextBlockConversion()) {
		const auto ordinal = candidate.textNodeOrdinal(*leaf);
		const auto owner = candidate.block(leaf->block);
		if (!owner || !candidate.setActiveTextByOrdinal(ordinal)) {
			_lastLimitError = candidate._lastLimitError;
			return false;
		}
		if (!candidate.insertBlockAfterActive(action, ActiveTextInsertContext{
				.before = {},
				.selected = owner->text.text,
				.after = {},
			})) {
			_lastLimitError = candidate._lastLimitError;
			return false;
		}
		if (destination && (candidate._activeTextOrdinal >= 0)) {
			*destination = {
				.action = BoundaryTarget::Action::Text,
				.textOrdinal = candidate._activeTextOrdinal,
			};
		}
		commitCheckedMutation(std::move(candidate));
		return true;
	}
	auto target = BoundaryTarget();
	switch (action.type) {
	case InsertBlockType::Blockquote:
	case InsertBlockType::Pullquote:
	case InsertBlockType::Details:
		if (selection.kind == PreparedEditSelectionKind::TableRows
			|| selection.kind == PreparedEditSelectionKind::TableCells) {
			return false;
		}
		if (candidate.unwrapMatchingStructuralWrapper(
				selection,
				action.type,
				&target)) {
			if (!commitValidatedCandidate(std::move(candidate))) {
				return false;
			}
			if (destination) {
				*destination = target;
			}
			return true;
		}
		if (selection.kind == PreparedEditSelectionKind::Blocks
			|| selection.kind == PreparedEditSelectionKind::ListItems) {
			const auto wrapped = (selection.kind
				== PreparedEditSelectionKind::ListItems)
				? candidate.wrapStructuralListItemSelection(
					selection,
					action,
					&target)
				: candidate.wrapStructuralBlockSelection(
					selection,
					action,
					&target);
			if (!wrapped) {
				_lastLimitError = candidate._lastLimitError;
				return false;
			}
			if (!commitValidatedCandidate(std::move(candidate))) {
				return false;
			}
			if (destination) {
				*destination = target;
			}
			return true;
		}
		break;
	case InsertBlockType::OrderedList:
	case InsertBlockType::BulletList:
	case InsertBlockType::TaskList:
		if (selection.kind == PreparedEditSelectionKind::TableRows
			|| selection.kind == PreparedEditSelectionKind::TableCells) {
			return false;
		}
		if (candidate.unwrapMatchingListItemWrapper(
				selection,
				action.type,
				&target)
			|| candidate.unwrapMatchingListBlockSelection(
				selection,
				action.type,
				&target)) {
			if (!commitValidatedCandidate(std::move(candidate))) {
				return false;
			}
			if (destination) {
				*destination = target;
			}
			return true;
		}
		if (selection.kind == PreparedEditSelectionKind::ListItems) {
			const auto style = (action.type == InsertBlockType::OrderedList)
				? ListStyle::Ordered
				: (action.type == InsertBlockType::TaskList)
				? ListStyle::Task
				: ListStyle::Bullet;
			const auto validated = candidate.validateListItemRange(
				selection.listItems);
			const auto owner = validated
				? candidate.block(validated->block)
				: nullptr;
			if (!owner || (owner->kind != BlockKind::List)) {
				return false;
			}
			if (destination) {
				*destination = {
					.action = BoundaryTarget::Action::StructuralSelection,
					.structuralSelection = selection,
				};
			}
			if (CurrentListStyle(*owner) == style) {
				return true;
			}
			if (!candidate.setListStyle(selection.listItems, style)) {
				return false;
			}
			if (const auto joined = candidate.joinListWithSiblings(
					validated->block,
					false)) {
				candidate.rebuild();
				if (destination) {
					const auto shift = joined->itemsFrom;
					*destination = {
						.action = BoundaryTarget::Action::StructuralSelection,
						.structuralSelection
							= candidate.preparedSelectionForListItems(
								joined->list,
								validated->from + shift,
								validated->till + shift),
					};
				}
			}
			return commitValidatedCandidate(std::move(candidate));
		}
		if (selection.kind == PreparedEditSelectionKind::Blocks) {
			if (!candidate.wrapStructuralBlockSelection(
					selection,
					action,
					&target)) {
				_lastLimitError = candidate._lastLimitError;
				return false;
			}
			if (!commitValidatedCandidate(std::move(candidate))) {
				return false;
			}
			if (destination) {
				*destination = target;
			}
			return true;
		}
		break;
	default:
		break;
	}
	if (!candidate.removeStructuralSelection(selection, true)) {
		_lastLimitError = candidate._lastLimitError;
		return false;
	}
	if (!candidate.insertBlockAfterActive(action, std::move(context))) {
		_lastLimitError = candidate._lastLimitError;
		return false;
	}
	if (destination && (candidate._activeTextOrdinal >= 0)) {
		*destination = {
			.action = BoundaryTarget::Action::Text,
			.textOrdinal = candidate._activeTextOrdinal,
		};
	}
	commitCheckedMutation(std::move(candidate));
	return true;
}

bool State::toggleCodeBlockForStructuralSelection(
		const Markdown::PreparedEditSelection &selection) {
	_lastLimitError = std::nullopt;
	if (selection.kind != PreparedEditSelectionKind::Blocks) {
		return false;
	}
	const auto range = validateBlockRange(selection.blocks);
	if (!range || (range->from + 1 != range->till)) {
		return false;
	}
	const auto path = BlockPath{
		.container = range->container,
		.index = range->from,
	};
	const auto owner = block(path);
	if (!owner) {
		return false;
	}
	switch (owner->kind) {
	case BlockKind::Paragraph:
		return replaceStructuralSelectionWithBlock(selection, {
			.type = InsertBlockType::Code,
		});
	case BlockKind::Code: {
		return applyCheckedMutation(false, [selection](State &candidate) {
			const auto range = candidate.validateBlockRange(selection.blocks);
			if (!range || (range->from + 1 != range->till)) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			const auto path = BlockPath{
				.container = range->container,
				.index = range->from,
			};
			const auto owner = candidate.block(path);
			if (!owner || owner->kind != BlockKind::Code) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			auto paragraph = MakeParagraphBlock();
			paragraph.anchorId = owner->anchorId;
			paragraph.text = owner->text;
			auto blocks = std::vector<Block>();
			blocks.push_back(std::move(paragraph));
			auto insertAt = range->from;
			if (!candidate.removeStructuralSelection(selection, true)) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			const auto normalized
				= candidate.normalizeTextOnlyListItemForInsertion(
					range->container);
			if (normalized && (*normalized >= 0)) {
				++insertAt;
			}
			if (!candidate.insertPreparedBlocksAtExplicitPosition(
					std::move(blocks),
					range->container,
					&insertAt)) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			candidate.rebuild();
			candidate.focusInsertedBlocks(range->container, insertAt, 1);
			return CheckedMutationResult<bool>{
				.apply = true,
				.result = true,
			};
		});
	}
	default:
		return false;
	}
}

bool State::replaceStructuralSelectionWithPreparedBlocks(
		const Markdown::PreparedEditSelection &selection,
		std::vector<Block> blocks,
		std::optional<ActiveTextInsertContext> context) {
	_lastLimitError = std::nullopt;
	auto candidate = State(
		std::make_shared<RichPage>(*_richPage),
		_mediaRuntime,
		_limits);
	candidate._activeTextOrdinal = _activeTextOrdinal;
	candidate._lastLimitError = std::nullopt;
	candidate._temporaryDownParagraph = _temporaryDownParagraph;
	const auto blocksRange = (selection.kind == PreparedEditSelectionKind::Blocks)
		? candidate.validateBlockRange(selection.blocks)
		: std::nullopt;
	if (!candidate.removeStructuralSelection(selection, true)) {
		_lastLimitError = candidate._lastLimitError;
		return false;
	}
	const auto inserted = blocksRange
		? candidate.insertPreparedBlocksAtRemovedBlockRange(
			std::move(blocks),
			*blocksRange)
		: candidate.insertPreparedBlocksAfterActive(
			std::move(blocks),
			std::move(context));
	if (!inserted) {
		_lastLimitError = candidate._lastLimitError;
		return false;
	}
	commitCheckedMutation(std::move(candidate));
	return true;
}

bool State::replaceStructuralSelectionWithClipboardListItems(
		const Markdown::PreparedEditSelection &selection,
		const ClipboardListItemsData &data,
		std::optional<ActiveTextInsertContext> context) {
	_lastLimitError = std::nullopt;
	auto candidate = State(
		std::make_shared<RichPage>(*_richPage),
		_mediaRuntime,
		_limits);
	candidate._activeTextOrdinal = _activeTextOrdinal;
	candidate._lastLimitError = std::nullopt;
	candidate._temporaryDownParagraph = _temporaryDownParagraph;
	if (!candidate.removeStructuralSelection(selection, true)) {
		_lastLimitError = candidate._lastLimitError;
		return false;
	}
	if (!candidate.pasteClipboardListItemsAfterActive(
			data,
			std::move(context))) {
		_lastLimitError = candidate._lastLimitError;
		return false;
	}
	commitCheckedMutation(std::move(candidate));
	return true;
}

} // namespace Iv::Editor
