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

State::TableSelectionInfo State::tableSelectionInfo(
		const Markdown::PreparedEditTableCellRange &range) const {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return {};
	}
	const auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return {};
	}
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	const auto selected = SelectedTableGridCells(grid, *validated);
	if (selected.empty()) {
		return {};
	}
	auto result = TableSelectionInfo{
		.valid = true,
		.allHeader = true,
		.allAlignLeft = true,
		.allAlignCenter = true,
		.allAlignRight = true,
		.allAlignTop = true,
		.allAlignMiddle = true,
		.allAlignBottom = true,
		.singleCell = (selected.size() == 1),
		.selectedRows = validated->rowTill - validated->rowFrom,
		.selectedColumns = validated->columnTill - validated->columnFrom,
		.totalRows = grid.rowCount,
		.totalColumns = grid.columnCount,
		.bordered = owner->bordered,
		.striped = owner->striped,
		.compact = owner->compact,
	};
	for (const auto &reference : selected) {
		const auto &cell = owner->tableRows[reference.rowIndex].cells[
			reference.cellIndex];
		if (!cell.header) {
			result.allHeader = false;
		}
		if (cell.alignment != RichPage::TableAlignment::Left) {
			result.allAlignLeft = false;
		}
		if (cell.alignment != RichPage::TableAlignment::Center) {
			result.allAlignCenter = false;
		}
		if (cell.alignment != RichPage::TableAlignment::Right) {
			result.allAlignRight = false;
		}
		if (cell.verticalAlignment != RichPage::TableVerticalAlignment::Top) {
			result.allAlignTop = false;
		}
		if (cell.verticalAlignment
			!= RichPage::TableVerticalAlignment::Middle) {
			result.allAlignMiddle = false;
		}
		if (cell.verticalAlignment
			!= RichPage::TableVerticalAlignment::Bottom) {
			result.allAlignBottom = false;
		}
		if (result.singleCell) {
			result.canSplitCell = (NormalizeTableSpan(cell.colspan) > 1)
				|| (NormalizeTableSpan(cell.rowspan) > 1);
		}
	}
	result.canUniteCells = !result.singleCell
		&& CleanTableGridUniteRange(grid, *validated);
	return result;
}

std::optional<Markdown::PreparedEditTableCellRange>
State::tableContextRangeForSelection(
		const Markdown::PreparedEditSelection &selection,
		const Markdown::PreparedEditTableCellSource &source) const {
	if (source.tableRowIndex < 0
		|| source.column < 0
		|| source.rowspan <= 0
		|| source.colspan <= 0) {
		return std::nullopt;
	}
	const auto sourceBlock = convertBlockPath(source.block);
	if (!sourceBlock) {
		return std::nullopt;
	}
	const auto owner = block(*sourceBlock);
	if (!owner || owner->kind != BlockKind::Table) {
		return std::nullopt;
	}
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	if (grid.rowCount <= 0 || grid.columnCount <= 0) {
		return std::nullopt;
	}
	const auto fullTableRange = [&] {
		return Markdown::PreparedEditTableCellRange{
			.block = source.block,
			.rowFrom = 0,
			.rowTill = grid.rowCount,
			.columnFrom = 0,
			.columnTill = grid.columnCount,
		};
	};
	const auto sourceIntersects = [&](const auto &range) {
		return (source.tableRowIndex < range.rowTill)
			&& (source.tableRowIndex + source.rowspan > range.rowFrom)
			&& (source.column < range.columnTill)
			&& (source.column + source.colspan > range.columnFrom);
	};
	switch (selection.kind) {
	case PreparedEditSelectionKind::TableCells: {
		const auto range = validateTableCellRange(selection.tableCells);
		if (!range || range->block != *sourceBlock) {
			return std::nullopt;
		}
		return sourceIntersects(*range)
			? std::make_optional(selection.tableCells)
			: std::nullopt;
	}
	case PreparedEditSelectionKind::TableRows: {
		const auto range = validateTableRowRange(selection.tableRows);
		if (!range || range->block != *sourceBlock) {
			return std::nullopt;
		}
		if (source.tableRowIndex >= range->till
			|| source.tableRowIndex + source.rowspan <= range->from) {
			return std::nullopt;
		}
		return Markdown::PreparedEditTableCellRange{
			.block = source.block,
			.rowFrom = range->from,
			.rowTill = range->till,
			.columnFrom = 0,
			.columnTill = grid.columnCount,
		};
	}
	case PreparedEditSelectionKind::Blocks: {
		const auto range = validateBlockRange(selection.blocks);
		if (!range
			|| sourceBlock->container != range->container
			|| sourceBlock->index < range->from
			|| sourceBlock->index >= range->till) {
			return std::nullopt;
		}
		return fullTableRange();
	}
	case PreparedEditSelectionKind::ListItems:
	case PreparedEditSelectionKind::None:
		return std::nullopt;
	}
	return std::nullopt;
}

