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

State::ApplyResult State::applyFormattingToTextSpans(
		const std::vector<TextNodeSpan> &spans,
		TextFormattingAction action,
		std::optional<bool> enabled) {
	if (spans.empty()) {
		return ApplyResult::Unchanged;
	}
	return applyCheckedMutation(ApplyResult::Failed, [
		spans,
		action,
		enabled
	](State &candidate) {
		const auto tag = FormattingActionTag(action);
		const auto shouldEnable = enabled.value_or([&] {
			if (!tag) {
				return false;
			}
			auto any = false;
			for (const auto &span : spans) {
				const auto current = candidate.richText(span.leaf);
				if (!current) {
					continue;
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
					continue;
				}
				any = true;
				if (!HasFullTextTag(
						ConvertRichTextToEditorTags(std::move(selected)).text,
						*tag)) {
					return true;
				}
			}
			return any ? false : true;
		}());
		auto changed = false;
		for (const auto &span : spans) {
			const auto current = candidate.richText(span.leaf);
			if (!current) {
				continue;
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
				if (action != TextFormattingAction::PlainText
					|| span.leaf.kind != LeafKind::BlockText
					|| !current->text.text.isEmpty()) {
					continue;
				}
			}
			auto converted = ConvertRichTextToEditorTags(std::move(selected));
			if (action == TextFormattingAction::PlainText) {
				converted.text.tags.clear();
			} else if (tag) {
				if (shouldEnable) {
					OverlayTag(
						&converted.text.tags,
						{
							.offset = 0,
							.length = int(converted.text.text.size()),
							.id = *tag,
						},
						converted.text.text);
				} else {
					RemoveTagFromSelection(&converted.text.tags, *tag);
				}
			}
			auto demoted = false;
			if (action == TextFormattingAction::PlainText
				&& span.leaf.kind == LeafKind::BlockText
				&& before.text.isEmpty()
				&& after.text.isEmpty()) {
				if (const auto owner = candidate.block(span.leaf.block);
					owner
					&& (owner->kind == BlockKind::Heading
						|| owner->kind == BlockKind::Footer)) {
					owner->kind = BlockKind::Paragraph;
					owner->headingLevel = 0;
					demoted = true;
				}
			}
			auto updated = JoinText(
				std::move(before),
				ConvertEditorTagsToRichText(std::move(converted.text)),
				std::move(after));
			if (current->text != updated) {
				current->text = std::move(updated);
				changed = true;
			}
			if (demoted) {
				changed = true;
			}
		}
		if (!changed) {
			return CheckedMutationResult<ApplyResult>{
				.result = ApplyResult::Unchanged,
			};
		}
		candidate.rebuild();
		return CheckedMutationResult<ApplyResult>{
			.apply = true,
			.result = ApplyResult::Changed,
		};
	});
}

bool State::toggleSpoilerOnBlocks(
		const std::vector<BlockPath> &blocks,
		std::optional<bool> enabled) {
	if (blocks.empty()) {
		return false;
	}
	return applyCheckedMutation(false, [blocks, enabled](State &candidate) {
		const auto shouldEnable = enabled.value_or([&] {
			auto any = false;
			for (const auto &path : blocks) {
				const auto current = candidate.block(path);
				if (!current || !MediaBlockSupportsSpoiler(*current)) {
					continue;
				}
				any = true;
				if (!MediaBlockHasSpoiler(*current)) {
					return true;
				}
			}
			return any ? false : true;
		}());
		auto changed = false;
		for (const auto &path : blocks) {
			const auto current = candidate.block(path);
			changed |= SetMediaBlockSpoiler(current, shouldEnable);
		}
		if (!changed) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		candidate.rebuild();
		return CheckedMutationResult<bool>{
			.apply = true,
			.result = true,
		};
	});
}

