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

// Registers only tgeshka:// (tg:// stays with the official client).
void RegisterUrlScheme(const QString &arguments);

// After the local settings are read, before the first connection.
void OnLocalStorageReady();

// Upstream proxy settings do nothing with the gateway: hidden.
[[nodiscard]] rpl::producer<bool> ProxySettingsShown();

// Takes tgeshka://gateway?url=...&token=... links out of the list.
void ConsumeStartUrls(QList<QUrl> &urls);

// Before login: asks for the access key if the gateway has none.
void OnIntroShown(not_null<Window::Controller*> window);

// Settings > Advanced > Data and storage, under "Connection type".
void AddGatewaySettingsButton(Settings::Builder::SectionBuilder &builder);

// First paragraph of the About box.
[[nodiscard]] rpl::producer<TextWithEntities> AboutText();

} // namespace Tgeshka