bool State::canRemoveStructuralSelection(
		const Markdown::PreparedEditSelection &selection) const {
	switch (selection.kind) {
	case PreparedEditSelectionKind::Blocks:
		return validateBlockRange(selection.blocks).has_value();
	case PreparedEditSelectionKind::ListItems:
		return validateListItemRange(selection.listItems).has_value();
	case PreparedEditSelectionKind::TableRows:
		return validateTableRowRange(selection.tableRows).has_value();
	case PreparedEditSelectionKind::TableCells: {
		const auto range = validateTableCellRange(selection.tableCells);
		if (!range) {
			return false;
		}
		const auto owner = block(range->block);
		if (!owner || owner->kind != BlockKind::Table) {
			return false;
		}
		return TableGridRangeSpansAllRows(
			BuildTableGrid(*owner, tableRenderLimits()),
			*range);
	}
	case PreparedEditSelectionKind::None:
		return false;
	}
	return false;
}

auto State::structuredClipboardDataForSelection(
		const Markdown::PreparedEditSelection &selection) const
-> std::optional<ClipboardData> {
	switch (selection.kind) {
	case PreparedEditSelectionKind::Blocks: {
		const auto range = validateBlockRange(selection.blocks);
		const auto blocks = range ? blockContainer(range->container) : nullptr;
		if (!range || !blocks) {
			return std::nullopt;
		}
		auto data = ClipboardBlockData();
		data.blocks = std::vector<Block>(
			blocks->begin() + range->from,
			blocks->begin() + range->till);
		return ClipboardData(std::move(data));
	}
	case PreparedEditSelectionKind::ListItems: {
		const auto range = validateListItemRange(selection.listItems);
		const auto owner = range ? block(range->block) : nullptr;
		if (!range || !owner || owner->kind != BlockKind::List) {
			return std::nullopt;
		}
		auto data = ClipboardListItemsData();
		data.listKind = owner->listKind;
		data.orderedList = owner->orderedList;
		data.items = std::vector<ListItem>(
			owner->listItems.begin() + range->from,
			owner->listItems.begin() + range->till);
		data.taskList = IsTaskList(data.items);
		return ClipboardData(std::move(data));
	}
	case PreparedEditSelectionKind::TableRows:
	case PreparedEditSelectionKind::TableCells:
	case PreparedEditSelectionKind::None:
		return std::nullopt;
	}
	return std::nullopt;
}

std::shared_ptr<const RichPage> State::richPageForTableSelection(
		const Markdown::PreparedEditSelection &selection) const {
	auto blockPath = BlockPath();
	auto rowFrom = 0;
	auto rowTill = 0;
	auto columnFrom = -1;
	auto columnTill = -1;
	if (selection.kind == PreparedEditSelectionKind::TableCells) {
		const auto range = validateTableCellRange(selection.tableCells);
		if (!range) {
			return nullptr;
		}
		blockPath = range->block;
		rowFrom = range->rowFrom;
		rowTill = range->rowTill;
		columnFrom = range->columnFrom;
		columnTill = range->columnTill;
	} else if (selection.kind == PreparedEditSelectionKind::TableRows) {
		const auto range = validateTableRowRange(selection.tableRows);
		if (!range) {
			return nullptr;
		}
		blockPath = range->block;
		rowFrom = range->from;
		rowTill = range->till;
	} else {
		return nullptr;
	}
	const auto owner = block(blockPath);
	if (!owner || owner->kind != BlockKind::Table) {
		return nullptr;
	}
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	if (columnFrom < 0 || columnTill < 0) {
		columnFrom = 0;
		columnTill = grid.columnCount;
	}
	if (rowFrom < 0
		|| rowTill <= rowFrom
		|| columnFrom < 0
		|| columnTill <= columnFrom) {
		return nullptr;
	}
	const auto rectangle = StructuralTableCellRange{
		.rowFrom = rowFrom,
		.rowTill = rowTill,
		.columnFrom = columnFrom,
		.columnTill = columnTill,
	};
	const auto references = SelectedTableGridCells(grid, rectangle);
	if (references.empty()) {
		return nullptr;
	}

	auto page = std::make_shared<RichPage>();
	if (references.size() == 1
		&& (rowTill - rowFrom == 1)
		&& (columnTill - columnFrom == 1)) {
		const auto &reference = references.front();
		const auto &row = owner->tableRows[reference.rowIndex];
		auto paragraph = MakeParagraphBlock();
		paragraph.text = row.cells[reference.cellIndex].text;
		page->blocks.push_back(std::move(paragraph));
		return page;
	}

	auto table = Block();
	table.kind = BlockKind::Table;
	table.bordered = owner->bordered;
	table.striped = owner->striped;
	table.compact = owner->compact;
	table.tableRows.resize(rowTill - rowFrom);
	for (const auto &reference : references) {
		auto cell = owner->tableRows[reference.rowIndex]
			.cells[reference.cellIndex];
		const auto clampedRowFrom = std::max(reference.rowFrom, rowFrom);
		const auto clampedRowTill = std::min(reference.rowTill, rowTill);
		const auto clampedColumnFrom = std::max(reference.columnFrom, columnFrom);
		const auto clampedColumnTill = std::min(reference.columnTill, columnTill);
		cell.rowspan = std::max(clampedRowTill - clampedRowFrom, 1);
		cell.colspan = std::max(clampedColumnTill - clampedColumnFrom, 1);
		table.tableRows[clampedRowFrom - rowFrom].cells.push_back(
			std::move(cell));
	}
	page->blocks.push_back(std::move(table));
	return page;
}