bool State::toggleSpoilerOnGroupedItem(
		const BlockPath &path,
		int itemIndex,
		std::optional<bool> enabled) {
	if (itemIndex < 0) {
		return false;
	}
	return applyCheckedMutation(false, [path, itemIndex, enabled](
			State &candidate) {
		const auto current = candidate.block(path);
		if (!current
			|| current->kind != BlockKind::GroupedMedia
			|| itemIndex >= int(current->mediaItems.size())) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto &item = current->mediaItems[itemIndex];
		if ((item.kind != BlockKind::Photo)
			&& (item.kind != BlockKind::Video)
			&& (item.kind != BlockKind::Audio)
			&& (item.kind != BlockKind::Map)) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		const auto shouldEnable = enabled.value_or(!item.spoiler);
		if (item.spoiler == shouldEnable) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		item.spoiler = shouldEnable;
		candidate.rebuild();
		return CheckedMutationResult<bool>{
			.apply = true,
			.result = true,
		};
	});
}

ApplyResult State::applySplitParagraphText(
		const TextNodeDescriptor &descriptor,
		std::vector<TextWithEntities> chunks) {
	if (chunks.empty()) {
		return applyActiveTextUnchecked(TextWithEntities());
	}
	const auto makeParagraph = [&](TextWithEntities text) {
		auto paragraph = MakeParagraphBlock();
		paragraph.text.text = std::move(text);
		return paragraph;
	};
	const auto focus = [&](LeafPath leaf) {
		rebuild();
		if (!activateRebuiltLeaf(leaf)) {
			ensureActiveTextOrdinal();
		}
	};
	if (descriptor.leaf.kind == LeafKind::BlockText) {
		const auto path = descriptor.leaf.block;
		if (auto owner = block(path)) {
			if (owner->kind == BlockKind::Paragraph) {
				auto container = blockContainer(path.container);
				if (!container
					|| path.index < 0
					|| path.index >= int(container->size())) {
					return ApplyResult::Failed;
				}
				clearTemporaryDownParagraph();
				owner->text.text = std::move(chunks.front());
				auto blocks = std::vector<Block>();
				blocks.reserve(chunks.size() - 1);
				for (auto i = 1; i != int(chunks.size()); ++i) {
					blocks.push_back(makeParagraph(std::move(chunks[i])));
				}
				container->insert(
					container->begin() + path.index + 1,
					std::make_move_iterator(blocks.begin()),
					std::make_move_iterator(blocks.end()));
				focus(descriptor.leaf);
				return ApplyResult::Changed;
			} else if (owner->kind == BlockKind::Quote && !owner->pullquote) {
				clearTemporaryDownParagraph();
				auto firstText = std::move(owner->text);
				firstText.text = std::move(chunks.front());
				auto blocks = std::vector<Block>();
				blocks.reserve(chunks.size());
				auto first = MakeParagraphBlock();
				first.text = std::move(firstText);
				blocks.push_back(std::move(first));
				for (auto i = 1; i != int(chunks.size()); ++i) {
					blocks.push_back(makeParagraph(std::move(chunks[i])));
				}
				owner->text = RichText();
				owner->blocks.insert(
					owner->blocks.begin(),
					std::make_move_iterator(blocks.begin()),
					std::make_move_iterator(blocks.end()));
				focus({
					.kind = LeafKind::BlockText,
					.block = {
						.container = BlockChildrenContainer(path),
						.index = 0,
					},
				});
				return ApplyResult::Changed;
			}
		}
	} else if (descriptor.leaf.kind == LeafKind::ListItemText) {
		const auto path = descriptor.leaf.block;
		if (auto item = listItem(path, descriptor.leaf.listItemIndex)) {
			clearTemporaryDownParagraph();
			auto firstText = std::move(item->text);
			firstText.text = std::move(chunks.front());
			auto blocks = std::vector<Block>();
			blocks.reserve(chunks.size());
			auto first = MakeParagraphBlock();
			first.anchorId = std::move(item->anchorId);
			first.text = std::move(firstText);
			blocks.push_back(std::move(first));
			for (auto i = 1; i != int(chunks.size()); ++i) {
				blocks.push_back(makeParagraph(std::move(chunks[i])));
			}
			item->anchorId.clear();
			item->text = RichText();
			item->blocks.insert(
				item->blocks.begin(),
				std::make_move_iterator(blocks.begin()),
				std::make_move_iterator(blocks.end()));
			focus({
				.kind = LeafKind::BlockText,
				.block = {
					.container = ListItemChildrenContainer(
						path,
						descriptor.leaf.listItemIndex),
					.index = 0,
				},
			});
			return ApplyResult::Changed;
		}
	}
	return ApplyResult::Failed;
}

