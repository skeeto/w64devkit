// rexxd: replacement xxd (for w64devkit)
// Chris Wellons <wellons@nullprogram.com>
//
// Drop-in for the original xxd. Not bug-compatable, but outputs are
// identical in the common, practical cases. Where the original xxd
// documentation and behavior differ, rexxd chooses the fastest.
//
// w64dk: $ cc -nostartfiles -O2 -funroll-loops -s -o xxd.exe     rexxd.c
// unix:  $ cc               -O2 -funroll-loops -s -o xxd         rexxd.c
// bench: $ cc   -DBENCH -g3 -O2 -funroll-loops                   rexxd.c
// tests: $ cc   -DTEST  -g3 -fsanitize=undefined -fsanitize-trap rexxd.c
// fuzz:  $ afl-gcc-fast -g3 -fsanitize=undefined -fsanitize-trap rexxd.c
//
// Features versus the original xxd:
// * supports both long and wide paths on Windows
// * 64-bit file offsets throughout
// * fewer bugs, particularly near extreme inputs
// * typically 2x to 100x faster (factor varies by host and operation)
// * limits are not hardcoded (256 cols, etc.), but by available memory
// * unicode variable names in C includes (-i)
// * "xxd -r" autodetects input "-c" configuration
// * extremely portable e.g. tests run on a virtual file system
// * can self-strace on all platforms (-x)
// * better license
//
// Differences:
// * On Windows emits LF, not CRLF, for text outputs
// * Standard output is always fully-buffered, never line-buffered
// * No color output
//
// Requires a C11 compiler and a couple of GCC builtins. While it works
// fine on 32-bit hosts, performance is designed for 64-bit hosts.
//
// This is free and unencumbered software released into the public domain.
#include <stddef.h>
#include <stdint.h>

#define VERSION "2025-07-17"

typedef uint8_t     u8;
typedef int32_t     b32;
typedef int32_t     i32;
typedef uint32_t    u32;
typedef int64_t     i64;
typedef uint64_t    u64;
typedef char        byte;
typedef ptrdiff_t   iz;
typedef size_t      uz;

typedef struct Arena Arena;


// Platform API

// Porting: Define Plt, implement these five functions, and call xxd().
//
// The C standard library lacks features necessary to implement the
// platform in pure, comforming C: (1) cannot open files for write
// without truncating, (2) cannot specify binary for standard input and
// standard output, and (3) seek is not necessarily 64 bits.
//
// The arena size constrains input ranges, and huge requests (grossly
// long lines, etc.) are realized as OOM. A few 32-bit calculations
// assume simple operations on quantities validated against the arena
// size will not overflow, so do not pass an arena sized above 256MiB.

enum {
    PLT_SEEK_SET = 0,
    PLT_SEEK_CUR = 1,
    PLT_SEEK_END = 2,
};

typedef struct Plt Plt;
static b32  plt_open(Plt *, i32 fd, u8 *, b32 trunc, Arena *);  // open(2)
static i64  plt_seek(Plt *, i32 fd, i64, i32 whence);           // lseek(2)
static i32  plt_read(Plt *, u8 *, i32);                         // read(2)
static b32  plt_write(Plt *, i32 fd, u8 *, i32);                // write(2)
static void plt_exit(Plt *, i32);                               // _exit(2)
static i32  xxd(i32, u8 **, Plt *, byte *, iz);                 // main


// Application

#define countof(a)      (iz)(sizeof(a) / sizeof(*(a)))
#define affirm(c)       while (!(c)) __builtin_unreachable()
#define new(a, n, t)    (t *)alloc(a, n, sizeof(t), _Alignof(t))
#define S(s)            (Str){(u8 *)s, sizeof(s)-1}
#define maxof(t)        ((t)-1<1 ? (((t)1<<(sizeof(t)*8-2))-1)*2+1 : (t)-1)
#define xset(d, c, n)   __builtin_memset(d, c, n)
#define xcpy(d, s, n)   __builtin_memcpy(d, s, n)

// strace-like infrastructure

typedef struct {
    u8 *data;
    iz  len;
} Str;
static Str import(u8 *);

typedef struct Output Output;
static void print(Output *, Str);
static void printq(Output *, Str);
static void printu8(Output *, u8);
static void printi64(Output *, i64);
static void flush(Output *);

typedef struct {
    Plt    *plt;
    Output *be;
    b32     trace;
} Xxd;

static b32 xxd_open(Xxd *ctx, i32 fd, u8 *path, b32 trunc, Arena *a)
{
    b32 r = plt_open(ctx->plt, fd, path, trunc, a);
    if (!ctx->trace) {
        return r;
    }

    Output *be = ctx->be;
    print(be, S("open(\""));
    printq(be, import(path));
    print(be, S("\", "));

    if (fd == 0) {
        print(be, S("O_RDONLY"));
    } else {
        print(be, S("O_CREAT|O_WRONLY"));
    }
    if (trunc) {
        print(be, S("|O_TRUNC"));
    }
    if (fd != 0) {
        print(be, S(", 0666"));
    }

    print(be, S(") = "));
    printi64(be, r ? fd : -1);

    print(be, S("\n"));
    flush(be);
    return r;
}

static i64 xxd_seek(Xxd *ctx, i32 fd, i64 off, i32 whence)
{
    i64 r = plt_seek(ctx->plt, fd, off, whence);
    if (!ctx->trace) {
        return r;
    }

    Output *be = ctx->be;

    print(be, S("lseek("));
    printi64(be, fd);
    print(be, S(", "));
    printi64(be, off);
    print(be, S(", "));
    static Str name[] = {
        [PLT_SEEK_SET] = S("SEEK_SET"),
        [PLT_SEEK_CUR] = S("SEEK_CUR"),
        [PLT_SEEK_END] = S("SEEK_END"),
    };
    print(be, name[whence]);
    print(be, S(") = "));
    printi64(be, r);

    print(be, S("\n"));
    flush(be);
    return r;
}

static i32 xxd_read(Xxd *ctx, u8 *buf, i32 len)
{
    i32 r = plt_read(ctx->plt, buf, len);
    if (!ctx->trace) {
        return r;
    }

    Output *be = ctx->be;
    print(be, S("read(0, ..., "));
    printi64(be, len);
    print(be, S(") = "));
    printi64(be, r);
    print(be, S("\n"));
    flush(be);
    return r;
}

static b32 xxd_write(Xxd *ctx, i32 fd, u8 *buf, i32 len)
{
    b32 r = plt_write(ctx->plt, fd, buf, len);
    if (!ctx->trace || fd==2) {
        return r;
    }

    Output *be = ctx->be;
    print(be, S("write("));
    printi64(be, fd);
    print(be, S(", \""));
    if (len > 12) {
        printq(be, (Str){buf, 6});
        print(be, S("..."));
        printq(be, (Str){buf+len-6, 6});
    } else {
        printq(be, (Str){buf, len});
    }
    print(be, S("\", "));
    printi64(be, len);
    print(be, S(") = "));
    printi64(be, r ? len : -1);
    print(be, S("\n"));
    flush(be);
    return r;
}

static void xxd_exit(Xxd *ctx, i32 r)
{
    if (ctx->trace) {
        Output *be = ctx->be;
        print(be, S("exit("));
        printi64(be, r);
        print(be, S(") = ?\n"));
        flush(be);
    }
    plt_exit(ctx->plt, r);
    affirm(0);
}

// Main program

enum {
    STATUS_OK       = 0,
    STATUS_CMD      = 1,
    STATUS_INPUT    = 2,
    STATUS_OUTPUT   = 3,
    STATUS_OOM      = 6,
};

static u8 lohex[16] = "0123456789abcdef";
static u8 uphex[16] = "0123456789ABCDEF";

static u8 ascii[256] = {
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x20,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
    0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41,
    0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c,
    0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62,
    0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d,
    0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
    0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e,
};

static u8 ebcdic[256] = {
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x20, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x3c,
    0x28, 0x2b, 0x2e, 0x26, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x21, 0x24, 0x2a, 0x29, 0x3b, 0x5e, 0x2d, 0x2f, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x7c, 0x2c, 0x25, 0x5f,
    0x3e, 0x3f, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x60, 0x3a, 0x23, 0x40, 0x27, 0x3d, 0x22, 0x2e, 0x61, 0x62, 0x63,
    0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x7e, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7a, 0x2e, 0x2e, 0x2e, 0x5b, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x5d, 0x2e, 0x2e, 0x7b, 0x41, 0x42, 0x43, 0x44, 0x45,
    0x46, 0x47, 0x48, 0x49, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x7d,
    0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x5c, 0x2e, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x5a, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x30, 0x31,
    0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e,
};

struct Arena {
    Xxd  *ctx;
    byte *beg;
    byte *end;
};

static void oom(Xxd *ctx)
{
    print(ctx->be, S("xxd: out of memory\n"));
    flush(ctx->be);
    xxd_exit(ctx, STATUS_OOM);
}

static void *alloc(Arena *a, iz count, iz size, iz align)
{
    iz pad = -(uz)a->beg & (align - 1);
    if (count >= (a->end - a->beg - pad)/size) {
        oom(a->ctx);
    }
    byte *r = a->beg + pad;
    a->beg += pad + count*size;
    return xset(r, 0, count*size);
}

static Str import(u8 *s)
{
    Str r = {0};
    r.data = s;
    for (; r.data[r.len]; r.len++) {}
    return r;
}

static Str cuthead(Str s, iz len)
{
    affirm(len <= s.len);
    s.data += len;
    s.len  -= len;
    return s;
}

static b32 equals(Str a, Str b)
{
    if (a.len != b.len) {
        return 0;
    }
    for (iz i = 0; i < a.len; i++) {
        if (a.data[i] != b.data[i]) {
            return 0;
        }
    }
    return 1;
}

static Str clone(Arena *a, Str s)
{
    Str r = s;
    r.data = new(a, r.len, u8);
    if (r.len) xcpy(r.data, s.data, r.len);
    return r;
}

static Str concat(Arena *a, Str head, Str tail)
{
    if (!head.data || (byte *)(head.data+head.len) != a->beg) {
        head = clone(a, head);
    }
    head.len += clone(a, tail).len;
    return head;
}

static Str span(u8 *beg, u8 *end)
{
    Str r = {0};
    r.data = beg;
    r.len  = end - beg;
    return r;
}

// Use SWAR to determine if the 8 octets contain the value repeteated in
// the pre-computed permute comparator.
static b32 hasoctet(u8 s[8], u64 permute)
{
    u64 x = (u64)s[0] <<  0 | (u64)s[1] <<  8 |  // little endian optimized
            (u64)s[2] << 16 | (u64)s[3] << 24 |
            (u64)s[4] << 32 | (u64)s[5] << 40 |
            (u64)s[6] << 48 | (u64)s[7] << 56;
    x ^= permute;
    x |= (x>>4) & 0x0f0f0f0f0f0f0f0f;
    x |= (x>>2) & 0x0303030303030303;
    x |= (x>>1);
    x &= 0x0101010101010101;
    return x != 0x0101010101010101;
}

typedef struct {
    Str head;
    Str tail;
    b32 ok;
} Cut;

