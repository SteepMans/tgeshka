# This file is part of TGeshka, an unofficial client
# based on Telegram Desktop (GPLv3).
#
# Every upstream release bumps the version next to the brand lines of
# core/version.h and winrc/Telegram.rc, so a merge always conflicts
# there. The upstream-sync workflow takes the upstream file and puts the
# brand back with this script:
#
#   python3 rebrand_upstream.py Telegram/SourceFiles/core/version.h
#
# Fails if an expected upstream line is gone: then a human looks.
import pathlib
import sys

RULES = {
    'Telegram/SourceFiles/core/version.h': [
        ('#include "base/const_string.h"\n',
         '#include "base/const_string.h"\n#include "tgeshka/tgeshka_brand.h"\n'),
        ('constexpr auto AppId = "{53F49750-6209-4FBF-9CA8-7A333C87D1ED}"_cs;\n',
         'constexpr auto AppId = TGESHKA_APP_ID ""_cs;\n'),
        ('constexpr auto AppNameOld = "Telegram Win (Unofficial)"_cs;\n',
         'constexpr auto AppNameOld = TGESHKA_APP_NAME " Old"_cs;\n'),
        ('constexpr auto AppName = "Telegram Desktop"_cs;\n',
         'constexpr auto AppName = TGESHKA_APP_NAME ""_cs;\n'),
        ('constexpr auto AppFile = "Telegram"_cs;\n',
         'constexpr auto AppFile = TGESHKA_APP_FILE ""_cs;\n'),
    ],
    'Telegram/Resources/winrc/Telegram.rc': [
        ('#undef APSTUDIO_READONLY_SYMBOLS\n\n',
         '#undef APSTUDIO_READONLY_SYMBOLS\n\n'
         '#include "../../SourceFiles/tgeshka/tgeshka_brand.h"\n\n'),
        ('VALUE "CompanyName", "Telegram FZ-LLC"',
         'VALUE "CompanyName", TGESHKA_COMPANY'),
        ('VALUE "FileDescription", "Telegram Desktop"',
         'VALUE "FileDescription", TGESHKA_APP_NAME'),
        ('VALUE "ProductName", "Telegram Desktop"',
         'VALUE "ProductName", TGESHKA_APP_NAME'),
    ],
}


def main():
    for name in sys.argv[1:]:
        key = name.replace('\\', '/')
        if key not in RULES:
            sys.exit(f'rebrand_upstream: no rules for {name}')
        path = pathlib.Path(name)
        raw = path.read_bytes().decode('utf-8')
        crlf = '\r\n' in raw
        text = raw.replace('\r\n', '\n')
        for old, new in RULES[key]:
            if text.count(old) != 1:
                sys.exit(f'rebrand_upstream: {name}: expected once: {old.strip()}')
            text = text.replace(old, new)
        if crlf:
            text = text.replace('\n', '\r\n')
        path.write_bytes(text.encode('utf-8'))
        print(f'rebranded {name}')


if __name__ == '__main__':
    main()
