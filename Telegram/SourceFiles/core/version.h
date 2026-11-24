/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/const_string.h"
#include "tgeshka/tgeshka_brand.h"

#define TDESKTOP_REQUESTED_ALPHA_VERSION (0ULL)

#ifdef TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION TDESKTOP_REQUESTED_ALPHA_VERSION
#else // TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION (0ULL)
#endif // TDESKTOP_ALLOW_CLOSED_ALPHA

// used in Updater.cpp and Setup.iss for Windows
constexpr auto AppId = TGESHKA_APP_ID ""_cs;
constexpr auto AppNameOld = TGESHKA_APP_NAME " Old"_cs;
constexpr auto AppName = TGESHKA_APP_NAME ""_cs;
constexpr auto AppFile = TGESHKA_APP_FILE ""_cs;
constexpr auto AppVersion = 7004006;
constexpr auto AppVersionStr = "7.4.6";
constexpr auto AppBetaVersion = false;
constexpr auto AppAlphaVersion = TDESKTOP_ALPHA_VERSION;
