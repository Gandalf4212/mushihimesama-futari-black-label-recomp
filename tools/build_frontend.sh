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
u4 = [x for reg in r['regions'] if reg['region'] == 'maincpu' for x in reg['files']][0]
print(f'#define GAME_U4_CRC 0x{u4["crc32"]}u')
b = g['boot']; vt = b.get('vector_table') or {'addr': '0', 'size': '0'}
print(f'#define GAME_VBR {b["vbr"]}u\n#define GAME_VEC_ADDR {vt["addr"]}u\n#define GAME_VEC_SIZE {vt["size"]}u')
if 'lives' in g: print(f'#define GAME_LIVES_P1 {g["lives"]["p1"]}u\n#define GAME_LIVES_P2 {g["lives"]["p2"]}u')
if 'credits' in g: print(f'#define GAME_CREDITS {g["credits"]["addr"]}u')
if 'idle' in g: print(f'#define GAME_IDLE_PC {g["idle"]["pc"]}u\n#define GAME_IDLE_RAM {g["idle"]["ram_var"]}u')
snd = [reg['files'] for reg in r['regions'] if reg['region'] == 'ymz770']
roms = [x for reg in r['regions'] for x in reg['files']]
print('#define GAME_ROMS {' + ', '.join(f'{{"{x["name"]}", 0x{x["crc32"]}u, {x["size"]}u}}' for x in roms) + '}')
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
SRC="core/frontend/main_sdl.c core/frontend/present.c core/frontend/layout.c core/frontend/image.c core/frontend/romfind.c core/frontend/config.c core/frontend/input.c core/frontend/hud.c core/frontend/menu.c core/frontend/gamedll.c core/frontend/builder.c core/sh3/recomp.c core/runtime/cv1k.c core/runtime/timing.c core/runtime/dispatch.c core/render/blit_ref.c core/sh3/sh3_core.c"
if [ "$TARGET" = windows ]; then
  CC=x86_64-w64-mingw32-gcc
  mkdir -p "$OUT/obj"
  make -s build/isa_table.inc
  # what the first-run builder needs next to the exe (ours): runtime headers + glue, translator hints
  rm -rf "$OUT/recomp"; mkdir -p "$OUT/recomp/sh3" "$OUT/recomp/runtime"
  cp core/sh3/sh3.h core/sh3/sh3_ops.h "$OUT/recomp/sh3/"; cp core/runtime/recomp_abi.h core/runtime/recomp_dll.c "$OUT/recomp/runtime/"
  cp "$G/hints.txt" "$OUT/recomp/hints.txt"
  # game code -> game.dll, only for local testing (needs build/<set>/gen from tools/build_game.sh, i.e. your ROM).
  # Releases ship without it: the player's PC builds assets/game.dll (docs/builder-plan.md).
  if ls "$B"/gen/*.c >/dev/null 2>&1; then
    mkdir -p "$OUT/gen"; rm -f "$OUT"/obj/recomp_*.o   # (older builds linked the game code into the exe)
    ls "$B"/gen/*.c | xargs -P"$(nproc)" -I{} sh -c "o=$OUT/gen/\$(basename {}).o; [ \$o -nt {} ] || $CC -O1 -w -Icore/sh3 -c {} -o \$o"   # skip up-to-date objects
    U4CRC=$(sed -n 's/^#define GAME_U4_CRC \(0x[0-9a-fA-F]*\)u/\1/p' "$B/game_config.h")
    $CC -O2 -w -DRECOMP_U4_CRC=${U4CRC}u -c core/runtime/recomp_dll.c -o "$OUT/gen/recomp_dll.o"
    $CC -shared -o "$OUT/game.dll" "$OUT"/gen/*.o -static-libgcc -Wl,--exclude-all-symbols
  else
    echo "no $B/gen: building without game.dll (release build)"; rm -f "$OUT/game.dll"
  fi
  for f in $SRC; do $CC -O2 -w -Ibuild -I"$B" -Icore/frontend -I"$SDLW/include" -c $f -o "$OUT/obj/m_$(basename $f).o"; done
  for f in $CXXSRC; do x86_64-w64-mingw32-g++ -std=c++20 -O2 -w -c $f -o "$OUT/obj/m_$(basename $f).o"; done
  x86_64-w64-mingw32-g++ -o "$OUT/$SET.exe" "$OUT"/obj/*.o \
      -L"$SDLW/lib" -lmingw32 -lSDL3 -lole32 -luuid -lwindowscodecs -mwindows -static-libgcc -static-libstdc++
  cp "$SDLW/bin/SDL3.dll" "$OUT/"
else
  mkdir -p "$OUT/obj"
  for f in $SRC; do cc -O2 -w -Ibuild -I"$B" -Icore/frontend $(pkg-config --cflags sdl3) -c $f -o "$OUT/obj/m_$(basename $f).o"; done
  for f in $CXXSRC; do g++ -std=c++20 -O2 -w -c $f -o "$OUT/obj/m_$(basename $f).o"; done
  g++ -o "$OUT/$SET" "$OUT"/obj/m_*.o "$B"/gen/*.c.o $(pkg-config --libs sdl3)
fi
echo "built $OUT"