static Cut cut(Str s, u8 c)
{
    u8 *beg = s.data;
    u8 *end = s.data + s.len;
    u8 *cut = beg;

    // Quickly discard leading, non-matching blocks of 8 octets
    u64 permute = 0x0101010101010101 * c;
    for (; end-cut >= 8; cut += 8) {
        if (hasoctet(cut, permute)) {
            break;
        }
    }

    for (; cut<end && *cut!=c; cut++) {}
    Cut r  = {0};
    r.ok   = cut < end;
    r.head = span(beg, cut);
    r.tail = span(cut+r.ok, end);
    return r;
}

static b32 whitespace(u8 c)
{
    switch (c) {
    case '\t': case '\n': case '\r': case ' ':
        return 1;
    }
    return 0;
}

static Str trim(Str s)
{
    if (!s.len) return s;
    u8 *beg = s.data;
    u8 *end = s.data + s.len;
    for (; beg<end && whitespace(beg[ 0]); beg++) {}
    for (; beg<end && whitespace(end[-1]); end--) {}
    return span(beg, end);
}

typedef struct {
    i64  limit;
    Xxd *ctx;
    i32  len;
    i32  off;
    b32  eof;
    b32  err;
    u8   buf[1<<12];
} Input;

static Input *newinput(Arena *a, Xxd *ctx)
{
    Input *b = new(a, 1, Input);
    b->limit = maxof(i64);
    b->ctx = ctx;
    return b;
}

static void refill(Input *b)
{
    affirm(b->off == b->len);
    if (!b->eof) {
        b->len = b->off = 0;
        i32 want = countof(b->buf);
        want = b->limit<want ? (i32)b->limit : want;
        if (want) {
            i32 r = xxd_read(b->ctx, b->buf, want);
            if (r < 0) {
                r = 0;
                b->err = 1;
            }
            b->len = r;
        }
        b->limit -= b->len;
        b->eof = !b->len;
    }
}

// Simulate a forward seek using plt_read.
static b32 readseek(Input *b, i64 len)
{
    affirm(len >= 0);
    affirm(b->len == b->off);  // case not handled (nor needed)
    while (len) {
        i32 cap = countof(b->buf);
        i32 amt = len<cap ? (i32)len : cap;
        i32 ret = xxd_read(b->ctx, b->buf, amt);
        if (ret < 1) {
            b->eof = b->err = 1;
            return 0;
        }
        len -= ret;
    }
    return 1;
}

// Return the next line, viewing the input buffer if possible. Includes
// newline, and an empty line means end-of-input.
static Str nextline(Input *b, Arena *a)
{
    Str line = {0};
    do {
        if (b->off == b->len) {
            refill(b);
        }

        i32 cut = b->off;
        for (; cut<b->len && b->buf[cut]!='\n'; cut++) {}
        b32 found = cut < b->len;

        Str tail  = {0};
        tail.data = b->buf + b->off;
        tail.len  = cut - b->off + found;
        b->off    = cut + found;

        if (found) {
            // Avoid copy if possible; the common case
            line = line.data ? concat(a, line, tail) : tail;
            break;
        }
        line = concat(a, line, tail);
    } while (!b->eof);
    return line;
}

// Read some bytes, viewing the input buffer if possible.
static Str nextbytes(Input *b, iz len, Arena *a)
{
    Str r = {0};
    while (r.len < len) {
        if (b->off == b->len) {
            refill(b);
        }

        Str avail = {0};
        avail.data = b->buf + b->off;
        avail.len  = b->len - b->off;

        if (len-r.len <= avail.len) {
            avail.len = len - r.len;
            // Avoid copy if possible; the common case
            r = r.data ? concat(a, r, avail) : avail;
            b->off += (i32)avail.len;
            break;
        }

        r = concat(a, r, avail);
        b->off += (i32)avail.len;
        if (b->eof) {
            break;
        }
    }
    return r;
}

static i32 next(Input *b)
{
    if (b->off==b->len && !b->eof) {
        refill(b);
    }
    return b->eof ? -1 : b->buf[b->off++];
}

struct Output {
    i64  fpos;
    Xxd *ctx;
    i32  len;
    i32  fd;
    b32  err;
    b32  noseek;
    u8   buf[1<<12];
};

static Output *newoutput(Arena *a, i32 fd, Xxd *ctx)
{
    Output *b = new(a, 1, Output);
    b->fd = fd;
    b->ctx = ctx;
    return b;
}

static void flush(Output *b)
{
    if (!b->err && b->len) {
        b->err = !xxd_write(b->ctx, b->fd, b->buf, b->len);
        b->len = 0;
    }
}

static void output(Output *b, u8 *buf, iz len)
{
    b->err |= b->fpos > maxof(i64) - len;  // overflows fpos?
    for (iz off = 0; !b->err && off<len;) {
        i32 avail = countof(b->buf) - b->len;
        i32 count = len-off < avail ? (i32)(len-off) : avail;
        xcpy(b->buf+b->len, buf+off, count);
        off += count;
        b->len += count;
        b->fpos += count;
        if (b->len == countof(b->buf)) {
            flush(b);
        }
    }
}

static void seekout(Output *b, i64 pos)
{
    if (!b->err && pos!=b->fpos) {
        i64 r = -1;
        if (!b->noseek) {
            // Flush at current position before seeking
            flush(b);
            if (b->err) {
                return;
            }
            r = xxd_seek(b->ctx, 1, pos, PLT_SEEK_SET);
            b->noseek = r < 0;
        }
        if (r >= 0) {
            b->fpos = r;
        } else if (r<0 && pos>b->fpos) {
            // failed: write zeros instead
            while (pos > b->fpos) {
                printu8(b, 0);
            }
        } else {
            b->err = 1;
        }
    }
}

static void print(Output *b, Str s)
{
    output(b, s.data, s.len);
}

static void printu8(Output *b, u8 c)
{
    output(b, &c, 1);
}

static void printq(Output *b, Str s)
{
    b32 pending_null = 0;
    for (iz i = 0; i < s.len; i++) {
        u8 c = s.data[i];
        if (pending_null) {
            Str null = c<'0'||c>'7' ? S("\\0") : S("\\x00");
            print(b, null);
            pending_null = 0;
        }
        switch (c) {
        case '\0': pending_null = 1;    break;
        case '\t': print(b, S("\\t"));  break;
        case '\n': print(b, S("\\n"));  break;
        case '\r': print(b, S("\\r"));  break;
        case '\"': print(b, S("\\\"")); break;
        case '\\': print(b, S("\\\\")); break;
        default:
            if (c<' ' || c >=127) {
                print(b, S("\\x"));
                printu8(b, lohex[c>>4]);
                printu8(b, lohex[c&15]);
            } else {
                printu8(b, c);
            }
        }
    }
    if (pending_null) {
        print(b, S("\\0"));
    }
}

static void printhexoff(Output *b, i64 x)
{
    u8 hex[16];
    for (i32 i = 0; i < 8; i++) {
        u8 v = (u8)(x >> (56 - i*8));
        hex[2*i+0] = lohex[v>>4];
        hex[2*i+1] = lohex[v&15];
    }
    // Allow overflow above i64 range; a seek error in -r.
    i32 off = (u64)x<0x100000000 ? 8 : 0;
    output(b, hex+off, 16-off);
}

static void printu64(Output *b, u64 x, i32 minwidth)
{
    u8  buf[32];
    u8 *end = buf + countof(buf);
    u8 *beg = end;
    do {
        *--beg = '0' + (u8)(x%10);
    } while (x /= 10);
    while (end-beg < minwidth) {
        *--beg = '0';
    }
    print(b, span(beg, end));
}

static void printi64(Output *b, i64 x)
{
    u8  buf[32];
    u8 *end = buf + countof(buf);
    u8 *beg = end;
    i64 t   = x<0 ? x : -x;
    do {
        *--beg = '0' - (u8)(t%10);
    } while (t /= 10);
    if (x < 0) {
        *--beg = '-';
    }
    print(b, span(beg, end));
}

typedef struct {
    i64  dispoff;
    i64  rseek;
    i32  ncols;
    i32  grouplen;
    i32  dumpbase;
    Str  inpath;
    Str  outpath;
    Str  name;
    b32  autoskip;
    b32  capitalize;
    b32  decimal;
    b32  ebcdic;
    b32  lendian;
    b32  reverse;
    b32  upper;
} Config;

static b32 allzeros(Str s)
{
    u8 c = 0;
    for (iz i = 0; i < s.len; i++) {
        c |= s.data[i];
    }
    return !c;
}

static void printoffset(Output *bo, i64 offset, b32 decimal)
{
    if (decimal) {
        printu64(bo, offset, 8);  // allow overflow above i64 range
    } else {
        printhexoff(bo, offset);
    }
}

static void run_hexdump(Config c, Output *bo, Input *bi, Arena a)
{
    // Strategy: Construct a template of one line of output with fixed
    // slots filled, and precompute (including division) column offsets.
    u8  *hexset   = c.upper ? uphex : lohex;
    u8  *display  = c.ebcdic ? ebcdic : ascii;
    i32 *hexoffs  = new(&a, c.ncols, i32);
    i32  ngroups  = (c.ncols - 1 + c.grouplen) / c.grouplen;
    i32  txtbase  = 3 + 2*c.ncols + ngroups + !!(c.ncols%ngroups);
    i32  templlen = txtbase + c.ncols + 1;
    u8  *template = new(&a, templlen, u8);
    u8  *txt      = template + txtbase;

    template[0] = ':';
    for (i32 i = 1; i < templlen; i++) {
        template[i] = ' ';
    }

    for (i32 i = 0; i < c.ncols; i++) {
        i32 hi = 2 + i / c.grouplen;
        if (c.lendian) {
            hi += i / c.grouplen * c.grouplen * 2;
            hi += (c.grouplen - 1 - i%c.grouplen) * 2;
        } else {
            hi += 2*i;
        }
        hexoffs[i] = hi;
    }

    i32 skipping = 0;  // 0, 1, or 2 ("many")
    b32 prevzero = 0;
    i64 offset   = c.dispoff;
    for (;;) {
        Arena scratch = a;
        Str line = nextbytes(bi, c.ncols, &scratch);

        if (skipping && (!allzeros(line) || line.len!=c.ncols)) {
            // slow path: not an optimization priority
            if (!line.len) {
                // No more lines; print at least one final line so that
                // there's a seek and write that (maybe) fills the gap.
                switch (skipping) {
                case 2: print(bo, S("*\n"));  // fallthrough
                case 1: printoffset(bo, offset-c.ncols, c.decimal);
                        output(bo, template, templlen);
                }
            } else if (skipping) {
                print(bo, S("*\n"));
            }
            skipping = 0;
        }

        if (!line.len) {
            break;
        }

        for (i32 i = 0; i < line.len; i++) {
            u8  v   = line.data[i];
            u8 *hex = template + hexoffs[i];
            hex[0]  = hexset[v>>4];
            hex[1]  = hexset[v&15];
            txt[i]  = display[v];
        }

        for (iz i = line.len; i < c.ncols; i++) {
            // Only runs on final line, if at all
            u8 *hex = template + hexoffs[i];
            hex[0]  = ' ';
            hex[1]  = ' ';
        }
        txt[line.len] = '\n';

        i64 lineoff = offset;
        offset += (u64)line.len;  // allow overflow

        if (c.autoskip) {
            // slow path: not an optimization priority
            if (allzeros(line) && line.len==c.ncols) {
                if (prevzero) {
                    skipping = skipping<1 ? 1 : 2;
                    continue;  // do not print
                }
                prevzero = 1;
            } else {
                skipping = 0;
                prevzero = 0;
            }
        }

        printoffset(bo, lineoff, c.decimal);
        output(bo, template, txtbase+line.len+1);
    }
}