std::optional<RichMessageLimitError> State::lastLimitError() const {
	return _lastLimitError;
}

std::optional<QString> State::codeBlockLanguage(int ordinal) const {
	const auto descriptor = textNode(ordinal);
	if (!descriptor || descriptor->leaf.kind != LeafKind::BlockText) {
		return std::nullopt;
	}
	const auto owner = block(descriptor->leaf.block);
	return (owner && owner->kind == BlockKind::Code)
		? std::make_optional(owner->language)
		: std::nullopt;
}

bool State::setCodeBlockLanguage(int ordinal, QString language) {
	const auto descriptor = textNode(ordinal);
	if (!descriptor || descriptor->leaf.kind != LeafKind::BlockText) {
		return false;
	}
	auto owner = block(descriptor->leaf.block);
	if (!owner || owner->kind != BlockKind::Code) {
		return false;
	}
	owner->language = std::move(language).trimmed();
	rebuild();
	return true;
}

bool State::toggleTaskState(
		const Markdown::PreparedEditListItemSource &source) {
	const auto blockPath = convertBlockPath(source.block);
	if (!blockPath) {
		return false;
	}
	auto item = listItem(*blockPath, source.listItemIndex);
	if (!item || item->taskState == TaskState::None) {
		return false;
	}
	item->taskState = (item->taskState == TaskState::Unchecked)
		? TaskState::Checked
		: TaskState::Unchecked;
	rebuild();
	return true;
}

bool State::toggleDetailsOpen(
		const Markdown::PreparedEditBlockSource &source) {
	const auto path = convertBlockPath(source);
	if (!path) {
		return false;
	}
	auto owner = block(*path);
	if (!owner || owner->kind != BlockKind::Details) {
		return false;
	}
	owner->open = !owner->open;
	rebuild();
	return true;
}

bool State::toggleQuoteCollapsed(
		const Markdown::PreparedEditBlockSource &source,
		bool *movedCaret) {
	const auto path = convertBlockPath(source);
	if (!path) {
		return false;
	}
	auto owner = block(*path);
	if (!owner || !RichBlockquoteIsCollapsible(*owner)) {
		return false;
	}
	const auto activeLeaf = activeLeafPath();
	auto quoteOrdinal = textNodeCount();
	for (auto i = 0, count = textNodeCount(); i != count; ++i) {
		if (_textNodes[i].leaf.block == *path) {
			quoteOrdinal = i;
			break;
		}
	}
	owner->collapsed = !owner->collapsed;
	rebuild();
	if (activeLeaf
		&& (textOrdinalForLeafPath(*activeLeaf) >= 0)
		&& activateRebuiltLeaf(*activeLeaf)) {
		return true;
	}
	*movedCaret = true;
	const auto nearest = std::min(quoteOrdinal, textNodeCount() - 1);
	if (!setActiveTextByOrdinal(nearest)) {
		ensureActiveTextOrdinal();
	}
	return true;
}

