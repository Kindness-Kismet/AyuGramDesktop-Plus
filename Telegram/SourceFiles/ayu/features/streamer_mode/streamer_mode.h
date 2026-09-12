#pragma once

class QWidget;

namespace AyuFeatures::StreamerMode {

void apply(bool enabled);
void hideWidgetWindow(QWidget *widget);
void showWidgetWindow(QWidget *widget);

} // namespace AyuFeatures::StreamerMode
