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

std::vector<Block> *State::blockContainer(const BlockContainerPath &path) {
	auto *blocks = &_richPage->blocks;
	for (const auto &step : path.steps) {
		if (step.blockIndex < 0 || step.blockIndex >= int(blocks->size())) {
			return nullptr;
		}
		auto &parent = (*blocks)[step.blockIndex];
		if (step.kind == BlockContainerKind::BlockChildren) {
			blocks = &parent.blocks;
		} else if (step.kind == BlockContainerKind::ListItemChildren) {
			if (step.listItemIndex < 0
				|| step.listItemIndex >= int(parent.listItems.size())) {
				return nullptr;
			}
			blocks = &parent.listItems[step.listItemIndex].blocks;
		} else {
			return nullptr;
		}
	}
	return blocks;
}

const std::vector<Block> *State::blockContainer(
		const BlockContainerPath &path) const {
	const auto *blocks = &_richPage->blocks;
	for (const auto &step : path.steps) {
		if (step.blockIndex < 0 || step.blockIndex >= int(blocks->size())) {
			return nullptr;
		}
		const auto &parent = (*blocks)[step.blockIndex];
		if (step.kind == BlockContainerKind::BlockChildren) {
			blocks = &parent.blocks;
		} else if (step.kind == BlockContainerKind::ListItemChildren) {
			if (step.listItemIndex < 0
				|| step.listItemIndex >= int(parent.listItems.size())) {
				return nullptr;
			}
			blocks = &parent.listItems[step.listItemIndex].blocks;
		} else {
			return nullptr;
		}
	}
	return blocks;
}

Block *State::block(const BlockPath &path) {
	const auto blocks = blockContainer(path.container);
	if (!blocks || path.index < 0 || path.index >= int(blocks->size())) {
		return nullptr;
	}
	return &(*blocks)[path.index];
}

const Block *State::block(const BlockPath &path) const {
	const auto blocks = blockContainer(path.container);
	if (!blocks || path.index < 0 || path.index >= int(blocks->size())) {
		return nullptr;
	}
	return &(*blocks)[path.index];
}

ListItem *State::listItem(const BlockPath &blockPath, int itemIndex) {
	const auto list = block(blockPath);
	if (!list
		|| itemIndex < 0
		|| itemIndex >= int(list->listItems.size())) {
		return nullptr;
	}
	return &list->listItems[itemIndex];
}

const ListItem *State::listItem(
		const BlockPath &blockPath,
		int itemIndex) const {
	const auto list = block(blockPath);
	if (!list
		|| itemIndex < 0
		|| itemIndex >= int(list->listItems.size())) {
		return nullptr;
	}
	return &list->listItems[itemIndex];
}

TableCell *State::tableCell(
		const BlockPath &blockPath,
		int rowIndex,
		int cellIndex) {
	const auto table = block(blockPath);
	if (!table
		|| rowIndex < 0
		|| rowIndex >= int(table->tableRows.size())) {
		return nullptr;
	}
	auto &row = table->tableRows[rowIndex];
	if (cellIndex < 0 || cellIndex >= int(row.cells.size())) {
		return nullptr;
	}
	return &row.cells[cellIndex];
}

const TableCell *State::tableCell(
		const BlockPath &blockPath,
		int rowIndex,
		int cellIndex) const {
	const auto table = block(blockPath);
	if (!table
		|| rowIndex < 0
		|| rowIndex >= int(table->tableRows.size())) {
		return nullptr;
	}
	const auto &row = table->tableRows[rowIndex];
	if (cellIndex < 0 || cellIndex >= int(row.cells.size())) {
		return nullptr;
	}
	return &row.cells[cellIndex];
}

RichText *State::richText(const LeafPath &path) {
	switch (path.kind) {
	case LeafKind::BlockText:
		if (const auto owner = block(path.block)) {
			return &owner->text;
		}
		return nullptr;
	case LeafKind::BlockCaption:
		if (const auto owner = block(path.block)) {
			return &owner->caption;
		}
		return nullptr;
	case LeafKind::ListItemText:
		if (const auto item = listItem(path.block, path.listItemIndex)) {
			return &item->text;
		}
		return nullptr;
	case LeafKind::TableCellText:
		if (const auto cell = tableCell(
				path.block,
				path.tableRowIndex,
				path.tableCellIndex)) {
			return &cell->text;
		}
		return nullptr;
	case LeafKind::MathFormula:
		return nullptr;
	}
	return nullptr;
}

