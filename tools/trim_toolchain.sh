#!/bin/sh
# Make the trimmed C toolchain shipped in a release (toolchain/): just what the first-run builder needs to
# compile the generated C into game.dll (cc1, as, ld, CRT startup, a few import libraries, 10 headers).
# Usage: tools/trim_toolchain.sh <w64devkit dir (unpacked 2.9.1)> <out dir> <GPLv3 COPYING3 file>
# The whitelist below came from gcc -M and ld -t on generated code (2026-11-07, w64devkit 2.9.1, GCC 16.2.0).
set -e
K=$1; O=$2; GPL3=$3
V=$(ls "$K/lib/gcc/x86_64-w64-mingw32")
rm -rf "$O"; mkdir -p "$O"
cp_rel() { for f in "$@"; do mkdir -p "$O/$(dirname "$f")"; cp "$K/$f" "$O/$f"; done; }
cp_rel bin/gcc.exe bin/as.exe bin/ld.exe \
  libexec/gcc/x86_64-w64-mingw32/$V/cc1.exe libexec/gcc/x86_64-w64-mingw32/$V/collect2.exe \
  libexec/gcc/x86_64-w64-mingw32/$V/lto-wrapper.exe \
  lib/gcc/x86_64-w64-mingw32/$V/crtbegin.o lib/gcc/x86_64-w64-mingw32/$V/crtend.o lib/gcc/x86_64-w64-mingw32/$V/libgcc.a \
  lib/gcc/x86_64-w64-mingw32/$V/include/stddef.h lib/gcc/x86_64-w64-mingw32/$V/include/stdint.h \
  lib/dllcrt2.o lib/libadvapi32.a lib/libkernel32.a lib/libmingw32.a lib/libmingwex.a lib/libmsvcrt.a \
  lib/libpthread.a lib/libshell32.a lib/libuser32.a \
  include/_mingw.h include/_mingw_mac.h include/_mingw_secapi.h include/corecrt.h include/crtdefs.h \
  include/stddef.h include/stdint.h include/vadefs.h \
  COPYING.MinGW-w64-runtime.txt VERSION.txt
cp "$GPL3" "$O/COPYING3.txt"
cat > "$O/README.txt" <<EOF
This folder is a trimmed copy of w64devkit $(cat "$K/VERSION.txt") (https://github.com/skeeto/w64devkit),
a portable C compiler for Windows: GCC $V, GNU binutils and the Mingw-w64 runtime.
The game uses it once, on first start, to compile the game code from your own ROM into assets\\game.dll.

Licences: GCC and binutils are free software under the GNU GPL version 3 (COPYING3.txt). The Mingw-w64
runtime parts are under the licences in COPYING.MinGW-w64-runtime.txt. w64devkit itself is public domain.
Source code: the w64devkit $(cat "$K/VERSION.txt") release (its Dockerfile names the exact upstream GCC,
binutils and Mingw-w64 sources) at https://github.com/skeeto/w64devkit/releases, and the source archives
published with this game's release.
EOF
for f in "$O/README.txt" "$O/COPYING3.txt" "$O/COPYING.MinGW-w64-runtime.txt"; do sed -i 's/\r*$/\r/' "$f"; done
du -sh "$O"
