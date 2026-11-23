/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).
*/
#pragma once

#include "settings/settings_type.h"

namespace Settings::Builder {
class SectionBuilder;
} // namespace Settings::Builder

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Tgeshka {

// Live gateway state rows and the "Reconnect" button, updated while shown.
void FillGatewayState(not_null<Ui::VerticalLayout*> container);

// Settings > TGeshka: gateway connection and its live state, version,
// "About TGeshka" and the source code link.
[[nodiscard]] Settings::Type SettingsSectionId();

// Main settings menu entry, called from settings_main.cpp.
void AddSettingsSectionButton(Settings::Builder::SectionBuilder &builder);

} // namespace Tgeshka
