#pragma once

#include "base/basic_types.h"

class QWidget;

namespace AyuFeatures::StreamerMode::Platform {

void SetWindowCaptureExcluded(
	not_null<QWidget*> widget,
	bool excluded);

} // namespace AyuFeatures::StreamerMode::Platform