static void run_bindump(Config c, Output *bo, Input *bi, Arena a)
{
    u8  *display  = c.ebcdic ? ebcdic : ascii;
    i32 *offsets  = new(&a, c.ncols, i32);
    i32  ngroups  = (c.ncols - 1 + c.grouplen) / c.grouplen;
    i32  txtbase  = 3 + 8*c.ncols + ngroups + !!(c.ncols%ngroups);
    i32  templlen = txtbase + c.ncols + 1;
    u8  *template = new(&a, templlen, u8);
    u8  *txt      = template + txtbase;

    template[0] = ':';
    for (i32 i = 1; i < templlen; i++) {
        template[i] = ' ';
    }

    for (i32 i = 0; i < c.ncols; i++) {
        offsets[i] = 8*i + 2 + i/c.grouplen;
    }

    b32 skipping = 0;
    b32 prevzero = 0;
    i64 offset   = c.dispoff;
    for (;;) {
        Arena scratch = a;
        Str line = nextbytes(bi, c.ncols, &scratch);

        if (skipping && (!allzeros(line) || line.len!=c.ncols)) {
            if (!line.len) {
                // No more lines, print at least one final line.
                switch (skipping) {
                case 2: print(bo, S("*\n"));  // fallthrough
                case 1: printoffset(bo, offset-c.ncols, c.decimal);
                        output(bo, template, templlen);
                }
            } else if (skipping) {
                print(bo, S("*\n"));
            }
            skipping = 0;
        }

        if (!line.len) {
            break;
        }

        i64 lineoff = offset;
        offset += (u64)c.ncols;  // allow overflow

        for (iz i = 0; i < line.len; i++) {
            u8  v   = line.data[i];
            u8 *bin = template + offsets[i];
            bin[0] = '0'+(v>>7&1);  bin[1] = '0'+(v>>6&1);
            bin[2] = '0'+(v>>5&1);  bin[3] = '0'+(v>>4&1);
            bin[4] = '0'+(v>>3&1);  bin[5] = '0'+(v>>2&1);
            bin[6] = '0'+(v>>1&1);  bin[7] = '0'+(v>>0&1);
            txt[i] = display[v];
        }
        txt[line.len] = '\n';

        for (iz i = line.len; i < c.ncols; i++) {
            u8 *bin = template + offsets[i];
            bin[0] = ' ';  bin[1] = ' ';
            bin[2] = ' ';  bin[3] = ' ';
            bin[4] = ' ';  bin[5] = ' ';
            bin[6] = ' ';  bin[7] = ' ';
        }

        if (c.autoskip) {
            if (allzeros(line) && line.len==c.ncols) {
                if (prevzero) {
                    skipping = skipping<1 ? 1 : 2;
                    continue;  // do not print
                }
                prevzero = 1;
            } else {
                skipping = 0;
                prevzero = 0;
            }
        }

        printoffset(bo, lineoff, c.decimal);
        output(bo, template, txtbase+line.len+1);
    }
}

static b32 varstart(u8 c)
{
    return (c>='A' && c<='Z') ||
           (c>='a' && c<='z') ||
           c=='$' || c>0x7f || c == '_';
}

static b32 varchar(u8 c)
{
    return varstart(c) || (c>='0' && c<='9');
}

static void filtername(Output *bo, Str s, b32 capitalize)
{
    // The original xxd is over-restricted. This filter is under
    // restricted instead, allowing Unicode variable names.
    if (!s.len || (varchar(*s.data) && !varstart(*s.data))) {
        print(bo, S("__"));
    }
    for (iz i = 0; i < s.len; i++) {
        u8 c = s.data[i];
        c = varchar(c) ? c : '_';
        if (capitalize && c>='a' && c<='z') {
            // This option is stupid, so keep it simple.
            c += 'A' - 'a';
        }
        printu8(bo, c);
    }
}

static void run_include(Config conf, Output *bo, Input *bi, Arena a)
{
    b32 named = conf.name.data || conf.inpath.data;
    if (named) {
        print(bo, S("unsigned char "));
    }

    if (conf.name.data) {
        print(bo, conf.name);
    } else if (conf.inpath.data) {
        filtername(bo, conf.inpath, conf.capitalize);
    }

    if (named) {
        print(bo, S("[] = {\n"));
    }

    u8  *hexset   = conf.upper ? uphex : lohex;
    i32 *offsets  = new(&a, conf.ncols, i32);
    i32  templlen = 2 + 6*conf.ncols;
    u8  *template = new(&a, templlen, u8);

    // NOTE(skeeto): The original "xxd -i -u" also capitalizes the 'x'
    // in "0x". I choose not to do this for aesthetic reasons. Otherwise
    // that would happen here in the template.
    for (i32 i = 0; i < conf.ncols; i++) {
        template[2+i*6+0] = ',';
        template[2+i*6+1] = ' ';
        template[2+i*6+2] = '0';
        template[2+i*6+3] = 'x';
        offsets[i] = 2 + i*6 + 4;
    }
    template[0] = ',';
    template[1] = '\n';
    template[2] = ' ';

    i64 len = 0;
    for (;;) {
        Arena scratch = a;
        Str line = nextbytes(bi, conf.ncols, &scratch);
        if (!line.len) {
            break;
        }

        for (i32 i = 0; i < line.len; i++) {
            u8  v   = line.data[i];
            u8 *hex = template + offsets[i];
            hex[0]  = hexset[v>>4];
            hex[1]  = hexset[v&15];
        }

        Str s = {0};
        s.data = template;
        s.len  = 2 + 6*line.len;
        if (!len) {
            s = cuthead(s, 2);
        }
        print(bo, s);

        len += line.len;
    }
    if (len) {
        printu8(bo, '\n');
    }

    if (named) {
        print(bo, S("};\nunsigned int "));
        if (conf.name.data) {
            print(bo, conf.name);
        } else if (conf.inpath.data) {
            filtername(bo, conf.inpath, conf.capitalize);
        }
        print(bo, conf.capitalize ? S("_LEN = ") : S("_len = "));
        printi64(bo, len);
        print(bo, S(";\n"));
    }
}

static void run_postscript(Config c, Output *bo, Input *bi, Arena a)
{
    u8 *hexset   = c.upper ? uphex : lohex;
    u8 *template = new(&a, 2*c.ncols + 1, u8);
    for (;;) {
        Arena scratch = a;
        Str line = nextbytes(bi, c.ncols, &scratch);
        if (!line.len) {
            break;
        }

        for (i32 i = 0; i < line.len; i++) {
            u8 v = line.data[i];
            template[2*i+0] = hexset[v>>4];
            template[2*i+1] = hexset[v&15];
        }
        template[2*line.len] = '\n';
        output(bo, template, 1+2*line.len);
    }
}

static i32 nibbleval(u8 c)
{
    static u8 t[256] = {
        ['0']= 1, ['1']= 2, ['2']= 3, ['3']= 4, ['4']= 5,
        ['5']= 6, ['6']= 7, ['7']= 8, ['8']= 9, ['9']=10,
        ['a']=11, ['b']=12, ['c']=13, ['d']=14, ['e']=15, ['f'] = 16,
        ['A']=11, ['B']=12, ['C']=13, ['D']=14, ['E']=15, ['F'] = 16,
    };
    return t[c] - 1;
}

enum {
    PARSE_INVALID  = 0,
    PARSE_OK       = 1,
    PARSE_OVERFLOW = 2,
};

typedef struct {
    i64 offset;
    i32 status;
} ParsedOffset;

static ParsedOffset parseoffet(Str s)
{
    ParsedOffset r = {0};
    for (iz i = 0; i < s.len; i++) {
        i32 c = nibbleval(s.data[i]);
        if (c < 0) {
            return r;
        }
        if (r.offset >> (63 - 4)) {
            r.status = PARSE_OVERFLOW;
            return r;
        }
        r.offset = r.offset<<4 | c;
    }
    r.status = s.len ? PARSE_OK : PARSE_INVALID;
    return r;
}

typedef struct {
    i64 value;
    b32 ok;
} Parsed64;

static Parsed64 parse64(Str s)
{
    Parsed64 r = {0};

    // Hexadecimal
    if (s.len>2 && s.data[0]=='0' && (s.data[1]=='x' || s.data[1]=='X')) {
        ParsedOffset o = parseoffet(cuthead(s, 2));
        r.value = o.offset;
        r.ok    = o.status == PARSE_OK;
        return r;
    }

    // Octal
    if (s.len>1 && s.data[0]=='0') {
        for (iz i = 1; i < s.len; i++) {
            u8 c = s.data[i] - '0';
            if (c > 7) {
                return r;
            } else if (r.value >> (63 - 3)) {
                return r;  // overflow
            }
            r.value = r.value<<3 | c;
        }
        r.ok = 1;
        return r;
    }

    // Decimal
    for (iz i = 0; i < s.len; i++) {
        u8 c = s.data[i] - '0';
        if (c > 9) {
            return r;
        } else if (r.value > (maxof(i64) - c)/10) {
            return r;  // overflow
        }
        r.value = r.value*10 + c;
    }
    r.ok = !!s.len;
    return r;
}

typedef struct {
    u64 accum;
    u32 len;
} Accum;

static Accum accumulate(Accum a, Output *b, u8 c)
{
    i32 v = nibbleval(c);
    if (v >= 0) {
        a.accum = a.accum<<4 | v;
        if (!(++a.len & 15)) {
            u64 accum = a.accum;
            u8 buf[] = {
                (u8)(accum >> 56), (u8)(accum >> 48),
                (u8)(accum >> 40), (u8)(accum >> 32),
                (u8)(accum >> 24), (u8)(accum >> 16),
                (u8)(accum >>  8), (u8)(accum >>  0),
            };
            output(b, buf, countof(buf));
        }
    }
    return a;
}

static void finish(Accum a, Output *b)
{
    i32 blen = a.len>>1 & 7;
    if (blen) {
        u64 accum = a.accum << (8 - blen) * 8;
        u8 buf[] = {
            (u8)(accum >> 56), (u8)(accum >> 48),
            (u8)(accum >> 40), (u8)(accum >> 32),
            (u8)(accum >> 24), (u8)(accum >> 16),
            (u8)(accum >>  8), (u8)(accum >>  0),
        };
        output(b, buf, blen);
    }
}