const RichText *State::richText(const LeafPath &path) const {
	switch (path.kind) {
	case LeafKind::BlockText:
		if (const auto owner = block(path.block)) {
			return &owner->text;
		}
		return nullptr;
	case LeafKind::BlockCaption:
		if (const auto owner = block(path.block)) {
			return &owner->caption;
		}
		return nullptr;
	case LeafKind::ListItemText:
		if (const auto item = listItem(path.block, path.listItemIndex)) {
			return &item->text;
		}
		return nullptr;
	case LeafKind::TableCellText:
		if (const auto cell = tableCell(
				path.block,
				path.tableRowIndex,
				path.tableCellIndex)) {
			return &cell->text;
		}
		return nullptr;
	case LeafKind::MathFormula:
		return nullptr;
	}
	return nullptr;
}

QString *State::rawText(const LeafPath &path) {
	if (path.kind != LeafKind::MathFormula) {
		return nullptr;
	}
	const auto owner = block(path.block);
	return owner ? &owner->formula : nullptr;
}

const QString *State::rawText(const LeafPath &path) const {
	if (path.kind != LeafKind::MathFormula) {
		return nullptr;
	}
	const auto owner = block(path.block);
	return owner ? &owner->formula : nullptr;
}

const TextNodeDescriptor *State::textNode(int ordinal) const {
	return (ordinal >= 0 && ordinal < textNodeCount())
		? &_textNodes[ordinal]
		: nullptr;
}

int State::textNodeOrdinal(const LeafPath &path) const {
	for (auto i = 0, count = textNodeCount(); i != count; ++i) {
		if (_textNodes[i].leaf == path) {
			return i;
		}
	}
	return -1;
}

bool State::leafMutationKeepsTextNodes(
		const TextNodeDescriptor &descriptor) const {
	switch (descriptor.leaf.kind) {
	case LeafKind::BlockText:
		if (const auto owner = block(descriptor.leaf.block)) {
			switch (owner->kind) {
			case BlockKind::Heading:
			case BlockKind::Paragraph:
			case BlockKind::Footer:
			case BlockKind::Code:
			case BlockKind::Details:
				return true;
			case BlockKind::Quote:
				return owner->blocks.empty();
			case BlockKind::Table:
				return !owner->text.text.text.isEmpty();
			default:
				return false;
			}
		}
		return false;
	case LeafKind::BlockCaption:
	case LeafKind::TableCellText:
	case LeafKind::MathFormula:
		return true;
	case LeafKind::ListItemText:
		if (const auto item = listItem(
				descriptor.leaf.block,
				descriptor.leaf.listItemIndex)) {
			return !RichTextIsEmpty(item->text) || item->blocks.empty();
		}
		return false;
	}
	return false;
}

bool State::updatePreparedActiveLeaf(
		const TextNodeDescriptor &descriptor) {
	if (DetermineRichPageRtl(*_richPage) != _richPage->rtl) {
		return false;
	}
	const auto source = convertPreparedLeafSource(descriptor);
	if (!source) {
		return false;
	}
	return (Markdown::UpdatePreparedNativeInstantViewLeaf(
		&_prepared,
		*_richPage,
		*source,
		tableRenderLimits()) == NativeInstantViewLeafUpdateResult::Updated);
}

void State::rebuild() {
	_lastPreparedMutationKind = PreparedMutationKind::FullRebuild;
	rebuildTextNodes();
	ensureEditableNodes();
	ensureActiveTextOrdinal();
	clearTemporaryDownParagraphIfInvalid();
	rebuildPrepared();
}

void State::rebuildPrepared() {
	_lastPreparedMutationKind = PreparedMutationKind::FullRebuild;
	_richPage->rtl = DetermineRichPageRtl(*_richPage);
	_prepared = Markdown::TryPrepareNativeInstantView({
		.richPage = _richPage,
		.mediaRuntime = _mediaRuntime,
		.tableRenderLimits = tableRenderLimits(),
		.editMode = true,
	}).content;
}

void State::rebuildTextNodes() {
	_textNodes.clear();
	_textNodes.reserve(_richPage->blocks.size() * 3);
	rebuildTextNodes(_richPage->blocks, BlockContainerPath());
}

