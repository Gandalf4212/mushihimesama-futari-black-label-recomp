# Release checklist

1. Bump `core/frontend/version.h` and add the version to `CHANGELOG.md`. If the generated code's
   interface changed (sh3.h, recomp_abi.h, gen_c.py / recomp.c output), bump `RECOMP_ABI`.
2. `tools/check_translator.sh futaribl` and a headless run (`run_headless ... --input stage1.txt`).
3. Commit and push. Optional dry run: Actions > release > Run workflow (files kept as an artifact).
4. On GitHub: Releases > Draft a new release, tag `vX.Y.Z` (must match version.h), CHANGELOG section as
   notes, **Publish**. `.github/workflows/release.yml` then builds on a clean runner (no ROM needed),
   refuses a zip with `game.dll`, and attaches `futaribl-vX.Y.Z-windows-x64.zip` plus
   `toolchain-sources-vX.Y.Z.zip` (the sources below in one archive; a few minutes).
   To redo only the sources of an existing release: Actions > release > Run workflow, tag `vX.Y.Z`.
5. Test the attached zip in an empty folder on Windows: ROM check -> building screen -> game; second
   start skips the build.

Local equivalent: `tools/build_frontend.sh futaribl windows` (without build/<set>/gen it makes no
game.dll), `tools/package_release.sh futaribl out.zip`.

## Toolchain sources (GPLv3: GCC and binutils ship as binaries in `toolchain/`)

The workflow packs these into `toolchain-sources-vX.Y.Z.zip` for every release (w64devkit 2.9.1; versions read from its Dockerfile):

| File | From |
|---|---|
| w64devkit-2.9.1-source.zip | `git archive` of tag v2.9.1 of https://github.com/skeeto/w64devkit (Dockerfile, patches) |
| gcc-16.2.0.tar.xz | https://ftp.gnu.org/gnu/gcc/gcc-16.2.0/gcc-16.2.0.tar.xz |
| binutils-2.47.tar.xz | https://ftp.gnu.org/gnu/binutils/binutils-2.47.tar.xz |
| gmp-6.3.0.tar.xz | https://ftp.gnu.org/gnu/gmp/gmp-6.3.0.tar.xz |
| mpc-1.4.1.tar.xz | https://ftp.gnu.org/gnu/mpc/mpc-1.4.1.tar.xz |
| mpfr-4.2.2.tar.xz | https://ftp.gnu.org/gnu/mpfr/mpfr-4.2.2.tar.xz |
| mingw-w64-v14.0.0.tar.bz2 | https://downloads.sourceforge.net/project/mingw-w64/mingw-w64/mingw-w64-release/mingw-w64-v14.0.0.tar.bz2 |

