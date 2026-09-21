#pragma once

#include "base/basic_types.h"

namespace Ui {
class BoxContent;
class VerticalLayout;
} // namespace Ui

namespace Ayu::EmojiPacks {

void addImportButton(not_null<Ui::BoxContent*> box, Fn<void()> refresh);
void addPresetRows(not_null<Ui::VerticalLayout*> content, Fn<void()> refresh);

} // namespace Ayu::EmojiPacks
