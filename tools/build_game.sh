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
python3 -I core/sh3/discover.py "$B/ram.bin" "$G/game.json" "$B/discovery.json"
python3 -I core/sh3/gen_c.py "$B/ram.bin" "$B/discovery.json" "$B/gen"
ls "$B"/gen/*.c | xargs -P"$(nproc)" -I{} cc -O1 -w -Icore/sh3 -c {} -o {}.o
cc -O2 -w -Ibuild -o "$B/lockstep_test" core/runtime/lockstep_test.c core/runtime/dispatch.c core/sh3/sh3_core.c "$B"/gen/*.c.o
"./$B/lockstep_test" "$ROMS/$U4" "$RAMSZ" "$ENTRY" "$CYC" | head -6
