#!/usr/bin/env python3
# This file is part of TGeshka, an unofficial client
# based on Telegram Desktop (GPLv3).
#
# Adds an item to CHANGELOG-tgeshka.json (the "What's new" list built into
# the app) for the given version:
#   changelog_add.py 0.1.3-beta "Обновлено до Telegram Desktop 7.4.6."
# The item goes into the top entry if it is the same version (0.1.3-beta
# and 0.1.3-beta.2 are one entry), otherwise a new entry is put on top.
# An "Обновлено до Telegram Desktop" item replaces the previous one.
import datetime
import json
import pathlib
import sys

UPSTREAM_PREFIX = 'Обновлено до Telegram Desktop'


def base(version):
    return version.split('-', 1)[0]


def main():
    if len(sys.argv) != 3:
        sys.exit('usage: changelog_add.py <version> <item>')
    version, item = sys.argv[1], sys.argv[2].strip()
    path = pathlib.Path(__file__).resolve().parents[2] / 'CHANGELOG-tgeshka.json'
    releases = json.loads(path.read_text(encoding='utf-8'))
    if releases and base(releases[0]['version']) == base(version):
        top = releases[0]
    else:
        top = {
            'version': version,
            'date': datetime.date.today().isoformat(),
            'items': [],
        }
        releases.insert(0, top)
    items = top['items']
    if item.startswith(UPSTREAM_PREFIX):
        items[:] = [i for i in items if not i.startswith(UPSTREAM_PREFIX)]
    if item not in items:
        items.append(item)
    path.write_text(
        json.dumps(releases, ensure_ascii=False, indent=2) + '\n',
        encoding='utf-8',
        newline='\n')
    print(f'{path.name}: {top["version"]}: {item}')


if __name__ == '__main__':
    main()
