#!/usr/bin/env python3
"""Generate per-game ROM manifests from MAME's CV1000 driver (src/mame/cave/cv1k.cpp).

Manifests hold only file names, sizes and checksums (facts, no game data).
Usage: mame_romsets.py path/to/cv1k.cpp [out_dir] [--mame-version X]
Writes out_dir/<set>/romset.json for every ROM_START in the driver.
"""
import json, os, re, sys

def parse(src):
    games = {}
    for m in re.finditer(r'^GAME\(\s*(\d+),\s*(\w+),\s*(\w+),\s*(\w+),\s*(\w+),\s*\w+,\s*(\w+),\s*(\w+),\s*"([^"]*)",\s*"([^"]*)"', src, re.M):
        year, name, parent, machine, inputs, init, rot, maker, desc = m.groups()
        games[name] = dict(year=int(year), parent=None if parent == "0" else parent, machine=machine,
                           inputs=inputs, init=init, rotation=rot, manufacturer=maker, description=desc)
    sets = {}
    for m in re.finditer(r'^ROM_START\(\s*(\w+)\s*\)(.*?)^ROM_END', src, re.M | re.S):
        name, body = m.group(1), m.group(2)
        regions, cur = [], None
        for line in body.splitlines():
            line = line.split('//')[0].strip()
            r = re.match(r'ROM_REGION\(\s*(0x[0-9a-fA-F]+),\s*"(\w+)"', line)
            if r:
                cur = dict(region=r.group(2), size=int(r.group(1), 16), files=[]); regions.append(cur); continue
            f = re.match(r'(ROM_LOAD\w*)\(\s*"([^"]+)",\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),\s*CRC\((\w+)\)\s*SHA1\((\w+)\)', line)
            if f and cur is not None:
                cur['files'].append(dict(name=f.group(2), load=f.group(1), offset=int(f.group(3), 16),
                                         size=int(f.group(4), 16), crc32=f.group(5).lower(), sha1=f.group(6).lower()))
                continue
            rr = re.match(r'ROM_RELOAD\(\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+)', line)
            if rr and cur is not None and cur['files']:
                cur['files'][-1].setdefault('reload', []).append(int(rr.group(1), 16))
        sets[name] = dict(set=name, **games.get(name, dict(note="no GAME() entry in driver")), regions=regions)
    return sets

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    ver = next((sys.argv[i + 1] for i, a in enumerate(sys.argv) if a == '--mame-version'), None)
    src = open(args[0], encoding='utf-8').read()
    out = args[1] if len(args) > 1 else 'games'
    for name, s in parse(src).items():
        if ver: s['source'] = f'MAME {ver} src/mame/cave/cv1k.cpp'
        d = os.path.join(out, name); os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, 'romset.json'), 'w') as fh:
            json.dump(s, fh, indent=2); fh.write('\n')
        print(name, s.get('machine', '?'), s.get('description', s.get('note')))

if __name__ == '__main__':
    main()
