/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).
*/
#pragma once

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Tgeshka {

// Settings > Advanced > "Gateway connection".
void GatewaySettingsBox(not_null<Ui::GenericBox*> box);

// First launch, before the Telegram login: "Paste the access key".
void AccessKeyBox(not_null<Ui::GenericBox*> box);

// Short status for the settings button label.
[[nodiscard]] rpl::producer<QString> GatewayStatusText();

} // namespace Tgeshka