State::ActiveTextBlockActionResult State::applyActiveTextBlockAction(
		InsertAction action,
		ActiveTextInsertContext context) {
	return applyCheckedMutation(ActiveTextBlockActionResult{
		.result = ApplyResult::Failed,
	}, [action, context = std::move(context)](State &candidate) mutable {
		ActiveTextSelectionTarget target;
		ActiveTextBlockActionResult result{
			.result = ApplyResult::Failed,
		};
		const auto changed = [&] {
			result = {
				.result = ApplyResult::Changed,
				.destinationLeaf = target.leaf,
				.selectionFrom = target.selectionFrom,
				.selectionTo = target.selectionTo,
			};
			return CheckedMutationResult<ActiveTextBlockActionResult>{
				.apply = true,
				.result = result,
			};
		};
		if (action.type == InsertBlockType::Blockquote
			|| action.type == InsertBlockType::Pullquote) {
			const auto pullquote = (action.type == InsertBlockType::Pullquote);
			if (candidate.activeQuote(pullquote)) {
				if (candidate.unwrapActiveQuoteUnchecked(
						pullquote,
						context,
						&target)) {
					return changed();
				}
				return CheckedMutationResult<ActiveTextBlockActionResult>{
					.result = result,
				};
			}
		}
		if (action.type == InsertBlockType::Code
			&& candidate.unwrapActiveCodeBlockUnchecked(context, &target)) {
			return changed();
		}
		if ((action.type == InsertBlockType::Heading
				|| action.type == InsertBlockType::Footer)
			&& candidate.convertActiveHeadingOrFooterUnchecked(
				action,
				context,
				&target)) {
			return changed();
		}
		const auto hadSelection = !context.selected.text.isEmpty();
		const auto beforeSize = int(context.before.text.size());
		const auto lineStart = hadSelection
			? -1
			: int(context.before.text.lastIndexOf('\n'));
		ExpandInsertContextToActiveLine(context);
		const auto cursorInLine = hadSelection
			? 0
			: (beforeSize - (lineStart + 1));
		const auto selectedSize = int(context.selected.text.size());
		auto blocks = std::vector<Block>();
		auto made = candidate.makeBlock(action);
		auto lines = IsListInsertType(action.type)
			? SplitTextIntoLines(context.selected)
			: std::vector<TextWithEntities>();
		if ((lines.size() > 1)
			&& (made.kind == BlockKind::List)
			&& (made.listItems.size() == 1)) {
			const auto sample = made.listItems.front();
			made.listItems.clear();
			for (auto &line : lines) {
				auto item = sample;
				item.text.text = std::move(line);
				made.listItems.push_back(std::move(item));
			}
			context.selected = TextWithEntities();

			if (context.before.text.endsWith('\n')) {
				context.before = Ui::Text::Mid(
					context.before,
					0,
					int(context.before.text.size()) - 1);
			}
			if (context.after.text.startsWith('\n')) {
				context.after = Ui::Text::Mid(context.after, 1);
			}
		}
		blocks.push_back(std::move(made));
		const auto applied = candidate.insertBlocksAfterActiveUnchecked(
			std::move(blocks),
			context);
		if (!applied) {
			return CheckedMutationResult<ActiveTextBlockActionResult>{
				.result = result,
			};
		}
		if (IsListInsertType(action.type)) {
			candidate.joinInsertedListWithSiblings(
				action.orderedStartExplicit);
		}
		const auto descriptor = candidate.textNode(candidate._activeTextOrdinal);
		if (!descriptor) {
			return CheckedMutationResult<ActiveTextBlockActionResult>{
				.result = result,
			};
		}
		return CheckedMutationResult<ActiveTextBlockActionResult>{
			.apply = true,
			.result = {
				.result = ApplyResult::Changed,
				.destinationLeaf = descriptor->leaf,
				.selectionFrom = (hadSelection ? 0 : cursorInLine),
				.selectionTo = (hadSelection ? selectedSize : cursorInLine),
			},
		};
	});
}

State::ActiveTextBlockActionResult State::replaceActiveTextSelectionWithText(
		TextWithEntities text,
		ActiveTextInsertContext context) {
	return applyCheckedMutation(ActiveTextBlockActionResult{
		.result = ApplyResult::Failed,
	}, [text = std::move(text), context = std::move(context)](
			State &candidate) mutable {
		const auto failed = [&] {
			return CheckedMutationResult<ActiveTextBlockActionResult>{
				.result = ActiveTextBlockActionResult{
					.result = ApplyResult::Failed,
				},
			};
		};
		if (text.text.isEmpty()) {
			return failed();
		}
		const auto descriptor = candidate.textNode(
			candidate._activeTextOrdinal);
		if (!descriptor || (descriptor->leaf.kind == LeafKind::MathFormula)) {
			return failed();
		}
		const auto selectionFrom = int(context.before.text.size());
		const auto selectionTo = selectionFrom + int(text.text.size());
		const auto applied = candidate.applyActiveTextUnchecked(JoinText(
			std::move(context.before),
			std::move(text),
			std::move(context.after)));
		if (applied == ApplyResult::Failed) {
			return failed();
		}
		const auto updated = candidate.textNode(candidate._activeTextOrdinal);
		return CheckedMutationResult<ActiveTextBlockActionResult>{
			.apply = true,
			.result = {
				.result = ApplyResult::Changed,
				.destinationLeaf = (updated
					? updated->leaf
					: descriptor->leaf),
				.selectionFrom = selectionFrom,
				.selectionTo = selectionTo,
			},
		};
	});
}