void State::rebuildTextNodes(
		const std::vector<Block> &blocks,
		const BlockContainerPath &container) {
	for (auto i = 0, count = int(blocks.size()); i != count; ++i) {
		const auto path = BlockPath{
			.container = container,
			.index = i,
		};
		const auto &block = blocks[i];
		switch (block.kind) {
		case BlockKind::Heading:
		case BlockKind::Paragraph:
		case BlockKind::Footer:
		case BlockKind::Code:
			appendBlockTextNode(path, LeafKind::BlockText);
			break;
		case BlockKind::Quote:
			if (block.collapsed && RichBlockquoteIsCollapsible(block)) {
				break;
			}
			if (block.blocks.empty()) {
				appendBlockTextNode(
					path,
					LeafKind::BlockText,
					FieldMode::Rich,
					!block.pullquote
						? std::make_optional(InsertionAnchor{
							.container = BlockChildrenContainer(path),
							.blockIndex = -1,
						})
						: std::nullopt);
			}
			rebuildTextNodes(block.blocks, BlockChildrenContainer(path));
			appendBlockTextNode(path, LeafKind::BlockCaption);
			break;
		case BlockKind::List:
			for (auto j = 0, itemCount = int(block.listItems.size());
					j != itemCount;
					++j) {
				const auto &item = block.listItems[j];
				if (!RichTextIsEmpty(item.text) || item.blocks.empty()) {
					appendListItemTextNode(path, j);
				}
				if (!item.blocks.empty()) {
					rebuildTextNodes(
						item.blocks,
						ListItemChildrenContainer(path, j));
				}
			}
			break;
		case BlockKind::Photo:
		case BlockKind::Video:
		case BlockKind::Audio:
		case BlockKind::File:
		case BlockKind::Map:
		case BlockKind::GroupedMedia:
			appendBlockTextNode(path, LeafKind::BlockCaption);
			break;
		case BlockKind::Math:
			appendBlockTextNode(path, LeafKind::MathFormula, FieldMode::Raw);
			break;
		case BlockKind::Table:
			appendBlockTextNode(path, LeafKind::BlockText);
			for (auto j = 0, rowCount = int(block.tableRows.size());
					j != rowCount;
					++j) {
				const auto &row = block.tableRows[j];
				for (auto k = 0, cellCount = int(row.cells.size());
						k != cellCount;
						++k) {
					appendTableCellTextNode(path, j, k);
				}
			}
			break;
		case BlockKind::Details:
			appendBlockTextNode(
				path,
				LeafKind::BlockText,
				FieldMode::Rich,
				InsertionAnchor{
					.container = BlockChildrenContainer(path),
					.blockIndex = -1,
				});
			rebuildTextNodes(block.blocks, BlockChildrenContainer(path));
			break;
		default:
			rebuildTextNodes(block.blocks, BlockChildrenContainer(path));
			break;
		}
	}
}

bool State::lastBlockOwnsLastTextNode() const {
	if (_textNodes.empty()) {
		return false;
	}
	const auto path = BlockPath{
		.container = BlockContainerPath(),
		.index = int(_richPage->blocks.size()) - 1,
	};
	return descriptorBelongsToBlock(_textNodes.back(), path);
}

void State::appendBlockTextNode(
		const BlockPath &path,
		LeafKind kind,
		FieldMode mode,
		std::optional<InsertionAnchor> insertionAnchor) {
	_textNodes.push_back({
		.leaf = {
			.kind = kind,
			.block = path,
		},
		.insertionAnchor = insertionAnchor.value_or(InsertionAnchor{
			.container = path.container,
			.blockIndex = path.index,
		}),
		.removalTarget = {
			.kind = RemovalKind::Block,
			.block = path,
		},
		.mode = mode,
	});
}

void State::appendListItemTextNode(const BlockPath &path, int itemIndex) {
	_textNodes.push_back({
		.leaf = {
			.kind = LeafKind::ListItemText,
			.block = path,
			.listItemIndex = itemIndex,
		},
		.insertionAnchor = {
			.container = ListItemChildrenContainer(path, itemIndex),
			.blockIndex = -1,
		},
		.removalTarget = {
			.kind = RemovalKind::ListItem,
			.block = path,
			.listItemIndex = itemIndex,
		},
		.mode = FieldMode::Rich,
	});
}

void State::appendTableCellTextNode(
		const BlockPath &path,
		int rowIndex,
		int cellIndex) {
	_textNodes.push_back({
		.leaf = {
			.kind = LeafKind::TableCellText,
			.block = path,
			.tableRowIndex = rowIndex,
			.tableCellIndex = cellIndex,
		},
		.insertionAnchor = {
			.container = path.container,
			.blockIndex = path.index,
		},
		.removalTarget = {
			.kind = RemovalKind::TableCell,
			.block = path,
			.tableRowIndex = rowIndex,
			.tableCellIndex = cellIndex,
		},
		.mode = FieldMode::Rich,
	});
}

void State::ensureActiveTextOrdinal() {
	if (_textNodes.empty()) {
		_activeTextOrdinal = -1;
	} else if (_activeTextOrdinal < 0 || _activeTextOrdinal >= textNodeCount()) {
		_activeTextOrdinal = 0;
	}
}

void State::ensureEditableNodes() {
	if (!_textNodes.empty()) {
		return;
	}
	_richPage->blocks.push_back(MakeParagraphBlock());
	rebuildTextNodes();
}

void State::normalizeInsertedBlockAnchors(std::vector<Block> &blocks) {
	for (auto &block : blocks) {
		normalizeInsertedBlockAnchors(blocks, block);
	}
}