bool State::insertPreparedBlocksAfterTableSelection(
		const Markdown::PreparedEditSelection &selection,
		std::vector<RichPage::Block> blocks) {
	if (blocks.empty()) {
		return false;
	}
	auto blockPath = std::optional<Markdown::PreparedEditBlockPath>();
	if (selection.kind == PreparedEditSelectionKind::TableCells) {
		if (selection.tableCells.empty()) {
			return false;
		}
		blockPath = selection.tableCells.block;
	} else if (selection.kind == PreparedEditSelectionKind::TableRows) {
		if (selection.tableRows.empty()) {
			return false;
		}
		blockPath = selection.tableRows.block;
	} else {
		return false;
	}
	const auto path = *blockPath;
	return applyCheckedMutation(false, [
			blocks = std::move(blocks),
			path](State &candidate) mutable {
		const auto container = candidate.convertBlockContainerPath(
			path.container);
		if (!container || !candidate.blockContainer(*container)) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		candidate.normalizeInsertedBlockAnchors(blocks);
		auto insertAt = path.index + 1;
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

State::TableInPlaceApplyResult State::replaceTableSelectionCellsInPlace(
		const Markdown::PreparedEditSelection &selection,
		const RichPage &page) {
	if (page.blocks.size() != 1
		|| page.blocks.front().kind != BlockKind::Table) {
		return TableInPlaceApplyResult::StructureMismatch;
	}
	const auto source = richPageForTableSelection(selection);
	if (!source
		|| source->blocks.size() != 1
		|| source->blocks.front().kind != BlockKind::Table) {
		return TableInPlaceApplyResult::StructureMismatch;
	}
	const auto &sourceTable = source->blocks.front();
	const auto &resultTable = page.blocks.front();
	if (sourceTable.tableRows.size() != resultTable.tableRows.size()) {
		return TableInPlaceApplyResult::StructureMismatch;
	}
	auto texts = std::vector<RichText>();
	auto unchanged = true;
	for (auto row = 0, rows = int(sourceTable.tableRows.size());
			row != rows;
			++row) {
		const auto &sourceCells = sourceTable.tableRows[row].cells;
		const auto &resultCells = resultTable.tableRows[row].cells;
		if (sourceCells.size() != resultCells.size()) {
			return TableInPlaceApplyResult::StructureMismatch;
		}
		for (auto index = 0, count = int(sourceCells.size());
				index != count;
				++index) {
			const auto &sourceCell = sourceCells[index];
			const auto &resultCell = resultCells[index];
			if (NormalizeTableSpan(sourceCell.colspan)
					!= NormalizeTableSpan(resultCell.colspan)
				|| NormalizeTableSpan(sourceCell.rowspan)
					!= NormalizeTableSpan(resultCell.rowspan)) {
				return TableInPlaceApplyResult::StructureMismatch;
			}
			if (sourceCell.text != resultCell.text) {
				unchanged = false;
			}
			texts.push_back(resultCell.text);
		}
	}
	if (unchanged) {
		return TableInPlaceApplyResult::Unchanged;
	}
	auto preparedPath = Markdown::PreparedEditBlockPath();
	auto blockPath = BlockPath();
	auto rowFrom = 0;
	auto rowTill = 0;
	auto columnFrom = -1;
	auto columnTill = -1;
	if (selection.kind == PreparedEditSelectionKind::TableCells) {
		const auto range = validateTableCellRange(selection.tableCells);
		if (!range) {
			return TableInPlaceApplyResult::StructureMismatch;
		}
		preparedPath = selection.tableCells.block;
		blockPath = range->block;
		rowFrom = range->rowFrom;
		rowTill = range->rowTill;
		columnFrom = range->columnFrom;
		columnTill = range->columnTill;
	} else if (selection.kind == PreparedEditSelectionKind::TableRows) {
		const auto range = validateTableRowRange(selection.tableRows);
		if (!range) {
			return TableInPlaceApplyResult::StructureMismatch;
		}
		preparedPath = selection.tableRows.block;
		blockPath = range->block;
		rowFrom = range->from;
		rowTill = range->till;
	} else {
		return TableInPlaceApplyResult::StructureMismatch;
	}
	const auto owner = block(blockPath);
	if (!owner || owner->kind != BlockKind::Table) {
		return TableInPlaceApplyResult::StructureMismatch;
	}
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	if (columnFrom < 0 || columnTill < 0) {
		columnFrom = 0;
		columnTill = grid.columnCount;
	}
	if (rowFrom < 0
		|| rowTill <= rowFrom
		|| columnFrom < 0
		|| columnTill <= columnFrom) {
		return TableInPlaceApplyResult::StructureMismatch;
	}
	const auto rectangle = StructuralTableCellRange{
		.rowFrom = rowFrom,
		.rowTill = rowTill,
		.columnFrom = columnFrom,
		.columnTill = columnTill,
	};
	const auto references = SelectedTableGridCells(grid, rectangle);
	if (references.size() != texts.size()) {
		return TableInPlaceApplyResult::StructureMismatch;
	}
	const auto applied = applyCheckedMutation(false, [&](State &candidate) {
		const auto path = candidate.convertBlockPath(preparedPath);
		if (!path) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		const auto table = candidate.block(*path);
		if (!table || table->kind != BlockKind::Table) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		for (auto i = 0, count = int(references.size()); i != count; ++i) {
			const auto cell = candidate.tableCell(
				*path,
				references[i].rowIndex,
				references[i].cellIndex);
			if (!cell) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			cell->text = std::move(texts[i]);
		}
		candidate.rebuild();
		return CheckedMutationResult<bool>{ .apply = true, .result = true };
	});
	return applied
		? TableInPlaceApplyResult::Applied
		: TableInPlaceApplyResult::Failed;
}

bool State::addTableRow(
		const Markdown::PreparedEditTableCellRange &range,
		bool after) {
	return applyCheckedMutation(false, [range, after](State &candidate) {
		const auto applied = candidate.addTableRowUnchecked(range, after);
		return CheckedMutationResult<bool>{
			.apply = applied,
			.result = applied,
		};
	});
}

bool State::addTableRowUnchecked(
		const Markdown::PreparedEditTableCellRange &range,
		bool after) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	const auto insertAt = after ? validated->rowTill : validated->rowFrom;
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	if (insertAt < 0 || insertAt > int(owner->tableRows.size())) {
		return false;
	}
	const auto sourceRow = after
		? validated->rowTill - 1
		: validated->rowFrom;

	auto coveredColumns = std::vector<char>(grid.columnCount, false);
	for (const auto &reference : grid.cells) {
		if (reference.rowFrom >= insertAt || reference.rowTill <= insertAt) {
			continue;
		}
		auto &cell = owner->tableRows[reference.rowIndex].cells[
			reference.cellIndex];
		cell.rowspan = IncrementTableSpan(cell.rowspan);
		for (auto column = reference.columnFrom;
				column != reference.columnTill;
				++column) {
			coveredColumns[column] = true;
		}
	}

	auto row = TableRow();
	if (grid.columnCount <= 0) {
		row.cells.push_back(MakeDefaultTableCell());
	} else {
		row.cells.reserve(grid.columnCount);
		for (auto column = 0; column != grid.columnCount; ++column) {
			if (!coveredColumns[column]) {
				const auto source = TableGridCellAt(
					*owner,
					grid,
					sourceRow,
					column);
				row.cells.push_back(MakeDefaultTableCell(
					source ? source->header : false));
			}
		}
	}
	owner->tableRows.insert(
		owner->tableRows.begin() + insertAt,
		std::move(row));
	rebuild();
	return true;
}

bool State::addTableColumn(
		const Markdown::PreparedEditTableCellRange &range,
		bool after) {
	return applyCheckedMutation(false, [range, after](State &candidate) {
		const auto applied = candidate.addTableColumnUnchecked(range, after);
		return CheckedMutationResult<bool>{
			.apply = applied,
			.result = applied,
		};
	});
}

bool State::addTableColumnUnchecked(
		const Markdown::PreparedEditTableCellRange &range,
		bool after) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	const auto insertAt = after
		? validated->columnTill
		: validated->columnFrom;
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	const auto sourceColumn = after
		? validated->columnTill - 1
		: validated->columnFrom;

	auto coveredRows = std::vector<char>(owner->tableRows.size(), false);
	for (const auto &reference : grid.cells) {
		if (reference.columnFrom >= insertAt
			|| reference.columnTill <= insertAt) {
			continue;
		}
		auto &cell = owner->tableRows[reference.rowIndex].cells[
			reference.cellIndex];
		cell.colspan = IncrementTableSpan(cell.colspan);
		for (auto row = reference.rowFrom; row != reference.rowTill; ++row) {
			coveredRows[row] = true;
		}
	}

	for (auto rowIndex = 0;
			rowIndex != int(owner->tableRows.size());
			++rowIndex) {
		if (coveredRows[rowIndex]) {
			continue;
		}
		const auto source = TableGridCellAt(
			*owner,
			grid,
			rowIndex,
			sourceColumn);
		InsertTableCellBeforeVisualColumn(
			&owner->tableRows[rowIndex],
			grid,
			rowIndex,
			insertAt,
			MakeDefaultTableCell(source ? source->header : false));
	}
	rebuild();
	return true;
}

bool State::setTableHeader(
		const Markdown::PreparedEditTableCellRange &range,
		bool header) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	const auto selected = SelectedTableGridCells(
		BuildTableGrid(*owner, tableRenderLimits()),
		*validated);
	if (selected.empty()) {
		return false;
	}
	auto changed = false;
	for (const auto &reference : selected) {
		auto &cell = owner->tableRows[reference.rowIndex].cells[
			reference.cellIndex];
		if (cell.header != header) {
			cell.header = header;
			changed = true;
		}
	}
	if (changed) {
		rebuild();
	}
	return true;
}

