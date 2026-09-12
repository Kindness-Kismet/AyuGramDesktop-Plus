#pragma once

namespace Ui {

class GenericBox;

void FillDonateQrBox(
	not_null<Ui::GenericBox*> box,
	const QString &address,
	const QString &iconResourcePath);

} // namespace Ui
