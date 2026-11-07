#!/bin/sh
# Full local pipeline for one game. Everything it produces stays in build/ (gitignored).
# Usage: tools/build_game.sh <set> <rom_dir_or_zip> [lockstep_cycles]
set -e
SET=$1; ROMS=$2; CYC=${3:-400000000}
G=games/$SET; B=build/$SET
mkdir -p "$B/gen"
python3 tools/verify_roms.py "$G/romset.json" "$ROMS"
U4=$(python3 -c "import json;print([f['name'] for r in json.load(open('$G/romset.json'))['regions'] if r['region']=='maincpu' for f in r['files']][0])")
RAMSZ=$(python3 -c "import json;print(int(json.load(open('$G/game.json'))['ram_size'],16))" | xargs printf '%x')
ENTRY=$(python3 -c "import json;print(json.load(open('$G/game.json'))['boot']['main_entry'][2:])")
make -s
./build/boot_runner "$ROMS/$U4" "$RAMSZ" "$ENTRY" "$B/ram.bin" | head -1
# translator: core/sh3/recomp.c (the C port of discover.py + gen_c.py; tools/check_translator.sh diffs them)
cat "$G/hints.txt" "$B/misses.txt" 2>/dev/null > "$B/hints_all.txt" || true
VEC=$(python3 -c "import json;v=json.load(open('$G/game.json'))['boot'].get('vector_table');print(f\"--vectors {v['addr'][2:]} {v['size'][2:]}\" if v else '')")
VBR=$(python3 -c "import json;print(json.load(open('$G/game.json'))['boot']['vbr'][2:])")
rm -rf "$B/gen"; mkdir -p "$B/gen"
./build/recomp "$B/ram.bin" "$B/gen" --entry "$ENTRY" --vbr "$VBR" $VEC --extra "$B/hints_all.txt"
ls "$B"/gen/*.c | xargs -P"$(nproc)" -I{} cc -O1 -w -Icore/sh3 -c {} -o {}.o
cc -O2 -w -Ibuild -o "$B/lockstep_test" core/runtime/lockstep_test.c core/runtime/dispatch.c core/sh3/sh3_core.c "$B"/gen/*.c.o
"./$B/lockstep_test" "$ROMS/$U4" "$RAMSZ" "$ENTRY" "$CYC" | head -6
U2=$(python3 -c "import json;print([f['name'] for r in json.load(open('$G/romset.json'))['regions'] if r['region']=='nand' for f in r['files']][0])")
mkdir -p "$B/obj"
for f in core/runtime/run_headless.c core/runtime/cv1k.c core/runtime/timing.c core/render/blit_ref.c core/render/png.c core/runtime/dispatch.c core/sh3/sh3_core.c; do cc -O2 -w -Ibuild -c "$f" -o "$B/obj/$(basename "$f").o"; done
for f in core/sound/ymz770.cpp core/sound/mpeg_audio.cpp; do g++ -std=c++20 -O2 -w -c "$f" -o "$B/obj/$(basename "$f").o"; done
g++ -O2 -o "$B/run_headless" "$B"/obj/*.o "$B"/gen/*.c.o
"./$B/run_headless" "$ROMS/$U4" "$ROMS/$U2" "$RAMSZ" "$ENTRY" 1200 --report 300 --misses "$B/misses.new" | tail -6
# profile-guided discovery: keep every address the runtime had to interpret; rebuild to include them
cat "$B/misses.new" "$B/misses.txt" 2>/dev/null | sort -u > "$B/misses.tmp"; mv "$B/misses.tmp" "$B/misses.txt"; rm -f "$B/misses.new"
echo "runtime misses recorded: $(wc -l < "$B/misses.txt") (rerun this script to compile them in)"
