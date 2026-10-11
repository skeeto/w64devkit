# Patch queues

Each subdirectory here is a [quilt][] patch queue for one component: a
`series` file listing patches in application order, plus the patches. The
Dockerfile applies each queue to a freshly unpacked source tree. Order
comes from `series`, never from file names.

## Working on a queue

Drive a queue straight out of this directory by pointing `QUILT_PATCHES`
at it from inside an unpacked source tree, with `--quiltrc` selecting the
shared settings in `quiltrc` so that everyone produces identical patches:

    $ W=/path/to/w64devkit
    $ tar xf $W/source/cmake-4.4.3.tar.gz
    $ cd cmake-4.4.3/
    $ export QUILT_PATCHES=$W/src/cmake
    $ quilt --quiltrc $W/src/quiltrc push -a

From there use quilt as usual (`push`, `pop`, `refresh`, `new`, `edit`,
etc.). `refresh` writes directly into this directory, so check `git
status` afterward.

Always start from a freshly unpacked tree. Quilt records applied patches
in `.pc/`, and a tree reused with a different `QUILT_PATCHES` acts on the
queue recorded there.

## Updating a component

When a component's version changes, refresh its entire queue against the
new source in the same commit:

    $ quilt --quiltrc $W/src/quiltrc push -a --refresh

Read the output before committing. "Hunk succeeded at N (offset M)" is
harmless and is what the refresh cleans up. "with fuzz" means patch
discarded context to make a hunk fit, so check that the hunk landed where
intended before accepting the refreshed result.

## GCC's two queues

`gcc` and `gcc-final` apply to the same GCC tree. `gcc` goes on first and
affects both the bootstrap cross-compiler and the distributed compiler.
`gcc-final` goes on top afterward, just before the distributed compiler
is configured, and affects only that compiler. Patches in `gcc-final`
therefore apply to an already-patched tree, and must be refreshed that
way:

    $ QUILT_PATCHES=$W/src/gcc quilt --quiltrc $W/src/quiltrc push -a --refresh
    $ rm -rf .pc
    $ QUILT_PATCHES=$W/src/gcc-final quilt --quiltrc $W/src/quiltrc push -a --refresh

Removing `.pc` between queues mirrors the Dockerfile, which never pops.

The w64devkit distribution includes this directory and `quilt.exe`, so
the same commands work inside the kit.

[quilt]: https://savannah.nongnu.org/projects/quilt