bool State::insertBlockAfterActive(
		InsertAction action,
		std::optional<ActiveTextInsertContext> context) {
	return applyCheckedMutation(false, [action, context = std::move(context)](
			State &candidate) mutable {
		if (context && candidate.blockActionExpandsToActiveLine(action.type)) {
			ExpandInsertContextToActiveLine(*context);
		}
		auto blocks = std::vector<Block>();
		blocks.push_back(candidate.makeBlock(action));
		if (action.type == InsertBlockType::Divider) {
			// A divider has no editable content, so insert a paragraph
			// together with it: the selected text piece (if any) seeds into
			// the paragraph and focus lands there, keeping an editable spot
			// below the divider while the block above stays editable too.
			blocks.push_back(MakeParagraphBlock());
			if (context) {
				// Treat it as a paragraph split with a divider in between:
				// everything from the cursor on moves into the paragraph
				// below the divider instead of a separate trailing one.
				context->selected = JoinText(
					std::move(context->selected),
					std::move(context->after),
					{});
				context->after = {};
			}
		}
		const auto applied = candidate.insertBlocksAfterActiveUnchecked(
			std::move(blocks),
			std::move(context));
		if (applied && IsListInsertType(action.type)) {
			candidate.joinInsertedListWithSiblings(
				action.orderedStartExplicit);
		}
		return CheckedMutationResult<bool>{
			.apply = applied,
			.result = applied,
		};
	});
}

bool State::insertPreparedBlocksAfterActive(
		std::vector<Block> blocks,
		std::optional<ActiveTextInsertContext> context) {
	return applyCheckedMutation(false, [
			blocks = std::move(blocks),
			context = std::move(context)](State &candidate) mutable {
		const auto applied = candidate.insertBlocksAfterActiveUnchecked(
			std::move(blocks),
			std::move(context));
		return CheckedMutationResult<bool>{
			.apply = applied,
			.result = applied,
		};
	});
}

