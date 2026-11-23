/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).
*/
#pragma once

namespace Settings::Builder {
class SectionBuilder;
} // namespace Settings::Builder

namespace Tgeshka::Update {

// Starts the checks and the "update required" screen.
void StartUi();

// The button under the chats list, empty when no update is ready.
[[nodiscard]] std::optional<QString> ReadyButtonText();
[[nodiscard]] rpl::producer<> ReadyButtonChanges();
void ReadyButtonClicked();

// Settings > TGeshka: version, channel, check and rollback.
void AddSettings(Settings::Builder::SectionBuilder &builder);

} // namespace Tgeshka::Update
