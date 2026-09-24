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

auto State::inlineButtonAt(int ordinal, int offset) const
-> std::optional<Markdown::InlineTextObjectButtonData> {
	const auto descriptor = textNode(ordinal);
	const auto text = descriptor ? richText(descriptor->leaf) : nullptr;
	if (!text) {
		return std::nullopt;
	}
	for (const auto &entity : text->text.entities) {
		if (entity.offset() != offset) {
			continue;
		} else if (auto button = ButtonDataFromEntity(entity)) {
			return button;
		}
	}
	return std::nullopt;
}

ApplyResult State::editInlineButtonAt(
		int ordinal,
		int offset,
		Markdown::InlineTextObjectButtonData data) {
	data.label = Markdown::NormalizeRichButtonLabel(std::move(data.label));
	auto serialized = Markdown::SerializeInlineTextObjectEntity({
		.kind = Markdown::InlineTextObjectKind::Button,
		.data = std::move(data),
	});
	if (serialized.isEmpty()) {
		return ApplyResult::Unchanged;
	}
	return applyCheckedMutation(ApplyResult::Failed, [
		ordinal,
		offset,
		serialized = std::move(serialized)
	](State &candidate) {
		const auto descriptor = candidate.textNode(ordinal);
		const auto text = descriptor
			? candidate.richText(descriptor->leaf)
			: nullptr;
		if (!text) {
			return CheckedMutationResult<ApplyResult>{
				.result = ApplyResult::Unchanged,
			};
		}
		for (auto &entity : text->text.entities) {
			if ((entity.offset() != offset)
				|| !ButtonDataFromEntity(entity)) {
				continue;
			} else if (entity.data() == serialized) {
				return CheckedMutationResult<ApplyResult>{
					.result = ApplyResult::Unchanged,
				};
			}
			entity = EntityInText(
				EntityType::CustomEmoji,
				entity.offset(),
				entity.length(),
				serialized);
			candidate.rebuild();
			return CheckedMutationResult<ApplyResult>{
				.apply = true,
				.result = ApplyResult::Changed,
			};
		}
		return CheckedMutationResult<ApplyResult>{
			.result = ApplyResult::Unchanged,
		};
	});
}

const RichPage::Button *State::rowButtonAt(
		const Markdown::PreparedEditBlockSource &source,
		int index) const {
	const auto path = convertBlockPath(source);
	const auto owner = path ? block(*path) : nullptr;
	if (!ValidRowButtonIndex(owner, index)) {
		return nullptr;
	}
	return &owner->buttons[index];
}

ApplyResult State::editRowButtonAt(
		const Markdown::PreparedEditBlockSource &source,
		int index,
		RichPage::Button button) {
	button = NormalizedRowButton(std::move(button));
	return applyCheckedMutation(ApplyResult::Failed, [
		source,
		index,
		button = std::move(button)
	](State &candidate) {
		const auto path = candidate.convertBlockPath(source);
		const auto owner = path ? candidate.block(*path) : nullptr;
		if (!ValidRowButtonIndex(owner, index)) {
			return CheckedMutationResult<ApplyResult>{
				.result = ApplyResult::Unchanged,
			};
		}
		auto &entry = owner->buttons[index];
		auto updated = entry;
		updated.text.text = button.text.text;
		updated.button.text = button.button.text;
		updated.button.type = button.button.type;
		updated.button.data = button.button.data;
		updated.button.visual.color = button.button.visual.color;
		if (entry == updated) {
			return CheckedMutationResult<ApplyResult>{
				.result = ApplyResult::Unchanged,
			};
		}
		entry = std::move(updated);
		candidate.rebuild();
		return CheckedMutationResult<ApplyResult>{
			.apply = true,
			.result = ApplyResult::Changed,
		};
	});
}

State::RowButtonRemoveResult State::removeRowButtonAt(
		const Markdown::PreparedEditBlockSource &source,
		int index) {
	const auto path = convertBlockPath(source);
	const auto owner = path ? block(*path) : nullptr;
	if (!ValidRowButtonIndex(owner, index)) {
		return { .result = ApplyResult::Unchanged };
	} else if (owner->buttons.size() == 1) {
		return {
			.result = ApplyResult::Changed,
			.caretOrdinal = removeBlock(*path, true),
			.removedRow = true,
		};
	}
	const auto result = applyCheckedMutation(ApplyResult::Failed, [
		source,
		index
	](State &candidate) {
		const auto path = candidate.convertBlockPath(source);
		const auto owner = path ? candidate.block(*path) : nullptr;
		if (!ValidRowButtonIndex(owner, index)) {
			return CheckedMutationResult<ApplyResult>{
				.result = ApplyResult::Unchanged,
			};
		}
		owner->buttons.erase(owner->buttons.begin() + index);
		candidate.rebuild();
		return CheckedMutationResult<ApplyResult>{
			.apply = true,
			.result = ApplyResult::Changed,
		};
	});
	return { .result = result };
}

std::optional<RichPage::ButtonAlignment> State::rowAlignment(
		const Markdown::PreparedEditBlockSource &source) const {
	const auto path = convertBlockPath(source);
	const auto owner = path ? block(*path) : nullptr;
	if (!owner || (owner->kind != BlockKind::ButtonRow)) {
		return std::nullopt;
	}
	return owner->buttonAlignment;
}

ApplyResult State::setRowAlignment(
		const Markdown::PreparedEditBlockSource &source,
		RichPage::ButtonAlignment alignment) {
	return applyCheckedMutation(ApplyResult::Failed, [
		source,
		alignment
	](State &candidate) {
		const auto path = candidate.convertBlockPath(source);
		const auto owner = path ? candidate.block(*path) : nullptr;
		if (!owner
			|| (owner->kind != BlockKind::ButtonRow)
			|| (owner->buttonAlignment == alignment)) {
			return CheckedMutationResult<ApplyResult>{
				.result = ApplyResult::Unchanged,
			};
		}
		owner->buttonAlignment = alignment;
		candidate.rebuild();
		return CheckedMutationResult<ApplyResult>{
			.apply = true,
			.result = ApplyResult::Changed,
		};
	});
}

ApplyResult State::addRowButton(
		const Markdown::PreparedEditBlockSource &source,
		RichPage::Button button) {
	button = NormalizedRowButton(std::move(button));
	return applyCheckedMutation(ApplyResult::Failed, [
		source,
		button = std::move(button)
	](State &candidate) {
		const auto path = candidate.convertBlockPath(source);
		const auto owner = path ? candidate.block(*path) : nullptr;
		if (!owner || (owner->kind != BlockKind::ButtonRow)) {
			return CheckedMutationResult<ApplyResult>{
				.result = ApplyResult::Unchanged,
			};
		}
		owner->buttons.push_back(button);
		candidate.rebuild();
		return CheckedMutationResult<ApplyResult>{
			.apply = true,
			.result = ApplyResult::Changed,
		};
	});
}

} // namespace Iv::Editor
