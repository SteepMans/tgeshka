/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).

All brand names and identifiers live here: to rename the application
change this file only. It is included from C++, the Windows resource
script (.rc) and parsed by Telegram/cmake/tgeshka.cmake, so keep it
to plain #define lines with string literals.
*/
#ifndef TGESHKA_BRAND_H
#define TGESHKA_BRAND_H

// Shown in window titles, tray, notifications, "About" and installer.
#define TGESHKA_APP_NAME "TGeshka"

// Executable name without extension and data folder in %APPDATA%.
#define TGESHKA_APP_FILE "TGeshka"

// Windows uninstall key / installer AppId. Must differ from Telegram.
#define TGESHKA_APP_ID "{D33EC070-86E5-4A75-9C86-A6EC0133BF7E}"

// Windows AppUserModelID (taskbar grouping, toast notifications).
#define TGESHKA_APP_USER_MODEL_ID "SteepMans.TGeshka"

#define TGESHKA_COMPANY "SteepMans"

// Own link scheme for importing gateway settings:
// tgeshka://gateway?url=...&token=...
#define TGESHKA_URL_SCHEME "tgeshka"

#define TGESHKA_SOURCES_URL "https://github.com/SteepMans/tgeshka"

// Default gateway address. Empty in the sources: a build passes it with
// -DTGESHKA_GATEWAY_URL=wss://... (see Telegram/cmake/tgeshka.cmake),
// otherwise the first launch asks for a tgeshka://gateway link or
// the address and the access key.
#ifndef TGESHKA_DEFAULT_GATEWAY_URL
#define TGESHKA_DEFAULT_GATEWAY_URL ""
#endif

#endif // TGESHKA_BRAND_H