// Use SWAR to find the length of an 8-octet hexdump region, which ends
// on a (double space). Ignores the MSB of each octet, and so matches
// some non-spaces. That's acceptable because parsing may safely stop on
// any invalid input.
static i32 hexdumplen(u8 s[8])
{
    static u8 lens[256] = {
        0,1,2,2,0,3,3,3,0,1,4,4,0,4,4,4,0,1,2,2,0,5,5,5,0,1,5,5,0,5,5,5,
        0,1,2,2,0,3,3,3,0,1,6,6,0,6,6,6,0,1,2,2,0,6,6,6,0,1,6,6,0,6,6,6,
        0,1,2,2,0,3,3,3,0,1,4,4,0,4,4,4,0,1,2,2,0,8,8,8,0,1,8,8,0,8,8,8,
        0,1,2,2,0,3,3,3,0,1,8,8,0,8,8,8,0,1,2,2,0,8,8,8,0,1,8,8,0,8,8,8,
        0,1,2,2,0,3,3,3,0,1,4,4,0,4,4,4,0,1,2,2,0,5,5,5,0,1,5,5,0,5,5,5,
        0,1,2,2,0,3,3,3,0,1,8,8,0,8,8,8,0,1,2,2,0,8,8,8,0,1,8,8,0,8,8,8,
        0,1,2,2,0,3,3,3,0,1,4,4,0,4,4,4,0,1,2,2,0,8,8,8,0,1,8,8,0,8,8,8,
        0,1,2,2,0,3,3,3,0,1,8,8,0,8,8,8,0,1,2,2,0,8,8,8,0,1,8,8,0,8,8,8,
    };
    u64 x = (u64)s[0] <<  0 | (u64)s[1] <<  8 |  // little endian optimized
            (u64)s[2] << 16 | (u64)s[3] << 24 |
            (u64)s[4] << 32 | (u64)s[5] << 40 |
            (u64)s[6] << 48 | (u64)s[7] << 56;
    x ^= 0x2020202020202020;
    x |= 0x8080808080808080;
    x -= 0x0101010101010101;
    x &= 0x8080808080808080;
    x *= 0x0002040810204081;
    u8 b = (u8)(x>>56);
    return lens[b];
}

// Trim the text column, which is separated by a double space.
static Str hexdumptrim(Str s)
{
    Str r = s;
    r.len = 0;

    // Step by 7 in case double space straddles two blocks.
    for (; r.len < s.len-7; r.len += 7) {
        i32 len = hexdumplen(s.data+r.len);
        if (len < 8) {
            r.len += len;
            return r;
        }
    }

    u8 tmp[8] = "        ";
    xcpy(tmp, s.data+r.len, s.len-r.len);
    r.len += hexdumplen(tmp);
    return r;
}

static void run_rhexdump(Config conf, Output *bo, Input *bi, Arena a)
{
    while (!bo->err && !bi->err) {
        Arena scratch = a;
        Str line = nextline(bi, &scratch);
        if (!line.len) {
            break;
        }

        Cut c = cut(line, ':');
        if (!c.ok) continue;

        Str addr = trim(c.head);
        ParsedOffset p = parseoffet(addr);
        switch (p.status) {
        case PARSE_INVALID:
            continue;
        case PARSE_OVERFLOW:
            bo->err = 1;
            continue;  // as seek error
        case PARSE_OK:
            if (conf.rseek > maxof(i64) - p.offset) {
                bo->err = 1;
                continue;  // as seek error
            }
            p.offset += conf.rseek;
            seekout(bo, p.offset);
        };

        Str   data = hexdumptrim(c.tail);
        Accum acc  = {0};
        for (iz i = 0; i < data.len; i++) {
            acc = accumulate(acc, bo, data.data[i]);
        }
        finish(acc, bo);
    }
}

static void run_rpostscript(Output *bo, Input *bi)
{
    Accum acc = {0};
    for (i32 v; (v = next(bi)) >= 0;) {
        acc = accumulate(acc, bo, (u8)v);
    }
    finish(acc, bo);
}

static void usage(Output *b)
{
    static u8 usage_text[] =
    "usage: xxd [options] [INPATH [OUTPATH]]\n"
    "  -a            toggle \"autoskip\": print * for repeated null lines\n"
    "  -b            dump in binary format (incompatible with -r)\n"
    "  -C            capitalize variable names in -i\n"
    "  -c INT        number of octets per line\n"
    "  -d            display offsets in decimal (incompatible with -r)\n"
    "  -E            display text data in EBCDIC\n"
    "  -e            reverse byte order within groups (incompatible with -r)\n"
    "  -g INT        number of octets per group\n"
    "  -h            print this message\n"
    "  -i            dump as a C program\n"
    "  -l INT        number of octets to read\n"
    "  -n NAME       variable name for -i\n"
    "  -o INT        offset to add to printed file positions\n"
    "  -ps           output in postscript format\n"
    "  -r            reverse hexdump to patch a binary\n"
    "  -s [+][-]INT  initial absolute seek (or + for rel) on input\n"
    "  -u            use uppercase hexadecimal for octets\n"
    "  -v            display version information\n"
    "  -x            strace-like log on standard error [rexxd only]\n";
    print(b, S(usage_text));
}

// Does not return on error.
static Str getarg(i32 argc, u8 **argv, i32 *i, Output *be)
{
    Str r = {0};
    if (argv[*i][2]) {
        r = import(argv[*i]+2);
    } else if (*i+1 == argc) {
        print(be, S("xxd: missing argument: -"));
        printu8(be, argv[*i][1]);
        printu8(be, '\n');
        flush(be);
        xxd_exit(be->ctx, STATUS_CMD);
    } else {
        r = import(argv[++*i]);
    }
    return r;
}

static i32 xxd_(i32 argc, u8 **argv, Xxd *ctx, Arena a)
{
    Input  *bi = newinput(&a, ctx);
    Output *bo = newoutput(&a, 1, ctx);
    Output *be = newoutput(&a, 2, ctx);
    ctx->be = be;

    enum {
        MODE_HEXDUMP,
        MODE_BINDUMP,
        MODE_INCLUDE,
        MODE_POSTSCRIPT,
    };
    i32 mode     = MODE_HEXDUMP;
    Config conf  = {0};
    b32 splus    = 0;
    b32 sminus   = 0;
    i64 initseek = 0;

    // The original xxd uses a crude, custom option parser. The rexxd
    // parser is intended to match its semantics.
    i32 optind = 1;
    for (b32 done = 0; optind < argc; optind++) {
        u8 *arg = argv[optind];
        if (arg[0] != '-') {
            break;
        }

        switch (arg[1]) {
            Str optarg;
            Parsed64 p;

        case '-':  // --...
            if (!arg[2]) {
                optind++;  // consume --
                done = 1;
                break;
            }
            // fallthrough

        default:
            print(be, S("xxd: unknown option "));
            print(be, import(arg));
            print(be, S("\n"));
            usage(be);
            flush(be);
            return STATUS_CMD;

        case 0:  // "-" (standard input)
            done = 1;
            break;

        case 'a':
            conf.autoskip ^= 1;
            break;

        case 'b':
            mode = MODE_BINDUMP;
            conf.grouplen = conf.grouplen ? conf.grouplen : 1;
            conf.ncols = conf.ncols ? conf.ncols : 6;
            break;

        case 'C':
            conf.capitalize = 1;
            break;

        case 'c':
            optarg = getarg(argc, argv, &optind, be);
            p = parse64(optarg);
            if (!p.ok || p.value<1 || (i32)p.value!=p.value) {
                print(be, S("xxd: invalid argument: -c: "));
                print(be, optarg);
                print(be, S("\n"));
                flush(be);
                return STATUS_CMD;
            } else if (p.value > a.end-a.beg) {
                oom(ctx);  // not enough memory
            }
            conf.ncols = (i32)p.value;
            break;

        case 'd':
            conf.decimal = 1;
            break;

        case 'E':
            conf.ebcdic = 1;
            break;

        case 'e':
            // BUG: In the original xxd this mode is broken when -c is
            // not a multiple of -g.
            mode = MODE_HEXDUMP;
            conf.grouplen = conf.grouplen ? conf.grouplen : 4;
            conf.lendian = 1;
            break;

        case 'g':
            optarg = getarg(argc, argv, &optind, be);
            p = parse64(optarg);
            if (!p.ok || (i32)p.value!=p.value) {
                print(be, S("xxd: invalid argument: -g: "));
                print(be, optarg);
                print(be, S("\n"));
                flush(be);
                return STATUS_CMD;
            } else if (p.value > a.end-a.beg) {
                oom(ctx);  // not enough memory
            }
            conf.grouplen = !p.value ? -1 : (i32)p.value;
            break;

        case 'h':
            usage(bo);
            flush(bo);
            return bo->err ? STATUS_OUTPUT : STATUS_OK;

        case 'i':
            mode = MODE_INCLUDE;
            conf.ncols = conf.ncols ? conf.ncols : 12;
            break;

        case 'l':
            optarg = getarg(argc, argv, &optind, be);
            p = parse64(optarg);
            if (!p.ok) {
                print(be, S("xxd: invalid argument: -l: "));
                print(be, optarg);
                print(be, S("\n"));
                flush(be);
                return STATUS_CMD;
            }
            bi->limit = p.value;
            break;

        case 'n':
            // NOTE: Prints the name exactly as requested, no filtering
            // nor capitalization. This differs from the original xxd
            // which filters the name like a file name, except when it
            // doesn't (BUG: try "-n ''").
            conf.name = getarg(argc, argv, &optind, be);
            break;

        case 'o':
            optarg = getarg(argc, argv, &optind, be);
            p = parse64(optarg);
            if (!p.ok) {
                print(be, S("xxd: invalid argument: -o: "));
                print(be, import(arg));
                print(be, S("\n"));
                flush(be);
                return STATUS_CMD;
            }
            conf.dispoff = p.value;
            break;

        case 'p':
            mode = MODE_POSTSCRIPT;
            conf.ncols = conf.ncols ? conf.ncols : 30;
            break;

        case 'R':
            // color: consume argument and ignore for compatibility
            //
            // In the original xxd (as of 2024-12-07), -R is broken:
            // * BUG: Breaks -e formatting even further (ex. "-e -c5")
            // * BUG: Writes ANSI escapes to files by default (Windows)
            getarg(argc, argv, &optind, be);
            break;

        case 'r':
            conf.reverse = 1;
            break;

        case 's':
            // We cannot use two's complement for -s because negative
            // zero (-0) is distinct from non-negative zero (0). So we
            // use sminus to track the presence of '-' in the input.
            //
            // BUG: Broken in the original xxd on Windows for pipe
            // inputs. Elsewhere it uses read(2) to seek forward in the
            // pipe, but on Windows it skips the seek without error.
            //
            // NOTE(skeeto): This option is deeply confusing, and after
            // hours trying to understand it, I'm still unsure if I do.
            // It has 13 "modes" when augmented by context.
            optarg = getarg(argc, argv, &optind, be);

            if (conf.reverse) {
                // An entirely different option when after -r
                i32 sign = +1;
                if (optarg.len && *optarg.data=='-') {
                    optarg = cuthead(optarg, 1);
                    sign = -1;
                }
                p = parse64(optarg);
                if (!p.ok) {
                    print(be, S("xxd: invalid argument: -s: "));
                    print(be, optarg);
                    print(be, S("\n"));
                    flush(be);
                    return STATUS_CMD;
                }
                conf.rseek = sign * p.value;
                break;
            }

            Str orig = optarg;
            if (optarg.len && optarg.data[0]=='+') {
                optarg = cuthead(optarg, 1);
                splus = 1;
            }
            b32 sign = +1;
            if (optarg.len && optarg.data[0]=='-') {
                optarg = cuthead(optarg, 1);
                sign = -1;
                sminus = 1;
            }
            p = parse64(optarg);
            if (!p.ok) {
                print(be, S("xxd: invalid argument: -s: "));
                print(be, orig);
                print(be, S("\n"));
                flush(be);
                return STATUS_CMD;
            }
            initseek = sign * p.value;
            break;

        case 'u':
            conf.upper = 1;
            break;

        case 'v':
            print(bo, S("xxd " VERSION " (rexxd for w64devkit)\n"));
            flush(bo);
            return bo->err ? STATUS_OUTPUT : STATUS_OK;

        case 'x':
            ctx->trace = 1;
            break;
        }

        if (done) break;
    }

    // Configure defaults if unconfigured
    conf.ncols = conf.ncols ? conf.ncols : 16;
    switch (conf.grouplen) {
    case  0: conf.grouplen = 2; break;
    case -1: conf.grouplen = conf.ncols;
    }

    // Validate -e against other options
    if (conf.lendian && (conf.ncols % conf.grouplen)) {
        print(be, S("xxd: for -e, -c must be a multiple of -g\n"));
        flush(be);
        return STATUS_CMD;
    }

    // Validate -r against other options
    if (conf.reverse) {
        b32 ok = 1;
        ok &= !conf.lendian;
        ok &= !conf.decimal;
        ok &= mode==MODE_HEXDUMP || mode==MODE_POSTSCRIPT;
        if (!ok) {
            print(be, S("xxd: -r incompatible with format\n"));
            flush(be);
            return STATUS_CMD;
        }
    }

    switch (argc - optind) {
    default:
        print(be, S("xxd: too many arguments\n"));
        usage(be);
        flush(be);
        return STATUS_CMD;
    case  2:
        conf.outpath = import(argv[optind+1]);
        if (equals(conf.outpath, S("-"))) {
            conf.outpath = (Str){0};
        }
        // fallthrough
    case  1:
        conf.inpath = import(argv[optind+0]);
        if (equals(conf.inpath, S("-"))) {
            conf.inpath = (Str){0};
        }
        break;
    case  0:
    case -1:
        break;
    }

    // Open input before output
    b32 seekok = 1;
    if (conf.inpath.data) {
        if (!xxd_open(ctx, 0, conf.inpath.data, 0, &a)) {
            print(be, S("xxd: error opening input file: "));
            print(be, conf.inpath);
            print(be, S("\n"));
            flush(be);
            return STATUS_INPUT;
        }

        // -s: '+' is ignored for non-stdin
        if (sminus) {
            // -s -N  /  -s +-N  (N may be zero)
            i64 r = xxd_seek(ctx, 0, initseek, PLT_SEEK_END);
            seekok = r >= 0;
            conf.dispoff += (u64)r;  // allow overflow
        } else if (initseek) {
            // -s N   /  -s +N  (nonzero N)
            i64 r = xxd_seek(ctx, 0, initseek, PLT_SEEK_SET);
            if (r<0 && initseek>0 && readseek(bi, initseek)) {
                r = initseek;
            }
            seekok = r >= 0;
            conf.dispoff += (u64)r;  // allow overflow
        } else {
            // -s 0  /  -s +0  (do nothing)
        }
    } else if (!splus && sminus) {
        // -s -N  (N may be zero)
        i64 r = xxd_seek(ctx, 0, initseek, PLT_SEEK_END);
        seekok = r >= 0;
        conf.dispoff += (u64)r;  // allow overflow
    } else if (!splus && initseek) {
        // -s N  (nonzero N)
        i64 r = xxd_seek(ctx, 0, initseek, PLT_SEEK_SET);
        if (r<0 && initseek>0 && readseek(bi, initseek)) {
            r = initseek;
        }
        seekok = r >= 0;
        conf.dispoff += (u64)r;  // allow overflow
    } else if (splus && initseek) {
        // -s +N  /  -s +-N  (nonzero N)
        i64 r = xxd_seek(ctx, 0, initseek, PLT_SEEK_CUR);
        if (r<0 && initseek>0 && readseek(bi, initseek)) {
            r = initseek;
        }
        seekok = r >= 0;
        conf.dispoff += (u64)r;  // allow overflow
    } else {
        // -s 0  / -s +0 (do nothing)
    }
    if (!seekok) {
        print(be, S("xxd: failed -s seek on input\n"));
        flush(be);
        return STATUS_INPUT;
    }

    if (conf.outpath.data) {
        if (!xxd_open(ctx, 1, conf.outpath.data, !conf.reverse, &a)) {
            print(be, S("xxd: error opening output file: "));
            print(be, conf.outpath);
            print(be, S("\n"));
            flush(be);
            return STATUS_OUTPUT;
        }
    }

    switch (mode) {
    case MODE_HEXDUMP:
        if (conf.reverse) {
            run_rhexdump(conf, bo, bi, a);
        } else {
            run_hexdump(conf, bo, bi, a);
        }
        break;
    case MODE_BINDUMP:
        run_bindump(conf, bo, bi, a);
        break;
    case MODE_INCLUDE:
        run_include(conf, bo, bi, a);
        break;
    case MODE_POSTSCRIPT:
        if (conf.reverse) {
            run_rpostscript(bo, bi);
        } else {
            run_postscript(conf, bo, bi, a);
        }
        break;
    }

    i32 r = STATUS_OK;
    flush(bo);
    if (bo->err) {
        print(be, S("xxd: error writing output\n"));
        r = STATUS_OUTPUT;
    } else if (bi->err) {
        print(be, S("xxd: error reading input\n"));
        r = STATUS_INPUT;
    }
    flush(be);
    return r;
}

