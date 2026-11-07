#!/bin/sh
# Check the C translator (core/sh3/recomp.c) against the Python reference (discover.py + gen_c.py):
# both must write byte-identical C. Usage: tools/check_translator.sh <set>   (after tools/build_game.sh)
set -e
SET=$1; G=games/$SET; B=build/$SET; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
make -s build/recomp
VEC=$(python3 -c "import json;v=json.load(open('$G/game.json'))['boot'].get('vector_table');print(f\"--vectors {v['addr'][2:]} {v['size'][2:]}\" if v else '')")
VBR=$(python3 -c "import json;print(json.load(open('$G/game.json'))['boot']['vbr'][2:])")
ENTRY=$(python3 -c "import json;print(json.load(open('$G/game.json'))['boot']['main_entry'][2:])")
cat "$G/hints.txt" "$B/misses.txt" 2>/dev/null > "$T/hints.txt" || true
mkdir -p "$T/py" "$T/c"
python3 -I core/sh3/discover.py "$B/ram.bin" "$G/game.json" "$T/d.json" --extra "$T/hints.txt" >/dev/null
python3 -I core/sh3/gen_c.py "$B/ram.bin" "$T/d.json" "$T/py" >/dev/null
./build/recomp "$B/ram.bin" "$T/c" --entry "$ENTRY" --vbr "$VBR" $VEC --extra "$T/hints.txt" >/dev/null
diff -rq "$T/py" "$T/c" && echo "translator OK: C output identical to Python ($(ls "$T/c" | wc -l) files)"
