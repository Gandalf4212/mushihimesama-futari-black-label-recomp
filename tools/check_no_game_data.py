#!/usr/bin/env python3
"""Refuse commits that look like game data. Used by .githooks/pre-commit and CI.

Blocks: files over 512 KB, known ROM file names, and any staged file whose SHA1
matches a ROM listed in games/*/romset.json.
Usage: check_no_game_data.py [files...]   (default: files staged in git)
"""
import glob, hashlib, json, os, subprocess, sys

MAX = 512 * 1024
ROM_NAMES = {'u2', 'u4', 'u23', 'u24'}

def main():
    files = sys.argv[1:] or subprocess.run(['git', 'diff', '--cached', '--name-only', '--diff-filter=ACM'],
                                           capture_output=True, text=True).stdout.split()
    known = {f['sha1']: f['name'] for m in glob.glob('games/*/romset.json')
             for r in json.load(open(m))['regions'] for f in r['files']}
    bad = []
    for f in files:
        if not os.path.isfile(f):
            continue
        base = os.path.basename(f)
        if base in ROM_NAMES or base.endswith(('_u2', '_u4')) or base in known.values():
            bad.append(f'{f}: ROM file name'); continue
        if os.path.getsize(f) > MAX:
            bad.append(f'{f}: larger than 512 KB'); continue
        if hashlib.sha1(open(f, 'rb').read()).hexdigest() in known:
            bad.append(f'{f}: matches a ROM checksum')
    for b in bad:
        print('BLOCKED', b)
    sys.exit(1 if bad else 0)

if __name__ == '__main__':
    main()