bool State::setTableAlignment(
		const Markdown::PreparedEditTableCellRange &range,
		RichPage::TableAlignment alignment) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	const auto selected = SelectedTableGridCells(
		BuildTableGrid(*owner, tableRenderLimits()),
		*validated);
	if (selected.empty()) {
		return false;
	}
	auto changed = false;
	for (const auto &reference : selected) {
		auto &cell = owner->tableRows[reference.rowIndex].cells[
			reference.cellIndex];
		if (cell.alignment != alignment) {
			cell.alignment = alignment;
			changed = true;
		}
	}
	if (changed) {
		rebuild();
	}
	return true;
}

bool State::setTableVerticalAlignment(
		const Markdown::PreparedEditTableCellRange &range,
		RichPage::TableVerticalAlignment alignment) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	const auto selected = SelectedTableGridCells(
		BuildTableGrid(*owner, tableRenderLimits()),
		*validated);
	if (selected.empty()) {
		return false;
	}
	auto changed = false;
	for (const auto &reference : selected) {
		auto &cell = owner->tableRows[reference.rowIndex].cells[
			reference.cellIndex];
		if (cell.verticalAlignment != alignment) {
			cell.verticalAlignment = alignment;
			changed = true;
		}
	}
	if (changed) {
		rebuild();
	}
	return true;
}

