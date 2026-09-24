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

State::StructuralSelectionDropResult State::moveStructuralSelectionToDropTarget(
		const PreparedEditSelection &selection,
		const Markdown::PreparedEditDropTarget &target) {
	struct BlockInsertionTarget {
		BlockContainerPath container;
		int insertIndex = -1;
	};
	struct ListInsertionTarget {
		BlockPath block;
		int insertIndex = -1;
	};
	const auto failure = StructuralSelectionDropResult{
		.result = ApplyResult::Failed,
	};
	return applyCheckedMutation(failure, [selection, target](State &candidate) {
		auto result = StructuralSelectionDropResult{
			.result = ApplyResult::Failed,
		};
		const auto payload = candidate.structuredClipboardDataForSelection(
			selection);
		if (!payload) {
			return CheckedMutationResult<StructuralSelectionDropResult>{
				.apply = false,
				.result = result,
			};
		}
		auto blockTarget = std::optional<BlockInsertionTarget>();
		auto listTarget = std::optional<ListInsertionTarget>();
		if (const auto block = std::get_if<Markdown::PreparedEditBlockDropTarget>(
				&target)) {
			const auto container = candidate.convertBlockContainerPath(
				block->container);
			if (!container
				|| !candidate.blockContainer(*container)
				|| block->insertIndex < 0) {
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			blockTarget = {
				.container = *container,
				.insertIndex = block->insertIndex,
			};
		} else if (const auto list
			= std::get_if<Markdown::PreparedEditListItemDropTarget>(&target)) {
			const auto blockPath = candidate.convertBlockPath(list->block);
			const auto owner = blockPath ? candidate.block(*blockPath) : nullptr;
			if (!blockPath
				|| !owner
				|| owner->kind != BlockKind::List
				|| list->insertIndex < 0
				|| list->insertIndex > int(owner->listItems.size())) {
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			listTarget = {
				.block = *blockPath,
				.insertIndex = list->insertIndex,
			};
		} else {
			return CheckedMutationResult<StructuralSelectionDropResult>{
				.apply = false,
				.result = result,
			};
		}
		switch (selection.kind) {
		case PreparedEditSelectionKind::Blocks: {
			const auto range = candidate.validateBlockRange(selection.blocks);
			if (!range || !blockTarget) {
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			if (blockTarget->container == range->container) {
				if (blockTarget->insertIndex >= range->from
					&& blockTarget->insertIndex <= range->till) {
					result.result = ApplyResult::Unchanged;
					return CheckedMutationResult<StructuralSelectionDropResult>{
						.apply = false,
						.result = result,
					};
				} else if (blockTarget->insertIndex > range->till) {
					blockTarget->insertIndex -= (range->till - range->from);
				}
			}
			for (auto i = range->till; i != range->from;) {
				--i;
				const auto removed = BlockPath{
					.container = range->container,
					.index = i,
				};
				if (!ShiftBlockContainerPathAfterRemovedBlock(
						blockTarget->container,
						removed)) {
					result.result = ApplyResult::Unchanged;
					return CheckedMutationResult<StructuralSelectionDropResult>{
						.apply = false,
						.result = result,
					};
				}
			}
		} break;
		case PreparedEditSelectionKind::ListItems: {
			const auto range = candidate.validateListItemRange(
				selection.listItems);
			const auto owner = range ? candidate.block(range->block) : nullptr;
			if (!range || !owner || owner->kind != BlockKind::List) {
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			const auto removesWholeList = (range->from == 0)
				&& (range->till == int(owner->listItems.size()));
			if (blockTarget) {
				if (removesWholeList
					&& blockTarget->container == range->block.container
					&& blockTarget->insertIndex >= range->block.index
					&& blockTarget->insertIndex <= range->block.index + 1) {
					result.result = ApplyResult::Unchanged;
					return CheckedMutationResult<StructuralSelectionDropResult>{
						.apply = false,
						.result = result,
					};
				}
				if (removesWholeList) {
					if (blockTarget->container == range->block.container
						&& blockTarget->insertIndex > range->block.index) {
						--blockTarget->insertIndex;
					}
					if (!ShiftBlockContainerPathAfterRemovedBlock(
							blockTarget->container,
							range->block)) {
						result.result = ApplyResult::Unchanged;
						return CheckedMutationResult<StructuralSelectionDropResult>{
							.apply = false,
							.result = result,
						};
					}
				} else {
					for (auto i = range->till; i != range->from;) {
						--i;
						if (!ShiftBlockContainerPathAfterRemovedListItem(
								blockTarget->container,
								range->block,
								i)) {
							result.result = ApplyResult::Unchanged;
							return CheckedMutationResult<StructuralSelectionDropResult>{
								.apply = false,
								.result = result,
							};
						}
					}
				}
			} else if (listTarget) {
				if (listTarget->block == range->block) {
					if (listTarget->insertIndex >= range->from
						&& listTarget->insertIndex <= range->till) {
						result.result = ApplyResult::Unchanged;
						return CheckedMutationResult<StructuralSelectionDropResult>{
							.apply = false,
							.result = result,
						};
					} else if (listTarget->insertIndex > range->till) {
						listTarget->insertIndex -= (range->till - range->from);
					}
				} else if (removesWholeList) {
					if (!ShiftBlockPathAfterRemovedBlock(
							listTarget->block,
							range->block)) {
						result.result = ApplyResult::Unchanged;
						return CheckedMutationResult<StructuralSelectionDropResult>{
							.apply = false,
							.result = result,
						};
					}
				} else {
					for (auto i = range->till; i != range->from;) {
						--i;
						if (!ShiftBlockPathAfterRemovedListItem(
								listTarget->block,
								range->block,
								i)) {
							result.result = ApplyResult::Unchanged;
							return CheckedMutationResult<StructuralSelectionDropResult>{
								.apply = false,
								.result = result,
							};
						}
					}
				}
			} else {
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
		} break;
		case PreparedEditSelectionKind::TableRows:
		case PreparedEditSelectionKind::TableCells:
		case PreparedEditSelectionKind::None:
			return CheckedMutationResult<StructuralSelectionDropResult>{
				.apply = false,
				.result = result,
			};
		}
		if (!candidate.removeStructuralSelection(selection, true)) {
			return CheckedMutationResult<StructuralSelectionDropResult>{
				.apply = false,
				.result = result,
			};
		}
		if (const auto blocks = std::get_if<ClipboardBlockData>(&*payload)) {
			if (!blockTarget) {
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			auto inserted = blocks->blocks;
			NormalizeInsertedOrderedListMetadata(&inserted);
			candidate.normalizeInsertedBlockAnchors(inserted);
			const auto count = int(inserted.size());
			if (!candidate.insertPreparedBlocksAtExplicitPosition(
					std::move(inserted),
					blockTarget->container,
					&blockTarget->insertIndex)) {
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			candidate.rebuild();
			result.result = ApplyResult::Changed;
			result.destination = candidate.destinationTargetForInsertedBlocks(
				blockTarget->container,
				blockTarget->insertIndex,
				count);
			return CheckedMutationResult<StructuralSelectionDropResult>{
				.apply = true,
				.result = result,
			};
		}
		const auto items = std::get_if<ClipboardListItemsData>(&*payload);
		if (!items) {
			return CheckedMutationResult<StructuralSelectionDropResult>{
				.apply = false,
				.result = result,
			};
		}
		auto listBlock = Block();
		listBlock.kind = BlockKind::List;
		listBlock.listKind = items->listKind;
		listBlock.orderedList = items->orderedList;
		listBlock.listItems = items->items;
		auto insertedBlocks = std::vector<Block>();
		insertedBlocks.push_back(std::move(listBlock));
		NormalizeInsertedOrderedListMetadata(&insertedBlocks);
		candidate.normalizeInsertedBlockAnchors(insertedBlocks);
		if (listTarget) {
			const auto owner = candidate.block(listTarget->block);
			if (!owner || owner->kind != BlockKind::List) {
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			if (ListBlockMatchesClipboardData(*owner, *items)) {
				auto insertedItems = std::move(insertedBlocks.front().listItems);
				const auto count = int(insertedItems.size());
				if (!candidate.insertPreparedListItemsAtExplicitPosition(
						std::move(insertedItems),
						listTarget->block,
						listTarget->insertIndex)) {
					return CheckedMutationResult<StructuralSelectionDropResult>{
						.apply = false,
						.result = result,
					};
				}
				candidate.rebuild();
				result.result = ApplyResult::Changed;
				result.destination = candidate.destinationTargetForInsertedListItems(
					listTarget->block,
					listTarget->insertIndex,
					count);
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = true,
					.result = result,
				};
			}
			auto container = listTarget->block.container;
			auto insertAt = listTarget->block.index;
			auto trailingBlocks = std::vector<Block>();
			const auto splitLeadingStart = (owner->listKind == ListKind::Ordered
				&& listTarget->insertIndex > 0
				&& listTarget->insertIndex < int(owner->listItems.size()))
				? EffectiveOrderedItemValue(*owner, 0)
				: std::optional<int>();
			const auto splitTrailingStart = (owner->listKind == ListKind::Ordered
				&& listTarget->insertIndex > 0
				&& listTarget->insertIndex < int(owner->listItems.size()))
				? EffectiveOrderedItemValue(*owner, listTarget->insertIndex)
				: std::optional<int>();
			if (listTarget->insertIndex > 0) {
				insertAt = listTarget->block.index + 1;
				if (listTarget->insertIndex < int(owner->listItems.size())) {
					auto trailing = Block();
					trailing.kind = BlockKind::List;
					trailing.listKind = owner->listKind;
					trailing.orderedList = owner->orderedList;
					if (splitTrailingStart.has_value()) {
						trailing.orderedList.start = splitTrailingStart;
					}
					trailing.listItems = std::vector<ListItem>(
						std::make_move_iterator(
							owner->listItems.begin() + listTarget->insertIndex),
						std::make_move_iterator(owner->listItems.end()));
					owner->listItems.erase(
						owner->listItems.begin() + listTarget->insertIndex,
						owner->listItems.end());
					if (splitLeadingStart.has_value()) {
						owner->orderedList.start = splitLeadingStart;
					}
					trailingBlocks.push_back(std::move(trailing));
				}
			}
			NormalizeInsertedOrderedListMetadata(&trailingBlocks);
			if (!candidate.insertPreparedBlocksAtExplicitPosition(
					std::move(insertedBlocks),
					container,
					&insertAt)) {
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			auto trailingInsertAt = insertAt + 1;
			if (!trailingBlocks.empty()
				&& !candidate.insertPreparedBlocksAtExplicitPosition(
					std::move(trailingBlocks),
					container,
					&trailingInsertAt)) {
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			candidate.rebuild();
			result.result = ApplyResult::Changed;
			result.destination = candidate.destinationTargetForInsertedBlocks(
				container,
				insertAt,
				1);
			return CheckedMutationResult<StructuralSelectionDropResult>{
				.apply = true,
				.result = result,
			};
		}
		if (!blockTarget
			|| !candidate.insertPreparedBlocksAtExplicitPosition(
				std::move(insertedBlocks),
				blockTarget->container,
				&blockTarget->insertIndex)) {
			return CheckedMutationResult<StructuralSelectionDropResult>{
				.apply = false,
				.result = result,
			};
		}
		candidate.rebuild();
		result.result = ApplyResult::Changed;
		result.destination = candidate.destinationTargetForInsertedBlocks(
			blockTarget->container,
			blockTarget->insertIndex,
			1);
		return CheckedMutationResult<StructuralSelectionDropResult>{
			.apply = true,
			.result = result,
		};
	});
}

State::TextSelectionDropResult State::moveTextSelectionToDropTarget(
		const std::vector<TextNodeSpan> &source,
		const Markdown::PreparedEditDropTarget &target) {
	const auto failure = TextSelectionDropResult{
		.result = ApplyResult::Failed,
	};
	if (source.empty()) {
		return failure;
	}
	return applyCheckedMutation(failure, [source, target](State &candidate) {
		struct SourceRewrite {
			LeafPath leaf;
			TextWithEntities text;
		};

		auto result = TextSelectionDropResult{
			.result = ApplyResult::Failed,
		};
		auto moved = TextWithEntities();
		auto sourceRewrites = std::vector<SourceRewrite>();
		sourceRewrites.reserve(source.size());
		for (const auto &span : source) {
			const auto current = candidate.richText(span.leaf);
			if (!current) {
				return CheckedMutationResult<TextSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			auto sourceBefore = TextWithEntities();
			auto selected = TextWithEntities();
			auto sourceAfter = TextWithEntities();
			if (!SplitTextSpan(
					current->text,
					span.from,
					span.till,
					&sourceBefore,
					&selected,
					&sourceAfter)) {
				return CheckedMutationResult<TextSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			moved.append(std::move(selected));
			sourceRewrites.push_back(SourceRewrite{
				.leaf = span.leaf,
				.text = JoinText(
					std::move(sourceBefore),
					TextWithEntities(),
					std::move(sourceAfter)),
			});
		}
		const auto movedLength = int(moved.text.size());
		const auto applySourceRewrites = [&](std::vector<SourceRewrite> rewrites) {
			for (auto &rewrite : rewrites) {
				const auto current = candidate.richText(rewrite.leaf);
				if (!current) {
					return false;
				}
				current->text = std::move(rewrite.text);
			}
			return true;
		};
		const auto finishAtLeaf = [&](
				const LeafPath &leaf,
				int selectionFrom,
				int selectionTo) {
			candidate.rebuild();
			const auto ordinal = candidate.textOrdinalForLeafPath(leaf);
			if (ordinal < 0 || !candidate.setActiveTextByOrdinal(ordinal)) {
				candidate.ensureActiveTextOrdinal();
			}
			result.result = ApplyResult::Changed;
			result.destinationLeaf = leaf;
			result.selectionFrom = selectionFrom;
			result.selectionTo = selectionTo;
			return CheckedMutationResult<TextSelectionDropResult>{
				.apply = true,
				.result = result,
			};
		};
		if (const auto text = std::get_if<Markdown::PreparedEditTextDropTarget>(
				&target)) {
			const auto destinationLeaf = candidate.convertLeafPath(text->leaf);
			const auto destination = destinationLeaf
				? candidate.richText(*destinationLeaf)
				: nullptr;
			if (!destination
				|| (text->leaf.kind
					== Markdown::PreparedEditLeafKind::MathFormula)
				|| !RangeInsideText(destination->text.text, text->offset, 0)) {
				return CheckedMutationResult<TextSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			auto insertAt = text->offset;
			auto destinationRewrite = -1;
			for (auto i = 0, count = int(source.size()); i != count; ++i) {
				const auto &span = source[i];
				if (!(span.leaf == *destinationLeaf)) {
					continue;
				}
				if (text->offset >= span.from && text->offset <= span.till) {
					result.result = ApplyResult::Unchanged;
					return CheckedMutationResult<TextSelectionDropResult>{
						.apply = false,
						.result = result,
					};
				}
				if (span.from < insertAt) {
					insertAt -= std::min(span.till, insertAt) - span.from;
				}
				destinationRewrite = i;
			}
			const auto destinationText = (destinationRewrite >= 0)
				? sourceRewrites[destinationRewrite].text
				: destination->text;
			if (!RangeInsideText(destinationText.text, insertAt, 0)) {
				return CheckedMutationResult<TextSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			auto destinationBefore = Ui::Text::Mid(destinationText, 0, insertAt);
			auto destinationAfter = Ui::Text::Mid(destinationText, insertAt);
			auto updated = JoinText(
				std::move(destinationBefore),
				std::move(moved),
				std::move(destinationAfter));
			if (destinationRewrite >= 0) {
				sourceRewrites[destinationRewrite].text = std::move(updated);
			} else {
				destination->text = std::move(updated);
			}
			if (!applySourceRewrites(std::move(sourceRewrites))) {
				return CheckedMutationResult<TextSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			return finishAtLeaf(
				*destinationLeaf,
				insertAt,
				insertAt + movedLength);
		}
		const auto block = std::get_if<Markdown::PreparedEditBlockDropTarget>(
			&target);
		auto container = block
			? candidate.convertBlockContainerPath(block->container)
			: std::nullopt;
		const auto destination = container
			? candidate.blockContainer(*container)
			: nullptr;
		if (!block
			|| !destination
			|| block->insertIndex < 0) {
			return CheckedMutationResult<TextSelectionDropResult>{
				.apply = false,
				.result = result,
			};
		}
		const auto &sourceLeaf = source.front().leaf;
		const auto owner = (sourceLeaf.kind == LeafKind::BlockText)
			? candidate.block(sourceLeaf.block)
			: nullptr;
		const auto textOnly = owner && JoinableTextBlockKind(owner->kind);
		const auto removeSource = textOnly
			&& StringIsEmpty(sourceRewrites.front().text.text);
		auto insertIndex = block->insertIndex;
		if (removeSource) {
			const auto &sourcePath = sourceLeaf.block;
			if (*container == sourcePath.container) {
				if (insertIndex >= sourcePath.index
					&& insertIndex <= sourcePath.index + 1) {
					result.result = ApplyResult::Unchanged;
					return CheckedMutationResult<TextSelectionDropResult>{
						.apply = false,
						.result = result,
					};
				} else if (insertIndex > sourcePath.index) {
					--insertIndex;
				}
			}
			if (!ShiftBlockContainerPathAfterRemovedBlock(
					*container,
					sourcePath)) {
				result.result = ApplyResult::Unchanged;
				return CheckedMutationResult<TextSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
		}
		auto inserted = MakeParagraphBlock();
		if (textOnly) {
			inserted.kind = owner->kind;
			inserted.headingLevel = owner->headingLevel;
		}
		inserted.text.text = std::move(moved);
		auto blocks = std::vector<Block>();
		blocks.push_back(std::move(inserted));
		if (!applySourceRewrites(std::move(sourceRewrites))) {
			return CheckedMutationResult<TextSelectionDropResult>{
				.apply = false,
				.result = result,
			};
		}
		if (removeSource) {
			const auto sourceBlocks = candidate.blockContainer(
				sourceLeaf.block.container);
			if (!sourceBlocks
				|| sourceLeaf.block.index < 0
				|| sourceLeaf.block.index >= int(sourceBlocks->size())) {
				return CheckedMutationResult<TextSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			sourceBlocks->erase(
				sourceBlocks->begin() + sourceLeaf.block.index);
		}
		if (!candidate.insertPreparedBlocksAtExplicitPosition(
				std::move(blocks),
				*container,
				&insertIndex)) {
			return CheckedMutationResult<TextSelectionDropResult>{
				.apply = false,
				.result = result,
			};
		}
		return finishAtLeaf(
			LeafPath{
				.kind = LeafKind::BlockText,
				.block = BlockPath{
					.container = *container,
					.index = insertIndex,
				},
			},
			0,
			movedLength);
	});
}

State::TextSelectionDropResult State::moveTextSelectionToDropTarget(
		const TextNodeSpan &source,
		const Markdown::PreparedEditDropTarget &target) {
	return moveTextSelectionToDropTarget(
		std::vector<TextNodeSpan>{ source },
		target);
}

} // namespace Iv::Editor
