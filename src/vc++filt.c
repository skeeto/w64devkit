// vc++filt: c++filt for Microsoft Visual C++, like undname
// $ cc -nostartfiles -O -o vc++filt.exe vc++filt.c -ldbghelp
// $ cl /O1 vc++filt.c /link /subsystem:console
//      kernel32.lib shell32.lib dbghelp.lib libvcruntime.lib
// * Full Unicode support, including UTF-8 and wide console output
// * Tiny, slim, fast binary, low resource use, CRT-free
// This is free and unencumbered software released into the public domain.
#include <stddef.h>

enum {
    BUFFER_CAP  = 1<<14,
    CONVERT_CAP = 1<<10,
    SYMBOL_MAX  = 1<<16,
    MEMORY_CAP  = 1<<24,
};

#define countof(a)    (size)(sizeof(a) / sizeof(*(a)))
#define new(h, t, n)  (t *)alloc(h, sizeof(t)*n)

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef   signed int       b32;
typedef   signed int       i32;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef          char      byte;
typedef          size_t    uptr;
typedef          ptrdiff_t size;

#define W32(r) __declspec(dllimport) r __stdcall
W32(u16 **) CommandLineToArgvW(u16 *, i32 *);
W32(void)   ExitProcess(i32);
W32(u16 *)  GetCommandLineW(void);
W32(b32)    GetConsoleMode(uptr, i32 *);
W32(i32)    GetStdHandle(i32);
W32(b32)    ReadFile(uptr, u8 *, i32, i32 *, uptr);
W32(i32)    UnDecorateSymbolName(u8 *, u8 *, i32, i32);  // dbghelp.dll
W32(byte *) VirtualAlloc(byte *, size, i32, i32);
W32(i32)    WideCharToMultiByte(i32, i32, u16 *, i32, u8 *, i32, uptr, uptr);
W32(b32)    WriteConsoleW(uptr, u16 *, i32, i32 *, uptr);
W32(b32)    WriteFile(uptr, u8 *, i32, i32 *, uptr);

typedef struct {
    byte *beg;
    byte *end;
} arena;

// Allocate pointer-aligned, zero-initialized memory, or null on failure.
static void *alloc(arena *a, size len)
{
    size available = a->end - a->beg;
    size alignment = -len & (sizeof(void *) - 1);
    if (len > available-alignment) {
        return 0;  // out of memory
    }
    return a->end -= len + alignment;  // NOTE: assume arena is zeroed
}

typedef struct {
    u8  *data;
    size len;
} s8;

static u64 s8hash(s8 s)
{
    // NOTE: hash is fast enough; faster has no throughput impact
    u64 h = 0x100;
    for (size i = 0; i < s.len; i++) {
        h ^= s.data[i];
        h *= 1111111111111111111u;
    }
    return h;
}

static b32 s8equals(s8 a, s8 b)
{
    if (a.len != b.len) {
        return 0;
    }
    for (size i = 0; i < a.len; i++) {
        if (a.data[i] != b.data[i]) {
            return 0;
        }
    }
    return 1;
}

static s8 s8push(s8 s, u8 c)
{
    s.data[s.len++] = c;
    return s;
}

static s8 s8dup(s8 s, arena *perm)
{
    s8 r = {0};
    r.data = new(perm, u8, s.len);
    if (!r.data) {
        return r;  // out of memory
    }
    for (size i = 0; i < s.len; i++) {
        r.data[i] = s.data[i];
    }
    r.len = s.len;
    return r;
}

static s8 s8cuthead(s8 s, size len)
{
    s.data += len;
    s.len  -= len;
    return s;
}

typedef struct {
    s8  remain;
    i32 rune;
} utf8result;