bool State::splitTableCell(
		const Markdown::PreparedEditTableCellRange &range) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	const auto selected = SelectedTableGridCells(grid, *validated);
	if (selected.size() != 1) {
		return false;
	}
	const auto reference = selected.front();
	auto &cell = owner->tableRows[reference.rowIndex].cells[
		reference.cellIndex];
	if (cell.rowspan <= 1 && cell.colspan <= 1) {
		return false;
	}
	const auto header = cell.header;

	cell.rowspan = 1;
	cell.colspan = 1;
	for (auto rowIndex = reference.rowFrom;
			rowIndex != reference.rowTill;
			++rowIndex) {
		auto &row = owner->tableRows[rowIndex];
		for (auto column = reference.columnTill;
				column != reference.columnFrom;
				--column) {
			const auto currentColumn = column - 1;
			if (rowIndex == reference.rowFrom
				&& currentColumn == reference.columnFrom) {
				continue;
			}
			InsertTableCellBeforeVisualColumn(
				&row,
				grid,
				rowIndex,
				currentColumn,
				MakeDefaultTableCell(header));
		}
	}
	rebuild();
	return true;
}

bool State::uniteTableCells(
		const Markdown::PreparedEditTableCellRange &range) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	const auto selected = SelectedTableGridCells(grid, *validated);
	if (selected.size() <= 1
		|| !CleanTableGridUniteRange(grid, *validated)) {
		return false;
	}
	const auto keeper = *std::min_element(
		selected.begin(),
		selected.end(),
		[](const auto &a, const auto &b) {
			if (a.rowFrom != b.rowFrom) {
				return a.rowFrom < b.rowFrom;
			} else if (a.columnFrom != b.columnFrom) {
				return a.columnFrom < b.columnFrom;
			} else if (a.rowIndex != b.rowIndex) {
				return a.rowIndex < b.rowIndex;
			}
			return a.cellIndex < b.cellIndex;
		});

	auto &keeperCell = owner->tableRows[keeper.rowIndex].cells[
		keeper.cellIndex];
	keeperCell.rowspan = validated->rowTill - validated->rowFrom;
	keeperCell.colspan = validated->columnTill - validated->columnFrom;

	auto toErase = selected;
	toErase.erase(
		std::remove_if(
			toErase.begin(),
			toErase.end(),
			[&](const TableGridCellReference &cell) {
				return cell.rowIndex == keeper.rowIndex
					&& cell.cellIndex == keeper.cellIndex;
			}),
		toErase.end());
	std::sort(
		toErase.begin(),
		toErase.end(),
		[](const auto &a, const auto &b) {
			if (a.rowIndex != b.rowIndex) {
				return a.rowIndex > b.rowIndex;
			}
			return a.cellIndex > b.cellIndex;
		});
	for (const auto &reference : toErase) {
		auto &row = owner->tableRows[reference.rowIndex];
		row.cells.erase(row.cells.begin() + reference.cellIndex);
	}
	rebuild();
	return true;
}