State::DisplayMathEditResult State::editActiveDisplayMath(
		QString source,
		bool separateLine) {
	auto failure = DisplayMathEditResult{
		.result = ApplyResult::Failed,
	};
	return applyCheckedMutation(failure, [
			source = std::move(source),
			separateLine](State &candidate) mutable {
		const auto descriptor = candidate.textNode(candidate._activeTextOrdinal);
		if (!descriptor || descriptor->leaf.kind != LeafKind::MathFormula) {
			return CheckedMutationResult<DisplayMathEditResult>{
				.result = {
					.result = ApplyResult::Failed,
				},
			};
		}
		const auto leaf = descriptor->leaf;
		auto *blocks = candidate.blockContainer(leaf.block.container);
		if (!blocks
			|| leaf.block.index < 0
			|| leaf.block.index >= int(blocks->size())) {
			return CheckedMutationResult<DisplayMathEditResult>{
				.result = {
					.result = ApplyResult::Failed,
				},
			};
		}
		auto &math = (*blocks)[leaf.block.index];
		if (math.kind != BlockKind::Math) {
			return CheckedMutationResult<DisplayMathEditResult>{
				.result = {
					.result = ApplyResult::Failed,
				},
			};
		}
		if (separateLine) {
			if (math.formula == source) {
				return CheckedMutationResult<DisplayMathEditResult>{
					.result = {
						.result = ApplyResult::Unchanged,
					},
				};
			}
			math.formula = std::move(source);
			candidate.rebuild();
			if (!candidate.activateRebuiltLeaf(leaf)) {
				return CheckedMutationResult<DisplayMathEditResult>{
					.result = {
						.result = ApplyResult::Failed,
					},
				};
			}
			return CheckedMutationResult<DisplayMathEditResult>{
				.apply = true,
				.result = {
					.result = ApplyResult::Changed,
				},
			};
		}
		auto inlineMath = FormulaSourceToRichText(std::move(source));
		const auto mathIndex = leaf.block.index;
		const auto previousIndex = mathIndex - 1;
		const auto nextIndex = mathIndex + 1;
		const auto paragraphAt = [&](int index) -> Block* {
			return (index >= 0
				&& index < int(blocks->size())
				&& (*blocks)[index].kind == BlockKind::Paragraph)
				? &(*blocks)[index]
				: nullptr;
		};
		auto *previous = paragraphAt(previousIndex);
		auto *next = paragraphAt(nextIndex);
		const auto previousHasText = previous
			&& RichTextHasVisibleText(previous->text);
		const auto nextHasText = next
			&& RichTextHasVisibleText(next->text);
		enum class Target {
			Previous,
			Next,
			Replace,
		};
		auto target = Target::Replace;
		auto removePrevious = false;
		auto removeNext = false;
		if (previousHasText) {
			target = Target::Previous;
			removeNext = (next != nullptr);
		} else if (nextHasText) {
			target = Target::Next;
			removePrevious = (previous != nullptr);
		} else if (previous) {
			target = Target::Previous;
			removeNext = (next != nullptr);
		} else if (next) {
			target = Target::Next;
		}
		auto inlineLeaf = LeafPath{
			.kind = LeafKind::BlockText,
			.block = {
				.container = leaf.block.container,
				.index = mathIndex,
			},
		};
		auto selectionFrom = 0;
		auto selectionTo = 0;
		switch (target) {
		case Target::Previous: {
			auto updated = std::move(previous->text.text);
			if (previousHasText) {
				updated.append(' ');
			}
			selectionFrom = int(updated.text.size());
			updated.append(std::move(inlineMath));
			selectionTo = int(updated.text.size());
			if (next) {
				if (nextHasText) {
					updated.append(' ');
				}
				updated.append(next->text.text);
			}
			previous->text.text = std::move(updated);
			if (next) {
				MergeRichTextAnchors(&previous->text, std::move(next->text));
			}
			if (removeNext) {
				blocks->erase(blocks->begin() + nextIndex);
			}
			blocks->erase(blocks->begin() + mathIndex);
			inlineLeaf.block.index = previousIndex;
		} break;
		case Target::Next: {
			auto updated = TextWithEntities();
			auto nextText = std::move(next->text.text);
			updated.append(std::move(inlineMath));
			selectionFrom = 0;
			selectionTo = updated.text.size();
			if (nextHasText) {
				updated.append(' ');
			}
			updated.append(std::move(nextText));
			next->text.text = std::move(updated);
			if (previous && removePrevious) {
				MergeRichTextAnchors(&next->text, std::move(previous->text));
			}
			blocks->erase(blocks->begin() + mathIndex);
			inlineLeaf.block.index = mathIndex;
			if (removePrevious) {
				blocks->erase(blocks->begin() + previousIndex);
				inlineLeaf.block.index = previousIndex;
			}
		} break;
		case Target::Replace: {
			auto paragraph = MakeParagraphBlock();
			paragraph.text.text = std::move(inlineMath);
			selectionTo = paragraph.text.text.text.size();
			(*blocks)[mathIndex] = std::move(paragraph);
		} break;
		}
		candidate.rebuild();
		if (!candidate.activateRebuiltLeaf(inlineLeaf)) {
			return CheckedMutationResult<DisplayMathEditResult>{
				.result = {
					.result = ApplyResult::Failed,
				},
			};
		}
		return CheckedMutationResult<DisplayMathEditResult>{
			.apply = true,
			.result = {
				.result = ApplyResult::Changed,
				.inlineLeaf = inlineLeaf,
				.selectionFrom = selectionFrom,
				.selectionTo = selectionTo,
			},
		};
	});
}

} // namespace Iv::Editor
