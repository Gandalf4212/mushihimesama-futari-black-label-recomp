#!/usr/bin/env python3
"""Verify a user's ROM files against a game's romset.json.

Usage: verify_roms.py games/<set>/romset.json <rom_dir_or_zip>
Exit code 0 if every file matches by SHA1, 1 otherwise.
"""
import hashlib, json, os, sys, zipfile

def open_source(path):
    if zipfile.is_zipfile(path):
        z = zipfile.ZipFile(path)
        names = {os.path.basename(n): n for n in z.namelist()}
        return lambda f: z.read(names[f]) if f in names else None
    return lambda f: open(os.path.join(path, f), 'rb').read() if os.path.exists(os.path.join(path, f)) else None

def main():
    manifest = json.load(open(sys.argv[1]))
    read = open_source(sys.argv[2])
    ok = True
    for region in manifest['regions']:
        for f in region['files']:
            data = read(f['name'])
            if data is None:
                print(f"MISSING  {f['name']}"); ok = False; continue
            sha1 = hashlib.sha1(data).hexdigest()
            good = sha1 == f['sha1'] and len(data) == f['size']
            ok &= good
            print(f"{'OK      ' if good else 'BAD     '} {f['name']:<16} {len(data):>10}  sha1 {sha1}")
    print('ROM set verified.' if ok else 'ROM set does NOT match.')
    sys.exit(0 if ok else 1)

if __name__ == '__main__':
    main()
