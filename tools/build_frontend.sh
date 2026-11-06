#!/bin/sh
# Build the playable frontend for one game (after tools/build_game.sh generated build/<set>/gen).
# Usage: tools/build_frontend.sh <set> linux|windows [sdl2_mingw_dir]
set -e
SET=$1; TARGET=${2:-linux}; SDLW=${3:-$(cd "$(dirname "$0")/../.." && pwd)/deps/SDL2-2.30.9/x86_64-w64-mingw32}
G=games/$SET; B=build/$SET; OUT=$B/$TARGET; mkdir -p "$OUT"
python3 - "$G" > "$B/game_config.h" <<'PY'
import json, sys
g = json.load(open(sys.argv[1] + '/game.json')); r = json.load(open(sys.argv[1] + '/romset.json'))
f = {reg['region']: reg['files'][0]['name'] for reg in r['regions']}
print(f'#define GAME_SET "{g["set"]}"\n#define GAME_TITLE "{g["title"].split(" (")[0]}"')
print(f'#define GAME_RAM_SIZE {int(g["ram_size"], 16)}u\n#define GAME_MAIN_ENTRY {g["boot"]["main_entry"]}u')
print(f'#define GAME_U4 "{f["maincpu"]}"\n#define GAME_U2 "{f["nand"]}"')
PY
SRC="core/frontend/main_sdl.c core/frontend/config.c core/frontend/input.c core/frontend/hud.c core/runtime/cv1k.c core/runtime/timing.c core/runtime/dispatch.c core/render/blit_ref.c core/sh3/sh3_core.c"
if [ "$TARGET" = windows ]; then
  CC=x86_64-w64-mingw32-gcc
  mkdir -p "$OUT/obj"
  ls "$B"/gen/*.c | xargs -P"$(nproc)" -I{} sh -c "$CC -O1 -w -Icore/sh3 -c {} -o $OUT/obj/\$(basename {}).o"
  $CC -O2 -w -Ibuild -I"$B" -Icore/frontend -I"$SDLW/include/SDL2" -Dmain=SDL_main -o "$OUT/$SET.exe" $SRC "$OUT"/obj/*.o \
      -L"$SDLW/lib" -lmingw32 -lSDL2main -lSDL2 -mwindows -static-libgcc
  cp "$SDLW/bin/SDL2.dll" "$OUT/"
else
  cc -O2 -w -Ibuild -I"$B" -Icore/frontend $(sdl2-config --cflags) -o "$OUT/$SET" $SRC "$B"/gen/*.c.o $(sdl2-config --libs)
fi
echo "built $OUT"
