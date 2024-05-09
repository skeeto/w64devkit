#if 0
# Minimalist, stripped down, w64devkit, static, unity build of Boehm GC.
# Provides an easy-to-use garbage collector weighing ~24KiB. The API is
# two functions: GC_malloc and GC_realloc. Tested with gc 8.2.4.
#
# Place this source file in the Boehm GC source tree root, then invoke
# it with the shell to produce gc.h and libgc.a.
#
#     $ sh libgc.c
#
# Philosophy: If the goal is ease-of-use, remove all knobs that are not
# strictly necessary. If performance isn't good enough, don't use garbage
# collection; switch to a region-based allocator.
#
# Ref: https://www.hboehm.info/gc/gc_source/gc-8.2.4.tar.gz
# This is free and unencumbered software released into the public domain.
set -ex

PREFIX="${PREFIX:-$(gcc -print-sysroot)}"

${CC:-cc} -c -Iinclude -fwhole-program ${CFLAGS:--Os} libgc.c
${STRIP-strip} -x libgc.o

mkdir -p "$PREFIX/lib"
rm -f "$PREFIX/lib/libgc.a"
${AR:-ar} -r "$PREFIX/lib/libgc.a" libgc.o

mkdir -p "$PREFIX/include"
tee "$PREFIX/include/gc.h" <<EOF
#ifndef GC_H
#define GC_H
// Single-threaded, garbage-collected, zero-initialized memory allocation.
#include <stddef.h>
void *GC_malloc(size_t)          __attribute((alloc_size(1), malloc));
void *GC_realloc(void *, size_t) __attribute((alloc_size(2)));
#endif
EOF
exit 0
#endif

#define ALL_INTERIOR_POINTERS
#define DONT_USE_ATEXIT
#define GC_API
#define GC_NO_FINALIZATION
#define GC_TOGGLE_REFS_NOT_NEEDED
#define GC_USE_ENTIRE_HEAP
#define NO_CLOCK
#define NO_DEBUGGING
#define NO_GETENV
#define NO_MSGBOX_ON_ERROR

#include "allchblk.c"
#include "alloc.c"
#include "blacklst.c"
#include "dbg_mlc.c"
#include "dyn_load.c"
#include "finalize.c"
#include "headers.c"
#include "mach_dep.c"
#include "malloc.c"
#include "mallocx.c"
#include "mark.c"
#include "mark_rts.c"
#include "misc.c"
#include "new_hblk.c"
#include "obj_map.c"
#include "os_dep.c"
#include "ptr_chck.c"
#include "reclaim.c"

__attribute((used)) void *GC_malloc(size_t);
__attribute((used)) void *GC_realloc(void *, size_t);