static utf8result utf8decode(s8 s)
{
    utf8result r = {0};
    switch (s.data[0]&0xf0) {
    default  : r.rune = s.data[0];
               if (r.rune > 0x7f) break;
               r.remain = s8cuthead(s, 1);
               return r;
    case 0xc0:
    case 0xd0: if (s.len < 2) break;
               if ((s.data[1]&0xc0) != 0x80) break;
               r.rune = (i32)(s.data[0]&0x1f) << 6 |
                        (i32)(s.data[1]&0x3f) << 0;
               if (r.rune < 0x80) break;
               r.remain = s8cuthead(s, 2);
               return r;
    case 0xe0: if (s.len < 3) break;
               if ((s.data[1]&0xc0) != 0x80) break;
               if ((s.data[2]&0xc0) != 0x80) break;
               r.rune = (i32)(s.data[0]&0x0f) << 12 |
                        (i32)(s.data[1]&0x3f) <<  6 |
                        (i32)(s.data[2]&0x3f) <<  0;
               if (r.rune < 0x800) break;
               if (r.rune>=0xd800 && r.rune<=0xdfff) break;
               r.remain = s8cuthead(s, 3);
               return r;
    case 0xf0: if (s.len < 4) break;
               if ((s.data[1]&0xc0) != 0x80) break;
               if ((s.data[2]&0xc0) != 0x80) break;
               if ((s.data[3]&0xc0) != 0x80) break;
               r.rune = (i32)(s.data[0]&0x0f) << 18 |
                        (i32)(s.data[1]&0x3f) << 12 |
                        (i32)(s.data[2]&0x3f) <<  6 |
                        (i32)(s.data[3]&0x3f) <<  0;
               if (r.rune < 0x10000) break;
               if (r.rune > 0x10ffff) break;
               r.remain = s8cuthead(s, 4);
               return r;
    }
    r.rune = 0xfffd;  // Replacement Character
    r.remain = s8cuthead(s, 1);
    return r;
}

// Encode code point returning the output length (1-2).
static i32 utf16encode(u16 *dst, i32 rune)
{
    if (rune >= 0x10000) {
        rune -= 0x10000;
        dst[0] = (u16)((rune >> 10) + 0xd800);
        dst[1] = (u16)((rune&0x3ff) + 0xdc00);
        return 2;
    }
    dst[0] = (u16)rune;
    return 1;
}

typedef struct {
    u8  buf[BUFFER_CAP];
    i32 len;
    i32 off;
    i32 fd;
    b32 eof;
} bufin;

static bufin *newbufin(arena *perm)
{
    bufin *b = new(perm, bufin, 1);
    b->fd = GetStdHandle(-10);
    return b;
}

static u8 readu8(bufin *b)
{
    if (b->eof) {
        return 0;
    } else if (b->off == b->len) {
        ReadFile(b->fd, b->buf, BUFFER_CAP, &b->len, 0);
        if (!b->len) {
            b->eof = 1;
            return 0;
        }
        b->off = 0;
    }
    return b->buf[b->off++];
}

typedef struct {
    u8   buf[BUFFER_CAP];
    i32  len;
    i32  fd;
    b32  err;
    u16 *convert;  // scratch space for WriteConsoleW
} bufout;

static bufout *newbufout(arena *perm)
{
    bufout *b = new(perm, bufout, 1);
    b->fd = GetStdHandle(-11);
    i32 dummy;
    if (GetConsoleMode(b->fd, &dummy)) {
        b->convert = new(perm, u16, CONVERT_CAP);
    }
    return b;
}

static void flushconsole(bufout *b)
{
    u16 *convert = b->convert;
    utf8result r = {0};
    r.remain.data = b->buf;
    r.remain.len = b->len;
    for (i32 len = 0; !b->err && r.remain.len;) {
        r = utf8decode(r.remain);
        len += utf16encode(convert+len, r.rune);
        if (len>=CONVERT_CAP-1 || !r.remain.len) {
            i32 dummy;
            b->err = !WriteConsoleW(b->fd, convert, len, &dummy, 0);
            len = 0;
        }
    }
    b->len = 0;
}

