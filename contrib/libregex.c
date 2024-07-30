#if 0
# Stripped down, w64devkit, static, unity build of the PCRE2 POSIX regex
# library. This script builds and installs regex.h and libregex.a inside
# w64devkit. Link with -lregex. It is pretty bulky, nearly 300KiB.
#
# Copy and run this script in the root fo the PCRE source tree. Tested
# with PCRE 10.43.
#
#     $ sh libregex.c
#
# Ref: https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/regex.h.html
# This is free and unencumbered software released into the public domain.
set -ex

PREFIX="${PREFIX:-$(gcc -print-sysroot)}"

cp src/pcre2.h.generic src/pcre2.h
${CC:-cc} -c -fwhole-program ${CFLAGS:--Os} "$0"
${STRIP:-strip} -x libregex.o

mkdir -p "$PREFIX/lib"
rm -f "$PREFIX/lib/libregex.a"
${AR:-ar} -r "$PREFIX/lib/libregex.a" libregex.o

mkdir -p "$PREFIX/include"
tee >nul "$PREFIX/include/regex.h" <<EOF
#pragma once
#include <stddef.h>

enum {
    REG_EXTENDED = 0x0000,
    REG_ICASE    = 0x0001,
    REG_NEWLINE  = 0x0002,
    REG_NOTBOL   = 0x0004,
    REG_NOTEOL   = 0x0008,
    REG_DOTALL   = 0x0010,
    REG_NOSUB    = 0x0020,
    REG_UTF      = 0x0040,
    REG_STARTEND = 0x0080,
    REG_NOTEMPTY = 0x0100,
    REG_UNGREEDY = 0x0200,
    REG_UCP      = 0x0400,
    REG_PEND     = 0x0800,
    REG_NOSPEC   = 0x1000,
};

enum {
    REG_ASSERT = 1,
    REG_BADBR,
    REG_BADPAT,
    REG_BADRPT,
    REG_EBRACE,
    REG_EBRACK,
    REG_ECOLLATE,
    REG_ECTYPE,
    REG_EESCAPE,
    REG_EMPTY,
    REG_EPAREN,
    REG_ERANGE,
    REG_ESIZE,
    REG_ESPACE,
    REG_ESUBREG,
    REG_INVARG,
    REG_NOMATCH,
};

typedef struct {
    void  *re_pcre2_code;
    void  *re_match_data;
    char  *re_endp;
    size_t re_nsub;
    size_t re_erroffset;
    int    re_cflags;
} regex_t;

typedef int regoff_t;

typedef struct {
    regoff_t rm_so;
    regoff_t rm_eo;
} regmatch_t;

int    regcomp(regex_t *, const char *, int);
size_t regerror(int, const regex_t *, char *, size_t);
int    regexec(const regex_t *, const char *, size_t, regmatch_t *, int);
void   regfree(regex_t *);
EOF
exit 0
#endif

#define PCRE2_EXP_DEFN          static
#define PCRE2_EXP_DECL          static
#define PCRE2POSIX_EXP_DECL
#define PCRE2POSIX_EXP_DEFN     __attribute((used))

#define HAVE_BUILTIN_MUL_OVERFLOW
#define HAVE_MEMMOVE
#define HEAP_LIMIT              20000000
#define LINK_SIZE               2
#define MATCH_LIMIT		        10000000
#define MATCH_LIMIT_DEPTH	    MATCH_LIMIT
#define MAX_NAME_COUNT	        10000
#define MAX_NAME_SIZE	        32
#define MAX_VARLOOKBEHIND       255
#define NEWLINE_DEFAULT         2
#define PARENS_NEST_LIMIT       250
#define PCRE2_CODE_UNIT_WIDTH   8
#define PCRE2_STATIC
#define SUPPORT_UNICODE

#include "src/pcre2posix.h"
#undef  regcomp
#undef  regexec
#undef  regerror
#undef  regfree
#define pcre2_regcomp   regcomp
#define pcre2_regexec   regexec
#define pcre2_regerror  regerror
#define pcre2_regfree   regfree
#include "src/pcre2posix.c"

#include "src/pcre2_auto_possess.c"
#include "src/pcre2_chartables.c.dist"
#include "src/pcre2_chkdint.c"
#include "src/pcre2_compile.c"
#include "src/pcre2_context.c"
#include "src/pcre2_extuni.c"
#include "src/pcre2_find_bracket.c"
#include "src/pcre2_match.c"
#include "src/pcre2_match_data.c"
#include "src/pcre2_newline.c"
#include "src/pcre2_ord2utf.c"
#include "src/pcre2_pattern_info.c"
#include "src/pcre2_script_run.c"
#include "src/pcre2_string_utils.c"
#include "src/pcre2_study.c"
#include "src/pcre2_tables.c"
#include "src/pcre2_ucd.c"
#include "src/pcre2_valid_utf.c"
#include "src/pcre2_xclass.c"
