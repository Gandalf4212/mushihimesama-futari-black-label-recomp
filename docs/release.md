# Release checklist

1. Bump `core/frontend/version.h` and add the version to `CHANGELOG.md`. If the generated code's
   interface changed (sh3.h, recomp_abi.h, gen_c.py / recomp.c output), bump `RECOMP_ABI`.
2. `tools/check_translator.sh futaribl` and a headless run (`run_headless ... --input stage1.txt`).
3. `tools/build_frontend.sh futaribl windows`, then
   `tools/package_release.sh futaribl futaribl-vX.Y.Z-windows-x64.zip` (never `--with-game-code`).
   Check: `unzip -l` lists no `game.dll`.
4. Test the zip in an empty folder on Windows: ROM check -> building screen -> game; second start
   skips the build.
5. Tag `vX.Y.Z`, push the tag, create the GitHub release with the CHANGELOG section as notes, and
   attach the zip **plus the toolchain sources** (below).

## Toolchain sources (GPLv3: GCC and binutils ship as binaries in `toolchain/`)

Attach to every release that ships the toolchain (w64devkit 2.9.1):

| File | From |
|---|---|
| w64devkit-2.9.1-source.zip | `git archive` of tag v2.9.1 of https://github.com/skeeto/w64devkit (Dockerfile, patches) |
| gcc-16.2.0.tar.xz | https://ftp.gnu.org/gnu/gcc/gcc-16.2.0/gcc-16.2.0.tar.xz |
| binutils-2.47.tar.xz | https://ftp.gnu.org/gnu/binutils/binutils-2.47.tar.xz |
| gmp-6.3.0.tar.xz | https://ftp.gnu.org/gnu/gmp/gmp-6.3.0.tar.xz |
| mpc-1.4.1.tar.xz | https://ftp.gnu.org/gnu/mpc/mpc-1.4.1.tar.xz |
| mpfr-4.2.2.tar.xz | https://ftp.gnu.org/gnu/mpfr/mpfr-4.2.2.tar.xz |
| mingw-w64-v14.0.0.tar.bz2 | https://downloads.sourceforge.net/project/mingw-w64/mingw-w64/mingw-w64-release/mingw-w64-v14.0.0.tar.bz2 |

(The build workspace can't reach ftp.gnu.org or SourceForge: download these on a PC.)