static i32 xxd(i32 argc, u8 **argv, Plt *plt, byte *mem, iz cap)
{
    // Bootstrap a context
    Arena a  = {0, mem, mem+cap};
    Xxd *ctx = a.ctx = new(&a, 1, Xxd);  // cannot fail (always fits)
    ctx->plt = plt;

    i32 r = xxd_(argc, argv, ctx, a);
    if (ctx->trace) {
        Output *be = ctx->be;
        print(be, S("exit("));
        printi64(be, r);
        print(be, S(") = ?\n"));
        flush(be);
    }
    return r;
}


#if NO_PLATFORM
// Platform defined external to this source


#elif TEST
// All tests run on a virtual file system, so no real files are read nor
// written during tests. The main program is also run repeatedly in the
// same process with the environment reset between tests.
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Plt {
    // Output
    i64 off;
    Str output;
    iz  cap;
    b32 outpipe;

    // Input
    Str input;
    iz  inpos;
    i32 pipebuf;
    b32 inpipe;

    // Exit
    i32      status;
    jmp_buf *oom;  // pointer hides ugly GDB printout
};

static b32 plt_open(Plt *plt, i32 fd, u8 *buf, b32 trunc, Arena *a)
{
    (void)a;
    (void)buf;
    switch (fd) {
    case  0: plt->inpos = 0;
             break;
    case  1: plt->off = 0;
             if (trunc) {
                 plt->output.len = 0;
             }
             break;
    default: affirm(0);
    }
    return 1;
}

static i64 plt_seek(Plt *plt, i32 fd, i64 off, i32 whence)
{
    i64 from  = 0;
    i64 limit = maxof(i64);

    switch (fd) {
    default: affirm(0);
    case 0: if (plt->inpipe) return -1;
            limit = plt->input.len;  // seeks beyond end are errors
            switch (whence) {
            case PLT_SEEK_SET: from = 0;          break;
            case PLT_SEEK_CUR: from = plt->inpos; break;
            case PLT_SEEK_END: from = limit;      break;
            default:           affirm(0);
            }
            break;
    case 1: if (plt->outpipe) return -1;
            limit = maxof(i64);  // seeks beyond end are ok
            switch (whence) {
            case PLT_SEEK_SET: from = 0;               break;
            case PLT_SEEK_CUR: from = plt->off;        break;
            case PLT_SEEK_END: from = plt->output.len; break;
            default:           affirm(0);
            }
            break;
    }

    if (off<0 && off < -from) {
        affirm(0);  // negative position (rexxd should never attempt this)
    } else if (off>0 && off > limit - from) {
        return -1;  // beyond limit
    }

    switch (fd) {
    case 0: plt->inpos = (iz)from + (iz)off;
            return plt->inpos;
    case 1: plt->off = from + off;
            return plt->off;
    }
    affirm(0);
}

static i32 plt_read(Plt *plt, u8 *buf, i32 len)
{
    i32 max = plt->pipebuf;
    iz  rem = plt->input.len - plt->inpos;
    len = len<max ?      len : max;
    len = rem<len ? (i32)rem : len;
    if (len) memcpy(buf, plt->input.data+plt->inpos, len);
    plt->inpos += len;
    return len;
}

static b32 plt_write(Plt *plt, i32 fd, u8 *buf, i32 len)
{
    affirm(len >= 0);
    affirm(fd==1 || fd==2);

    if (fd != 1) {
        return 1;  // discard standard error
    }

    if (plt->off > plt->cap - len) {
        return 0;  // like ENOSPC "No space left on device"
    }

    iz extend = plt->off - plt->output.len;
    if (extend > 0) {
        u8 *dst = plt->output.data + plt->output.len;
        plt->output.len += extend;
        memset(dst, 0, extend);
    }

    u8 *dst = plt->output.data + plt->off;
    if (plt->off+len > plt->output.len) {
        plt->output.len = plt->off + len;
    }
    memcpy(dst, buf, len);
    return 1;
}

static void plt_exit(Plt *plt, i32 r)
{
    longjmp(*plt->oom, r);
}

static Plt *newplt(Arena *a, iz cap)
{
    Plt *plt = new(a, 1, Plt);
    plt->oom = new(a, 1, jmp_buf);
    plt->output.data = new(a, cap, u8);
    plt->cap = cap;
    plt->pipebuf = 80;
    return plt;
}

#define expect(r, s, ...) \
    do { \
        if (!(plt->status = setjmp(*plt->oom))) { \
            char *argv[] = {"xxd", __VA_ARGS__, 0}; \
            i32 argc = countof(argv) - 1; \
            plt->status = xxd(argc, (u8 **)argv, plt, a.beg, a.end-a.beg); \
        } \
        affirm(r == plt->status); \
        Str want = S(s); \
        affirm(r!=STATUS_OK || equals(plt->output, want)); \
    } while (0)

static void test_basic(Arena scratch)
{
    // Outputs here are straight from the original xxd
    puts("TEST: xxd -c -g -e -E");

    Arena a   = {0};
    Plt  *plt = 0;

    a   = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S("Hello, world!!!\n");
    expect(
        STATUS_OK,
        "00000000: 4865 6c6c 6f2c 2077 6f72 6c64 2121 210a  "
        "Hello, world!!!.\n",
        "-"
    );

    a   = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S("Hello, world!!!\n");
    expect(
        STATUS_OK,
        "00000000: 48 65 6c 6c 6f 2c 20 77 6f 72 6c 64 21 21 21 0a  "
        "Hello, world!!!.\n",
        "-g", "0x1"
    );

    a   = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S("Hello, world!!!\n");
    expect(
        STATUS_OK,
        "00000000: 48656c6c6f2c20776f726c642121210a  "
        "Hello, world!!!.\n",
        "-g0"
    );

    a   = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S("Hello, world!!!\n");
    expect(
        STATUS_OK,
        "00000000: 6c6c6548 77202c6f  Hello, w\n"
        "00000008: 646c726f 0a212121  orld!!!.\n",
        "-e", "-c010"
    );

    a   = scratch;
    plt = newplt(&a, 1<<12);
    expect(
        STATUS_CMD,
        ""
        "-e", "-c08"
    );

    a   = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S(
        "\xc8\x85\x93\x93\x96\x6b\x40\xa6\x96\x99\x93\x84\x5a\x5a\x5a\x25"
    );
    expect(
        STATUS_OK,
        "00000000: c885 9393 966b 40a6 9699 9384 5a5a 5a25  "
        "Hello, world!!!.\n",
        "-E"
    );
}

