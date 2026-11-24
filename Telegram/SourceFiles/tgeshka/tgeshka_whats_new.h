/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).

"What's new": the list of TGeshka changes from CHANGELOG-tgeshka.json
in the repository root, built into the app (no network needed).
*/
#pragma once

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Tgeshka::WhatsNew {

// Settings > TGeshka > "What's new": every version, newest first.
void ListBox(not_null<Ui::GenericBox*> box);

// Before the first-launch markers are written: tells a fresh data
// folder (nothing to show) from an update of an existing one.
void RememberDataFolder();

// Once after an update to a version listed in the changelog:
// "What's new in X.Y.Z" in the active window.
void ShowAfterUpdate();

} // namespace Tgeshka::WhatsNew
