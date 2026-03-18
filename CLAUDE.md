# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Project Is

w64devkit is a portable, offline, no-install C/C++/Fortran development kit for x64 and x86 Windows. The entire toolkit is built from source using Docker and distributed as a self-extracting 7z archive (`w64devkit-x64.exe` / `w64devkit-x86.exe`).

## Build Commands

Build x64 (primary architecture):
```
docker build -t w64devkit .
docker run --rm w64devkit | tar -C /tmp -x
```

Build both architectures using the convenience script:
```
./multibuild.sh -a          # build x64 and x86
./multibuild.sh -8          # x64 only
./multibuild.sh -4          # x86 only
./multibuild.sh -n -a       # dry run
./multibuild.sh -s 2.6.0 -a # with version suffix
```

The build takes ~15 minutes on modern hardware. Output is a self-extracting archive.

**There is no test suite.** Build success verifies correctness.

## Architecture

The build is orchestrated entirely by the `Dockerfile` (multi-stage):

1. **Download stages** (`dl-*`): Parallel isolated stages that fetch and verify source tarballs via SHA256.
2. **`cross` stage**: Builds the mingw-w64 cross-compiler (binutils → GCC bootstrap → mingw CRT/winpthreads → full GCC). Also builds custom libs (`libmemory.a`, `libchkstk.a`) and command aliases.
3. **Tool build stages** (`build-*`): Parallel stages for GDB, Make, BusyBox, Vim, Ctags, Ccache, Ninja, CMake, 7-Zip — each using the cross-compiler from `cross`.
4. **`final` stage**: Merges all tool outputs, builds the Windows launcher (`w64devkit.exe`), custom utilities, and packages everything into the self-extracting archive.

## Key Files

- `Dockerfile` — the entire build definition; all version pins and patches are here
- `multibuild.sh` — convenience wrapper for multi-arch builds
- `src/` — custom C sources and patches applied to upstream packages:
  - `w64devkit.c` — Windows launcher that sets up PATH and runs a shell
  - `pkg-config.c`, `peports.c`, `vc++filt.c`, `debugbreak.c` — custom utilities included in the kit
  - `libmemory.c` — optimized memset/memcpy/strlen using x86 string instructions
  - `libchkstk.S` — leaner stack-check routine
  - `profile` — shell profile sourced at startup (sets PATH, aliases, etc.)
  - `*.patch` — patches applied to upstream sources during build
  - `variant-x86.patch` — applied on top of the x64 Dockerfile when building the x86 variant
- `contrib/` — optional extras not included in the main distribution (cppcheck, libgc, llama.cpp build scripts)
- `.github/workflows/build.yml` — CI: builds both architectures, creates GitHub releases on tags

## Patching Upstream Components

Patches in `src/` are applied inside the Dockerfile via `patch -p1`. To modify upstream behavior, add or edit `.patch` files and reference them in the appropriate `RUN` block in the Dockerfile.

The x86 variant is produced by applying `src/variant-x86.patch` on top of the standard Dockerfile (handled automatically by `multibuild.sh`).

## Version Bumping

Component versions are defined as `ARG` variables in the Dockerfile near the top of each download stage. SHA256 checksums must be updated alongside version numbers.

## Releasing

Tagged commits (`v*`) trigger the GitHub Actions workflow to build both architectures and upload release artifacts automatically.
