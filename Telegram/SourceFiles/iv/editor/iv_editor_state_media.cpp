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

std::optional<State::ReplaceTarget> State::replaceTargetForBlock(
		const BlockPath &path) const {
	const auto current = block(path);
	if (!current || !IsReplaceableMediaBlockKind(current->kind)) {
		return std::nullopt;
	}
	const auto mediaId = ReplaceTargetMediaId(*current);
	if (!mediaId) {
		return std::nullopt;
	}
	return ReplaceTarget{
		.path = path,
		.kind = current->kind,
		.mediaId = *mediaId,
	};
}

std::optional<State::ReplaceTarget> State::replaceTargetForGroupedItem(
		const BlockPath &path,
		int itemIndex) const {
	const auto current = block(path);
	if (!current
		|| current->kind != BlockKind::GroupedMedia
		|| itemIndex < 0
		|| itemIndex >= int(current->mediaItems.size())) {
		return std::nullopt;
	}
	const auto &item = current->mediaItems[itemIndex];
	if (!IsPhotoVideoBlockKind(item.kind)) {
		return std::nullopt;
	}
	const auto mediaId = ReplaceTargetMediaId(item);
	if (!mediaId) {
		return std::nullopt;
	}
	return ReplaceTarget{
		.path = path,
		.kind = item.kind,
		.mediaId = *mediaId,
		.itemIndex = itemIndex,
	};
}

bool State::replaceBlockWithPreparedBlock(
		const ReplaceTarget &target,
		Block block) {
	return applyCheckedMutation(false, [
		target,
		block = std::move(block)
	](State &candidate) mutable {
		const auto &path = target.path;
		const auto blocks = candidate.blockContainer(path.container);
		if (!blocks
			|| path.index < 0
			|| path.index >= int(blocks->size())) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto &current = (*blocks)[path.index];
		if (target.itemIndex >= 0) {
			if (current.kind != BlockKind::GroupedMedia
				|| target.itemIndex >= int(current.mediaItems.size())) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			auto &item = current.mediaItems[target.itemIndex];
			if (!GroupedItemMatchesReplaceTarget(item, target)) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			auto replacement = GroupedItemFromPhotoVideoBlock(block);
			if (!replacement) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			replacement->spoiler = item.spoiler;
			item = std::move(*replacement);
			candidate.rebuild();
			return CheckedMutationResult<bool>{
				.apply = true,
				.result = true,
			};
		}
		if (!BlockMatchesReplaceTarget(current, target)
			|| !IsReplaceableMediaBlockKind(block.kind)) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		block.caption = std::move(current.caption);
		block.anchorId = std::move(current.anchorId);
		if (IsPhotoVideoBlockKind(current.kind)
			&& IsPhotoVideoBlockKind(block.kind)) {
			block.spoiler = current.spoiler;
		}
		current = std::move(block);
		candidate.rebuild();
		return CheckedMutationResult<bool>{
			.apply = true,
			.result = true,
		};
	});
}

std::optional<int> State::removeBlock(
		const BlockPath &path,
		bool forward) {
	return removeStructuralSelection(preparedSelectionForBlock(path), forward);
}

bool State::canGroupPhotoVideoBlocks(
		const PreparedEditSelection &selection) const {
	if (selection.kind != PreparedEditSelectionKind::Blocks) {
		return false;
	}
	const auto range = validateBlockRange(selection.blocks);
	if (!range || range->till - range->from < 2) {
		return false;
	}
	const auto blocks = blockContainer(range->container);
	if (!blocks) {
		return false;
	}
	auto sources = 0;
	auto captioned = 0;
	for (auto i = range->from; i != range->till; ++i) {
		const auto &block = (*blocks)[i];
		if (!IsGroupableMediaBlock(block)) {
			continue;
		}
		++sources;
		if (BlockHasGroupingCaptionOrAnchor(block)) {
			++captioned;
		}
	}
	return (sources > 1) && (captioned < 2);
}

