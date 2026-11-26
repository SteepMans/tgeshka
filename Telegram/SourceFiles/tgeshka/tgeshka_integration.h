/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).

Entry points called from upstream Telegram Desktop files. Each hook is
a single line in the upstream code, everything else lives here.
*/
#pragma once

namespace Settings::Builder {
class SectionBuilder;
} // namespace Settings::Builder

namespace Window {
class Controller;
} // namespace Window

namespace Tgeshka {

// Registers tgeshka:// and, while the switch is on, tg:// as well.
void RegisterUrlScheme(const QString &arguments);

// "Open tg:// links in TGeshka", on by default (Windows only).
// The official client takes tg:// back when it updates, so the scheme
// is checked on every launch and then every few minutes. Turning the
// switch off hands tg:// back to the official client if it is installed.
[[nodiscard]] bool TgLinksSupported();
[[nodiscard]] bool OpensTgLinks();
void SetOpensTgLinks(bool enabled);

// Easter egg (пасхалка) for an outgoing text message, see
// tgeshka-easter-egg.txt. Text with formatting stays as it is.
[[nodiscard]] TextWithEntities ApplyEasterEgg(TextWithEntities text);

// After the local settings are read, before the first connection.
void OnLocalStorageReady();

// Upstream proxy settings do nothing with the gateway: hidden.
[[nodiscard]] rpl::producer<bool> ProxySettingsShown();

// Takes tgeshka://gateway?url=...&token=... links out of the list.
void ConsumeStartUrls(QList<QUrl> &urls);

// Before login: asks for the access key if the gateway has none.
void OnIntroShown(not_null<Window::Controller*> window);

// First paragraph of the About box.
[[nodiscard]] rpl::producer<TextWithEntities> AboutText();

} // namespace Tgeshka
