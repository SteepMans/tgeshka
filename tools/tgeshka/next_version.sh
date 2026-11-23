#!/bin/sh
# This file is part of TGeshka, an unofficial client
# based on Telegram Desktop (GPLv3).
#
# Prints the next TGeshka version from the v* tags of the repository:
#   next_version.sh beta    0.1.2 -> 0.1.3-beta -> 0.1.3-beta.2 -> ...
#   next_version.sh stable  0.1.3-beta.2 -> 0.1.3 (0.1.2 -> 0.1.3)
set -eu

kind=${1:-beta}
git fetch -q --tags origin 2> /dev/null || true

stable=$(git tag -l 'v*' | grep -E '^v[0-9]+\.[0-9]+\.[0-9]+$' | sed 's/^v//' | sort -V | tail -n 1)
[ -n "$stable" ] || stable=0.0.0
major=$(echo "$stable" | cut -d. -f1)
minor=$(echo "$stable" | cut -d. -f2)
patch=$(echo "$stable" | cut -d. -f3)
next="$major.$minor.$((patch + 1))"

case "$kind" in
  stable)
    echo "$next" ;;
  beta)
    last=$(git tag -l "v$next-beta*" | sed "s/^v$next-beta//; s/^\.//" | sed 's/^$/1/' | sort -n | tail -n 1)
    if [ -z "$last" ]; then
      echo "$next-beta"
    else
      echo "$next-beta.$((last + 1))"
    fi ;;
  *)
    echo "usage: next_version.sh beta|stable" >&2; exit 1 ;;
esac
