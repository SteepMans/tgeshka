# TGeshka

TGeshka is an unofficial Telegram client for Windows based on [Telegram Desktop](https://github.com/telegramdesktop/tdesktop)
(GPLv3). It is not affiliated with Telegram FZ-LLC. Every MTProto connection goes as a WebSocket over TLS to a gateway
that forwards the same encrypted stream to Telegram data centers (see [docs/gateway.md](docs/gateway.md)).
The gateway itself is not part of this repository and no gateway address is built into the sources.

TGeshka - неофициальный клиент Telegram для Windows на базе Telegram Desktop. Все соединения с Telegram идут через
шлюз по WebSocket. Адрес шлюза и ключ доступа выдаёт владелец шлюза (ссылка `tgeshka://gateway?url=...&token=...`).

Branch `main` = upstream release tag + TGeshka changes. Submodules point to the upstream repositories.

## Сборка

- **GitHub Actions** (`.github/workflows/windows.yml`): сборка Windows x64 Release на `windows-latest` для `main`,
  pull request и вручную (Run workflow). Артефакт - portable zip. Тег `v*` собирает и публикует zip в GitHub Release.
  Зависимости (Libraries, ThirdParty) кэшируются между запусками, ключ - хэш `Telegram/build/prepare/prepare.py`.
  Секреты репозитория: `TG_API_ID`, `TG_API_HASH` (api_id/api_hash), `TGESHKA_GATEWAY_URL` (адрес шлюза по
  умолчанию). Без секретов (pull request из форка) клиент собирается с тестовыми api_id upstream, без адреса шлюза,
  и не публикуется.
- **Локально** (Windows, Visual Studio 2022 с C++ x64 и тулсетом 14.44, Python, Git): репозиторий в
  `<BuildPath>\tdesktop`, первый запуск `.\build.ps1 -Prepare` (зависимости в `<BuildPath>\Libraries`, около 1,5 ч
  и 60+ ГБ на пике), дальше `.\build.ps1` - инкрементальная сборка Release x64 и zip в `out\`. api_id/api_hash
  берутся из `%USERPROFILE%\.config\tg-fork\api.txt` (`api_id=...`, `api_hash=...`), адрес шлюза - из
  `-GatewayUrl`, `TGESHKA_GATEWAY_URL` или `%USERPROFILE%\.config\tg-fork\gateway.txt`. В репозиторий и лог они
  не попадают. Параметры - в шапке `build.ps1`.

## Как обновиться с upstream

1. Получить новый релизный тег upstream (теги upstream не тянутся автоматически):

       git remote add upstream https://github.com/telegramdesktop/tdesktop.git
       git fetch upstream refs/tags/vX.Y.Z:refs/tags/upstream-vX.Y.Z

2. Создать ветку от свежего `main` и влить тег (не rebase, история `main` не переписывается):

       git fetch origin main
       git checkout -b feature/upstream-vX.Y.Z origin/main
       git merge --no-ff upstream-vX.Y.Z

3. Конфликты:
   - `.github/workflows/*`: у нас только `windows.yml`, workflow upstream снова удалить;
   - `README.md`: оставить этот раздел наверху;
   - остальное - по коду TGeshka.
4. Обновить сабмодули до версий тега: `git submodule update --init --recursive`.
5. Если upstream поменял версии зависимостей (`Telegram/build/prepare/prepare.py`), CI соберёт их заново
   (новый ключ кэша), локально - `build.ps1 -Prepare`.
6. Открыть PR в `main`, после мержа и зелёной сборки поставить тег релиза `vX.Y.Z`. Теги upstream с префиксом
   `v` в этот репозиторий не пушить: они запускают релиз.

# [Telegram Desktop][telegram_desktop] – Official Messenger

This is the complete source code and the build instructions for the official [Telegram][telegram] messenger desktop client, based on the [Telegram API][telegram_api] and the [MTProto][telegram_proto] secure protocol.

[![Version](https://badge.fury.io/gh/telegramdesktop%2Ftdesktop.svg)](https://github.com/telegramdesktop/tdesktop/releases)
[![Build Status](https://github.com/telegramdesktop/tdesktop/workflows/Windows./badge.svg)](https://github.com/telegramdesktop/tdesktop/actions)
[![Build Status](https://github.com/telegramdesktop/tdesktop/workflows/MacOS./badge.svg)](https://github.com/telegramdesktop/tdesktop/actions)
[![Build Status](https://github.com/telegramdesktop/tdesktop/workflows/Linux./badge.svg)](https://github.com/telegramdesktop/tdesktop/actions)
[![Built with Depot](https://img.shields.io/badge/Built%20with-Depot.dev-46A75A)](https://depot.dev)

[![Preview of Telegram Desktop][preview_image]][preview_image_url]

The source code is published under GPLv3 with OpenSSL exception, the license is available [here][license].

## Supported systems

The latest version is available for

* [Windows 7 and above (64 bit)](https://telegram.org/dl/desktop/win64) ([portable](https://telegram.org/dl/desktop/win64_portable))
* [Windows 7 and above (32 bit)](https://telegram.org/dl/desktop/win) ([portable](https://telegram.org/dl/desktop/win_portable))
* [macOS 10.13 and above](https://telegram.org/dl/desktop/mac)
* [Linux static build for 64 bit](https://telegram.org/dl/desktop/linux)
* [Snap](https://snapcraft.io/telegram-desktop)
* [Flatpak](https://flathub.org/apps/details/org.telegram.desktop)

## Old system versions

Version **4.9.9** was the last that supports older systems

* [macOS 10.12](https://updates.tdesktop.com/tmac/tsetup.4.9.9.dmg)
* [Linux with glibc < 2.28 static build](https://updates.tdesktop.com/tlinux/tsetup.4.9.9.tar.xz)

Version **2.4.4** was the last that supports older systems

* [OS X 10.10 and 10.11](https://updates.tdesktop.com/tosx/tsetup-osx.2.4.4.dmg)
* [Linux static build for 32 bit](https://updates.tdesktop.com/tlinux32/tsetup32.2.4.4.tar.xz)

Version **1.8.15** was the last that supports older systems

* [Windows XP and Vista](https://updates.tdesktop.com/tsetup/tsetup.1.8.15.exe) ([portable](https://updates.tdesktop.com/tsetup/tportable.1.8.15.zip))
* [OS X 10.8 and 10.9](https://updates.tdesktop.com/tmac/tsetup.1.8.15.dmg)
* [OS X 10.6 and 10.7](https://updates.tdesktop.com/tmac32/tsetup32.1.8.15.dmg)

## Third-party

* Qt 6 ([LGPL](http://doc.qt.io/qt-6/lgpl.html)) slightly patched
* OpenSSL 3.2.1 ([Apache License 2.0](https://openssl-library.org/source/license/apache-license-2.0.txt))
* WebRTC ([New BSD License](https://github.com/desktop-app/tg_owt/blob/master/LICENSE))
* zlib ([zlib License](http://www.zlib.net/zlib_license.html))
* LZMA SDK 9.20 ([public domain](http://www.7-zip.org/sdk.html))
* liblzma ([public domain](http://tukaani.org/xz/))
* Google Breakpad ([License](https://chromium.googlesource.com/breakpad/breakpad/+/master/LICENSE))
* Google Crashpad ([Apache License 2.0](https://chromium.googlesource.com/crashpad/crashpad/+/master/LICENSE))
* GYP ([BSD License](https://github.com/bnoordhuis/gyp/blob/master/LICENSE))
* Ninja ([Apache License 2.0](https://github.com/ninja-build/ninja/blob/master/COPYING))
* OpenAL Soft ([LGPL](https://github.com/kcat/openal-soft/blob/master/COPYING))
* Opus codec ([BSD License](http://www.opus-codec.org/license/))
* FFmpeg ([LGPL](https://www.ffmpeg.org/legal.html))
* Guideline Support Library ([MIT License](https://github.com/Microsoft/GSL/blob/master/LICENSE))
* Range-v3 ([Boost License](https://github.com/ericniebler/range-v3/blob/master/LICENSE.txt))
* Open Sans font ([Apache License 2.0](http://www.apache.org/licenses/LICENSE-2.0.html))
* Vazirmatn font ([SIL Open Font License 1.1](https://github.com/rastikerdar/vazirmatn/blob/master/OFL.txt))
* Emoji alpha codes ([MIT License](https://github.com/emojione/emojione/blob/master/extras/alpha-codes/LICENSE.md))
* xxHash ([BSD License](https://github.com/Cyan4973/xxHash/blob/dev/LICENSE))
* QR Code generator ([MIT License](https://github.com/nayuki/QR-Code-generator#license))
* CMake ([New BSD License](https://github.com/Kitware/CMake/blob/master/Copyright.txt))
* Hunspell ([LGPL](https://github.com/hunspell/hunspell/blob/master/COPYING.LESSER))
* zxcvbn-c ([MIT License](https://github.com/tsyrogit/zxcvbn-c/blob/master/LICENSE.txt))
* twidget ([BSD 3-Clause License](https://github.com/TelegramMessenger/twidget/blob/main/LICENSE))
* litehtml ([BSD 3-Clause License](https://github.com/litehtml/litehtml/blob/master/LICENSE))
* Gumbo ([Apache License 2.0](https://github.com/google/gumbo-parser/blob/master/COPYING))
* Nova ([Mozilla Public License 2.0](https://github.com/trynova/nova/blob/main/LICENSE.md))
* FrequencyWords by Hermit Dave ([Creative Commons Attribution-ShareAlike 4.0](https://creativecommons.org/licenses/by-sa/4.0/))

## Build instructions

* [Windows][win]
* [macOS][mac]
* [GNU/Linux using Docker][linux]

[//]: # (LINKS)
[telegram]: https://telegram.org
[telegram_desktop]: https://desktop.telegram.org
[telegram_api]: https://core.telegram.org
[telegram_proto]: https://core.telegram.org/mtproto
[license]: LICENSE
[win]: docs/building-win.md
[mac]: docs/building-mac.md
[linux]: docs/building-linux.md
[preview_image]: https://github.com/telegramdesktop/tdesktop/blob/dev/docs/assets/preview.png "Preview of Telegram Desktop"
[preview_image_url]: https://raw.githubusercontent.com/telegramdesktop/tdesktop/dev/docs/assets/preview.png

## Thanks to

<a href="https://depot.dev">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://depot.dev/assets/brand/1693758816/depot-logo-horizontal-on-dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="https://depot.dev/assets/brand/1693758816/depot-logo-horizontal-on-light.svg">
    <img alt="Depot" src="https://depot.dev/assets/brand/1693758816/depot-logo-horizontal-on-light.svg" width="150">
  </picture>
</a>

CI infrastructure sponsored by [Depot](https://depot.dev) — fast GitHub Actions runners.

