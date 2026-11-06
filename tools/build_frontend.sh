#!/bin/sh
# Build the playable frontend for one game (after tools/build_game.sh generated build/<set>/gen).
# Usage: tools/build_frontend.sh <set> linux|windows [sdl3_mingw_dir]
set -e
SET=$1; TARGET=${2:-linux}; SDLW=${3:-$(cd "$(dirname "$0")/../.." && pwd)/deps/SDL3-3.4.20/x86_64-w64-mingw32}
G=games/$SET; B=build/$SET; OUT=$B/$TARGET; mkdir -p "$OUT"
python3 - "$G" > "$B/game_config.h" <<'PY'
import json, sys
g = json.load(open(sys.argv[1] + '/game.json')); r = json.load(open(sys.argv[1] + '/romset.json'))
f = {reg['region']: reg['files'][0]['name'] for reg in r['regions']}
print(f'#define GAME_SET "{g["set"]}"\n#define GAME_TITLE "{g["title"].split(" (")[0]}"')
print(f'#define GAME_RAM_SIZE {int(g["ram_size"], 16)}u\n#define GAME_MAIN_ENTRY {g["boot"]["main_entry"]}u')
print(f'#define GAME_U4 "{f["maincpu"]}"\n#define GAME_U2 "{f["nand"]}"')
if 'idle' in g: print(f'#define GAME_IDLE_PC {g["idle"]["pc"]}u\n#define GAME_IDLE_RAM {g["idle"]["ram_var"]}u')
snd = [reg['files'] for reg in r['regions'] if reg['region'] == 'ymz770']
if snd and len(snd[0]) >= 2: print(f'#define GAME_SND0 "{snd[0][0]["name"]}"\n#define GAME_SND1 "{snd[0][1]["name"]}"')
PY
# our shaders: GLSL -> SPIR-V (glslang-tools) -> C arrays in $B/shaders_spv.h (generated, not committed)
for f in core/frontend/shaders/*.frag; do glslangValidator -V -o "$B/$(basename $f .frag)_frag.spv" "$f" >/dev/null || { glslangValidator -V -o /dev/null "$f"; exit 1; }; done
python3 - "$B" > "$B/shaders_spv.h" <<'PY'
import glob, os, sys
for p in sorted(glob.glob(sys.argv[1] + '/*_frag.spv')):
    n = os.path.basename(p)[:-4]; d = open(p, 'rb').read()
    print(f'static const unsigned char {n}_spv[] = {{' + ','.join(str(b) for b in d) + '};')
    print(f'static const size_t {n}_spv_len = {len(d)};')
PY
CXXSRC="core/sound/ymz770.cpp core/sound/mpeg_audio.cpp"
SRC="core/frontend/main_sdl.c core/frontend/present.c core/frontend/config.c core/frontend/input.c core/frontend/hud.c core/frontend/menu.c core/runtime/cv1k.c core/runtime/timing.c core/runtime/dispatch.c core/render/blit_ref.c core/sh3/sh3_core.c"
if [ "$TARGET" = windows ]; then
  CC=x86_64-w64-mingw32-gcc
  mkdir -p "$OUT/obj"
  ls "$B"/gen/*.c | xargs -P"$(nproc)" -I{} sh -c "$CC -O1 -w -Icore/sh3 -c {} -o $OUT/obj/\$(basename {}).o"
  for f in $SRC; do $CC -O2 -w -Ibuild -I"$B" -Icore/frontend -I"$SDLW/include" -c $f -o "$OUT/obj/m_$(basename $f).o"; done
  for f in $CXXSRC; do x86_64-w64-mingw32-g++ -std=c++20 -O2 -w -c $f -o "$OUT/obj/m_$(basename $f).o"; done
  x86_64-w64-mingw32-g++ -o "$OUT/$SET.exe" "$OUT"/obj/*.o \
      -L"$SDLW/lib" -lmingw32 -lSDL3 -mwindows -static-libgcc -static-libstdc++
  cp "$SDLW/bin/SDL3.dll" "$OUT/"
else
  mkdir -p "$OUT/obj"
  for f in $SRC; do cc -O2 -w -Ibuild -I"$B" -Icore/frontend $(pkg-config --cflags sdl3) -c $f -o "$OUT/obj/m_$(basename $f).o"; done
  for f in $CXXSRC; do g++ -std=c++20 -O2 -w -c $f -o "$OUT/obj/m_$(basename $f).o"; done
  g++ -o "$OUT/$SET" "$OUT"/obj/m_*.o "$B"/gen/*.c.o $(pkg-config --libs sdl3)
fi
echo "built $OUT"