void State::normalizeInsertedBlockAnchors(
		std::vector<Block> &root,
		Block &block) {
	const auto normalize = [&](QString &id) {
		if (id.isEmpty()) {
			return;
		}
		const auto base = id;
		id.clear();
		auto candidate = base;
		for (auto suffix = 2;
			anchorIdExists(candidate) || anchorIdExists(root, candidate);
			++suffix) {
			candidate = base + u"-%1"_q.arg(suffix);
		}
		id = std::move(candidate);
	};
	normalize(block.anchorId);
	normalizeInsertedRichTextAnchors(root, block.text);
	normalizeInsertedRichTextAnchors(root, block.caption);
	for (auto &child : block.blocks) {
		normalizeInsertedBlockAnchors(root, child);
	}
	for (auto &item : block.listItems) {
		normalize(item.anchorId);
		normalizeInsertedRichTextAnchors(root, item.text);
		for (auto &child : item.blocks) {
			normalizeInsertedBlockAnchors(root, child);
		}
	}
	for (auto &row : block.tableRows) {
		for (auto &cell : row.cells) {
			normalizeInsertedRichTextAnchors(root, cell.text);
		}
	}
}

void State::normalizeInsertedRichTextAnchors(
		std::vector<Block> &root,
		RichText &text) {
	const auto normalize = [&](QString &id) {
		if (id.isEmpty()) {
			return;
		}
		const auto base = id;
		id.clear();
		auto candidate = base;
		for (auto suffix = 2;
			anchorIdExists(candidate) || anchorIdExists(root, candidate);
			++suffix) {
			candidate = base + u"-%1"_q.arg(suffix);
		}
		id = std::move(candidate);
	};
	normalize(text.anchorId);
	for (auto &id : text.anchorIds) {
		normalize(id);
	}
}

bool State::anchorIdExists(const QString &id) const {
	return anchorIdExists(_richPage->blocks, id);
}

bool State::anchorIdExists(
		const std::vector<Block> &blocks,
		const QString &id) const {
	for (const auto &block : blocks) {
		if (block.anchorId == id
			|| block.text.anchorId == id
			|| block.caption.anchorId == id
			|| ranges::contains(block.text.anchorIds, id)
			|| ranges::contains(block.caption.anchorIds, id)) {
			return true;
		}
		if (anchorIdExists(block.blocks, id)) {
			return true;
		}
		for (const auto &item : block.listItems) {
			if (item.anchorId == id
				|| item.text.anchorId == id
				|| ranges::contains(item.text.anchorIds, id)) {
				return true;
			}
			if (anchorIdExists(item.blocks, id)) {
				return true;
			}
		}
		for (const auto &row : block.tableRows) {
			for (const auto &cell : row.cells) {
				if (cell.text.anchorId == id
					|| ranges::contains(cell.text.anchorIds, id)) {
					return true;
				}
			}
		}
	}
	return false;
}

QString State::nextAnchorId() const {
	for (auto i = 1;; ++i) {
		auto result = u"anchor-%1"_q.arg(i);
		if (!anchorIdExists(result)) {
			return result;
		}
	}
}

Block State::makeBlock(InsertAction action) const {
	switch (action.type) {
	case InsertBlockType::Heading:
		return MakeHeadingBlock(action.headingLevel);
	case InsertBlockType::Blockquote:
		return MakeQuoteBlock(false);
	case InsertBlockType::Code: {
		auto result = MakeCodeBlock();
		result.language = action.codeLanguage;
		return result;
	}
	case InsertBlockType::Math:
		return MakeMathBlock();
	case InsertBlockType::Footer:
		return MakeFooterBlock();
	case InsertBlockType::Divider:
		return MakeDividerBlock();
	case InsertBlockType::Anchor:
		return MakeAnchorBlock(nextAnchorId());
	case InsertBlockType::OrderedList: {
		auto result = MakeListBlock(ListKind::Ordered);
		if (action.orderedStart != 1) {
			result.orderedList.start = action.orderedStart;
		}
		return result;
	}
	case InsertBlockType::BulletList:
		return MakeListBlock(ListKind::Bullet);
	case InsertBlockType::TaskList:
		return MakeListBlock(
			ListKind::Bullet,
			action.taskChecked ? TaskState::Checked : TaskState::Unchecked);
	case InsertBlockType::Pullquote:
		return MakeQuoteBlock(true);
	case InsertBlockType::Photo:
		return MakeMediaBlock(BlockKind::Photo);
	case InsertBlockType::Video:
		return MakeMediaBlock(BlockKind::Video);
	case InsertBlockType::Audio:
		return MakeMediaBlock(BlockKind::Audio);
	case InsertBlockType::Details:
		return MakeDetailsBlock();
	case InsertBlockType::Table:
		return MakeTableBlock();
	case InsertBlockType::Map:
		return MakeMapBlock(action.latitude, action.longitude);
	}
	return MakeParagraphBlock();
}

} // namespace Iv::Editor
