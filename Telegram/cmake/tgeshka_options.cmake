# This file is part of TGeshka, an unofficial client
# based on Telegram Desktop (GPLv3).
#
# Included from the top-level CMakeLists.txt before the desktop-app
# options are declared, so these values win over the defaults.

# No updates from Telegram servers: no updater, no version checks,
# no "update available" alerts. Own updates are not part of v1.
set(DESKTOP_APP_DISABLE_AUTOUPDATE ON CACHE BOOL "Disable autoupdate" FORCE)

# Crash reports would go to Telegram, not to us.
set(DESKTOP_APP_DISABLE_CRASH_REPORTS ON CACHE BOOL "Disable crash reports" FORCE)