static void test_autoskip(Arena scratch)
{
    // NOTE: These differs slightly from the original xxd, which doesn't
    // produce a skip line as often as it could (and perhaps should?).
    puts("TEST: xxd -a");

    Arena a   = {0};
    Plt  *plt = 0;

    a = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S("\0\0\0");
    expect(
        STATUS_OK,
        "00000000: 00  .\n"
        "*\n"
        "00000002: 00  .\n",
        "-a", "-c1"
    );

    a = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S("\0\0\1");
    expect(
        STATUS_OK,
        "00000000: 00  .\n"
        "*\n"
        "00000002: 01  .\n",
        "-a", "-c1"
    );

    a = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S("\0\0");
    expect(
        STATUS_OK,
        "00000000: 00  .\n"
        "00000001: 00  .\n",
        "-a", "-c1"
    );

    a = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S("\0\0\0");
    expect(
        STATUS_OK,
        "00000000: 00000000  .\n"
        "*\n"
        "00000002: 00000000  .\n",
        "-a", "-b", "-c1"
    );

    a = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S("\0\0\1");
    expect(
        STATUS_OK,
        "00000000: 00000000  .\n"
        "*\n"
        "00000002: 00000001  .\n",
        "-a", "-b", "-c1"
    );

    a = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S("\0\0");
    expect(
        STATUS_OK,
        "00000000: 00000000  .\n"
        "00000001: 00000000  .\n",
        "-a", "-b", "-c1"
    );
}

static void test_include(Arena a)
{
    // Expected output comes from the original xxd.
    puts("TEST: xxd -i");
    Plt *plt = newplt(&a, 1<<12);
    plt->input = S("This is a test of the -i include option.");
    expect(
        STATUS_OK,
        "unsigned char foo_bar[] = {\n"
        "  0x54, 0x68, 0x69, 0x73, 0x20, 0x69, "
        "0x73, 0x20, 0x61, 0x20, 0x74, 0x65,\n"
        "  0x73, 0x74, 0x20, 0x6f, 0x66, 0x20, "
        "0x74, 0x68, 0x65, 0x20, 0x2d, 0x69,\n"
        "  0x20, 0x69, 0x6e, 0x63, 0x6c, 0x75, "
        "0x64, 0x65, 0x20, 0x6f, 0x70, 0x74,\n"
        "  0x69, 0x6f, 0x6e, 0x2e\n"
        "};\n"
        "unsigned int foo_bar_len = 40;\n",
        "-i", "foo/bar"
    );
}

static void test_reverse(Arena scratch)
{
    puts("TEST: xxd -r");

    for (i32 pipebuf = 1; pipebuf < 80; pipebuf++) {
        Arena a = scratch;
        Plt *plt = newplt(&a, 1<<12);
        plt->pipebuf = pipebuf;
        plt->input = S(
            "20: abcd 1234\n"
            "10: dead beef\n"
        );
        plt->output.len = 32;
        memcpy(plt->output.data+10, "hello world", 11);
        expect(
            STATUS_OK,
            "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00hello "
            "\xde\xad\xbe\xef""d\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
            "\xab\xcd\x12\x34",
            "-r"
        );
    }

    Arena a   = {0};
    Plt  *plt = 0;

    a = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S(
        "00000000: 4142  AB\n"
    );
    expect(
        STATUS_OK,
        "AB",
        "-r"
    );

    a = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S(
        "00000000: 4142 4344 4546 4748  ABCDEFGH\n"
    );
    expect(
        STATUS_OK,
        "ABCDEFGH",
        "-r"
    );
}

static void test_seek(Arena scratch)
{
    puts("TEST: xxd -s");

    Arena a   = {0};
    Plt  *plt = 0;

    a = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S("010000: 41");
    expect(
        STATUS_OK,
        "A",
        "-r", "-s", "-0x10000"
    );

    a = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S("0000: 58\n");
    plt->outpipe = 1;
    expect(
        STATUS_OK,
        "\x00\x00\x00\x00\x00\x00\x00\x00X",
        "-r", "-s", "8"
    );

    a = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S("AB");
    plt->inpipe = 1;
    expect(
        STATUS_OK,
        "42\n",
        "-ps", "-s1", "fifo"
    );

    a = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S("AB");
    plt->inpipe = 1;
    expect(
        STATUS_OK,
        "42\n",
        "-ps", "-s1", "-"
    );

    a = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S("AB");
    plt->inpipe = 1;
    expect(
        STATUS_OK,
        "42\n",
        "-ps", "-s+1", "-"
    );

    a = scratch;
    plt = newplt(&a, 1<<12);
    plt->inpipe = 1;
    expect(
        STATUS_INPUT,
        "",
        "-s", "1"
    );
}

static void test_limit(Arena a)
{
    puts("TEST: xxd -l");
    Plt *plt = newplt(&a, 1<<12);
    plt->input = S("this is a hello world test");
    expect(
        STATUS_OK,
        "0000000a: 68656C6C6F  hello\n",
        "-s10", "-l5", "-c5", "-g0", "-u"
    );
}

static void test_postscript(Arena scratch)
{
    puts("TEST: xxd -ps");

    // Postscript data comes from the original xxd.
    static char text[] =
        "Let fame, that all hunt after in their lives,\n"
        "Live registered upon our brazen tombs,\n"
        "And then grace us in the disgrace of death;\n"
        "When, spite of cormorant devouring time,\n"
        "The endeavour of this present breath may buy\n"
        "That honour which shall bate his scythe's keen edge\n"
        "And make us heirs of all eternity.\n";
    static char ps[] =
        "4c65742066616d652c207468617420616c6c2068756e7420616674657220\n"
        "696e207468656972206c697665732c0a4c69766520726567697374657265\n"
        "642075706f6e206f7572206272617a656e20746f6d62732c0a416e642074\n"
        "68656e20677261636520757320696e20746865206469736772616365206f\n"
        "662064656174683b0a5768656e2c207370697465206f6620636f726d6f72\n"
        "616e74206465766f7572696e672074696d652c0a54686520656e64656176\n"
        "6f7572206f6620746869732070726573656e7420627265617468206d6179\n"
        "206275790a5468617420686f6e6f7572207768696368207368616c6c2062\n"
        "61746520686973207363797468652773206b65656e20656467650a416e64\n"
        "206d616b65207573206865697273206f6620616c6c20657465726e697479\n"
        "2e0a\n";

    Arena a   = {0};
    Plt  *plt = 0;

    a = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S(text);
    expect(
        STATUS_OK,
        ps,
        "-ps"
    );

    a = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S(ps);
    expect(
        STATUS_OK,
        text,
        "-ps", "-r"
    );

    a = scratch;
    plt = newplt(&a, 1<<12);
    plt->input = S("d ead b e e  f");
    expect(
        STATUS_OK,
        "\xde\xad\xbe\xef",
        "-ps", "-r"
    );
}

static void test_bindump(Arena a)
{
    // Expected output comes from the original xxd.
    puts("TEST: xxd -b");
    Plt *plt = newplt(&a, 1<<12);
    plt->input = S("We have heard the chimes at midnight");
    expect(
        STATUS_OK,
        "00000000: 01010111 01100101 00100000 01101000 01100001  We ha\n"
        "00000005: 01110110 01100101 00100000 01101000 01100101  ve he\n"
        "0000000a: 01100001 01110010 01100100 00100000 01110100  ard t\n"
        "0000000f: 01101000 01100101 00100000 01100011 01101000  he ch\n"
        "00000014: 01101001 01101101 01100101 01110011 00100000  imes \n"
        "00000019: 01100001 01110100 00100000 01101101 01101001  at mi\n"
        "0000001e: 01100100 01101110 01101001 01100111 01101000  dnigh\n"
        "00000023: 01110100                                      t\n",
        "-b", "-c", "5"
    );
}

static void test_nospace(Arena a)
{
    puts("TEST: ENOSPC");
    Plt *plt = newplt(&a, 1);
    plt->input = S("test");
    expect(STATUS_OUTPUT, "", "-");
}

int main(void)
{
    i32   cap = 1<<20;
    byte *mem = malloc(cap);
    Arena a   = {0, mem, mem+cap};
    test_basic(a);
    test_autoskip(a);
    test_include(a);
    test_reverse(a);
    test_seek(a);
    test_limit(a);
    test_postscript(a);
    test_bindump(a);
    test_nospace(a);
    puts("all tests passed");
}


#elif RANDTEST
// Ram random inputs through hexdump and inverse with random short reads.
// $ cc -DRANDTEST -g3 -O2 -funroll-loops -o randtest
//      -fsanitize=undefined -fsanitize-trap rexxd.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Plt {
    u64 rng;
    u8 *input;
    i32 inoff;
    i32 inlen;
    u8 *output;
    i32 outoff;
    i32 outlen;
};

static b32  plt_open(Plt *, i32, u8 *, b32, Arena *) { affirm(0); }
static i64  plt_seek(Plt *, i32, i64, i32) { affirm(0); }
static void plt_exit(Plt *, i32) { affirm(0); }

static u64 rand64(u64 *rng)
{
    return (*rng = *rng*0x3243f6a8885a308d + 1);
}

static i32 randrange(u64 *rng, i32 lo, i32 hi)
{
    return (i32)(((rand64(rng)>>32) * (hi - lo))>>32) + lo;
}

static i32  plt_read(Plt *plt, u8 *buf, i32 len)
{
    iz avail = plt->inlen - plt->inoff;
    len = avail<len ? (i32)avail : len;
    if (len) {
        len = randrange(&plt->rng, 1, len+1);
        memcpy(buf, plt->input+plt->inoff, len);
        plt->inoff += len;
    }
    return len;
}

static b32 plt_write(Plt *plt, i32 fd, u8 *buf, i32 len)
{
    affirm(fd == 1);
    affirm(len <= plt->outlen - plt->outoff);
    memcpy(plt->output+plt->outoff, buf, len);
    plt->outoff += len;
    return 1;
}

static u8 *encode(Arena *a, i32 x)
{
    u8 *r = new(a, 16, u8) + 15;
    do *--r = '0' + (u8)(x%10);
    while (x /= 10);
    return r;
}