bool State::removeTableRows(
		const Markdown::PreparedEditTableCellRange &range) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	const auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	if (!TableGridRangeSpansAllColumns(
			BuildTableGrid(*owner, tableRenderLimits()),
			*validated)) {
		return false;
	}
	return removeStructuralSelection({
		.kind = PreparedEditSelectionKind::TableRows,
		.tableRows = {
			.block = range.block,
			.from = validated->rowFrom,
			.till = validated->rowTill,
		},
	}, true).has_value();
}

bool State::removeTableColumns(
		const Markdown::PreparedEditTableCellRange &range) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	const auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	if (!TableGridRangeSpansAllRows(
			BuildTableGrid(*owner, tableRenderLimits()),
			*validated)) {
		return false;
	}
	return removeStructuralSelection({
		.kind = PreparedEditSelectionKind::TableCells,
		.tableCells = range,
	}, true).has_value();
}

bool State::removeTable(
		const Markdown::PreparedEditTableCellRange &range) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	const auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	if (!TableGridRangeCoversFullTable(
			BuildTableGrid(*owner, tableRenderLimits()),
			*validated)) {
		return false;
	}
	return removeStructuralSelection({
		.kind = PreparedEditSelectionKind::Blocks,
		.blocks = {
			.container = range.block.container,
			.from = range.block.index,
			.till = range.block.index + 1,
		},
	}, true).has_value();
}

bool State::setTableBordered(
		const Markdown::PreparedEditTableCellRange &range,
		bool bordered) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	if (owner->bordered != bordered) {
		owner->bordered = bordered;
		rebuild();
	}
	return true;
}

bool State::setTableStriped(
		const Markdown::PreparedEditTableCellRange &range,
		bool striped) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	if (owner->striped != striped) {
		owner->striped = striped;
		rebuild();
	}
	return true;
}

bool State::setTableCompact(
		const Markdown::PreparedEditTableCellRange &range,
		bool compact) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	if (owner->compact != compact) {
		owner->compact = compact;
		rebuild();
	}
	return true;
}

} // namespace Iv::Editor