static void flush(bufout *b)
{
    if (!b->err && b->len) {
        if (b->convert) {
            flushconsole(b);
        } else {
            i32 dummy;
            b->err = !WriteFile(b->fd, b->buf, b->len, &dummy, 0);
            b->len = 0;
        }
    }
}

static void writes8(bufout *b, s8 s)
{
    for (size off = 0; !b->err && off<s.len;) {
        i32 avail = BUFFER_CAP - b->len;
        i32 count = s.len-off<avail ? (i32)(s.len-off) : avail;
        u8 *dst = b->buf + b->len;
        for (i32 i = 0; i < count; i++) {
            dst[i] = s.data[off+i];
        }
        off += count;
        b->len += count;
        if (b->len == BUFFER_CAP) {
            flush(b);
        }
    }
}

static void writeu8(bufout *b, u8 c)
{
    s8 s = {0};
    s.data = &c;
    s.len = 1;
    writes8(b, s);
}

static b32 ident(u8 c)
{
    // matches $0-9?@A-Z_a-z and 0x80-0xff
    static const u32 t[] = {
        0x00000000, 0x83ff0010, 0x87ffffff, 0x07fffffe,
        0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
    };
    return t[c>>5] & (u32)1<<(c&31);
}

typedef struct map map;
struct map {
    map *child[4];
    s8   input;
    s8   output;
};

static map *upsert(map **m, s8 input, arena *perm)
{
    for (u64 h = s8hash(input); *m; h <<= 2) {
        if (s8equals(input, (*m)->input)) {
            return *m;
        }
        m = &(*m)->child[h>>62];
    }
    input = s8dup(input, perm);
    *m = input.data ? new(perm, map, 1) : 0;
    if (*m) {
        (*m)->input = input;
    }
    return *m;
}

// Caches UnDecorateSymbolName calls. Typically ~90% of the symbols are
// repeats. A hash map lookup is significantly faster than undecorating
// through this API, so this measurably improves performance. The cache
// operates correctly when out of memory, just with less caching.
typedef struct {
    map   *seen;
    arena *perm;
    u8    *temp;
    i32    flags;
    i32    hits;    // for debugging / tuning
    i32    misses;  // "
} callcache;

static callcache *newcache(i32 flags, arena *perm)
{
    callcache *c = new(perm, callcache, 1);
    c->perm = perm;
    c->temp = new(perm, u8, SYMBOL_MAX);
    c->flags = flags;
    return c;
}

static s8 undecorate(callcache *c, s8 sym)
{
    map *m = upsert(&c->seen, sym, c->perm);
    if (m && m->output.data) {
        c->hits++;
        return m->output;
    }

    c->misses++;
    s8 r = {0};
    r.data = c->temp;
    r.len = UnDecorateSymbolName(sym.data, c->temp, SYMBOL_MAX, c->flags);
    if (m) {
        m->output = s8dup(r, c->perm);
    }
    return r;
}

static b32 usage(i32 fd)
{
    static const u8 usage[] =
    "usage: vc++filt [-_ahnp] [symbols...] <input\n"
    "  -_     32-bit mode\n"
    "  -a     print access specifiers\n"
    "  -h     print usage message\n"
    "  -n     64-bit mode\n"
    "  -p     do not print parameters\n";
    uptr h = GetStdHandle(-10 - fd);
    i32 dummy;
    return WriteFile(h, (u8 *)usage, countof(usage)-1, &dummy, 0);
}

typedef enum {
    status_OK,
    status_EXIT,
    status_FAIL,
} status;

typedef struct {
    i32 flags;
    i32 index;
    i32 status;
} parsedargs;