int main(void)
{
    i32   cap = 1<<21;
    byte *mem = malloc(cap);
    Arena a   = {0, mem, mem+cap};

    i32 truthcap = 1<<12;
    u8 *truth    = new(&a, truthcap, u8);
    i32 dumpcap  = 1<<20;
    u8 *dump     = new(&a, dumpcap,  u8);
    i32 rdumpcap = 1<<12;
    u8 *rdump    = new(&a, rdumpcap, u8);

    Plt plt = {0};
    plt.rng = 1;

    for (u64 i = 1;; i++) {
        if (!(i % 100000)) {
            printf("%llu\n", (long long)i);
        }

        i32 len = randrange(&plt.rng, truthcap/2, truthcap+1);

        plt.input = truth;
        plt.inoff = 0;
        plt.inlen = len;
        for (i32 i = 0; i < len; i++) {
            truth[i] = (u8)(rand64(&plt.rng) >> 56);
        }
        plt.output = dump;
        plt.outoff = 0;
        plt.outlen = dumpcap;

        i32 ncols    = randrange(&plt.rng, 1<<0, 1<<10);
        i32 grouplen = randrange(&plt.rng, 1, ncols+1);
        {
            Arena t = a;
            u8 *argv[] = {
                0,
                (u8[]){"-c"}, encode(&t, ncols),
                (u8[]){"-g"}, encode(&t, grouplen),
            };
            i32 argc   = countof(argv);
            i32 status = xxd(argc, argv, &plt, t.beg, t.end-t.beg);
            affirm(status == STATUS_OK);
        }

        plt.input = dump;
        plt.inoff = 0;
        plt.inlen = plt.outoff;
        plt.output = rdump;
        plt.outoff = 0;
        plt.outlen = rdumpcap;

        {
            Arena t = a;
            u8 *argv[] = {0, (u8[]){"-r"}};
            i32 argc   = countof(argv);
            i32 status = xxd(argc, argv, &plt, t.beg, t.end-t.beg);
            affirm(status == STATUS_OK);
        }

        affirm(plt.outoff == len);
        affirm(!memcmp(truth, rdump, len));
    }
}


#elif __AFL_COMPILER
// Fuzz test "xxd -r"; other input types are trivial and not worth fuzzing.
//   $ mkdir i
//   $ echo hello world | xxd >i/sample
//   $ afl-gcc-fast -g3 -fsanitize=undefined -fsanitize-trap rexxd.c
#include <stdlib.h>
#include <unistd.h>

struct Plt {
    Str input;
    iz  off;
    uz  oom[5];
};

static b32 plt_open(Plt *, i32, u8 *, b32, Arena *) { affirm(0); }
static b32 plt_write(Plt *, i32, u8 *, i32) { return 1; }

static i64 plt_seek(Plt *, i32, i64 off, i32)
{
    affirm(off >= 0);
    return off;
}

static i32 plt_read(Plt *plt, u8 *buf, i32 len)
{
    iz rem = plt->input.len - plt->off;
    len = rem<len ? (i32)rem : len;
    __builtin_memcpy(buf, plt->input.data+plt->off, len);
    plt->off += len;
    return len;
}

static void plt_exit(Plt *plt, i32)
{
    __builtin_longjmp(plt->oom, 1);
}

__AFL_FUZZ_INIT();

int main(void)
{
    __AFL_INIT();
    i32   cap = 1<<18;
    byte *mem = malloc(cap);
    u8   *buf = __AFL_FUZZ_TESTCASE_BUF;
    while (__AFL_LOOP(10000)) {
        i32 len = __AFL_FUZZ_TESTCASE_LEN;
        Plt plt = {0};
        if (!__builtin_setjmp(plt.oom)) {
            plt.input.data = buf;
            plt.input.len  = len;
            u8 *argv[] = {0, (u8[]){'-', 'r', 0}};
            xxd(2, argv, &plt, mem, cap);
        }
    }
}


#elif BENCH
// Verifies that changes intended to improve performance actually have
// an effect, and in the right direction. The benchmark doesn't need to
// be particularly portable, just work on the targets where peformance
// is desired.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Plt {
    Str input;
    iz  inpos;
    Str output;
    iz  outpos;
};

static i64 rdtscp(void)
{
    uz hi, lo;
    asm volatile ("rdtscp" : "=d"(hi), "=a"(lo) :: "cx", "memory");
    return (i64)hi<<32 | lo;
}

static b32  plt_open(Plt *, i32, u8 *, b32, Arena *) { affirm(0); }
static i64  plt_seek(Plt *, i32, i64 off, i32) { return off; }
static void plt_exit(Plt *, i32) { affirm(0); }

static i32 plt_read(Plt *plt, u8 *buf, i32 len)
{
    iz rem = plt->input.len - plt->inpos;
    len = rem<len ? (i32)rem : len;
    memcpy(buf, plt->input.data+plt->inpos, len);
    plt->inpos += len;
    return len;
}

static b32 plt_write(Plt *plt, i32, u8 *buf, i32 len)
{
    if (plt->output.data) {
        affirm(plt->output.len-plt->outpos >= len);
        memcpy(plt->output.data+plt->outpos, buf, len);
        plt->outpos += len;
    }
    return 1;
}

static void report(char *cmd, i64 time)
{
    printf("%-20s%lld\n", cmd, (long long)time);
}

static u64 rand64(u64 *rng)
{
    return (*rng = *rng*0x3243f6a8885a308d + 1);
}

int main(void)
{
    i32   cap = 1<<28;
    byte *mem = malloc(cap);
    Arena a   = {0, mem, mem+cap};
    memset(mem, 0xa5, cap);  // pre-commit whole arena

    Str random = {0};
    random.len = 1<<20;
    random.data = new(&a, random.len, u8);
    u64 rng  = 1;
    for (iz i = 0; i < random.len; i++) {
        random.data[i] = (u8)(rand64(&rng) >> 56);
    }

    {
        Arena tmp = a;
        Plt  *plt = new(&tmp, 1, Plt);
        plt->input = random;

        i64 best = maxof(i64);
        for (i32 n = 0; n < 1<<9; n++) {
            plt->inpos = 0;
            i64 total = -rdtscp();
            i32 r = xxd(0, 0, plt, tmp.beg, tmp.end-tmp.beg);
            affirm(r == STATUS_OK);
            total += rdtscp();
            best = total<best ? total : best;
        }
        report("xxd", best>>10);
    }

    {
        Arena tmp = a;

        Str source = random;
        source.len /= 8;

        Str input  = {0};
        input.len  = 16 * source.len;
        input.data = new(&tmp, input.len, u8);

        // Run xxd and capture output to produce a test input
        Plt *plt    = new(&tmp, 1, Plt);
        plt->input  = source;
        plt->output = input;
        i32 r = xxd(0, 0, plt, tmp.beg, tmp.end-tmp.beg);
        affirm(r == STATUS_OK);

        plt = new(&tmp, 1, Plt);
        plt->input = input;

        i64 best = maxof(i64);
        for (i32 n = 0; n < 1<<9; n++) {
            plt->inpos = 0;
            i64 total = -rdtscp();
            u8 *argv[] = {0, (u8[]){"-r"}};
            i32 r = xxd(countof(argv), argv, plt, tmp.beg, tmp.end-tmp.beg);
            affirm(r == STATUS_OK);
            total += rdtscp();
            best = total<best ? total : best;
        }
        report("xxd -r", best>>10);
    }

    {
        Arena tmp = a;
        Plt  *plt = new(&tmp, 1, Plt);
        plt->input = random;

        i64 best = maxof(i64);
        for (i32 n = 0; n < 1<<9; n++) {
            plt->inpos = 0;
            i64 total = -rdtscp();
            u8 *argv[] = {0, (u8[]){"-i"}};
            i32 r = xxd(countof(argv), argv, plt, tmp.beg, tmp.end-tmp.beg);
            affirm(r == STATUS_OK);
            total += rdtscp();
            best = total<best ? total : best;
        }
        report("xxd -i", best>>10);
    }

    {
        Arena tmp = a;
        Plt  *plt = new(&tmp, 1, Plt);
        plt->input = random;
        plt->input.len >>= 2;  // more work -> smaller test

        i64 best = maxof(i64);
        for (i32 n = 0; n < 1<<9; n++) {
            plt->inpos = 0;
            i64 total = -rdtscp();
            u8 *argv[] = {0, (u8[]){"-b"}};
            i32 r = xxd(countof(argv), argv, plt, tmp.beg, tmp.end-tmp.beg);
            affirm(r == STATUS_OK);
            total += rdtscp();
            best = total<best ? total : best;
        }
        report("xxd -b", best>>10);
    }

    {
        Arena tmp = a;
        Plt  *plt = new(&tmp, 1, Plt);
        plt->input = random;

        i64 best = maxof(i64);
        for (i32 n = 0; n < 1<<9; n++) {
            plt->inpos = 0;
            i64 total = -rdtscp();
            u8 *argv[] = {0, (u8[]){"-ps"}};
            i32 r = xxd(countof(argv), argv, plt, tmp.beg, tmp.end-tmp.beg);
            affirm(r == STATUS_OK);
            total += rdtscp();
            best = total<best ? total : best;
        }
        report("xxd -ps", best>>10);
    }

    {
        Arena tmp = a;

        i32 nlines = 1<<15;
        Str ps     = {0};
        ps.len     = 61*nlines;
        ps.data    = new(&a, ps.len, u8);

        u64 rng = 1;
        for (i32 i = 0; i < nlines; i++) {
            u8 *dst = ps.data + i*61;
            for (i32 c = 0; c < 30; c++) {
                u8 v = (u8)(rand64(&rng)>>56);
                dst[c*2+0] = lohex[v>>4];
                dst[c*2+1] = lohex[v&15];
            }
            dst[60] = '\n';
        }

        Plt  *plt = new(&tmp, 1, Plt);
        plt->input = ps;

        i64 best = maxof(i64);
        for (i32 n = 0; n < 1<<9; n++) {
            plt->inpos = 0;
            i64 total = -rdtscp();
            u8 *argv[] = {0, (u8[]){"-ps"}, (u8[]){"-r"}};
            i32 r = xxd(countof(argv), argv, plt, tmp.beg, tmp.end-tmp.beg);
            affirm(r == STATUS_OK);
            total += rdtscp();
            best = total<best ? total : best;
        }
        report("xxd -ps -r", best>>10);
    }
}


#elif DLL
// In-memory hexdump and reverse hexdump DLL. Mostly for showing off.
//
// $ cc -shared -nostdlib -O2 -funroll-loops -s -o xxd.dll
//      rexxd.c -lmemory -lchkstk
//
// Exports (return output size, or -1 on OOM):
//   ptrdiff_t xxd_reverse(void *, ptrdiff_t, void const *, ptrdiff_t);
//   ptrdiff_t xxd_hexdump(void *, ptrdiff_t, void const *, ptrdiff_t);

enum {
    PLT_ALLOCA = 1<<18,  // squeeze into the standard 1MiB stack
};

struct Plt {
    Str input;
    iz  inpos;
    Str output;
    iz  outpos;
    iz  outcap;
    uz  oom[5];
};

static b32  plt_open(Plt *, i32, u8 *, b32, Arena *) { affirm(0); }

static void plt_exit(Plt *plt, i32) { __builtin_longjmp(plt->oom, 1); }

static i64 plt_seek(Plt *plt, i32 fd, i64 off, i32 whence)
{
    affirm(fd == 1);
    affirm(off >= 0);
    affirm(whence == PLT_SEEK_SET);
    if (off > plt->outpos) {
        return -1;
    }
    plt->outpos = off;
    return off;
}

static i32 plt_read(Plt *plt, u8 *buf, i32 len)
{
    iz avail = plt->input.len - plt->inpos;
    len = len<avail ? len : (i32)avail;
    if (len) xcpy(buf, plt->input.data+plt->inpos, len);
    plt->inpos += len;
    return len;
}

