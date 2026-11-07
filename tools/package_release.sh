#!/bin/sh
# Package a Windows release: tools/package_release.sh <set> <out.zip> [--with-game-code]
# Contents: <set>.exe, SDL3.dll, README.txt (games/<set>/README.txt), LICENSE. Never ROMs or artwork.
# --with-game-code adds game.dll (the recompiled game code): PRIVATE TEST BUILDS ONLY, never a public release.
set -e
SET=$1; OUT=$(realpath -m "$2"); B=build/$SET/windows; WITH_GAME=$3
[ -f "$B/$SET.exe" ] || { echo "build first: tools/build_frontend.sh $SET windows"; exit 1; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cp "$B/$SET.exe" "$B/SDL3.dll" "$T/"
if [ "$WITH_GAME" = --with-game-code ]; then cp "$B/game.dll" "$T/"; echo "PRIVATE test build: contains game.dll"; fi
cp games/$SET/README.txt "$T/README.txt" && cp LICENSE "$T/LICENSE.txt"
# Windows line endings for the text files
for f in "$T/README.txt" "$T/LICENSE.txt"; do sed -i 's/$/\r/' "$f"; done
rm -f "$OUT"; (cd "$T" && zip -q -9 "$OUT" *)
unzip -l "$OUT"
