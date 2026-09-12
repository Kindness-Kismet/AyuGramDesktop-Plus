#pragma once

#include <QtGui/QPixmap>

namespace Ayu::Ui::Itunes {

QPixmap FetchCover(const QString &performer,
                   const QString &title,
                   int sizeHintPx = 300,
                   int timeoutMs = 5000);

}