static b32 plt_write(Plt *plt, i32 fd, u8 *buf, i32 len)
{
    if (fd == 2) return 1;

    iz avail = plt->outcap - plt->outpos;
    if (avail < len) {
        return 0;  // "No space left on device"
    }

    iz extend = plt->outpos - plt->output.len;
    if (extend > 0) {
        u8 *dst = plt->output.data + plt->output.len;
        plt->output.len += extend;
        xset(dst, 0, extend);
    }

    u8 *dst = plt->output.data + plt->outpos;
    if (plt->outpos+len > plt->output.len) {
        plt->output.len = plt->outpos + len;
    }
    if (len) xcpy(dst, buf, len);

    plt->outpos += len;
    return 1;
}

__declspec(dllexport)
iz xxd_hexdump(u8 *dst, iz dlen, u8 *src, iz slen)
{
    Plt plt = {0};
    plt.input.data  = dst;
    plt.input.len   = dlen;
    plt.output.data = src;
    plt.outcap      = slen;
    if (__builtin_setjmp(plt.oom)) {
        return -1;
    }
    byte *mem = __builtin_alloca(PLT_ALLOCA);
    i32   ret = xxd(0, 0, &plt, mem, PLT_ALLOCA);
    return ret==STATUS_OK ? plt.outpos : -1;
}

__declspec(dllexport)
iz xxd_reverse(u8 *dst, iz dlen, u8 *src, iz slen)
{
    Plt plt = {0};
    plt.input.data  = dst;
    plt.input.len   = dlen;
    plt.output.data = src;
    plt.outcap      = slen;
    if (__builtin_setjmp(plt.oom)) {
        return -1;
    }
    byte *mem    = __builtin_alloca(PLT_ALLOCA);
    u8   *argv[] = {0, (u8[]){"-r"}};
    i32   ret    = xxd(2, argv, &plt, mem, PLT_ALLOCA);
    return ret==STATUS_OK ? plt.outpos : -1;
}


#elif _WIN32
typedef uint16_t    char16_t;
typedef char16_t    c16;

enum {
    CP_UTF8                 = 65001,
    GENERIC_READ            = (i32)0x80000000,
    GENERIC_WRITE           = (i32)0x40000000,
    FILE_SHARE_ALL          = 7,
    FILE_ATTRIBUTE_NORMAL   = 0x80,
    CREATE_ALWAYS           = 2,
    OPEN_EXISTING           = 3,
    OPEN_ALWAYS             = 4,
    MEM_COMMIT              = 0x1000,
    MEM_RESERVE             = 0x2000,
    PAGE_READWRITE          = 4,
    FILE_TYPE_DISK          = 1,
};

#define W32(r)  __declspec(dllimport) r __stdcall
W32(c16 **) CommandLineToArgvW(c16 *, i32 *);
W32(uz)     CreateFileW(c16 *, i32, i32, uz, i32, i32, uz);
W32(void)   ExitProcess(i32);
W32(c16 *)  GetCommandLineW(void);
W32(i32)    GetFileType(uz);
W32(uz)     GetStdHandle(i32);
W32(i32)    MultiByteToWideChar(i32, i32, u8 *, i32, c16 *, i32);
W32(b32)    ReadFile(uz, u8 *, i32, i32 *, uz);
W32(b32)    SetFilePointerEx(uz, i64, i64 *, i32);
W32(byte *) VirtualAlloc(uz, iz, i32, i32);
W32(i32)    WideCharToMultiByte(i32, i32, c16 *, i32, u8 *, i32, uz, uz);
W32(b32)    WriteFile(uz, u8 *, i32, i32 *, uz);

struct Plt {
    uz  stdh[3];
    i32 seekable;
};

static void setstdh(Plt *plt, i32 fd, uz h)
{
    plt->stdh[fd] = h;
    plt->seekable &= ~(1 << fd);
    if (h) {
        // SetFilePointerEx on non-files has undefined results, so
        // applications must check the type explicitly.
        plt->seekable |= (GetFileType(h) == FILE_TYPE_DISK) << fd;
    }
}

static i32 trunc32(iz n)
{
    return n>maxof(i32) ? maxof(i32) : (i32)n;
}

static b32 plt_open(Plt *plt, i32 fd, u8 *path, b32 trunc, Arena *a)
{
    setstdh(plt, fd, 0);

    i32  avail = trunc32((a->end - a->beg)/2);
    i32  pad   = (i32)-(uz)a->beg & 1;
    c16 *wpath = (c16 *)(a->beg + pad);

    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, avail)) {
        return 0;
    }

    i32 access = fd==0 ? GENERIC_READ : GENERIC_WRITE;
    i32 share  = FILE_SHARE_ALL;
    i32 create = fd==0 ? OPEN_EXISTING : trunc ? CREATE_ALWAYS : OPEN_ALWAYS;
    i32 attr   = FILE_ATTRIBUTE_NORMAL;
    uz h = CreateFileW(wpath, access, share, 0, create, attr, 0);
    if (h == (uz)-1) {
        return 0;
    }
    setstdh(plt, fd, h);
    return 1;
}

static i64 plt_seek(Plt *plt, i32 fd, i64 off, i32 whence)
{
    b32 r = 0;
    if (plt->seekable & 1<<fd) {
        r = SetFilePointerEx(plt->stdh[fd], off, &off, whence);
    }
    return r ? off : -1;
}

static i32 plt_read(Plt *plt, u8 *buf, i32 len)
{
    b32 isfile = plt->seekable & 1;
    b32 r = ReadFile(plt->stdh[0], buf, len, &len, 0);
    return !r && isfile ? -1 : len;
}

static b32 plt_write(Plt *plt, i32 fd, u8 *buf, i32 len)
{
    return WriteFile(plt->stdh[fd], buf, len, &len, 0);
}

static void plt_exit(Plt *plt, i32 r)
{
    (void)plt;
    ExitProcess(r);
    affirm(0);
}

void __stdcall mainCRTStartup(void)
{
    Plt plt = {0};
    setstdh(&plt, 0, GetStdHandle(-10));
    setstdh(&plt, 1, GetStdHandle(-11));
    setstdh(&plt, 2, GetStdHandle(-12));

    iz    cap = (iz)1<<24;
    byte *mem = VirtualAlloc(0, cap, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    Arena a   = {0, mem, mem+cap};

    c16  *cmd   = GetCommandLineW();
    i32   argc  = 0;
    c16 **wargv = CommandLineToArgvW(cmd, &argc);
    u8  **argv  = new(&a, argc+1, u8 *);
    for (i32 i = 0; i < argc; i++) {
        i32 len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, 0, 0, 0, 0);
        argv[i] = new(&a, len, u8);
        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, argv[i], len, 0, 0);
    }

    i32 r = xxd(argc, argv, &plt, a.beg, a.end-a.beg);
    ExitProcess(r);
    affirm(0);
}


#elif __linux && __amd64 && !__STDC_HOSTED__
// Like the POSIX platform, but raw syscalls and no libc. Mostly for fun.
//   $ musl-gcc -ffreestanding -O2 -funroll-loops
//              -s -static -nostartfiles -o xxd rexxd.c

enum {
    SYS_read    = 0,
    SYS_write   = 1,
    SYS_open    = 2,
    SYS_lseek   = 8,
    SYS_exit    = 60,
    O_WRONLY    = 0x001,
    O_CREAT     = 0x040,
    O_TRUNC     = 0x200,
};

struct Plt {
    int fds[3];
};

static iz syscall3(i32 n, uz a, uz b, uz c)
{
    iz r;
    asm volatile (
        "syscall"
        : "=a"(r)
        : "a"(n), "D"(a), "S"(b), "d"(c)
        : "rcx", "r11", "memory"
    );
    return r;
}

static b32 plt_open(Plt *plt, i32 fd, u8 *path, b32 trunc, Arena *)
{
    i32 mode = fd ? O_CREAT|O_WRONLY : 0;
    mode |= trunc ? O_TRUNC : 0;
    plt->fds[fd] = (i32)syscall3(SYS_open, (uz)path, mode, 0666);
    return plt->fds[fd] >= 0;
}

static i64 plt_seek(Plt *plt, i32 fd, i64 off, i32 whence)
{
    return syscall3(SYS_lseek, plt->fds[fd], off, whence);
}

static i32 plt_read(Plt *plt, u8 *buf, i32 len)
{
    return (i32)syscall3(SYS_read, plt->fds[0], (uz)buf, len);
}

static b32 plt_write(Plt *plt, i32 fd, u8 *buf, i32 len)
{
    return len == syscall3(SYS_write, plt->fds[fd], (uz)buf, len);
}

static void plt_exit(Plt *, i32 r)
{
    syscall3(SYS_exit, r, 0, 0);
    affirm(0);
}

__attribute((used))
static void entrypoint(uz *stack)
{
    static byte heap[1<<24];
    byte *mem = heap;
    asm ("" : "+r"(mem)); // launder: disconnect from "heap"
    Plt plt = {{0, 1, 2}};
    i32 r   = xxd((i32)*stack, (u8 **)(stack+1), &plt, mem, countof(heap));
    plt_exit(0, r);
}

asm (
    "        .globl _start\n"
    "_start: mov   %rsp, %rdi\n"
    "        call  entrypoint\n"
);


#else  // POSIX
// POSIX has no explicitly-64-bit seek, so use lseek(2) and hope for the
// best. If off_t is smaller than 64 bits, rexxd still works correctly
// (won't seek to the wrong position), but huge seeks will fail.
//
// 32-bit Linux: Consider compiling with -D_FILE_OFFSET_BITS=64.
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

struct Plt {
    // Virtual descriptor table avoids dup2(2), and close(2) on failure.
    int fds[3];
};

static b32 plt_open(Plt *plt, i32 fd, u8 *path, b32 trunc, Arena *)
{
    int mode = fd==0 ? O_RDONLY : O_CREAT|O_WRONLY;
    mode |= trunc ? O_TRUNC : 0;
    plt->fds[fd] = open((char *)path, mode, 0666);
    return plt->fds[fd] != -1;
}

static i64 plt_seek(Plt *plt, i32 fd, i64 off, i32 whence)
{
    static i32 tx[] = {
        [PLT_SEEK_SET] = SEEK_SET,
        [PLT_SEEK_CUR] = SEEK_CUR,
        [PLT_SEEK_END] = SEEK_END,
    };
    if (off != (off_t)off) {  // off_t is 32-bit and truncates?
        return -1;  // too large for system lseek()
    }
    return lseek(plt->fds[fd], off, tx[whence]);
}

static i32 plt_read(Plt *plt, u8 *buf, i32 len)
{
    return (i32)read(plt->fds[0], buf, len);
}

static b32 plt_write(Plt *plt, i32 fd, u8 *buf, i32 len)
{
    return len == write(plt->fds[fd], buf, len);
}

static void plt_exit(Plt *, i32 r)
{
    _exit(r);
}

int main(int argc, char **argv)
{
    Plt   plt = {{0, 1, 2}};
    iz    cap = (iz)1<<24;
    byte *mem = mmap(0, cap, PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    i32   r   = xxd(argc, (u8 **)argv, &plt, mem, cap);
    _exit(r);
}
#endif