static parsedargs parseargs(i32 argc, u16 **argv)
{
    enum {
        UNDNAME_NO_ACCESS_SPECIFIERS = 0x0080,
        UNDNAME_32_BIT_DECODE        = 0x0800,
        UNDNAME_NAME_ONLY            = 0x1000,
    };

    parsedargs r = {0};
    // On Windows, c++filt -_/-n is, in practice, a 32-/64-bit toggle
    r.flags |= sizeof(void *)==4 ? UNDNAME_32_BIT_DECODE : 0;
    r.flags |= UNDNAME_NO_ACCESS_SPECIFIERS;

    for (r.index++; r.index < argc; r.index++) {
        u16 *arg = argv[r.index];
        if (arg[0] != '-') {
            return r;   // positional argument
        } else if (arg[1]=='-' && !arg[2]) {
            r.index++;  // discard "--"
            return r;
        }
        for (arg++; *arg; arg++) {
            switch (*arg) {
            case 'a':
                r.flags &= ~UNDNAME_NO_ACCESS_SPECIFIERS;
                break;
            case 'h':
                r.status = usage(1) ? status_EXIT : status_FAIL;
                return r;
            case '_':
                r.flags |= UNDNAME_32_BIT_DECODE;
                break;
            case 'n':
                r.flags &= ~UNDNAME_32_BIT_DECODE;
                break;
            case 'p':
                r.flags |= UNDNAME_NAME_ONLY;
                break;
            default:
                usage(2);
                r.status = status_FAIL;
                return r;
            }
        }
    }
    return r;
}

static i32 run(arena scratch)
{
    bufout *stdout = newbufout(&scratch);

    s8 sym = {0};
    sym.data = new(&scratch, u8, SYMBOL_MAX);
    b32 sym_at = 0;     // current symbol contains '@'?
    b32 sym_start = 1;  // current byte starts an identifier?

    u16 *cmdline = GetCommandLineW();
    i32 argc;
    u16 **argv = CommandLineToArgvW(cmdline, &argc);

    parsedargs args = parseargs(argc, argv);
    switch (args.status) {
    case status_EXIT: return 0;
    case status_FAIL: return 1;
    }

    callcache *cache = newcache(args.flags, &scratch);

    for (i32 i = args.index; i < argc; i++) {
        sym.len = WideCharToMultiByte(
            65001, 0, argv[i], -1, sym.data, SYMBOL_MAX, 0, 0
        );
        s8 out = undecorate(cache, sym);
        writes8(stdout, out);
        writeu8(stdout, '\n');
    }
    if (argc > args.index) {
        // Do not process standard input
        flush(stdout);
        return stdout->err;
    }

    bufin *stdin = newbufin(&scratch);
    for (u8 c = readu8(stdin); !stdin->eof; c = readu8(stdin)) {
        if (sym_start && c=='?') {
            // Start of a new identifier?
            sym = s8push(sym, c);

        } else if (sym.len) {
            if (ident(c)) {
                // Continue the current identifier
                sym = s8push(sym, c);
                sym_at |= c=='@';
                if (sym.len == SYMBOL_MAX) {
                    // Too long, give up on this one
                    writes8(stdout, sym);
                    sym.len = sym_at = 0;
                }

            } else if (sym_at) {
                // Contains at least one @? Try to decode it.
                sym = s8push(sym, 0);
                s8 out = undecorate(cache, sym);
                writes8(stdout, out);
                writeu8(stdout, c);
                sym.len = sym_at = 0;

            } else {
                // Don't try to decode it, just pass it through
                writes8(stdout, sym);
                writeu8(stdout, c);
                sym.len = sym_at = 0;
            }

        } else {
            // Pass it through
            writeu8(stdout, c);
        }

        sym_start = !ident(c);
    }

    if (sym_at) {
        sym = s8push(sym, 0);
        s8 out = undecorate(cache, sym);
        writes8(stdout, out);
    } else {
        writes8(stdout, sym);
    }

    flush(stdout);
    return stdout->err;
}

void mainCRTStartup(void)
{
    arena scratch = {0};
    scratch.beg = VirtualAlloc(0, MEMORY_CAP, 0x3000, 4);
    scratch.end = scratch.beg + MEMORY_CAP;
    i32 r = run(scratch);
    ExitProcess(r);
}