bool State::groupPhotoVideoBlocks(
		const PreparedEditSelection &selection,
		RichPage::GroupedMediaIntent intent) {
	return applyCheckedMutation(false, [selection, intent](State &candidate) {
		if (!candidate.canGroupPhotoVideoBlocks(selection)) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		const auto range = candidate.validateBlockRange(selection.blocks);
		if (!range) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto *blocks = candidate.blockContainer(range->container);
		if (!blocks) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto items = std::vector<RichPage::GroupedMediaItem>();
		items.reserve(range->till - range->from);
		auto kept = std::vector<Block>();
		auto keptBeforeMedia = 0;
		auto seenMedia = false;
		auto captionSource = std::optional<int>();
		for (auto i = range->from; i != range->till; ++i) {
			auto &block = (*blocks)[i];
			if (!IsGroupableMediaBlock(block)) {
				if (!seenMedia) {
					++keptBeforeMedia;
				}
				kept.push_back(std::move(block));
				continue;
			}
			seenMedia = true;
			if (block.kind == BlockKind::GroupedMedia) {
				items.insert(
					items.end(),
					block.mediaItems.begin(),
					block.mediaItems.end());
			} else if (const auto item = GroupedItemFromPhotoVideoBlock(block)) {
				items.push_back(*item);
			} else {
				return CheckedMutationResult<bool>{ .result = false };
			}
			if (!BlockHasGroupingCaptionOrAnchor(block)) {
				continue;
			} else if (captionSource) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			captionSource = i;
		}
		auto grouped = Block();
		grouped.kind = BlockKind::GroupedMedia;
		grouped.mediaIntent = intent;
		grouped.mediaItems = std::move(items);
		if (captionSource) {
			auto &source = (*blocks)[*captionSource];
			grouped.caption = std::move(source.caption);
			grouped.anchorId = std::move(source.anchorId);
		}
		auto groupedBlocks = SplitGroupedMediaBlock(std::move(grouped));
		auto replacement = std::vector<Block>();
		replacement.reserve(kept.size() + groupedBlocks.size());
		replacement.insert(
			replacement.end(),
			std::make_move_iterator(kept.begin()),
			std::make_move_iterator(kept.begin() + keptBeforeMedia));
		replacement.insert(
			replacement.end(),
			std::make_move_iterator(groupedBlocks.begin()),
			std::make_move_iterator(groupedBlocks.end()));
		replacement.insert(
			replacement.end(),
			std::make_move_iterator(kept.begin() + keptBeforeMedia),
			std::make_move_iterator(kept.end()));
		blocks->erase(
			blocks->begin() + range->from,
			blocks->begin() + range->till);
		blocks->insert(
			blocks->begin() + range->from,
			std::make_move_iterator(replacement.begin()),
			std::make_move_iterator(replacement.end()));
		candidate.rebuild();
		return CheckedMutationResult<bool>{
			.apply = true,
			.result = true,
		};
	});
}

bool State::ungroupGroupedMediaBlock(const BlockPath &path) {
	return applyCheckedMutation(false, [path](State &candidate) {
		auto *blocks = candidate.blockContainer(path.container);
		if (!blocks
			|| path.index < 0
			|| path.index >= int(blocks->size())) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto &current = (*blocks)[path.index];
		if (current.kind != BlockKind::GroupedMedia
			|| current.mediaItems.empty()) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto emitted = std::vector<Block>();
		emitted.reserve(current.mediaItems.size());
		for (const auto &item : current.mediaItems) {
			auto block = PhotoVideoBlockFromGroupedItem(item);
			if (!block) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			emitted.push_back(std::move(*block));
		}
		emitted.front().caption = std::move(current.caption);
		emitted.front().anchorId = std::move(current.anchorId);
		blocks->erase(blocks->begin() + path.index);
		blocks->insert(
			blocks->begin() + path.index,
			std::make_move_iterator(emitted.begin()),
			std::make_move_iterator(emitted.end()));
		candidate.rebuild();
		return CheckedMutationResult<bool>{
			.apply = true,
			.result = true,
		};
	});
}

bool State::canUngroupGroupedMediaBlocks(
		const PreparedEditSelection &selection) const {
	if (selection.kind != PreparedEditSelectionKind::Blocks) {
		return false;
	}
	const auto range = validateBlockRange(selection.blocks);
	if (!range) {
		return false;
	}
	const auto blocks = blockContainer(range->container);
	if (!blocks) {
		return false;
	}
	for (auto i = range->from; i != range->till; ++i) {
		const auto &block = (*blocks)[i];
		if (block.kind == BlockKind::GroupedMedia
			&& IsGroupableMediaBlock(block)) {
			return true;
		}
	}
	return false;
}

bool State::ungroupGroupedMediaBlocks(
		const PreparedEditSelection &selection) {
	return applyCheckedMutation(false, [selection](State &candidate) {
		if (!candidate.canUngroupGroupedMediaBlocks(selection)) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		const auto range = candidate.validateBlockRange(selection.blocks);
		if (!range) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto *blocks = candidate.blockContainer(range->container);
		if (!blocks) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto replacement = std::vector<Block>();
		replacement.reserve(range->till - range->from);
		for (auto i = range->from; i != range->till; ++i) {
			auto &block = (*blocks)[i];
			if (block.kind != BlockKind::GroupedMedia
				|| !IsGroupableMediaBlock(block)) {
				replacement.push_back(std::move(block));
				continue;
			}
			auto first = true;
			for (const auto &item : block.mediaItems) {
				auto single = PhotoVideoBlockFromGroupedItem(item);
				if (!single) {
					return CheckedMutationResult<bool>{ .result = false };
				}
				if (first) {
					single->caption = std::move(block.caption);
					single->anchorId = std::move(block.anchorId);
					first = false;
				}
				replacement.push_back(std::move(*single));
			}
		}
		blocks->erase(
			blocks->begin() + range->from,
			blocks->begin() + range->till);
		blocks->insert(
			blocks->begin() + range->from,
			std::make_move_iterator(replacement.begin()),
			std::make_move_iterator(replacement.end()));
		candidate.rebuild();
		return CheckedMutationResult<bool>{
			.apply = true,
			.result = true,
		};
	});
}

