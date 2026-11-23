/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).
*/
#pragma once

namespace Tgeshka {

// TGeshka strings are not in the Telegram language packs: every one has
// an English and a Russian variant, picked by the current language.
[[nodiscard]] bool IsRussian();
[[nodiscard]] QString Tr(const QString &en, const QString &ru);
[[nodiscard]] rpl::producer<QString> TrValue(QString en, QString ru);

// First launch only: with a Russian system language switch the interface
// to Russian from the strings built into the app, before any connection.
void ApplyBuiltInLanguage();

} // namespace Tgeshka