bool State::removeGroupedItem(
		const BlockPath &path,
		int itemIndex) {
	if (itemIndex < 0) {
		return false;
	}
	return applyCheckedMutation(false, [path, itemIndex](State &candidate) {
		auto *blocks = candidate.blockContainer(path.container);
		if (!blocks
			|| path.index < 0
			|| path.index >= int(blocks->size())) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto &current = (*blocks)[path.index];
		if (current.kind != BlockKind::GroupedMedia
			|| itemIndex >= int(current.mediaItems.size())) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		current.mediaItems.erase(current.mediaItems.begin() + itemIndex);
		if (current.mediaItems.size() >= 2) {
			candidate.rebuild();
			return CheckedMutationResult<bool>{
				.apply = true,
				.result = true,
			};
		}
		if (current.mediaItems.empty()) {
			blocks->erase(blocks->begin() + path.index);
			candidate.rebuild();
			return CheckedMutationResult<bool>{
				.apply = true,
				.result = true,
			};
		}
		auto single = PhotoVideoBlockFromGroupedItem(current.mediaItems.front());
		if (!single) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		single->caption = std::move(current.caption);
		single->anchorId = std::move(current.anchorId);
		(*blocks)[path.index] = std::move(*single);
		candidate.rebuild();
		return CheckedMutationResult<bool>{
			.apply = true,
			.result = true,
		};
	});
}

bool State::addItemsToGroupedMedia(
		const BlockPath &path,
		int insertedCount) {
	if (insertedCount < 1) {
		return false;
	}
	return applyCheckedMutation(false, [path, insertedCount](State &candidate) {
		auto *blocks = candidate.blockContainer(path.container);
		if (!blocks
			|| path.index < 0
			|| path.index >= int(blocks->size())) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		const auto from = path.index + 1;
		const auto till = from + insertedCount;
		if (till > int(blocks->size())) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto &group = (*blocks)[path.index];
		if (group.kind != BlockKind::GroupedMedia) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto appended = std::vector<RichPage::GroupedMediaItem>();
		appended.reserve(insertedCount);
		for (auto i = from; i != till; ++i) {
			const auto item = GroupedItemFromPhotoVideoBlock((*blocks)[i]);
			if (!item) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			appended.push_back(*item);
		}
		group.mediaItems.insert(
			group.mediaItems.end(),
			std::make_move_iterator(appended.begin()),
			std::make_move_iterator(appended.end()));
		auto groupedBlocks = SplitGroupedMediaBlock(std::move(group));
		blocks->erase(
			blocks->begin() + path.index,
			blocks->begin() + till);
		blocks->insert(
			blocks->begin() + path.index,
			std::make_move_iterator(groupedBlocks.begin()),
			std::make_move_iterator(groupedBlocks.end()));
		candidate.rebuild();
		return CheckedMutationResult<bool>{
			.apply = true,
			.result = true,
		};
	});
}

bool State::setGroupedMediaIntent(
		const BlockPath &path,
		RichPage::GroupedMediaIntent intent) {
	return applyCheckedMutation(false, [path, intent](State &candidate) {
		auto *blocks = candidate.blockContainer(path.container);
		if (!blocks
			|| path.index < 0
			|| path.index >= int(blocks->size())) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto &current = (*blocks)[path.index];
		if (current.kind != BlockKind::GroupedMedia) {
			return CheckedMutationResult<bool>{ .result = false };
		} else if (current.mediaIntent == intent) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		current.mediaIntent = intent;
		auto groupedBlocks = SplitGroupedMediaBlock(std::move(current));
		blocks->erase(blocks->begin() + path.index);
		blocks->insert(
			blocks->begin() + path.index,
			std::make_move_iterator(groupedBlocks.begin()),
			std::make_move_iterator(groupedBlocks.end()));
		candidate.rebuild();
		return CheckedMutationResult<bool>{
			.apply = true,
			.result = true,
		};
	});
}

} // namespace Iv::Editor
