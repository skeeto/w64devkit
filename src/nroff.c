// nroff: format manual pages for terminals (man, mdoc, tbl)
// Chris Wellons <wellons@nullprogram.com>
//
// w64dk: $ cc -nostartfiles -O2 -s -o nroff.exe nroff.c -lmemory
// unix:  $ cc               -O2 -s -o nroff     nroff.c
// tests: $ cc -DTEST -g3 -fsanitize=undefined -fsanitize-trap nroff.c
// fuzz:  $ afl-gcc-fast -g3 -fsanitize=undefined -fsanitize-trap nroff.c
//
// Formats man(7) and mdoc(7) manual pages, including tbl(1) tables, for
// display on a terminal, as invoked by man(1). It implements the subset
// of roff that real manual pages use, including the macro definitions,
// strings, registers, and conditionals in preambles written by pod2man,
// docutils, Sphinx, and asciidoctor. Output tracks groff's terminal
// layout closely. Unsupported requests and escapes are ignored, and -w
// reports them.
//
// Emphasis uses SGR escapes: bold is bold, italic is underline. Each
// output line closes its own SGR so that a pager may display any subset
// of lines. GROFF_NO_SGR disables emphasis.
//
// This is free and unencumbered software released into the public domain.
#include <stddef.h>
#include <stdint.h>

typedef int8_t      i8;
typedef uint8_t     u8;
typedef int32_t     b32;
typedef int32_t     i32;
typedef uint32_t    u32;
typedef int64_t     i64;
typedef uint64_t    u64;
typedef char        byte;
typedef ptrdiff_t   iz;
typedef size_t      uz;

#define countof(a)      (iz)(sizeof(a) / sizeof(*(a)))
#define affirm(c)       while (!(c)) __builtin_unreachable()
#define new(a, n, t)    (t *)alloc(a, n, sizeof(t), _Alignof(t))
#define S(s)            (Str){(u8 *)s, sizeof(s)-1}
#define STR(s)          {(u8 *)s, sizeof(s)-1}
#define maxof(t)        ((t)-1<1 ? (((t)1<<(sizeof(t)*8-2))-1)*2+1 : (t)-1)
#define xset(d, c, n)   __builtin_memset(d, c, n)
#define xcpy(d, s, n)   __builtin_memcpy(d, s, n)

typedef struct {
    u8 *data;
    iz  len;
} Str;

typedef struct Plt Plt;

typedef struct {
    Plt  *plt;
    byte *beg;
    byte *end;
} Arena;


// Platform API

// Read an entire file (null path: standard input). Null data on error.
static Str  plt_load(Plt *, Str path, Arena *);
// Write to standard output (1) or standard error (2), like write(2).
static b32  plt_write(Plt *, i32 fd, u8 *, i32);
// Value of an environment variable, or null if unset.
static Str  plt_getenv(Plt *, Str name, Arena *);
// May standard output carry SGR escapes? False on legacy consoles.
static b32  plt_sgr(Plt *);
// Print "nroff: out of memory" and exit. Does not return.
static void plt_oom(Plt *);
// Application entry point, returns the exit status.
static i32  nroff(i32 argc, u8 **argv, Plt *, Arena);


// Basics

static uz touz(iz n)
{
    affirm(n >= 0);
    return (uz)n;
}

static i32 clampi32(i64 x)
{
    i32 max = maxof(i32);
    return x>max ? max : x<-max-1 ? -max-1 : (i32)x;
}

static void *alloc(Arena *a, iz count, iz size, iz align)
{
    iz pad = (iz)-(uz)a->beg & (align - 1);
    if (count >= (a->end - a->beg - pad)/size) {
        plt_oom(a->plt);
    }
    byte *r = a->beg + pad;
    a->beg += pad + count*size;
    return xset(r, 0, touz(count*size));
}

// Allocate from the high end, released in LIFO order by restoring end.
static void *allochigh(Arena *a, iz count, iz size, iz align)
{
    iz pad = (iz)((uz)a->end & (uz)(align - 1));
    if (count >= (a->end - a->beg - pad)/size) {
        plt_oom(a->plt);
    }
    a->end -= pad + count*size;
    return xset(a->end, 0, touz(count*size));
}

static Str span(u8 *beg, u8 *end)
{
    affirm(beg <= end);
    return (Str){beg, end-beg};
}

static Str import(u8 *s)
{
    Str r = {0};
    r.data = s;
    for (; r.data[r.len]; r.len++) {}
    return r;
}

static b32 equals(Str a, Str b)
{
    return a.len==b.len && (!a.len || !__builtin_memcmp(a.data, b.data, touz(a.len)));
}

static Str cuthead(Str s, iz len)
{
    affirm(len >= 0 && len <= s.len);
    if (!len) return s;
    s.data += len;
    s.len  -= len;
    return s;
}

static Str takehead(Str s, iz len)
{
    affirm(len >= 0 && len <= s.len);
    s.len = len;
    return s;
}

static b32 startswith(Str s, Str prefix)
{
    return s.len>=prefix.len && equals(takehead(s, prefix.len), prefix);
}

static Str clone(Arena *a, Str s)
{
    Str r = s;
    r.data = new(a, s.len, u8);
    if (r.len) xcpy(r.data, s.data, touz(r.len));
    return r;
}

// Extends head in place when it sits at the front of the arena.
static Str concat(Arena *a, Str head, Str tail)
{
    if (!head.data || (byte *)(head.data+head.len) != a->beg) {
        head = clone(a, head);
    }
    head.len += clone(a, tail).len;
    return head;
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
    for (; cut<end && *cut!=c; cut++) {}
    Cut r  = {0};
    r.ok   = cut < end;
    r.head = span(beg, cut);
    r.tail = span(cut+r.ok, end);
    return r;
}

static b32 whitespace(u8 c)
{
    return c==' ' || c=='\t';
}

static b32 digit(u8 c)
{
    return c>='0' && c<='9';
}

static Str trimleft(Str s)
{
    iz i = 0;
    for (; i<s.len && whitespace(s.data[i]); i++) {}
    return cuthead(s, i);
}

static Str trimright(Str s)
{
    for (; s.len && whitespace(s.data[s.len-1]); s.len--) {}
    return s;
}

static Str trim(Str s)
{
    return trimright(trimleft(s));
}

static u64 hash64(Str s)
{
    u64 h = 0x100;
    for (iz i = 0; i < s.len; i++) {
        h ^= s.data[i];
        h *= 1111111111111111111u;
    }
    return h;
}

// Growable byte buffer, extended in place at the front of its arena.
typedef struct {
    u8 *data;
    iz  len;
    iz  cap;
} Buf;

static void bappend(Buf *b, Str s, Arena *a)
{
    if (s.len > b->cap - b->len) {
        iz cap = b->cap ? b->cap : 64;
        for (; cap-b->len < s.len; cap *= 2) {}
        if (b->data && (byte *)(b->data+b->cap)==a->beg && cap-b->cap<a->end-a->beg) {
            alloc(a, cap-b->cap, 1, 1);
        } else {
            u8 *data = new(a, cap, u8);
            if (b->len) xcpy(data, b->data, touz(b->len));
            b->data = data;
        }
        b->cap = cap;
    }
    if (s.len) xcpy(b->data+b->len, s.data, touz(s.len));
    b->len += s.len;
}

static void bappendu8(Buf *b, u8 c, Arena *a)
{
    bappend(b, (Str){&c, 1}, a);
}

static Str bstr(Buf b)
{
    return (Str){b.data, b.len};
}

// Append a zeroed element to a slice (data, len, cap), extended in
// place at the front of its arena.
#define push(a, s) \
  ((s)->len == (s)->cap \
    ? (s)->data = push_((a), (s)->data, &(s)->cap, \
          sizeof(*(s)->data), _Alignof(__typeof__(*(s)->data))), \
      (s)->data + (s)->len++ \
    : (s)->data + (s)->len++)

static void *push_(Arena *a, void *data, iz *pcap, iz size, iz align)
{
    iz cap = *pcap;
    if (!data || a->beg != (byte *)data + cap*size) {
        void *copy = alloc(a, cap, size, align);
        if (data) xcpy(copy, data, touz(cap*size));
        data = copy;
    }
    iz extend = cap ? cap : 4;
    alloc(a, extend, size, 1);  // already aligned
    *pcap = cap + extend;
    return data;
}

typedef struct {
    Str *data;
    iz   len;
    iz   cap;
} Strs;

typedef struct {
    b32 *data;
    iz   len;
    iz   cap;
} Flags;

static Str fmti64(Arena *a, i64 x)
{
    u8  buf[32];
    u8 *end = buf + countof(buf);
    u8 *beg = end;
    i64 t   = x<0 ? x : -x;
    do {
        *--beg = (u8)('0' - t%10);
    } while (t /= 10);
    if (x < 0) {
        *--beg = '-';
    }
    return clone(a, span(beg, end));
}

typedef struct {
    i32 cp;
    i32 len;
} Rune;

// Decode the first code point, or U+FFFD for an invalid byte.
static Rune decode(Str s)
{
    Rune r = {0xfffd, 1};
    if (!s.len) {
        r.len = 0;
        return r;
    }
    u8 c = s.data[0];
    if (c < 0x80) {
        r.cp = c;
        return r;
    }
    i32 n = 0, cp = 0, min = 0;
    if ((c&0xe0) == 0xc0) {
        n = 2; cp = c&0x1f; min = 0x80;
    } else if ((c&0xf0) == 0xe0) {
        n = 3; cp = c&0x0f; min = 0x800;
    } else if ((c&0xf8) == 0xf0) {
        n = 4; cp = c&0x07; min = 0x10000;
    } else {
        return r;
    }
    if (s.len < n) {
        return r;
    }
    for (i32 i = 1; i < n; i++) {
        if ((s.data[i]&0xc0) != 0x80) {
            return r;
        }
        cp = cp<<6 | (s.data[i]&0x3f);
    }
    if (cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) {
        return r;
    }
    r.cp  = cp;
    r.len = n;
    return r;
}

static Str encode(Arena *a, i32 cp)
{
    u8 b[4];
    i32 n = 0;
    if (cp<0 || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) {
        cp = 0xfffd;
    }
    if (cp < 0x80) {
        b[n++] = (u8)cp;
    } else if (cp < 0x800) {
        b[n++] = (u8)(0xc0 | cp>>6);
        b[n++] = (u8)(0x80 | (cp&0x3f));
    } else if (cp < 0x10000) {
        b[n++] = (u8)(0xe0 | cp>>12);
        b[n++] = (u8)(0x80 | (cp>>6&0x3f));
        b[n++] = (u8)(0x80 | (cp&0x3f));
    } else {
        b[n++] = (u8)(0xf0 | cp>>18);
        b[n++] = (u8)(0x80 | (cp>>12&0x3f));
        b[n++] = (u8)(0x80 | (cp>>6&0x3f));
        b[n++] = (u8)(0x80 | (cp&0x3f));
    }
    return clone(a, (Str){b, n});
}

// Display columns occupied by a code point.
static i32 runewidth(i32 cp)
{
    if ((cp>=0x300 && cp<=0x36f) || (cp>=0x200b && cp<=0x200f) ||
        (cp>=0xfe00 && cp<=0xfe0f)) {
        return 0;
    }
    if ((cp>=0x1100 && cp<=0x115f) || (cp>=0x2e80 && cp<=0xa4cf) ||
        (cp>=0xac00 && cp<=0xd7a3) || (cp>=0xf900 && cp<=0xfaff) ||
        (cp>=0xfe30 && cp<=0xfe4f) || (cp>=0xff00 && cp<=0xff60) ||
        (cp>=0xffe0 && cp<=0xffe6) || (cp>=0x1f300 && cp<=0x1f64f) ||
        (cp>=0x1f900 && cp<=0x1f9ff) || (cp>=0x20000 && cp<=0x3fffd)) {
        return 2;
    }
    return 1;
}

static i32 strwidth(Str s)
{
    i32 w = 0;
    while (s.len) {
        Rune r = decode(s);
        w += runewidth(r.cp);
        s = cuthead(s, r.len);
    }
    return w;
}


// Hash trie mapping names to definitions

typedef struct Entry Entry;
struct Entry {
    Entry *child[4];
    Str    key;
    Str    str;     // string, macro body, or glyph
    Str    alt;     // ASCII glyph
    i32    num;     // register value, built-in kind
    i32    inc;     // register auto-increment, mdoc callable flag
    b32    live;
};

// Find the entry for a key, inserting if the arena is non-null.
static Entry *lookup(Entry **m, Str key, Arena *perm)
{
    for (u64 h = hash64(key); *m; h <<= 2) {
        if (equals(key, (*m)->key)) {
            return *m;
        }
        m = &(*m)->child[h>>62];
    }
    if (!perm) {
        return 0;
    }
    *m = new(perm, 1, Entry);
    (*m)->key = clone(perm, key);
    return *m;
}

static Entry *find(Entry *m, Str key)
{
    Entry *e = lookup(&m, key, 0);
    return e && e->live ? e : 0;
}


// Buffered output

typedef struct {
    Plt *plt;
    i32  len;
    i32  fd;
    b32  err;
    u8   buf[1<<14];
} Output;

static Output *newoutput(Arena *a, Plt *plt, i32 fd)
{
    Output *b = new(a, 1, Output);
    b->plt = plt;
    b->fd  = fd;
    return b;
}

static void flush(Output *b)
{
    if (!b->err && b->len) {
        b->err = !plt_write(b->plt, b->fd, b->buf, b->len);
    }
    b->len = 0;
}

static void print(Output *b, Str s)
{
    for (iz off = 0; !b->err && off<s.len;) {
        i32 avail = (i32)countof(b->buf) - b->len;
        i32 count = s.len-off < avail ? (i32)(s.len-off) : avail;
        xcpy(b->buf+b->len, s.data+off, touz(count));
        off    += count;
        b->len += count;
        if (b->len == countof(b->buf)) {
            flush(b);
        }
    }
}

static void printu8(Output *b, u8 c)
{
    print(b, (Str){&c, 1});
}

static void printi64(Output *b, i64 x)
{
    u8  buf[32];
    u8 *end = buf + countof(buf);
    u8 *beg = end;
    i64 t   = x<0 ? x : -x;
    do {
        *--beg = (u8)('0' - t%10);
    } while (t /= 10);
    if (x < 0) {
        *--beg = '-';
    }
    print(b, span(beg, end));
}


// Special characters: name, UTF-8, ASCII fallback

static struct {
    Str name;
    Str utf8;
    Str ascii;
} glyphs[] = {
    {STR("aq"), STR("'"), STR("'")},
    {STR("dq"), STR("\""), STR("\"")},
    {STR("lq"), STR("\xe2\x80\x9c"), STR("\"")},
    {STR("rq"), STR("\xe2\x80\x9d"), STR("\"")},
    {STR("oq"), STR("\xe2\x80\x98"), STR("`")},
    {STR("cq"), STR("\xe2\x80\x99"), STR("'")},
    {STR("Bq"), STR("\xe2\x80\x9e"), STR(",,")},
    {STR("bq"), STR("\xe2\x80\x9a"), STR(",")},
    {STR("Fo"), STR("\xc2\xab"), STR("<<")},
    {STR("Fc"), STR("\xc2\xbb"), STR(">>")},
    {STR("fo"), STR("\xe2\x80\xb9"), STR("<")},
    {STR("fc"), STR("\xe2\x80\xba"), STR(">")},
    {STR("r!"), STR("\xc2\xa1"), STR("!")},
    {STR("r?"), STR("\xc2\xbf"), STR("?")},
    {STR("em"), STR("\xe2\x80\x94"), STR("--")},
    {STR("en"), STR("\xe2\x80\x93"), STR("-")},
    {STR("hy"), STR("\xe2\x80\x90"), STR("-")},
    {STR("mi"), STR("\xe2\x88\x92"), STR("-")},
    {STR("pl"), STR("+"), STR("+")},
    {STR("eq"), STR("="), STR("=")},
    {STR("bu"), STR("\xe2\x80\xa2"), STR("o")},
    {STR("ci"), STR("\xe2\x97\x8b"), STR("O")},
    {STR("sq"), STR("\xe2\x96\xa1"), STR("[]")},
    {STR("lz"), STR("\xe2\x97\x8a"), STR("<>")},
    {STR("co"), STR("\xc2\xa9"), STR("(C)")},
    {STR("rg"), STR("\xc2\xae"), STR("(R)")},
    {STR("tm"), STR("\xe2\x84\xa2"), STR("tm")},
    {STR("de"), STR("\xc2\xb0"), STR("o")},
    {STR("dg"), STR("\xe2\x80\xa0"), STR("+")},
    {STR("dd"), STR("\xe2\x80\xa1"), STR("++")},
    {STR("ps"), STR("\xc2\xb6"), STR("P")},
    {STR("sc"), STR("\xc2\xa7"), STR("S")},
    {STR("ct"), STR("\xc2\xa2"), STR("c")},
    {STR("Po"), STR("\xc2\xa3"), STR("L")},
    {STR("Ye"), STR("\xc2\xa5"), STR("Y")},
    {STR("Eu"), STR("\xe2\x82\xac"), STR("EUR")},
    {STR("eu"), STR("\xe2\x82\xac"), STR("EUR")},
    {STR("Cs"), STR("\xc2\xa4"), STR("o")},
    {STR("Do"), STR("$"), STR("$")},
    {STR("at"), STR("@"), STR("@")},
    {STR("sh"), STR("#"), STR("#")},
    {STR("rs"), STR("\\"), STR("\\")},
    {STR("sl"), STR("/"), STR("/")},
    {STR("ha"), STR("^"), STR("^")},
    {STR("ti"), STR("~"), STR("~")},
    {STR("ga"), STR("`"), STR("`")},
    {STR("aa"), STR("\xc2\xb4"), STR("'")},
    {STR("ul"), STR("_"), STR("_")},
    {STR("ru"), STR("_"), STR("_")},
    {STR("rn"), STR("\xe2\x80\xbe"), STR("-")},
    {STR("bv"), STR("|"), STR("|")},
    {STR("ba"), STR("|"), STR("|")},
    {STR("br"), STR("\xe2\x94\x82"), STR("|")},
    {STR("or"), STR("|"), STR("|")},
    {STR("lB"), STR("["), STR("[")},
    {STR("rB"), STR("]"), STR("]")},
    {STR("lC"), STR("{"), STR("{")},
    {STR("rC"), STR("}"), STR("}")},
    {STR("la"), STR("\xe2\x9f\xa8"), STR("<")},
    {STR("ra"), STR("\xe2\x9f\xa9"), STR(">")},
    {STR("fm"), STR("\xe2\x80\xb2"), STR("'")},
    {STR("sd"), STR("\xe2\x80\xb3"), STR("''")},
    {STR("%0"), STR("\xe2\x80\xb0"), STR("%o")},
    {STR("Fn"), STR("\xc6\x92"), STR("f")},
    {STR("ss"), STR("\xc3\x9f"), STR("ss")},
    {STR("AE"), STR("\xc3\x86"), STR("AE")},
    {STR("ae"), STR("\xc3\xa6"), STR("ae")},
    {STR("OE"), STR("\xc5\x92"), STR("OE")},
    {STR("oe"), STR("\xc5\x93"), STR("oe")},
    {STR("/O"), STR("\xc3\x98"), STR("O")},
    {STR("/o"), STR("\xc3\xb8"), STR("o")},
    {STR("/L"), STR("\xc5\x81"), STR("L")},
    {STR("/l"), STR("\xc5\x82"), STR("l")},
    {STR("-D"), STR("\xc3\x90"), STR("D")},
    {STR("Sd"), STR("\xc3\xb0"), STR("d")},
    {STR("TP"), STR("\xc3\x9e"), STR("TH")},
    {STR("Tp"), STR("\xc3\xbe"), STR("th")},
    {STR("IJ"), STR("\xc4\xb2"), STR("IJ")},
    {STR("ij"), STR("\xc4\xb3"), STR("ij")},
    {STR(".i"), STR("\xc4\xb1"), STR("i")},
    {STR(".j"), STR("\xc8\xb7"), STR("j")},
    {STR("'A"), STR("\xc3\x81"), STR("A")},
    {STR("'E"), STR("\xc3\x89"), STR("E")},
    {STR("'I"), STR("\xc3\x8d"), STR("I")},
    {STR("'O"), STR("\xc3\x93"), STR("O")},
    {STR("'U"), STR("\xc3\x9a"), STR("U")},
    {STR("'Y"), STR("\xc3\x9d"), STR("Y")},
    {STR("'a"), STR("\xc3\xa1"), STR("a")},
    {STR("'e"), STR("\xc3\xa9"), STR("e")},
    {STR("'i"), STR("\xc3\xad"), STR("i")},
    {STR("'o"), STR("\xc3\xb3"), STR("o")},
    {STR("'u"), STR("\xc3\xba"), STR("u")},
    {STR("'y"), STR("\xc3\xbd"), STR("y")},
    {STR("'C"), STR("\xc4\x86"), STR("C")},
    {STR("'c"), STR("\xc4\x87"), STR("c")},
    {STR("`A"), STR("\xc3\x80"), STR("A")},
    {STR("`E"), STR("\xc3\x88"), STR("E")},
    {STR("`I"), STR("\xc3\x8c"), STR("I")},
    {STR("`O"), STR("\xc3\x92"), STR("O")},
    {STR("`U"), STR("\xc3\x99"), STR("U")},
    {STR("`a"), STR("\xc3\xa0"), STR("a")},
    {STR("`e"), STR("\xc3\xa8"), STR("e")},
    {STR("`i"), STR("\xc3\xac"), STR("i")},
    {STR("`o"), STR("\xc3\xb2"), STR("o")},
    {STR("`u"), STR("\xc3\xb9"), STR("u")},
    {STR("^A"), STR("\xc3\x82"), STR("A")},
    {STR("^E"), STR("\xc3\x8a"), STR("E")},
    {STR("^I"), STR("\xc3\x8e"), STR("I")},
    {STR("^O"), STR("\xc3\x94"), STR("O")},
    {STR("^U"), STR("\xc3\x9b"), STR("U")},
    {STR("^a"), STR("\xc3\xa2"), STR("a")},
    {STR("^e"), STR("\xc3\xaa"), STR("e")},
    {STR("^i"), STR("\xc3\xae"), STR("i")},
    {STR("^o"), STR("\xc3\xb4"), STR("o")},
    {STR("^u"), STR("\xc3\xbb"), STR("u")},
    {STR(":A"), STR("\xc3\x84"), STR("A")},
    {STR(":E"), STR("\xc3\x8b"), STR("E")},
    {STR(":I"), STR("\xc3\x8f"), STR("I")},
    {STR(":O"), STR("\xc3\x96"), STR("O")},
    {STR(":U"), STR("\xc3\x9c"), STR("U")},
    {STR(":Y"), STR("\xc5\xb8"), STR("Y")},
    {STR(":a"), STR("\xc3\xa4"), STR("a")},
    {STR(":e"), STR("\xc3\xab"), STR("e")},
    {STR(":i"), STR("\xc3\xaf"), STR("i")},
    {STR(":o"), STR("\xc3\xb6"), STR("o")},
    {STR(":u"), STR("\xc3\xbc"), STR("u")},
    {STR(":y"), STR("\xc3\xbf"), STR("y")},
    {STR("~A"), STR("\xc3\x83"), STR("A")},
    {STR("~N"), STR("\xc3\x91"), STR("N")},
    {STR("~O"), STR("\xc3\x95"), STR("O")},
    {STR("~a"), STR("\xc3\xa3"), STR("a")},
    {STR("~n"), STR("\xc3\xb1"), STR("n")},
    {STR("~o"), STR("\xc3\xb5"), STR("o")},
    {STR(",C"), STR("\xc3\x87"), STR("C")},
    {STR(",c"), STR("\xc3\xa7"), STR("c")},
    {STR("oA"), STR("\xc3\x85"), STR("A")},
    {STR("oa"), STR("\xc3\xa5"), STR("a")},
    {STR("vS"), STR("\xc5\xa0"), STR("S")},
    {STR("vs"), STR("\xc5\xa1"), STR("s")},
    {STR("vZ"), STR("\xc5\xbd"), STR("Z")},
    {STR("vz"), STR("\xc5\xbe"), STR("z")},
    {STR("a-"), STR("\xc2\xaf"), STR("-")},
    {STR("a."), STR("\xcb\x99"), STR(".")},
    {STR("a^"), STR("^"), STR("^")},
    {STR("ab"), STR("\xcb\x98"), STR("u")},
    {STR("ac"), STR("\xc2\xb8"), STR(",")},
    {STR("ad"), STR("\xc2\xa8"), STR("\"")},
    {STR("ah"), STR("\xcb\x87"), STR("v")},
    {STR("ao"), STR("\xcb\x9a"), STR("o")},
    {STR("a~"), STR("~"), STR("~")},
    {STR("ho"), STR("\xcb\x9b"), STR(",")},
    {STR("a\""), STR("\xcb\x9d"), STR("\"")},
    {STR("<-"), STR("\xe2\x86\x90"), STR("<-")},
    {STR("->"), STR("\xe2\x86\x92"), STR("->")},
    {STR("<>"), STR("\xe2\x86\x94"), STR("<->")},
    {STR("da"), STR("\xe2\x86\x93"), STR("v")},
    {STR("ua"), STR("\xe2\x86\x91"), STR("^")},
    {STR("va"), STR("\xe2\x86\x95"), STR("|")},
    {STR("lA"), STR("\xe2\x87\x90"), STR("<=")},
    {STR("rA"), STR("\xe2\x87\x92"), STR("=>")},
    {STR("hA"), STR("\xe2\x87\x94"), STR("<=>")},
    {STR("dA"), STR("\xe2\x87\x93"), STR("v")},
    {STR("uA"), STR("\xe2\x87\x91"), STR("^")},
    {STR("vA"), STR("\xe2\x87\x95"), STR("|")},
    {STR("an"), STR("\xe2\x8e\xaf"), STR("-")},
    {STR("-+"), STR("\xe2\x88\x93"), STR("-+")},
    {STR("+-"), STR("\xc2\xb1"), STR("+-")},
    {STR("pc"), STR("\xc2\xb7"), STR(".")},
    {STR("md"), STR("\xe2\x8b\x85"), STR(".")},
    {STR("mu"), STR("\xc3\x97"), STR("x")},
    {STR("di"), STR("\xc3\xb7"), STR("/")},
    {STR("f/"), STR("\xe2\x81\x84"), STR("/")},
    {STR("**"), STR("\xe2\x88\x97"), STR("*")},
    {STR("<="), STR("\xe2\x89\xa4"), STR("<=")},
    {STR(">="), STR("\xe2\x89\xa5"), STR(">=")},
    {STR("<<"), STR("\xe2\x89\xaa"), STR("<<")},
    {STR(">>"), STR("\xe2\x89\xab"), STR(">>")},
    {STR("!="), STR("\xe2\x89\xa0"), STR("!=")},
    {STR("=="), STR("\xe2\x89\xa1"), STR("==")},
    {STR("ne"), STR("\xe2\x89\xa2"), STR("!==")},
    {STR("=~"), STR("\xe2\x89\x85"), STR("=~")},
    {STR("|="), STR("\xe2\x89\x83"), STR("-~")},
    {STR("ap"), STR("\xe2\x88\xbc"), STR("~")},
    {STR("~~"), STR("\xe2\x89\x88"), STR("~~")},
    {STR("~="), STR("\xe2\x89\x88"), STR("~=")},
    {STR("pt"), STR("\xe2\x88\x9d"), STR("oc")},
    {STR("es"), STR("\xe2\x88\x85"), STR("{}")},
    {STR("mo"), STR("\xe2\x88\x88"), STR("E")},
    {STR("nm"), STR("\xe2\x88\x89"), STR("!E")},
    {STR("sb"), STR("\xe2\x8a\x82"), STR("(=")},
    {STR("nb"), STR("\xe2\x8a\x84"), STR("(!=")},
    {STR("sp"), STR("\xe2\x8a\x83"), STR("=)")},
    {STR("nc"), STR("\xe2\x8a\x85"), STR("!=)")},
    {STR("ib"), STR("\xe2\x8a\x86"), STR("(=")},
    {STR("ip"), STR("\xe2\x8a\x87"), STR("=)")},
    {STR("ca"), STR("\xe2\x88\xa9"), STR("(^)")},
    {STR("cu"), STR("\xe2\x88\xaa"), STR("U")},
    {STR("/_"), STR("\xe2\x88\xa0"), STR("<")},
    {STR("pp"), STR("\xe2\x8a\xa5"), STR("|")},
    {STR("is"), STR("\xe2\x88\xab"), STR("S")},
    {STR("integral"), STR("\xe2\x88\xab"), STR("S")},
    {STR("sum"), STR("\xe2\x88\x91"), STR("E")},
    {STR("product"), STR("\xe2\x88\x8f"), STR("TT")},
    {STR("gr"), STR("\xe2\x88\x87"), STR("V")},
    {STR("sr"), STR("\xe2\x88\x9a"), STR("sqrt")},
    {STR("sqrt"), STR("\xe2\x88\x9a"), STR("sqrt")},
    {STR("lc"), STR("\xe2\x8c\x88"), STR("|")},
    {STR("rc"), STR("\xe2\x8c\x89"), STR("|")},
    {STR("lf"), STR("\xe2\x8c\x8a"), STR("|")},
    {STR("rf"), STR("\xe2\x8c\x8b"), STR("|")},
    {STR("if"), STR("\xe2\x88\x9e"), STR("oo")},
    {STR("Ah"), STR("\xe2\x84\xb5"), STR("N")},
    {STR("Im"), STR("\xe2\x84\x91"), STR("I")},
    {STR("Re"), STR("\xe2\x84\x9c"), STR("R")},
    {STR("wp"), STR("\xe2\x84\x98"), STR("P")},
    {STR("pd"), STR("\xe2\x88\x82"), STR("d")},
    {STR("-h"), STR("\xe2\x84\x8f"), STR("h")},
    {STR("hbar"), STR("\xe2\x84\x8f"), STR("h")},
    {STR("12"), STR("\xc2\xbd"), STR("1/2")},
    {STR("14"), STR("\xc2\xbc"), STR("1/4")},
    {STR("34"), STR("\xc2\xbe"), STR("3/4")},
    {STR("18"), STR("\xe2\x85\x9b"), STR("1/8")},
    {STR("38"), STR("\xe2\x85\x9c"), STR("3/8")},
    {STR("58"), STR("\xe2\x85\x9d"), STR("5/8")},
    {STR("78"), STR("\xe2\x85\x9e"), STR("7/8")},
    {STR("S1"), STR("\xc2\xb9"), STR("1")},
    {STR("S2"), STR("\xc2\xb2"), STR("2")},
    {STR("S3"), STR("\xc2\xb3"), STR("3")},
    {STR("no"), STR("\xc2\xac"), STR("~")},
    {STR("tno"), STR("\xc2\xac"), STR("~")},
    {STR("AN"), STR("\xe2\x88\xa7"), STR("^")},
    {STR("OR"), STR("\xe2\x88\xa8"), STR("v")},
    {STR("te"), STR("\xe2\x88\x83"), STR("E")},
    {STR("fa"), STR("\xe2\x88\x80"), STR("A")},
    {STR("st"), STR("\xe2\x88\x8b"), STR("-)")},
    {STR("tf"), STR("\xe2\x88\xb4"), STR(".:.")},
    {STR("3d"), STR("\xe2\x88\xb4"), STR(".:.")},
    {STR("mc"), STR("\xc2\xb5"), STR("u")},
    {STR("OK"), STR("\xe2\x9c\x93"), STR("\\/")},
    {STR("CR"), STR("\xe2\x86\xb5"), STR("<cr>")},
    {STR("rh"), STR("\xe2\x98\x9e"), STR("=>")},
    {STR("lh"), STR("\xe2\x98\x9c"), STR("<=")},
    {STR("ff"), STR("\xef\xac\x80"), STR("ff")},
    {STR("fi"), STR("\xef\xac\x81"), STR("fi")},
    {STR("fl"), STR("\xef\xac\x82"), STR("fl")},
    {STR("Fi"), STR("\xef\xac\x83"), STR("ffi")},
    {STR("Fl"), STR("\xef\xac\x84"), STR("ffl")},
    {STR("*A"), STR("\xce\x91"), STR("A")},
    {STR("*B"), STR("\xce\x92"), STR("B")},
    {STR("*G"), STR("\xce\x93"), STR("G")},
    {STR("*D"), STR("\xce\x94"), STR("D")},
    {STR("*E"), STR("\xce\x95"), STR("E")},
    {STR("*Z"), STR("\xce\x96"), STR("Z")},
    {STR("*Y"), STR("\xce\x97"), STR("H")},
    {STR("*H"), STR("\xce\x98"), STR("O")},
    {STR("*I"), STR("\xce\x99"), STR("I")},
    {STR("*K"), STR("\xce\x9a"), STR("K")},
    {STR("*L"), STR("\xce\x9b"), STR("L")},
    {STR("*M"), STR("\xce\x9c"), STR("M")},
    {STR("*N"), STR("\xce\x9d"), STR("N")},
    {STR("*C"), STR("\xce\x9e"), STR("H")},
    {STR("*O"), STR("\xce\x9f"), STR("O")},
    {STR("*P"), STR("\xce\xa0"), STR("TT")},
    {STR("*R"), STR("\xce\xa1"), STR("P")},
    {STR("*S"), STR("\xce\xa3"), STR("S")},
    {STR("*T"), STR("\xce\xa4"), STR("T")},
    {STR("*U"), STR("\xce\xa5"), STR("Y")},
    {STR("*F"), STR("\xce\xa6"), STR("O")},
    {STR("*X"), STR("\xce\xa7"), STR("X")},
    {STR("*Q"), STR("\xce\xa8"), STR("Y")},
    {STR("*W"), STR("\xce\xa9"), STR("O")},
    {STR("*a"), STR("\xce\xb1"), STR("a")},
    {STR("*b"), STR("\xce\xb2"), STR("B")},
    {STR("*g"), STR("\xce\xb3"), STR("y")},
    {STR("*d"), STR("\xce\xb4"), STR("d")},
    {STR("*e"), STR("\xce\xb5"), STR("e")},
    {STR("*z"), STR("\xce\xb6"), STR("C")},
    {STR("*y"), STR("\xce\xb7"), STR("n")},
    {STR("*h"), STR("\xce\xb8"), STR("0")},
    {STR("*i"), STR("\xce\xb9"), STR("i")},
    {STR("*k"), STR("\xce\xba"), STR("k")},
    {STR("*l"), STR("\xce\xbb"), STR("\\")},
    {STR("*m"), STR("\xce\xbc"), STR("u")},
    {STR("*n"), STR("\xce\xbd"), STR("v")},
    {STR("*c"), STR("\xce\xbe"), STR("E")},
    {STR("*o"), STR("\xce\xbf"), STR("o")},
    {STR("*p"), STR("\xcf\x80"), STR("n")},
    {STR("*r"), STR("\xcf\x81"), STR("p")},
    {STR("*s"), STR("\xcf\x83"), STR("o")},
    {STR("ts"), STR("\xcf\x82"), STR("s")},
    {STR("*t"), STR("\xcf\x84"), STR("t")},
    {STR("*u"), STR("\xcf\x85"), STR("u")},
    {STR("*f"), STR("\xcf\x86"), STR("o")},
    {STR("*x"), STR("\xcf\x87"), STR("x")},
    {STR("*q"), STR("\xcf\x88"), STR("u")},
    {STR("*w"), STR("\xcf\x89"), STR("w")},
    {STR("+h"), STR("\xcf\x91"), STR("0")},
    {STR("+f"), STR("\xcf\x95"), STR("o")},
    {STR("+p"), STR("\xcf\x96"), STR("w")},
    {STR("+e"), STR("\xcf\xb5"), STR("e")},
};

// Transliterations of common UTF-8 input that has no glyph name.
static struct {
    Str utf8;
    Str ascii;
} translits[] = {
    {STR("\xc2\xa0"), STR(" ")},
    {STR("\xe2\x80\xa6"), STR("...")},
    {STR("\xe2\x80\x8b"), STR("")},
    {STR("\xe2\x80\x91"), STR("-")},
    {STR("\xe2\x80\x92"), STR("-")},
    {STR("\xe2\x80\x95"), STR("--")},
    {STR("\xe2\x80\x82"), STR(" ")},
    {STR("\xe2\x80\x83"), STR(" ")},
    {STR("\xe2\x80\x89"), STR(" ")},
    {STR("\xe2\x9f\xa8"), STR("<")},
    {STR("\xe2\x9f\xa9"), STR(">")},
    {STR("\xe2\x80\x9b"), STR("'")},
    {STR("\xe2\x80\x9f"), STR("\"")},
};


// Typesetter

enum {
    F_R  = 0,
    F_B  = 1,
    F_I  = 2,
    F_BI = 3,
};

enum {
    TK_GLYPH,   // printable text
    TK_SPACE,   // adjustable, breakable word space
    TK_FIXED,   // unadjustable motion, possibly negative
    TK_ZERO,    // zero-width marker (\&)
};

// Bounds on token widths and indents, in columns, so that widths sum
// without overflow. Nothing beyond the row width can be seen anyway.
enum {
    MAXW   = 1<<12,
    MAXIND = 1<<10,
    MAXV   = 100,   // blank lines in one run
};

static i32 clampw(i64 w)
{
    return w<-MAXW ? -MAXW : w>MAXW ? MAXW : (i32)w;
}

enum {
    TF_BREAK = 1<<0,  // may break the line after this glyph
    TF_ZERO  = 1<<1,  // glyph does not advance (\z)
    TF_HYPH  = 1<<2,  // break point that adds a hyphen (\%)
};

typedef struct {
    i32 off;
    i32 len;
    i32 width;
    u8  type;
    u8  font;
    u8  flags;
} Tok;

typedef struct {
    u8 *data;
    i32 len;
    u8  font;
} Cell;

// A captured output line (table cells)
typedef struct {
    Cell *cells;
    i32   len;
} Capline;

typedef struct {
    Capline *data;
    iz       len;
    iz       cap;
} Caplines;

// groff's encoding: bit 0 enables adjustment, so .na and .ad toggle it
enum {
    AD_L = 0,
    AD_B = 1,
    AD_C = 3,
    AD_R = 5,
};

typedef struct {
    Output *out;
    i32     ll;         // line length
    b32     sgr;        // emit SGR escapes
    b32     utf8;       // UTF-8 glyphs, otherwise ASCII

    i32     indent;
    i32     previndent; // restored by .in without an argument
    i32     tindent;    // temporary indent for the next line, or -1
    b32     fill;
    i32     adjust;
    i32     center;     // input lines left to center
    i32     tabs[32];   // explicit tab stops (columns from indent)
    i32     ntabs;
    i32     vspace;     // pending blank lines
    b32     nospace;    // no-space mode
    b32     reverse;    // adjustment direction, toggled per line
    i32     tabw;       // default tab stop interval
    b32     vcollapse;  // vertical spaces do not accumulate (mdoc)
    b32     zerogap;    // zero-width words are part of a word space (mdoc)
    i32     sentspace;  // extra space after sentences (.ss, 1/12 em)
    i64     lines;      // output line count

    Tok    *toks;
    i32     ntoks;
    i32     maxtoks;
    i32     word;       // first token of the current word
    u8     *bytes;
    i32     nbytes;
    i32     maxbytes;

    Cell   *row;
    i32     rowcap;

    b32      capture;   // collect lines instead of printing them
    Arena   *caparena;
    Caplines caps;
} Typeset;

static Typeset *newtypeset(Arena *a, Output *out)
{
    Typeset *ts = new(a, 1, Typeset);
    ts->out       = out;
    ts->ll        = 78;
    ts->tindent   = -1;
    ts->fill      = 1;
    ts->adjust    = AD_B;
    ts->sentspace = 12;
    ts->tabw      = 5;
    ts->nospace   = 1;  // no blank lines at the very top
    ts->maxtoks   = 1<<14;
    ts->toks      = new(a, ts->maxtoks, Tok);
    ts->maxbytes  = 1<<16;
    ts->bytes     = new(a, ts->maxbytes, u8);
    ts->rowcap    = 1<<12;
    ts->row       = new(a, ts->rowcap, Cell);
    return ts;
}

static i32 tswidth(Typeset *ts, i32 beg, i32 end)
{
    i32 w = 0;
    for (i32 i = beg; i < end; i++) {
        Tok *t = ts->toks + i;
        w += t->type==TK_GLYPH && (t->flags&TF_ZERO) ? 0 : t->width;
    }
    return w;
}

static i32 tsstart(Typeset *ts)
{
    i32 r = ts->tindent>=0 ? ts->tindent : ts->indent;
    return r<0 ? 0 : r>MAXIND ? MAXIND : r;
}

static void sgrto(Output *out, u8 from, u8 to)
{
    if ((from&F_B) && !(to&F_B)) print(out, S("\x1b[22m"));
    if ((from&F_I) && !(to&F_I)) print(out, S("\x1b[24m"));
    if (!(from&F_B) && (to&F_B)) print(out, S("\x1b[1m"));
    if (!(from&F_I) && (to&F_I)) print(out, S("\x1b[4m"));
}

static void capture(Typeset *ts, i32 n)
{
    Arena   *a = ts->caparena;
    Capline *l = push(a, &ts->caps);
    l->cells = new(a, n, Cell);
    l->len   = n;
    for (i32 i = 0; i < n; i++) {
        Cell c = ts->row[i];
        if (c.data) {
            c.data = clone(a, (Str){c.data, c.len}).data;
        }
        l->cells[i] = c;
    }
}

// Write the first n cells of the row as one output line.
static void emitrow(Typeset *ts, i32 n)
{
    n = n<0 ? 0 : n>ts->rowcap ? ts->rowcap : n;
    if (ts->capture) {
        for (; ts->vspace > 0; ts->vspace--) {
            capture(ts, 0);
        }
        for (; n>0 && !ts->row[n-1].data; n--) {}
        capture(ts, n);
        ts->nospace = 0;
        ts->lines++;
        return;
    }

    Output *out = ts->out;
    for (; ts->vspace > 0; ts->vspace--) {
        printu8(out, '\n');
    }
    for (; n>0 && !ts->row[n-1].data; n--) {}
    u8 attr = 0;
    for (i32 i = 0; i < n; i++) {
        Cell *c = ts->row + i;
        u8 font = c->data ? c->font : 0;
        if (ts->sgr && font!=attr) {
            sgrto(out, attr, font);
            attr = font;
        }
        if (c->data) {
            print(out, (Str){c->data, c->len});
        } else {
            printu8(out, ' ');
        }
    }
    if (ts->sgr) {
        sgrto(out, attr, 0);
    }
    printu8(out, '\n');
    ts->nospace = 0;
    ts->lines++;
}

static void clearrow(Typeset *ts, i32 n)
{
    n = n<0 ? 0 : n>ts->rowcap ? ts->rowcap : n;
    xset(ts->row, 0, touz(n)*sizeof(Cell));
}

// Place text into the row at a column, returning the next column.
static i32 placetext(Typeset *ts, i32 col, Str s, u8 font, b32 advance)
{
    i32 start = col;
    while (s.len) {
        Rune r = decode(s);
        i32  w = runewidth(r.cp);
        if (col>=0 && col<ts->rowcap) {
            Cell *c = ts->row + col;
            if (w == 0 && col > 0 && ts->row[col-1].data) {
                // Combining mark: append to the previous cell
                c = ts->row + col - 1;
                if (c->data+c->len == s.data) {
                    c->len += r.len;
                }
            } else {
                c->data = s.data;
                c->len  = r.len;
                c->font = font;
                if (w==2 && col+1<ts->rowcap) {
                    ts->row[col+1].data = s.data;
                    ts->row[col+1].len  = 0;
                    ts->row[col+1].font = font;
                }
            }
        }
        col += w;
        s = cuthead(s, r.len);
    }
    return advance ? col : start;
}

// Lay out tokens [0, n) into the row starting at a column.
static i32 placetoks(Typeset *ts, i32 col, i32 n)
{
    for (i32 i = 0; i < n; i++) {
        Tok *t = ts->toks + i;
        switch (t->type) {
        case TK_GLYPH: {
            Str s = {ts->bytes+t->off, t->len};
            col = placetext(ts, col, s, t->font, !(t->flags&TF_ZERO));
        } break;
        case TK_SPACE:
        case TK_FIXED:
            col += t->width;
            if (col < 0) col = 0;
            break;
        case TK_ZERO:
            break;
        }
    }
    return col;
}

// Distribute extra space among the word gaps of tokens [0, n), like
// groff: each gap takes the remaining space divided by the remaining
// gaps, alternating traversal direction on each adjusted line.
static void spread(Typeset *ts, i32 n, i32 extra)
{
    i32 gaps = 0;
    for (i32 i = 0; i < n; i++) {
        if (ts->toks[i].type==TK_SPACE && (i==0 || ts->toks[i-1].type!=TK_SPACE)) {
            gaps++;
        }
    }
    if (!gaps || extra<=0) {
        return;
    }
    b32 rtl = !ts->reverse;
    ts->reverse = !ts->reverse;
    for (i32 k = 0; k < n; k++) {
        i32 i = rtl ? n-1-k : k;
        Tok *t = ts->toks + i;
        b32 first = rtl ? i==0 || t[-1].type!=TK_SPACE
                        : i+1==n || t[1].type!=TK_SPACE;
        if (t->type!=TK_SPACE || !first) {
            continue;
        }
        i32 add = gaps==1 ? extra : extra/gaps;
        t->width += add;
        extra -= add;
        gaps--;
    }
}

// Output tokens [0, n) as a line and shift the remainder to the front.
static void tsflushn(Typeset *ts, i32 n, b32 adjust)
{
    i32 end = n;
    for (; end>0 && ts->toks[end-1].type==TK_SPACE; end--) {}

    // A line ending at a discretionary hyphen shows the hyphen
    b32 hyph  = end>0 && ts->toks[end-1].type==TK_ZERO && (ts->toks[end-1].flags&TF_HYPH);
    i32 start = tsstart(ts);
    i32 width = tswidth(ts, 0, end) + hyph;
    if (ts->center > 0) {
        i32 indent = ts->indent<0 ? 0 : ts->indent>MAXIND ? MAXIND : ts->indent;
        i32 avail  = ts->ll - indent;
        start = indent + (avail>width ? (avail - width)/2 : 0);
    } else if (ts->adjust==AD_C && ts->fill) {
        i32 avail = ts->ll - start;
        start += avail>width ? (avail - width)/2 : 0;
    } else if (ts->adjust==AD_R && ts->fill) {
        i32 avail = ts->ll - start;
        start += avail>width ? avail - width : 0;
    } else if (adjust && ts->adjust==AD_B && ts->fill) {
        spread(ts, end, ts->ll - start - width);
    }

    // Columns that layout might reach: backward motions only lower it
    i32 need = start + hyph + 1;
    for (i32 i = 0; i < end; i++) {
        Tok *t = ts->toks + i;
        need += t->width>0 ? t->width : 0;
        need += t->type==TK_GLYPH ? t->len : 0;  // generous for \z
    }
    clearrow(ts, need+1);
    i32 cols = placetoks(ts, start, end);
    if (hyph) {
        cols = placetext(ts, cols, S("-"), 0, 1);
    }
    for (i32 i = 0; i < ts->rowcap && i < need; i++) {
        if (ts->row[i].data && i >= cols) cols = i + 1;
    }
    emitrow(ts, cols<ts->rowcap ? cols : ts->rowcap);

    // Drop the output tokens and leading spaces of the remainder
    i32 drop = n;
    for (; drop<ts->ntoks && ts->toks[drop].type==TK_SPACE; drop++) {}
    i32 keep = ts->ntoks - drop;
    if (keep) {
        // Only glyph offsets are meaningful; others may be stale
        i32 boff = ts->nbytes;
        for (i32 i = drop; i < ts->ntoks; i++) {
            if (ts->toks[i].type==TK_GLYPH && ts->toks[i].off<boff) {
                boff = ts->toks[i].off;
            }
        }
        // Glyph bytes are appended in token order, so the remainder's
        // bytes form a suffix of the byte buffer.
        i32 blen = ts->nbytes - boff;
        __builtin_memmove(ts->bytes, ts->bytes+boff, touz(blen));
        __builtin_memmove(ts->toks, ts->toks+drop, touz(keep)*sizeof(Tok));
        for (i32 i = 0; i < keep; i++) {
            if (ts->toks[i].type == TK_GLYPH) {
                ts->toks[i].off -= boff;
            }
        }
        ts->nbytes = blen;
    } else {
        ts->nbytes = 0;
    }
    ts->ntoks = keep;
    ts->word  = ts->word>drop ? ts->word-drop : 0;
    ts->tindent = -1;
    if (ts->center > 0) {
        ts->center--;
    }
}

static b32 tsempty(Typeset *ts)
{
    return ts->ntoks == 0;
}

static void tscommit(Typeset *ts);

// Break: output any pending partial line without adjustment.
static void tsbreak(Typeset *ts)
{
    if (ts->fill) {
        tscommit(ts);  // the last word may still need to wrap
    }
    ts->word = ts->ntoks;
    b32 glyphs = 0;
    for (i32 i = 0; i < ts->ntoks; i++) {
        glyphs |= ts->toks[i].type != TK_SPACE;
    }
    if (glyphs) {
        tsflushn(ts, ts->ntoks, 0);
    } else {
        ts->ntoks  = 0;
        ts->nbytes = 0;
        ts->word   = 0;
    }
}

static void tsvspace(Typeset *ts, i32 n)
{
    tsbreak(ts);
    if (!ts->nospace && n>0) {
        if (ts->vcollapse) {
            ts->vspace = ts->vspace>n ? ts->vspace : n;  // mandoc
        } else {
            ts->vspace += n;
        }
        ts->vspace = ts->vspace>MAXV ? MAXV : ts->vspace;
    }
}

static b32 hasglyphs(Typeset *ts, i32 beg, i32 end)
{
    for (i32 i = beg; i < end; i++) {
        if (ts->toks[i].type==TK_GLYPH || ts->toks[i].type==TK_FIXED) {
            return 1;
        }
    }
    return 0;
}

static b32 isletter(Typeset *ts, i32 i)
{
    if (i<0 || i>=ts->ntoks || ts->toks[i].type!=TK_GLYPH) {
        return 0;
    }
    u8 c = ts->bytes[ts->toks[i].off];
    return (c>='a' && c<='z') || (c>='A' && c<='Z') || c>=0x80;
}

// Find a break point inside the current word: the last hyphen between
// letters whose prefix fits within the given width, as groff does.
// Returns -1 if none.
static i32 hyphenbreak(Typeset *ts, i32 avail)
{
    i32 best = -1;
    i32 w = 0;
    for (i32 i = ts->word; i < ts->ntoks-1; i++) {
        Tok *t = ts->toks + i;
        w += t->type==TK_GLYPH && (t->flags&TF_ZERO) ? 0 : t->width;
        if (w > avail) {
            break;
        }
        if (!(t->flags&TF_BREAK) || i==ts->word) {
            continue;
        }
        if (t->type==TK_ZERO && (t->flags&TF_HYPH)) {
            if (w+1<=avail && hasglyphs(ts, ts->word, i) && hasglyphs(ts, i+1, ts->ntoks)) {
                best = i;
            }
        } else if (t->type==TK_ZERO ? hasglyphs(ts, i+1, ts->ntoks)
                                    : isletter(ts, i-1) && isletter(ts, i+1)) {
            best = i;
        }
    }
    return best;
}

// Finish the current word, breaking the line if it does not fit.
static void tscommit(Typeset *ts)
{
    while (ts->word < ts->ntoks) {
        i32 avail = ts->ll - tsstart(ts);
        i32 lw    = tswidth(ts, 0, ts->word);
        i32 ww    = tswidth(ts, ts->word, ts->ntoks);
        if (lw+ww<=avail || (ts->zerogap && !hasglyphs(ts, ts->word, ts->ntoks))) {
            ts->word = ts->ntoks;
            return;
        }
        i32 brk = hyphenbreak(ts, avail - lw);
        if (brk >= 0) {
            tsflushn(ts, brk+1, 1);
            continue;
        }
        if (!hasglyphs(ts, 0, ts->word)) {
            ts->word = ts->ntoks;  // overlong word
            return;
        }
        tsflushn(ts, ts->word, 1);
    }
}

static void tspush(Typeset *ts, Tok t, Str bytes)
{
    if (ts->ntoks==ts->maxtoks || bytes.len>ts->maxbytes-ts->nbytes) {
        // Pathological line: force it out
        tsbreak(ts);
        if (bytes.len > ts->maxbytes) {
            return;
        }
    }
    t.off   = ts->nbytes;
    t.len   = (i32)bytes.len;
    t.width = clampw(t.width);
    if (bytes.len) xcpy(ts->bytes+ts->nbytes, bytes.data, touz(bytes.len));
    ts->nbytes += (i32)bytes.len;
    ts->toks[ts->ntoks++] = t;
}

static void tsglyph(Typeset *ts, Str s, i32 font, u8 flags)
{
    Tok t   = {0};
    t.type  = TK_GLYPH;
    t.font  = (u8)font;
    t.flags = flags;
    t.width = strwidth(s);
    tspush(ts, t, s);
}

static void tsmotion(Typeset *ts, i32 n)
{
    Tok t   = {0};
    t.type  = TK_FIXED;
    t.width = n;
    tspush(ts, t, (Str){0});
}

static void tszero(Typeset *ts)
{
    Tok t  = {0};
    t.type = TK_ZERO;
    tspush(ts, t, (Str){0});
}

static void tsspace(Typeset *ts, i32 n)
{
    if (!ts->fill) {
        tsmotion(ts, n);
        return;
    }
    tscommit(ts);
    if (ts->ntoks && ts->toks[ts->ntoks-1].type==TK_SPACE) {
        Tok *t = ts->toks + ts->ntoks - 1;
        t->width = clampw((i64)t->width + n);
    } else {
        Tok t   = {0};
        t.type  = TK_SPACE;
        t.width = n;
        tspush(ts, t, (Str){0});
    }
    ts->word = ts->ntoks;
}

// Is the text before the end a sentence ending?
static b32 sentenceend(Typeset *ts)
{
    for (i32 i = ts->ntoks-1; i >= 0; i--) {
        Tok *t = ts->toks + i;
        if (t->type != TK_GLYPH) {
            return 0;
        }
        Str s = {ts->bytes+t->off, t->len};
        if (equals(s, S(".")) || equals(s, S("?")) || equals(s, S("!"))) {
            return 1;
        }
        static Str transparent[] = {
            STR(")"), STR("]"), STR("\""), STR("'"), STR("*"),
            STR("\xe2\x80\x9d"), STR("\xe2\x80\x99"),
            STR("\xe2\x80\xa0"), STR("\xe2\x80\xa1"),
        };
        b32 ok = 0;
        for (i32 j = 0; j < countof(transparent); j++) {
            ok |= equals(s, transparent[j]);
        }
        if (!ok) {
            return 0;
        }
    }
    return 0;
}

// End of a filled input line: a word space, wider after a sentence.
static void tsendline(Typeset *ts)
{
    tscommit(ts);
    if (!hasglyphs(ts, 0, ts->ntoks)) {
        return;
    }
    tsspace(ts, 1 + (sentenceend(ts) ? (ts->sentspace + 6)/12 : 0));
}

// Make the last word current again, so that what follows joins it.
static void tsreopen(Typeset *ts)
{
    ts->word = ts->ntoks;
    for (; ts->word>0 && ts->toks[ts->word-1].type!=TK_SPACE; ts->word--) {}
}

// Drop a pending end-of-line space so the next word joins the last.
static void tsunspace(Typeset *ts)
{
    if (ts->ntoks && ts->toks[ts->ntoks-1].type==TK_SPACE) {
        ts->ntoks--;
        tsreopen(ts);
    }
}

// Width of pending text, excluding trailing spaces.
static i32 tspending(Typeset *ts)
{
    i32 end = ts->ntoks;
    for (; end>0 && ts->toks[end-1].type==TK_SPACE; end--) {}
    return tswidth(ts, 0, end);
}

// Make the pending text an unbreakable, unadjustable unit (tags).
static void tsfreeze(Typeset *ts)
{
    for (; ts->ntoks && ts->toks[ts->ntoks-1].type==TK_SPACE; ts->ntoks--) {}
    for (i32 i = 0; i < ts->ntoks; i++) {
        if (ts->toks[i].type == TK_SPACE) {
            ts->toks[i].type = TK_FIXED;
        }
    }
    ts->word = ts->ntoks;
}

// Output the pending tokens as a standalone line of three unbreakable
// parts (titles): tokens [0, left), [left, center), and [center, ntoks).
static void tstitle(Typeset *ts, i32 left, i32 center)
{
    // A forced break while tokenizing the parts may have dropped tokens
    i32 n  = ts->ntoks;
    left   = left<0 ? 0 : left>n ? n : left;
    center = center<left ? left : center>n ? n : center;
    for (i32 i = 0; i < n; i++) {
        if (ts->toks[i].type == TK_SPACE) ts->toks[i].type = TK_FIXED;
    }
    i32 lw = tswidth(ts, 0, left);
    i32 cw = tswidth(ts, left, center);
    i32 rw = tswidth(ts, center, n);

    // Motions may place text anywhere, so use the whole row
    clearrow(ts, ts->rowcap);
    placetoks(ts, 0, left);
    i32 ccol = (ts->ll - cw + 1)/2;
    if (ccol < lw+1) ccol = lw + 1;
    i32 rcol = ts->ll - rw;
    if (rcol < ccol+cw+1) rcol = ccol + cw + 1;

    // Lay out each part from its own starting column
    Tok *toks = ts->toks;
    ts->toks += left;
    placetoks(ts, ccol, center - left);
    ts->toks += center - left;
    placetoks(ts, rcol, n - center);
    ts->toks = toks;
    emitrow(ts, ts->rowcap);
    ts->ntoks = ts->nbytes = ts->word = 0;
}


// Roff interpreter

enum {
    K_NONE,

    // Requests
    R_IGNORE, R_AD, R_ALS, R_AM, R_AS, R_BR, R_CE, R_DE, R_DO, R_DS, R_EL,
    R_FI, R_FT, R_IE, R_IF, R_IG, R_IN, R_LL, R_NA, R_NF, R_NOP, R_NR, R_NS,
    R_RM, R_RN, R_RR, R_RS, R_SHIFT, R_SO, R_SP, R_TA, R_TI, R_TL, R_TR,
    R_TS, R_EQ, R_PS, R_SS, R_MSO,

    // man(7)
    M_TH, M_SH, M_SS, M_PP, M_TP, M_TQ, M_IP, M_HP, M_RS, M_RE, M_PD, M_B,
    M_I, M_SM, M_SB, M_BI, M_BR, M_IB, M_IR, M_RB, M_RI, M_UR, M_UE, M_MT,
    M_ME, M_EX, M_EE, M_SY, M_YS, M_OP, M_MR, M_DT, M_IGNORE,

    // mdoc(7)
    D_DD, D_DT, D_OS, D_SH, D_SS, D_PP, D_D1, D_DL, D_BD, D_ED, D_BL, D_EL,
    D_IT, D_RS, D_RE, D_BK, D_EK, D_BF, D_EF, D_ND, D_NM, D_AD, D_AN, D_AP,
    D_AR, D_AT, D_BSX, D_BX, D_CD, D_CM, D_DV, D_DX, D_EM, D_ER, D_EV, D_EX,
    D_FA, D_FD, D_FL, D_FN, D_FT, D_FX, D_IC, D_IN, D_LB, D_LI, D_LK, D_MS,
    D_MT, D_NO, D_NS, D_NX, D_OX, D_PA, D_PF, D_RV, D_SM, D_ST, D_SX, D_SY,
    D_TN, D_UX, D_VA, D_VT, D_XR, D_AQ, D_BQ, D_BRQ, D_DQ, D_OP, D_PQ, D_QL,
    D_QQ, D_SQ, D_AO, D_AC, D_BO, D_BC, D_BRO, D_BRC, D_DO, D_DC, D_EO, D_EC,
    D_FO, D_FC, D_OO, D_OC, D_PO, D_PC, D_QO, D_QC, D_SO, D_SC, D_XO, D_XC,
    D_TA, D_REF, D_NOOP, D_IGNORE,
};

static struct {
    Str name;
    i32 kind;
} builtins[] = {
    {STR("ad"), R_AD}, {STR("als"), R_ALS}, {STR("am"), R_AM},
    {STR("am1"), R_AM}, {STR("ami"), R_IGNORE}, {STR("as"), R_AS},
    {STR("as1"), R_AS}, {STR("br"), R_BR}, {STR("ce"), R_CE},
    {STR("de"), R_DE}, {STR("de1"), R_DE}, {STR("do"), R_DO},
    {STR("ds"), R_DS}, {STR("ds1"), R_DS}, {STR("el"), R_EL},
    {STR("fi"), R_FI}, {STR("ft"), R_FT}, {STR("ie"), R_IE},
    {STR("if"), R_IF}, {STR("ig"), R_IG}, {STR("in"), R_IN},
    {STR("ll"), R_LL}, {STR("na"), R_NA}, {STR("nf"), R_NF},
    {STR("nop"), R_NOP}, {STR("nr"), R_NR}, {STR("ns"), R_NS},
    {STR("rm"), R_RM}, {STR("rn"), R_RN}, {STR("rr"), R_RR},
    {STR("rs"), R_RS}, {STR("shift"), R_SHIFT}, {STR("so"), R_SO},
    {STR("sp"), R_SP}, {STR("ta"), R_TA}, {STR("ti"), R_TI},
    {STR("tl"), R_TL}, {STR("tr"), R_TR}, {STR("TS"), R_TS},
    {STR("EQ"), R_EQ}, {STR("PS"), R_PS},

    {STR("ab"), R_IGNORE}, {STR("af"), R_IGNORE}, {STR("aln"), R_IGNORE},
    {STR("bd"), R_IGNORE}, {STR("bp"), R_IGNORE}, {STR("c2"), R_IGNORE},
    {STR("cc"), R_IGNORE}, {STR("cf"), R_IGNORE}, {STR("ch"), R_IGNORE},
    {STR("char"), R_IGNORE}, {STR("chop"), R_IGNORE}, {STR("color"), R_IGNORE},
    {STR("cp"), R_IGNORE}, {STR("cs"), R_IGNORE}, {STR("cu"), R_IGNORE},
    {STR("da"), R_IGNORE}, {STR("di"), R_IGNORE}, {STR("ec"), R_IGNORE},
    {STR("em"), R_IGNORE}, {STR("eo"), R_IGNORE}, {STR("ev"), R_IGNORE},
    {STR("fam"), R_IGNORE}, {STR("fc"), R_IGNORE}, {STR("fchar"), R_IGNORE},
    {STR("fl"), R_IGNORE}, {STR("fp"), R_IGNORE}, {STR("fspecial"), R_IGNORE},
    {STR("ftr"), R_IGNORE}, {STR("fzoom"), R_IGNORE}, {STR("hc"), R_IGNORE},
    {STR("hcode"), R_IGNORE}, {STR("hla"), R_IGNORE}, {STR("hlm"), R_IGNORE},
    {STR("hpf"), R_IGNORE}, {STR("hw"), R_IGNORE}, {STR("hy"), R_IGNORE},
    {STR("hym"), R_IGNORE}, {STR("hys"), R_IGNORE}, {STR("it"), R_IGNORE},
    {STR("itc"), R_IGNORE}, {STR("kern"), R_IGNORE}, {STR("lc"), R_IGNORE},
    {STR("lf"), R_IGNORE}, {STR("lg"), R_IGNORE}, {STR("ls"), R_IGNORE},
    {STR("lt"), R_IGNORE}, {STR("mc"), R_IGNORE}, {STR("mk"), R_IGNORE},
    {STR("mso"), R_MSO}, {STR("ne"), R_IGNORE}, {STR("nh"), R_IGNORE},
    {STR("nm"), R_IGNORE}, {STR("nn"), R_IGNORE}, {STR("nx"), R_IGNORE},
    {STR("os"), R_IGNORE}, {STR("pc"), R_IGNORE}, {STR("pl"), R_IGNORE},
    {STR("pm"), R_IGNORE}, {STR("pn"), R_IGNORE}, {STR("po"), R_IGNORE},
    {STR("ps"), R_IGNORE}, {STR("pso"), R_IGNORE}, {STR("pvs"), R_IGNORE},
    {STR("rchar"), R_IGNORE}, {STR("rd"), R_IGNORE}, {STR("return"), R_IGNORE},
    {STR("rj"), R_IGNORE}, {STR("rt"), R_IGNORE}, {STR("schar"), R_IGNORE},
    {STR("shc"), R_IGNORE}, {STR("ss"), R_SS}, {STR("sty"), R_IGNORE},
    {STR("sv"), R_IGNORE}, {STR("sy"), R_IGNORE}, {STR("tc"), R_IGNORE},
    {STR("tm"), R_IGNORE}, {STR("tm1"), R_IGNORE}, {STR("tmc"), R_IGNORE},
    {STR("trf"), R_IGNORE}, {STR("trin"), R_IGNORE}, {STR("trnt"), R_IGNORE},
    {STR("uf"), R_IGNORE}, {STR("ul"), R_IGNORE}, {STR("unformat"), R_IGNORE},
    {STR("vpt"), R_IGNORE}, {STR("vs"), R_IGNORE}, {STR("warn"), R_IGNORE},
    {STR("warnscale"), R_IGNORE}, {STR("wh"), R_IGNORE}, {STR("while"), R_IGNORE},
    {STR("write"), R_IGNORE}, {STR("writec"), R_IGNORE}, {STR("writem"), R_IGNORE},
    {STR("break"), R_IGNORE}, {STR("continue"), R_IGNORE},
    {STR("TE"), R_IGNORE}, {STR("EN"), R_IGNORE}, {STR("PE"), R_IGNORE},

    {STR("TH"), M_TH}, {STR("SH"), M_SH}, {STR("SS"), M_SS},
    {STR("PP"), M_PP}, {STR("LP"), M_PP}, {STR("P"), M_PP},
    {STR("TP"), M_TP}, {STR("TQ"), M_TQ}, {STR("IP"), M_IP},
    {STR("HP"), M_HP}, {STR("RS"), M_RS}, {STR("RE"), M_RE},
    {STR("PD"), M_PD}, {STR("B"), M_B}, {STR("I"), M_I},
    {STR("SM"), M_SM}, {STR("SB"), M_SB}, {STR("BI"), M_BI},
    {STR("BR"), M_BR}, {STR("IB"), M_IB}, {STR("IR"), M_IR},
    {STR("RB"), M_RB}, {STR("RI"), M_RI}, {STR("UR"), M_UR},
    {STR("UE"), M_UE}, {STR("MT"), M_MT}, {STR("ME"), M_ME},
    {STR("EX"), M_EX}, {STR("EE"), M_EE}, {STR("SY"), M_SY},
    {STR("YS"), M_YS}, {STR("OP"), M_OP}, {STR("MR"), M_MR},
    {STR("DT"), M_DT}, {STR("UC"), M_IGNORE}, {STR("AT"), M_IGNORE},
    {STR("PT"), M_IGNORE}, {STR("BT"), M_IGNORE}, {STR("HY"), M_IGNORE},
    {STR("LINKSTYLE"), M_IGNORE},
};

// mdoc(7) macros; "callable" macros may appear in macro arguments
static struct {
    Str name;
    i32 kind;
    b32 callable;
} mdocs[] = {
    {STR("Dd"), D_DD, 0}, {STR("Dt"), D_DT, 0}, {STR("Os"), D_OS, 0},
    {STR("Sh"), D_SH, 0}, {STR("Ss"), D_SS, 0}, {STR("Pp"), D_PP, 0},
    {STR("Lp"), D_PP, 0}, {STR("D1"), D_D1, 0}, {STR("Dl"), D_DL, 0},
    {STR("Bd"), D_BD, 0}, {STR("Ed"), D_ED, 0}, {STR("Bl"), D_BL, 0},
    {STR("El"), D_EL, 0}, {STR("It"), D_IT, 0}, {STR("Rs"), D_RS, 0},
    {STR("Re"), D_RE, 0}, {STR("Bk"), D_BK, 0}, {STR("Ek"), D_EK, 0},
    {STR("Bf"), D_BF, 0}, {STR("Ef"), D_EF, 0}, {STR("Nd"), D_ND, 0},
    {STR("Nm"), D_NM, 1}, {STR("Ad"), D_AD, 1}, {STR("An"), D_AN, 1},
    {STR("Ap"), D_AP, 1}, {STR("Ar"), D_AR, 1}, {STR("At"), D_AT, 1},
    {STR("Bsx"), D_BSX, 1}, {STR("Bx"), D_BX, 1}, {STR("Cd"), D_CD, 0},
    {STR("Cm"), D_CM, 1}, {STR("Dv"), D_DV, 1}, {STR("Dx"), D_DX, 1},
    {STR("Em"), D_EM, 1}, {STR("Er"), D_ER, 1}, {STR("Ev"), D_EV, 1},
    {STR("Ex"), D_EX, 0}, {STR("Fa"), D_FA, 1}, {STR("Fd"), D_FD, 0},
    {STR("Fl"), D_FL, 1}, {STR("Fn"), D_FN, 1}, {STR("Fr"), D_EM, 1},
    {STR("Ft"), D_FT, 1}, {STR("Fx"), D_FX, 1}, {STR("Ic"), D_IC, 1},
    {STR("In"), D_IN, 1}, {STR("Lb"), D_LB, 0}, {STR("Li"), D_LI, 1},
    {STR("Lk"), D_LK, 1}, {STR("Ms"), D_MS, 1}, {STR("Mt"), D_MT, 1},
    {STR("No"), D_NO, 1}, {STR("Ns"), D_NS, 1}, {STR("Nx"), D_NX, 1},
    {STR("Ox"), D_OX, 1}, {STR("Pa"), D_PA, 1}, {STR("Pf"), D_PF, 1},
    {STR("Rv"), D_RV, 0}, {STR("Sm"), D_SM, 0}, {STR("St"), D_ST, 1},
    {STR("Sx"), D_SX, 1}, {STR("Sy"), D_SY, 1}, {STR("Tn"), D_TN, 1},
    {STR("Ux"), D_UX, 1}, {STR("Va"), D_VA, 1}, {STR("Vt"), D_VT, 1},
    {STR("Xr"), D_XR, 1}, {STR("Aq"), D_AQ, 1}, {STR("Bq"), D_BQ, 1},
    {STR("Brq"), D_BRQ, 1}, {STR("Dq"), D_DQ, 1}, {STR("Op"), D_OP, 1},
    {STR("Pq"), D_PQ, 1}, {STR("Ql"), D_QL, 1}, {STR("Qq"), D_QQ, 1},
    {STR("Sq"), D_SQ, 1}, {STR("Ao"), D_AO, 1}, {STR("Ac"), D_AC, 1},
    {STR("Bo"), D_BO, 1}, {STR("Bc"), D_BC, 1}, {STR("Bro"), D_BRO, 1},
    {STR("Brc"), D_BRC, 1}, {STR("Do"), D_DO, 1}, {STR("Dc"), D_DC, 1},
    {STR("Eo"), D_EO, 1}, {STR("Ec"), D_EC, 1}, {STR("Fo"), D_FO, 0},
    {STR("Fc"), D_FC, 1}, {STR("Oo"), D_OO, 1}, {STR("Oc"), D_OC, 1},
    {STR("Po"), D_PO, 1}, {STR("Pc"), D_PC, 1}, {STR("Qo"), D_QO, 1},
    {STR("Qc"), D_QC, 1}, {STR("So"), D_SO, 1}, {STR("Sc"), D_SC, 1},
    {STR("Xo"), D_XO, 1}, {STR("Xc"), D_XC, 1}, {STR("Ta"), D_TA, 1},
    {STR("%A"), D_REF, 0}, {STR("%B"), D_REF, 0}, {STR("%C"), D_REF, 0},
    {STR("%D"), D_REF, 0}, {STR("%I"), D_REF, 0}, {STR("%J"), D_REF, 0},
    {STR("%N"), D_REF, 0}, {STR("%O"), D_REF, 0}, {STR("%P"), D_REF, 0},
    {STR("%Q"), D_REF, 0}, {STR("%R"), D_REF, 0}, {STR("%T"), D_REF, 0},
    {STR("%U"), D_REF, 0}, {STR("%V"), D_REF, 0}, {STR("Ot"), D_FT, 1},
    {STR("Hf"), D_IGNORE, 0}, {STR("Tg"), D_NOOP, 0},
    {STR("Es"), D_IGNORE, 1}, {STR("En"), D_IGNORE, 1},
    {STR("Bt"), D_IGNORE, 0}, {STR("Ud"), D_IGNORE, 0},
};

enum {
    IN = 7*24,  // default indent (man), basic units
    SN = 3,     // subsection heading indent

    U_PER_N = 24,
    U_PER_V = 40,

    MAXDEPTH  = 64,
    MAXRS     = 32,
};

// An open mdoc block: list, display, or reference
typedef struct {
    i32 kind;       // D_BL, D_BD, D_RS
    i32 type;       // list type (LT_*)
    i32 width;      // tag width, columns
    i32 indent;     // indent of the block's content
    i32 saveindent;
    b32 savefill;
    b32 compact;
    i32 count;      // -enum counter
    i32 ncols;      // -column
    i32 colw[16];
    i64 lines;      // output line count when the tag or body began
    i32 body;       // pending tokens when the body began
} MdBlock;

// One part of an .Rs reference, like %A or %T
typedef struct {
    Str v;
    u8  part;       // letter after the '%'
} MdRef;

typedef struct Frame Frame;
struct Frame {
    Frame *next;
    Str    src;
    Str   *args;    // args[0] is the macro name
    i32    nargs;
    i32    line;
    Str    name;
    b32    macro;
    byte  *end;     // perm.end before args, restored on pop
    i32    sonest;  // .so nesting level of a file frame
};

// Input trap actions, run after the next text line (an-input-trap)
enum {
    TRAP_FONT = 1<<0,   // return to roman
    TRAP_HEAD = 1<<1,   // end a heading: break, no-space mode
    TRAP_TAG  = 1<<2,   // write the paragraph tag
};

typedef struct {
    Plt     *plt;
    Arena    perm;
    Output  *err;
    Typeset *ts;
    b32      warn;
    i32      status;

    Frame   *frame;
    Frame   *freeframes;
    i32      depth;
    i64      budget;

    Entry   *strings;
    Entry   *macros;
    Entry   *regs;
    Entry   *kinds;
    Entry   *glyphmap;
    Entry   *asciimap;
    Entry   *trmap;

    u64      iestack;
    i32      iedepth;
    i32      skip;          // >0 while skipping a false block

    Entry   *defining;      // capturing a macro body
    Str      defend;
    Buf      defbody;
    b32      ignoring;      // inside .ig
    Str      igend;

    i32      font;
    i32      prevfont;
    b32      cont;          // previous text line ended with \c

    // man(7) state
    b32      page;
    Str      title;
    Str      section;
    Str      date;
    Str      source;
    Str      manual;
    i32      margin;        // an-margin
    i32      pindent;       // an-prevailing-indent
    i32      rsmargin[MAXRS];
    i32      rspindent[MAXRS];
    i32      rsdepth;
    i32      pd;            // paragraph distance (lines)
    i32      trap;
    i64      taglines;      // output line count when a tag began
    b32      example;
    i32      expd;
    i32      exfont;
    Str      url;
    b32      inurl;
    b32      sy;
    i32      syindent;
    i32      syadjust;

    b32      tblactive;
    Buf      tblsrc;

    // mdoc(7) state
    b32      mdoc;
    Str      mdtitle;
    Str      mdsec;
    Str      mdvol;
    Str      mddate;
    Str      mdos;
    Str      mdname;        // first .Nm argument
    i32      mdsection;     // MS_* of the current section
    b32      mdnospace;     // no space before the next word
    b32      mdsmoff;       // .Sm off
    b32      mdsplit;       // .An -split
    b32      mdsplitset;
    b32      mdauthors;     // seen an author in AUTHORS
    i32      mdsynprev;     // kind of the previous SYNOPSIS line
    i32      mdnmbody;      // indent of a SYNOPSIS .Nm block, or 0
    MdBlock  mdblocks[32];
    i32      mdnblocks;
    i32      mddepth;       // nested callable macros
    b32      mdintag;       // .It tag continues to .Xc
    MdRef    mdrefs[32];    // .Rs parts in mandoc's order
    i32      mdrefn;
    b32      mdfo;          // inside .Fo/.Fc
    i32      mdfoargs;
    b32      mdeos;         // last word ended a sentence
    b32      mdwrote;       // this macro line has emitted a word
    b32      nobreak;       // hyphens in words do not allow breaks
} Ctx;

static void warnf(Ctx *c, Str msg, Str detail)
{
    if (!c->warn) {
        return;
    }
    Frame *f = c->frame;
    for (; f && f->macro; f = f->next) {}
    Output *err = c->err;
    print(err, S("nroff: "));
    if (f) {
        print(err, f->name);
        printu8(err, ':');
        printi64(err, f->line);
        print(err, S(": "));
    }
    print(err, msg);
    if (detail.data) {
        print(err, S(": "));
        print(err, detail);
    }
    printu8(err, '\n');
    flush(err);
}

static Str getstring(Ctx *c, Str name)
{
    Entry *e = find(c->strings, name);
    return e ? e->str : (Str){0};
}

static void setstring(Ctx *c, Str name, Str value)
{
    Entry *e = lookup(&c->strings, name, &c->perm);
    e->str  = clone(&c->perm, value);
    e->live = 1;
}


// Input

static Frame *newframe(Ctx *c)
{
    Frame *f = c->freeframes;
    if (f) {
        c->freeframes = f->next;
        xset(f, 0, sizeof(*f));
    } else {
        f = new(&c->perm, 1, Frame);
    }
    return f;
}

static void pushfile(Ctx *c, Str name, Str src)
{
    Frame *f = newframe(c);
    f->src  = src;
    f->name = name;
    f->next = c->frame;
    c->frame = f;
}

static void popframe(Ctx *c)
{
    Frame *f = c->frame;
    c->frame = f->next;
    if (f->macro) {
        c->depth--;
        c->perm.end = f->end;  // frames pop in LIFO order
    }
    f->next = c->freeframes;
    c->freeframes = f;
}

static Frame *argframe(Ctx *c)
{
    Frame *f = c->frame;
    for (; f && !f->macro; f = f->next) {}
    return f;
}

static b32 oddbackslashes(Str s)
{
    i32 n = 0;
    for (iz i = s.len-1; i>=0 && s.data[i]=='\\'; i--) {
        n++;
    }
    return n & 1;
}

static Str stripcr(Str s)
{
    if (s.len && s.data[s.len-1]=='\r') s.len--;
    return s;
}

// Read the next logical input line, joining escaped newlines. Returns
// a null string at the end of input.
static Str nextline(Ctx *c, Arena *scratch)
{
    for (;;) {
        Frame *f = c->frame;
        if (!f) {
            return (Str){0};
        }
        if (!f->src.len || c->budget<=0) {
            if (c->budget <= 0) {
                f->src = (Str){0};
            }
            popframe(c);
            continue;
        }
        c->budget--;
        Cut l = cut(f->src, '\n');
        f->src = l.tail;
        f->line++;
        Str line = stripcr(l.head);
        if (!line.data) {
            line.data = l.tail.data;
        }
        // Drop each escaping backslash before joining, so that concat
        // extends the line in place. Joined runs of backslashes are then
        // even, and the next segment alone decides whether to continue.
        b32 more = oddbackslashes(line);
        if (more) line = takehead(line, line.len-1);
        while (more && f->src.len) {
            Cut n = cut(f->src, '\n');
            f->src = n.tail;
            f->line++;
            Str next = stripcr(n.head);
            more = oddbackslashes(next);
            if (more) next = takehead(next, next.len-1);
            line = concat(scratch, line, next);
        }
        return line;
    }
}


// Escapes

typedef struct {
    Str name;
    Str rest;
    b32 ok;
} Arg;

// End of a [bracketed] name, honoring nested brackets.
static iz bracketend(Str s)
{
    i32 depth = 0;
    for (iz i = 0; i < s.len; i++) {
        if (s.data[i] == '[') {
            depth++;
        } else if (s.data[i]==']' && !--depth) {
            return i;
        }
    }
    return -1;
}

// Parse an escape's name: x, (xx, or [name].
static Arg escname(Str s)
{
    Arg r = {0};
    if (!s.len) {
        return r;
    }
    if (s.data[0] == '(') {
        if (s.len < 3) {
            return r;
        }
        r.name = span(s.data+1, s.data+3);
        r.rest = cuthead(s, 3);
    } else if (s.data[0] == '[') {
        iz end = bracketend(s);
        if (end < 0) {
            return r;
        }
        r.name = span(s.data+1, s.data+end);
        r.rest = cuthead(s, end+1);
    } else {
        r.name = takehead(s, 1);
        r.rest = cuthead(s, 1);
    }
    r.ok = 1;
    return r;
}

// Parse a delimited escape argument: 'text'.
static Arg escdelim(Str s)
{
    Arg r = {0};
    if (!s.len) {
        return r;
    }
    u8 d = s.data[0];
    for (iz i = 1; i < s.len; i++) {
        if (s.data[i] == '\\') {
            i++;
        } else if (s.data[i] == d) {
            r.name = span(s.data+1, s.data+i);
            r.rest = cuthead(s, i+1);
            r.ok   = 1;
            return r;
        }
    }
    // Unterminated: take the rest
    r.name = cuthead(s, 1);
    r.rest = (Str){s.data+s.len, 0};
    r.ok   = 1;
    return r;
}

static i32 getreg(Ctx *c, Str name);
static i32 textwidth(Ctx *c, Str s, Arena scratch);

// Interpolate strings, registers, macro arguments, and widths. Other
// escapes pass through unchanged. Truncates at comments.
static Str expand(Ctx *c, Str s, Arena *a, i32 depth)
{
    iz i = 0;
    for (; i<s.len && s.data[i]!='\\'; i++) {}
    if (i == s.len) {
        return s;
    }

    Buf b = {0};
    bappend(&b, takehead(s, i), a);
    while (i < s.len) {
        u8 ch = s.data[i];
        if (ch!='\\' || i+1==s.len) {
            bappendu8(&b, ch, a);
            i++;
            continue;
        }
        u8 e = s.data[i+1];
        Str rest = cuthead(s, i+2);
        switch (e) {
        case '\\':
            bappend(&b, S("\\\\"), a);
            i += 2;
            continue;

        case '"':
            return bstr(b);

        case '#':
            return bstr(b);

        case '*': {
            Arg n = escname(rest);
            if (!n.ok) {
                i += 2;
                continue;
            }
            Str name = n.name;
            if (rest.data[0] == '[') {
                // \*[name args]: arguments are ignored
                if (depth < 32) {
                    name = expand(c, name, a, depth+1);
                }
                name = cut(trim(name), ' ').head;
            }
            Str v = getstring(c, name);
            if (!v.data) {
                warnf(c, S("undefined string"), name);
            } else if (depth<32 && c->budget>0 && b.len<1<<20) {
                // Bound nested string bombs by work and by size
                c->budget--;
                bappend(&b, expand(c, v, a, depth+1), a);
            }
            i = n.rest.data - s.data;
        } continue;

        case 'n': {
            i32 inc = 0;
            if (rest.len && (rest.data[0]=='+' || rest.data[0]=='-')) {
                inc = rest.data[0]=='+' ? 1 : -1;
                rest = cuthead(rest, 1);
            }
            Arg n = escname(rest);
            if (!n.ok) {
                i += 2;
                continue;
            }
            Str name = n.name;
            if (rest.data[0]=='[' && depth<32) {
                name = expand(c, name, a, depth+1);
            }
            if (inc) {
                Entry *r = lookup(&c->regs, name, &c->perm);
                r->live = 1;
                r->num  = clampi32((i64)r->num + (i64)inc*r->inc);
            }
            bappend(&b, fmti64(a, getreg(c, name)), a);
            i = n.rest.data - s.data;
        } continue;

        case '$': {
            Frame *f = argframe(c);
            Arg n = escname(rest);
            if (!n.ok) {
                i += 2;
                continue;
            }
            Str name = n.name;
            Str v = {0};
            if (f && equals(name, S("*"))) {
                for (i32 j = 1; j < f->nargs; j++) {
                    v = concat(a, v, j>1 ? S(" ") : S(""));
                    v = concat(a, v, f->args[j]);
                }
            } else if (f && equals(name, S("@"))) {
                for (i32 j = 1; j < f->nargs; j++) {
                    v = concat(a, v, j>1 ? S(" \"") : S("\""));
                    v = concat(a, v, f->args[j]);
                    v = concat(a, v, S("\""));
                }
            } else if (f) {
                i32 k = 0;
                for (iz j = 0; j < name.len; j++) {
                    if (!digit(name.data[j]) || k>1000) {
                        k = -1;
                        break;
                    }
                    k = k*10 + name.data[j] - '0';
                }
                if (k>=0 && k<f->nargs) {
                    v = f->args[k];
                }
            }
            if (depth < 32) {
                bappend(&b, expand(c, v, a, depth+1), a);
            }
            i = n.rest.data - s.data;
        } continue;

        case 'w': {
            Arg d = escdelim(rest);
            if (!d.ok) {
                i += 2;
                continue;
            }
            Str t = depth<32 ? expand(c, d.name, a, depth+1) : d.name;
            i32 w = textwidth(c, t, *a);
            bappend(&b, fmti64(a, (i64)w*U_PER_N), a);
            i = d.rest.data - s.data;
        } continue;

        default:
            bappendu8(&b, '\\', a);
            bappendu8(&b, e, a);
            i += 2;
            continue;
        }
    }
    return bstr(b);
}

// Copy mode: reduce \\ to \.
static Str copymode(Str s, Arena *a)
{
    iz i = 0;
    for (; i+1<s.len && !(s.data[i]=='\\' && s.data[i+1]=='\\'); i++) {}
    if (i+1 >= s.len) {
        return s;
    }
    Buf b = {0};
    for (i = 0; i < s.len; i++) {
        bappendu8(&b, s.data[i], a);
        if (s.data[i]=='\\' && i+1<s.len) {
            if (s.data[i+1] != '\\') {
                bappendu8(&b, s.data[i+1], a);
            }
            i++;
        }
    }
    return bstr(b);
}


// Registers and expressions

static i32 getreg(Ctx *c, Str name)
{
    Typeset *ts = c->ts;
    if (name.len && name.data[0]=='.') {
        static struct { Str name; i32 value; } fixed[] = {
            {STR(".g"),  1}, {STR(".H"), 24}, {STR(".V"), 40},
            {STR(".T"),  1}, {STR(".A"),  0}, {STR(".s"), 10},
            {STR(".v"), 40}, {STR(".w"), 24}, {STR(".x"),  1},
            {STR(".y"), 23}, {STR(".Y"),  0}, {STR(".ss"), 12},
            {STR(".C"), 0}, {STR(".cp"), 0},
            {STR(".P"),  1}, {STR(".R"), 10000}, {STR(".t"), 1<<24},
        };
        for (i32 i = 0; i < countof(fixed); i++) {
            if (equals(name, fixed[i].name)) {
                return fixed[i].value;
            }
        }
        if (equals(name, S(".sss"))) return ts->sentspace;
        if (equals(name, S(".l"))) return ts->ll * U_PER_N;
        if (equals(name, S(".i"))) return ts->indent * U_PER_N;
        if (equals(name, S(".u"))) return ts->fill;
        if (equals(name, S(".j"))) return ts->adjust;
        if (equals(name, S(".f"))) return c->font + 1;
        if (equals(name, S(".c"))) return c->frame ? c->frame->line : 0;
        if (equals(name, S(".$"))) {
            Frame *f = argframe(c);
            return f ? f->nargs-1 : 0;
        }
    }
    if (equals(name, S("an-margin"))) {
        return c->margin;
    }
    Entry *e = find(c->regs, name);
    return e ? e->num : 0;
}

static void setreg(Ctx *c, Str name, i32 value)
{
    Entry *e = lookup(&c->regs, name, &c->perm);
    e->num  = value;
    e->live = 1;
}

typedef struct {
    Str rest;
    i64 value;
    b32 ok;
} Num;

static i64 unitscale(u8 u)
{
    switch (u) {
    case 'i': return 240;
    case 'c': return 94;
    case 'p': return 3;
    case 'P': return 40;
    case 'm': return 24;
    case 'n': return 24;
    case 'v': return 40;
    case 'u': return 1;
    case 'M': return 0;  // hundredths of an em, handled below
    case 's': return 1;
    case 'z': return 1;
    case 'f': return 1;
    }
    return -1;
}

static i64 clampnum(i64 x)
{
    return x>(i64)1<<40 ? (i64)1<<40 : x<-((i64)1<<40) ? -((i64)1<<40) : x;
}

// Multiply clamped numbers, saturating instead of overflowing.
static i64 mulnum(i64 x, i64 y)
{
    i64 lim = (i64)1<<40;
    if (y && (x>lim/(y<0?-y:y) || x<-lim/(y<0?-y:y))) {
        return (x<0) == (y<0) ? lim : -lim;
    }
    return x * y;
}

static Num evalexpr(Str s, u8 unit, i32 depth);

static Num evalterm(Str s, u8 unit, i32 depth)
{
    Num r = {0};
    s = trimleft(s);
    if (!s.len || depth>32) {
        r.rest = s;
        return r;
    }
    u8 ch = s.data[0];
    if (ch == '(') {
        r = evalexpr(cuthead(s, 1), unit, depth+1);
        if (r.ok) {
            Str t = trimleft(r.rest);
            if (t.len && t.data[0]==')') {
                t = cuthead(t, 1);
            }
            r.rest = t;
        }
        return r;
    }
    if (ch=='-' || ch=='+') {
        r = evalterm(cuthead(s, 1), unit, depth+1);
        if (ch == '-') r.value = -r.value;
        return r;
    }
    if (ch == '|') {
        return evalterm(cuthead(s, 1), unit, depth+1);
    }

    // Number with optional fraction and unit
    i64 whole = 0, frac = 0, scale = 1;
    iz i = 0;
    b32 any = 0;
    for (; i<s.len && digit(s.data[i]); i++) {
        whole = clampnum(whole*10 + s.data[i] - '0');
        any = 1;
    }
    if (i<s.len && s.data[i]=='.') {
        for (i++; i<s.len && digit(s.data[i]); i++) {
            if (scale < 1000000) {
                frac = frac*10 + s.data[i] - '0';
                scale *= 10;
            }
            any = 1;
        }
    }
    if (!any) {
        r.rest = s;
        return r;
    }
    u8 u = unit;
    if (i<s.len && unitscale(s.data[i])>=0) {
        u = s.data[i++];
    }
    i64 m = unitscale(u);
    if (u == 'M') {
        r.value = (whole*24 + frac*24/scale) / 100;
    } else if (u == 'p') {
        r.value = (whole*240 + frac*240/scale) / 72;
    } else if (u == 'c') {
        r.value = (whole*24000 + frac*24000/scale) / 254;
    } else {
        r.value = whole*m + frac*m/scale;
    }
    r.value = clampnum(r.value);
    r.rest  = cuthead(s, i);
    r.ok    = 1;
    return r;
}

// Evaluate left to right without precedence, as roff does.
static Num evalexpr(Str s, u8 unit, i32 depth)
{
    Num r = evalterm(s, unit, depth);
    while (r.ok) {
        Str t = r.rest;
        if (!t.len) {
            break;
        }
        static Str ops[] = {
            STR("<="), STR(">="), STR("=="), STR("<>"), STR(">?"), STR("<?"),
            STR("+"), STR("-"), STR("*"), STR("/"), STR("%"), STR("<"),
            STR(">"), STR("="), STR("&"), STR(":"),
        };
        i32 op = -1;
        for (i32 i = 0; i < countof(ops); i++) {
            if (startswith(t, ops[i])) {
                op = i;
                break;
            }
        }
        if (op < 0) {
            break;
        }
        Num b = evalterm(cuthead(t, ops[op].len), unit, depth);
        if (!b.ok) {
            break;
        }
        i64 x = r.value, y = b.value;
        switch (op) {
        case  0: x = x <= y; break;
        case  1: x = x >= y; break;
        case  2: x = x == y; break;
        case  3: x = x != y; break;
        case  4: x = x>y ? x : y; break;
        case  5: x = x<y ? x : y; break;
        case  6: x = x + y; break;
        case  7: x = x - y; break;
        case  8: x = mulnum(x, y); break;
        case  9: x = y ? x / y : 0; break;
        case 10: x = y ? x % y : 0; break;
        case 11: x = x < y; break;
        case 12: x = x > y; break;
        case 13: x = x == y; break;
        case 14: x = x>0 && y>0; break;
        case 15: x = x>0 || y>0; break;
        }
        r.value = clampnum(x);
        r.rest  = b.rest;
    }
    return r;
}

typedef struct {
    i32 value;
    b32 rel;    // relative: -1, 0, +1
    b32 ok;
} Scaled;

// Parse a request argument with an optional leading sign.
static Scaled scaled(Str s, u8 unit)
{
    Scaled r = {0};
    s = trim(s);
    if (!s.len) {
        return r;
    }
    if (s.data[0]=='+' || s.data[0]=='-') {
        r.rel = s.data[0]=='+' ? 1 : -1;
        s = cuthead(s, 1);
    }
    Num n = evalexpr(s, unit, 0);
    if (!n.ok) {
        return r;
    }
    i64 v = n.value * (r.rel<0 ? -1 : 1);
    r.value = (i32)(v<-(1<<24) ? -(1<<24) : v>1<<24 ? 1<<24 : v);
    r.ok = 1;
    return r;
}

// Horizontal units to columns, rounded like groff.
static i32 hcols(i64 u)
{
    i64 r = u<0 ? -((-u + 11)/U_PER_N) : (u + 11)/U_PER_N;
    return (i32)(r<-(1<<20) ? -(1<<20) : r>1<<20 ? 1<<20 : r);
}

// Vertical units to lines, rounded like groff. Huge spaces are capped.
static i32 vlines(i64 u)
{
    i64 r = u<0 ? -((-u + 19)/U_PER_V) : (u + 19)/U_PER_V;
    return (i32)(r<-MAXV ? -MAXV : r>MAXV ? MAXV : r);
}

// Parse a man(7) indentation argument (default unit: n). Like groff's
// an-margin and an-prevailing-indent, the result stays in basic units,
// unrounded, so that fractional indents accumulate exactly.
static i32 manunits(Str s, i32 dflt)
{
    Scaled v = scaled(s, 'n');
    if (!v.ok) {
        return dflt;
    }
    i32 max = 200*U_PER_N;
    return v.value<0 ? 0 : v.value>max ? max : v.value;
}


// Text tokenizer

static i32 parsefont(Ctx *c, Str name)
{
    if (!name.len || equals(name, S("P"))) {
        return c->prevfont;
    }
    if (name.len==1 && digit(name.data[0])) {
        i32 n = name.data[0] - '0';
        return n==2 ? F_I : n==3 ? F_B : n==4 ? F_BI : F_R;
    }
    b32 b = 0, i = 0;
    for (iz k = 0; k < name.len; k++) {
        b |= name.data[k] == 'B';
        i |= name.data[k] == 'I';
    }
    if (equals(name, S("BI")) || (b && i)) return F_BI;
    if (b) return F_B;
    if (i) return F_I;
    return F_R;
}

static void setfont(Ctx *c, i32 font)
{
    c->prevfont = c->font;
    c->font = font;
}

static Str glyphtext(Ctx *c, Str name, Arena *a)
{
    Entry *e = find(c->glyphmap, name);
    if (e) {
        return c->ts->utf8 ? e->str : e->alt;
    }
    // \[uXXXX] and \[uXXXX_YYYY], with 4 to 6 hex digits
    if (name.len>=5 && name.data[0]=='u') {
        i32 cp = 0;
        iz  i  = 1;
        for (; i<name.len && i<7; i++) {
            u8 h = name.data[i];
            i32 v = digit(h) ? h-'0' : h>='A'&&h<='F' ? h-'A'+10 :
                    h>='a'&&h<='f' ? h-'a'+10 : -1;
            if (v < 0) break;
            cp = cp<<4 | v;
        }
        if (i>=5 && (i==name.len || name.data[i]=='_')) {
            Str s = encode(a, cp);
            if (!c->ts->utf8 && cp>=0x80) {
                Entry *t = find(c->asciimap, s);
                return t ? t->str : S("?");
            }
            return s;
        }
    }
    if (startswith(name, S("char"))) {
        i32 cp = 0;
        for (iz i = 4; i<name.len && digit(name.data[i]) && cp<1000; i++) {
            cp = cp*10 + name.data[i] - '0';
        }
        if (cp>=32 && cp<127) {
            return encode(a, cp);
        }
    }
    warnf(c, S("unknown glyph"), name);
    return (Str){0};
}

typedef struct {
    Ctx *c;
    i32  width;
    b32  measure;
    i32  part;      // first token of a title part, where its columns start
} Sink;

// Accumulate a measured width, saturating so that it cannot overflow.
static i32 measured(i32 w, i64 n)
{
    i64 r = w + n;
    return (i32)(r<-(1<<20) ? -(1<<20) : r>1<<20 ? 1<<20 : r);
}

static void sinkglyph(Sink *k, Str s, u8 flags)
{
    if (k->measure) {
        k->width = measured(k->width, strwidth(s));
        return;
    }
    Ctx *c = k->c;
    if (c->trmap) {
        Entry *e = find(c->trmap, s);
        if (e) s = e->str;
    }
    tsglyph(c->ts, s, c->font, flags);
}

static void sinkspace(Sink *k, i32 n)
{
    if (k->measure) {
        k->width = measured(k->width, n);
    } else {
        tsspace(k->c->ts, n);
    }
}

static void sinkmotion(Sink *k, i32 n)
{
    if (k->measure) {
        k->width = measured(k->width, n);
    } else {
        tsmotion(k->c->ts, n);
    }
}

// Current output column, for tabs.
static i32 sinkcol(Sink *k)
{
    if (k->measure) {
        return k->width;
    }
    Typeset *ts = k->c->ts;
    return tsstart(ts) + tswidth(ts, k->part, ts->ntoks);
}

static void sinktab(Sink *k)
{
    Typeset *ts = k->c->ts;
    i32 col = sinkcol(k);
    i32 rel = col - (k->measure ? 0 : ts->indent);
    i32 next = -1;
    for (i32 i = 0; i < ts->ntabs; i++) {
        if (ts->tabs[i] > rel) {
            next = ts->tabs[i];
            break;
        }
    }
    if (next < 0) {
        i32 last = ts->ntabs ? ts->tabs[ts->ntabs-1] : 0;
        if (ts->ntabs && rel>=last) {
            next = rel + 1;  // past explicit stops
        } else {
            next = (rel/ts->tabw + 1) * ts->tabw;
        }
    }
    sinkmotion(k, next - rel);
}

static b32 isbreakglyph(Str s)
{
    return equals(s, S("-")) || equals(s, S("\xe2\x80\x90")) ||
           equals(s, S("\xe2\x80\x94"));
}

// Interpret escapes in a text line and send glyphs to the sink.
// Returns true if the line ends with \c (continuation).
static b32 tokenize(Ctx *c, Str s, Sink *k, Arena scratch)
{
    b32 contin = 0;
    i32 i = 0;
    while (i < s.len) {
        contin = 0;
        u8 ch = s.data[i];
        if (ch == ' ') {
            i32 n = 0;
            for (; i<s.len && s.data[i]==' '; i++, n++) {}
            sinkspace(k, n);
            continue;
        }
        if (ch == '\t') {
            i++;
            sinktab(k);
            continue;
        }
        if (ch != '\\') {
            Rune r = decode(cuthead(s, i));
            Str g = {s.data+i, r.len};
            if (r.cp==0xfffd && r.len==1 && s.data[i]!=0xef) {
                g = S("?");
            }
            if (!c->ts->utf8 && r.cp>=0x80) {
                Entry *e = find(c->asciimap, g);
                g = e ? e->str : S("?");
            }
            if (r.cp >= 0x20) {
                sinkglyph(k, g, ch=='-' && !c->nobreak ? TF_BREAK : 0);
            }
            i += r.len;
            continue;
        }

        // Escapes
        if (i+1 == s.len) {
            i++;
            continue;
        }
        u8 e = s.data[i+1];
        Str rest = cuthead(s, i+2);
        i += 2;
        switch (e) {
        case '\\':
        case 'e':
        case 'E':
            sinkglyph(k, S("\\"), 0);
            break;
        case '-':
            sinkglyph(k, S("-"), 0);
            break;
        case '.':
            sinkglyph(k, S("."), 0);
            break;
        case '\'':
            sinkglyph(k, c->ts->utf8 ? S("\xc2\xb4") : S("'"), 0);
            break;
        case '`':
            sinkglyph(k, S("`"), 0);
            break;
        case '&':
        case ')':
            if (!k->measure) tszero(c->ts);
            break;
        case '%':
            // Discretionary hyphen, except at the start of a word
            if (!k->measure) {
                Typeset *ts = c->ts;
                if (hasglyphs(ts, ts->word, ts->ntoks)) {
                    tszero(ts);
                    ts->toks[ts->ntoks-1].flags = TF_BREAK | TF_HYPH;
                }
            }
            break;
        case '|': case '^': case ',': case '/':
        case '{': case '}':
            break;
        case ':':
            // Break point without a hyphen
            if (!k->measure) {
                tszero(c->ts);
                c->ts->toks[c->ts->ntoks-1].flags = TF_BREAK;
            }
            break;
        case ' ': case '~': case '0':
            if (k->measure) {
                k->width = measured(k->width, 1);
            } else {
                tsglyph(c->ts, S(" "), c->font, 0);
            }
            break;
        case 'c':
            contin = i == s.len;
            break;
        case 't':
        case 'a':
            sinktab(k);
            break;
        case 'p':
            break;

        case 'f': {
            Arg n = escname(rest);
            if (n.ok) {
                if (n.name.len==0 && rest.len && rest.data[0]=='[') {
                    setfont(c, c->prevfont);
                } else {
                    setfont(c, parsefont(c, n.name));
                }
                i = (i32)(n.rest.data - s.data);
            }
        } break;

        case '(':
        case '[': {
            Arg n = escname(cuthead(s, i-1));
            if (n.ok) {
                Str g = glyphtext(c, n.name, &scratch);
                if (g.len) {
                    sinkglyph(k, g, isbreakglyph(g) ? TF_BREAK : 0);
                }
                i = (i32)(n.rest.data - s.data);
            }
        } break;

        case 'C':
        case 'N': {
            Arg d = escdelim(rest);
            if (d.ok) {
                Str g = {0};
                if (e == 'C') {
                    g = glyphtext(c, d.name, &scratch);
                } else {
                    i32 cp = 0;
                    for (iz j = 0; j<d.name.len && digit(d.name.data[j]) && cp<0x110000; j++) {
                        cp = cp*10 + d.name.data[j] - '0';
                    }
                    if (cp >= 32) {
                        Str t = encode(&scratch, cp);
                        g = c->ts->utf8||cp<0x80 ? t : S("?");
                    }
                }
                if (g.len) {
                    sinkglyph(k, g, 0);
                }
                i = (i32)(d.rest.data - s.data);
            }
        } break;

        case 'h': {
            Arg d = escdelim(rest);
            if (d.ok) {
                Str arg = d.name;
                b32 abs = arg.len && arg.data[0]=='|';
                Num n = evalexpr(abs ? cuthead(arg, 1) : arg, 'm', 0);
                if (n.ok) {
                    i32 cols = hcols(n.value);
                    if (abs) {
                        cols -= sinkcol(k) - (k->measure ? 0 : tsstart(c->ts));
                    }
                    sinkmotion(k, cols);
                }
                i = (i32)(d.rest.data - s.data);
            }
        } break;

        case 'z': {
            // Zero-width glyph: the next character does not advance
            if (i < s.len && s.data[i] != '\\') {
                Rune r = decode(cuthead(s, i));
                if (!k->measure) {
                    tsglyph(c->ts, (Str){s.data+i, r.len}, c->font, TF_ZERO);
                }
                i += r.len;
            }
        } break;

        case 'o': {
            // Overstrike: approximate with the last character
            Arg d = escdelim(rest);
            if (d.ok) {
                Str t = d.name;
                if (t.len) {
                    iz j = t.len - 1;
                    for (; j>0 && (t.data[j]&0xc0)==0x80; j--) {}
                    sinkglyph(k, cuthead(t, j), 0);
                }
                i = (i32)(d.rest.data - s.data);
            }
        } break;

        case 's': {
            // Size changes are ignored: \s-1 \s+2 \s0 \s(12 \s[12] \s'12'
            iz j = i;
            if (j<s.len && (s.data[j]=='+' || s.data[j]=='-')) j++;
            if (j < s.len) {
                u8 d = s.data[j];
                if (d == '(') {
                    j += 3;
                } else if (d == '[') {
                    for (; j<s.len && s.data[j]!=']'; j++) {}
                    j++;
                } else if (d=='\'' || d=='"') {
                    Arg a = escdelim(cuthead(s, j));
                    j = a.rest.data - s.data;
                } else if (digit(d)) {
                    j++;
                    if (j<s.len && digit(s.data[j]) && s.data[j-1]>='1' && s.data[j-1]<='3') {
                        j++;
                    }
                }
            }
            i = (i32)(j<s.len ? j : s.len);
        } break;

        case 'k': {
            Arg n = escname(rest);
            if (n.ok) {
                if (!k->measure) {
                    Typeset *ts = c->ts;
                    setreg(c, n.name, clampi32((i64)tswidth(ts, 0, ts->ntoks)*U_PER_N));
                }
                i = (i32)(n.rest.data - s.data);
            }
        } break;

        case 'v': case 'X': case 'Z': case 'D': case 'l': case 'L':
        case 'b': case 'x': case 'R': case 'A': case 'B': case 'S':
        case 'H': {
            // Not representable on a terminal: dropped
            Arg d = escdelim(rest);
            if (d.ok) {
                i = (i32)(d.rest.data - s.data);
            }
        } break;

        case 'm': case 'M': case 'g': case 'V': case 'Y': case 'F': case 'j': {
            Arg n = escname(rest);
            if (n.ok) {
                i = (i32)(n.rest.data - s.data);
            }
        } break;

        case 'u': case 'd': case 'r':
            break;

        case '*': case 'n': case '$': case 'w': case '"': case '#':
            // Already expanded; leftovers are malformed
            break;

        default: {
            // Unknown escape: print the character
            Rune r = decode(cuthead(s, i-1));
            warnf(c, S("unknown escape"), (Str){s.data+i-1, r.len});
            sinkglyph(k, (Str){s.data+i-1, r.len}, 0);
            i += r.len - 1;
        } break;
        }
    }
    return contin;
}

static i32 textwidth(Ctx *c, Str s, Arena scratch)
{
    Sink k = {0};
    k.c = c;
    k.measure = 1;
    i32 font = c->font, prev = c->prevfont;
    tokenize(c, s, &k, scratch);
    c->font = font;
    c->prevfont = prev;
    return k.width;
}


// man(7)

static void processline(Ctx *c, Str line, Arena scratch, b32 expanded, i32 depth);
static void firetrap(Ctx *c);
static void resetadjust(Ctx *c);
static Str  joinargs(Str *args, i32 nargs, Arena *a);
static void mdheader(Ctx *c, Arena scratch);
static void mdfooter(Ctx *c, Arena scratch);

static void textline(Ctx *c, Str s, Arena scratch)
{
    Typeset *ts = c->ts;
    if (c->mdoc && !c->page) {
        mdheader(c, scratch);
    }
    if (c->mdnospace && !c->cont) {
        tsunspace(ts);  // .Sm off joins text lines too
    }
    c->mdnospace = c->mdsmoff;  // a text word uses up a no-space request
    c->mdeos     = 0;
    c->mdsynprev = D_NO;        // in SYNOPSIS, text is like a .No line
    if (!s.len && !c->cont) {
        tsvspace(ts, 1);  // blank line
        return;
    }

    // Trailing spaces are not significant, unless escaped
    iz end = s.len;
    for (; end>0 && s.data[end-1]==' '; end--) {}
    if (end<s.len && oddbackslashes(takehead(s, end))) {
        end++;
    }
    s = takehead(s, end);

    b32 brk = ts->center>0 || !ts->fill;
    if (ts->fill && !c->cont && (s.data[0]==' ' || s.data[0]=='\t')) {
        tsbreak(ts);
        iz n = 0;
        for (; n<s.len && s.data[n]==' '; n++) {}
        if (n) {
            tsmotion(ts, (i32)n);
            s = cuthead(s, n);
        }
    }

    Sink k = {0};
    k.c = c;
    b32 contin = tokenize(c, s, &k, scratch);
    c->cont = contin;
    if (contin) {
        return;
    }

    if (brk) {
        b32 empty = tsempty(ts);
        tsbreak(ts);
        if (empty) {
            // An empty no-fill line still produces a blank line
            ts->vspace++;
        }
    } else {
        tsendline(ts);
    }
    firetrap(c);
}

static void setroman(Ctx *c)
{
    c->font     = F_R;
    c->prevfont = F_R;
}

// Write a paragraph tag (an-write-paragraph-tag): on the same line as
// the body if it fits within the prevailing indent, else on its own.
static void finishtag(Ctx *c)
{
    Typeset *ts = c->ts;
    tscommit(ts);
    i32 w    = tspending(ts);
    b32 any  = hasglyphs(ts, 0, ts->ntoks);
    b32 fits = any && ts->lines==c->taglines && (i64)(w+1)*U_PER_N<=c->pindent;
    if (fits) {
        tsfreeze(ts);
        tsmotion(ts, hcols(c->margin + c->pindent) - hcols(c->margin) - w);
        ts->word = ts->ntoks;
    } else if (any) {
        tsbreak(ts);
    } else {
        // An empty tag that does not fit still occupies a line
        ts->ntoks = ts->nbytes = ts->word = 0;
        if (c->pindent < U_PER_N) {
            clearrow(ts, 1);
            emitrow(ts, 0);
        }
    }
    ts->indent  = hcols(c->margin + c->pindent);
    ts->tindent = fits ? hcols(c->margin) : -1;
    resetadjust(c);
}

static void firetrap(Ctx *c)
{
    i32 trap = c->trap;
    if (!trap) {
        return;
    }
    c->trap = 0;
    setroman(c);
    if (trap & TRAP_HEAD) {
        tsbreak(c->ts);
        c->ts->nospace = 1;
    }
    if (trap & TRAP_TAG) {
        finishtag(c);
    }
}

// Paragraph spacing, then no-space mode (an-break-paragraph).
static void breakpara(Ctx *c)
{
    setroman(c);
    tsvspace(c->ts, c->pd);
    c->ts->nospace = 1;
}

static void resetmargin(Ctx *c)
{
    c->rsdepth = 0;
    c->margin  = IN;
    c->pindent = IN;
}

static void endpage(Ctx *c, Arena scratch)
{
    if (!c->page) {
        return;
    }
    if (c->mdoc) {
        mdfooter(c, scratch);
        return;
    }
    Typeset *ts = c->ts;
    tsbreak(ts);
    ts->vspace  = 1;
    ts->nospace = 0;
    ts->ntabs   = 0;  // titles use the default tab stops

    // Footer: source, date, title(section)
    i32 font = c->font, prev = c->prevfont;
    b32 fill = ts->fill;
    ts->fill = 0;  // titles never wrap
    Sink k = {0};
    k.c = c;
    c->font = F_R;
    tokenize(c, c->source, &k, scratch);
    i32 left = k.part = ts->ntoks;
    tokenize(c, c->date, &k, scratch);
    i32 center = k.part = ts->ntoks;
    c->font = F_I;
    tokenize(c, c->title, &k, scratch);
    c->font = F_R;
    tokenize(c, S("("), &k, scratch);
    tokenize(c, c->section, &k, scratch);
    tokenize(c, S(")"), &k, scratch);
    tstitle(ts, left, center);
    ts->fill = fill;
    c->font = font;
    c->prevfont = prev;
    c->page = 0;
}

static Str defaultmanual(Str section)
{
    if (section.len != 1) return S("");
    switch (section.data[0]) {
    case '1': return S("General Commands Manual");
    case '2': return S("System Calls Manual");
    case '3': return S("Library Functions Manual");
    case '4': return S("Kernel Interfaces Manual");
    case '5': return S("File Formats Manual");
    case '6': return S("Games Manual");
    case '7': return S("Miscellaneous Information Manual");
    case '8': return S("System Manager's Manual");
    case '9': return S("Kernel Developer's Manual");
    }
    return S("");
}

static b32 hasbackslash(Str s)
{
    for (iz i = 0; i < s.len; i++) {
        if (s.data[i] == '\\') return 1;
    }
    return 0;
}

// Length of the copy-mode character at the start of a string, where an
// escape like \- is one character but \fB is three.
static iz charlen(Str s)
{
    b32 one = s.len>1 && s.data[0]=='\\' &&
              cut(S("-&e ~|^:%'`_)c\\.!?{}Eat"), s.data[1]).ok;
    return one ? 2 : 1;
}

// Offset of a copy-mode character in a string.
static iz charoff(Str s, iz n)
{
    iz i = 0;
    for (; n>0 && i<s.len; n--) {
        i += charlen(cuthead(s, i));
    }
    return i;
}

// Shorten an overlong page topic by removing characters from its
// middle, like groff's an*abbreviate-page-topic, including its left to
// right evaluation of "m2 <? len - 1". It counts copy-mode characters
// and measures the formatted header, where tabs depend on position.
// Each round keeps a head and tail of the previous round's, so the
// result is a head and tail of the topic joined by "...".
static Str abbrevtopic(Ctx *c, Str topic, Arena scratch)
{
    iz n = 0;
    for (iz i = 0; i < topic.len; n++) {
        i += charlen(cuthead(topic, i));
    }

    Str sect = concat(&scratch, S("("), c->section);
    sect = concat(&scratch, sect, S(")"));
    i32 rest  = 2*textwidth(c, sect, scratch) + textwidth(c, c->manual, scratch);
    b32 tabs  = cut(topic, '\t').ok || cut(sect, '\t').ok || cut(c->manual, '\t').ok;
    b32 plain = (!tabs && !hasbackslash(topic)) || n>MAXW;  // width is length
    iz  len   = n;  // without the "..."
    iz  head  = n;
    iz  tail  = 0;
    iz  dots  = 0;
    for (;;) {
        i64 w = 2*(head + dots + tail) + rest;
        if (!plain) {
            Arena tmp = scratch;
            Str r = concat(&tmp, takehead(topic, charoff(topic, head)), dots ? S("...") : S(""));
            r = concat(&tmp, r, cuthead(topic, charoff(topic, n-tail)));
            r = concat(&tmp, r, sect);
            w = textwidth(c, concat(&tmp, concat(&tmp, r, c->manual), r), tmp);
        }
        if (w < c->ts->ll) break;
        iz m1   = len/2 - 2;
        iz m2   = len/2 + 2;
        iz pend = m1>1 ? m1 : 1;
        iz sbeg = (m2<len ? m2 : len) - 1;
        if (pend+1>len || sbeg<0 || sbeg>len) break;
        head = pend + 1;
        tail = len - sbeg;
        dots = 3;
        if (head+tail >= len) break;
        len = head + tail;
    }
    if (!dots) {
        return topic;
    }
    Str r = concat(&c->perm, takehead(topic, charoff(topic, head)), S("..."));
    return concat(&c->perm, r, cuthead(topic, charoff(topic, n-tail)));
}

// Truncate an overlong inner footer with an ellipsis, like groff.
static Str abbrevfooter(Ctx *c, Str foot, Arena scratch)
{
    i64 halftitle = c->ts->ll*U_PER_N/2;
    i64 halfcfoot = (i64)textwidth(c, c->date, scratch)*U_PER_N/2;
    i64 width = (i64)textwidth(c, foot, scratch)*U_PER_N + halfcfoot;
    if (width<halftitle || hasbackslash(foot)) {
        return foot;
    }
    width = ((i64)foot.len + 3)*U_PER_N + halfcfoot;
    Str r = foot;
    for (iz end = foot.len-1; width>=halftitle && end>0; end--) {
        i64 nw = ((i64)end + 3)*U_PER_N + halfcfoot;
        if (nw >= width) break;
        width = nw;
        r = takehead(foot, end);
    }
    return concat(&c->perm, r, S("..."));
}

static void manth(Ctx *c, Str *args, i32 nargs, Arena scratch)
{
    Typeset *ts = c->ts;
    endpage(c, scratch);
    tsbreak(ts);
    Arena *perm = &c->perm;
    c->title   = clone(perm, nargs>0 ? args[0] : S(""));
    c->section = clone(perm, nargs>1 ? args[1] : S(""));
    c->date    = clone(perm, nargs>2 ? args[2] : S(""));
    c->source  = clone(perm, nargs>3 ? args[3] : S(""));
    c->manual  = clone(perm, nargs>4 ? args[4] : defaultmanual(c->section));
    c->page    = 1;
    ts->ntabs  = 0;  // titles use the default tab stops
    c->title   = abbrevtopic(c, c->title, scratch);
    c->source  = abbrevfooter(c, c->source, scratch);

    // Header: title(section), manual, title(section)
    ts->fill = 0;  // titles never wrap
    Sink k = {0};
    k.c = c;
    c->font = F_I;
    tokenize(c, c->title, &k, scratch);
    c->font = F_R;
    tokenize(c, S("("), &k, scratch);
    tokenize(c, c->section, &k, scratch);
    tokenize(c, S(")"), &k, scratch);
    i32 left = k.part = ts->ntoks;
    tokenize(c, c->manual, &k, scratch);
    i32 center = k.part = ts->ntoks;
    c->font = F_I;
    tokenize(c, c->title, &k, scratch);
    c->font = F_R;
    tokenize(c, S("("), &k, scratch);
    tokenize(c, c->section, &k, scratch);
    tokenize(c, S(")"), &k, scratch);
    tstitle(ts, left, center);
    printu8(ts->out, '\n');

    resetmargin(c);
    setroman(c);
    c->pd       = 1;
    c->trap     = 0;
    c->example  = 0;
    c->sy       = 0;
    ts->fill    = 1;
    ts->indent  = hcols(c->margin);
    ts->tindent = -1;
    resetadjust(c);
    ts->nospace = 1;
    ts->vspace  = 0;
}

static void manheading(Ctx *c, i32 kind, Str *args, i32 nargs, Arena scratch)
{
    Typeset *ts = c->ts;
    breakpara(c);
    resetmargin(c);
    ts->fill    = 1;
    ts->indent  = hcols(c->margin);
    ts->tindent = kind==M_SH ? 0 : SN;
    c->trap |= TRAP_HEAD;
    setfont(c, F_B);
    if (nargs) {
        Str text = concat(&scratch, S("\\&"), joinargs(args, nargs, &scratch));
        textline(c, text, scratch);
    }
}

static Str joinargs(Str *args, i32 nargs, Arena *a)
{
    Str r = {0};
    for (i32 i = 0; i < nargs; i++) {
        if (i) r = concat(a, r, S(" "));
        r = concat(a, r, args[i]);
    }
    if (!r.data) r = S("");
    return r;
}

static Str fontesc(i32 font)
{
    switch (font) {
    case F_B:  return S("\\fB");
    case F_I:  return S("\\fI");
    case F_BI: return S("\\f(BI");
    }
    return S("\\fR");
}

static void manfont(Ctx *c, i32 kind, Str *args, i32 nargs, Arena scratch)
{
    i32 a = F_R, b = F_R;
    switch (kind) {
    case M_B:  case M_SB: a = F_B; break;
    case M_I:  a = F_I; break;
    case M_SM: a = F_R; break;
    case M_BI: a = F_B; b = F_I; break;
    case M_BR: a = F_B; b = F_R; break;
    case M_IB: a = F_I; b = F_B; break;
    case M_IR: a = F_I; b = F_R; break;
    case M_RB: a = F_R; b = F_B; break;
    case M_RI: a = F_R; b = F_I; break;
    }

    b32 alternating = kind!=M_B && kind!=M_I && kind!=M_SM && kind!=M_SB;
    if (!alternating) {
        // Arguments, or else the next text line, then back to roman
        c->trap |= TRAP_FONT;
        if (kind != M_SM) {
            setfont(c, a);
        }
        if (nargs) {
            Str text = concat(&scratch, S("\\&"), joinargs(args, nargs, &scratch));
            textline(c, text, scratch);
        }
        return;
    }
    if (!nargs) {
        return;
    }

    Str text = S("\\&");
    for (i32 i = 0; i < nargs; i++) {
        text = concat(&scratch, text, fontesc(i&1 ? b : a));
        text = concat(&scratch, text, args[i]);
    }
    textline(c, text, scratch);
    setroman(c);
}

// The AD string sets adjustment at .TH and after each paragraph tag.
static void resetadjust(Ctx *c)
{
    Str ad = getstring(c, S("AD"));
    c->ts->adjust = ad.len && ad.data[0]=='l' ? AD_L :
                    ad.len && ad.data[0]=='c' ? AD_C :
                    ad.len && ad.data[0]=='r' ? AD_R : AD_B;
}

// Begin a tagged paragraph: the next text line is the tag.
static void mantag(Ctx *c, Str width)
{
    Typeset *ts = c->ts;
    breakpara(c);
    if (width.data) {
        c->pindent = manunits(width, c->pindent);
    }
    ts->indent  = hcols(c->margin);
    ts->tindent = -1;
    ts->adjust  = AD_L;  // tags are not adjusted
    c->taglines = ts->lines;
    c->trap |= TRAP_TAG;
}

static void manmacro(Ctx *c, i32 kind, Str *args, i32 nargs, Arena scratch)
{
    Typeset *ts = c->ts;
    switch (kind) {
    case M_TH:
        manth(c, args, nargs, scratch);
        break;

    case M_SH:
    case M_SS:
        manheading(c, kind, args, nargs, scratch);
        break;

    case M_PP:
        breakpara(c);
        ts->indent  = hcols(c->margin);
        ts->tindent = -1;
        c->pindent  = IN;
        break;

    case M_TP:
        mantag(c, nargs ? args[0] : (Str){0});
        break;

    case M_TQ:
        tsbreak(ts);
        ts->nospace = 1;
        mantag(c, nargs ? args[0] : (Str){0});
        break;

    case M_IP:
        if (!nargs) {
            breakpara(c);
            ts->indent  = hcols(c->margin + c->pindent);
            ts->tindent = -1;
            break;
        }
        mantag(c, nargs>1 ? args[1] : (Str){0});
        textline(c, concat(&scratch, S("\\&"), args[0]), scratch);
        break;

    case M_HP:
        breakpara(c);
        if (nargs) c->pindent = manunits(args[0], c->pindent);
        ts->indent  = hcols(c->margin + c->pindent);
        ts->tindent = hcols(c->margin);
        break;

    case M_RS: {
        tsbreak(ts);
        if (c->rsdepth < MAXRS) {
            c->rsmargin[c->rsdepth]  = c->margin;
            c->rspindent[c->rsdepth] = c->pindent;
            c->rsdepth++;
        }
        // Signed, like groff's ".nr an-margin +(n;\$1)": ".RS -4" outdents
        i64 m = c->margin + (i64)c->pindent;
        if (nargs) {
            Num v = evalexpr(args[0], 'n', 0);
            m = c->margin + (v.ok ? v.value : 0);
        }
        c->margin   = (i32)(m<0 ? 0 : m>MAXIND*U_PER_N ? MAXIND*U_PER_N : m);
        c->pindent  = IN;
        ts->indent  = hcols(c->margin);
        ts->tindent = -1;
    } break;

    case M_RE: {
        // Inset levels count from 1; level 1 has no saved state
        tsbreak(ts);
        i32 level = c->rsdepth;  // target level, 0-based
        if (nargs) {
            Scaled v = scaled(args[0], 'u');
            if (v.ok && v.value-1 < level) level = v.value - 1;
        } else {
            level--;
        }
        if (level>=0 && level<c->rsdepth) {
            c->margin  = c->rsmargin[level];
            c->pindent = c->rspindent[level];
            c->rsdepth = level;
        }
        ts->indent  = hcols(c->margin);
        ts->tindent = -1;
    } break;

    case M_PD:
        if (nargs) {
            Scaled v = scaled(args[0], 'v');
            c->pd = v.ok ? vlines(v.value) : 1;
            if (c->pd < 0) c->pd = 0;
        } else {
            c->pd = 1;
        }
        break;

    case M_B: case M_I: case M_SM: case M_SB: case M_BI: case M_BR:
    case M_IB: case M_IR: case M_RB: case M_RI:
        manfont(c, kind, args, nargs, scratch);
        break;

    case M_UR:
    case M_MT:
        c->url   = clone(&c->perm, nargs ? args[0] : S(""));
        c->inurl = 1;
        break;

    case M_UE:
    case M_ME: {
        // Show the address in angle brackets after any link text
        Str text = {0};
        if (c->inurl) {
            text = concat(&scratch, S("\\(la"), c->url);
            text = concat(&scratch, text, S("\\(ra"));
        }
        if (nargs) {
            text = concat(&scratch, text, args[0]);
        }
        c->inurl = 0;
        if (text.len) {
            textline(c, text, scratch);
        }
    } break;

    case M_EX:
        tsbreak(ts);
        if (!c->example) {
            c->example = 1;
            c->expd    = c->pd;
            c->exfont  = c->font;
            c->pd      = 1;
            ts->fill   = 0;
            setroman(c);
        }
        break;

    case M_EE:
        tsbreak(ts);
        if (c->example) {
            c->example = 0;
            c->pd      = c->expd;
            c->font    = c->exfont;
            ts->fill   = 1;
        }
        break;

    case M_SY: {
        if (!c->sy) {
            breakpara(c);
            c->sy       = 1;
            c->syindent = ts->indent;
            c->syadjust = ts->adjust;
            ts->adjust  = AD_L;
        } else {
            tsbreak(ts);
            ts->nospace = 1;
        }
        Str cmd = nargs ? args[0] : S("");
        i32 w = textwidth(c, cmd, scratch) + 1;
        ts->indent  = hcols(c->margin + w*U_PER_N);
        ts->tindent = hcols(c->margin);
        manfont(c, M_B, &cmd, 1, scratch);
    } break;

    case M_YS:
        tsbreak(ts);
        if (c->sy) {
            ts->indent = c->syindent;
            ts->adjust = c->syadjust;
            c->sy = 0;
        }
        break;

    case M_OP: {
        Str text = S("\\&[\\fB");
        if (nargs) text = concat(&scratch, text, args[0]);
        text = concat(&scratch, text, S("\\fR"));
        if (nargs > 1) {
            text = concat(&scratch, text, S("\\fI\\~"));
            text = concat(&scratch, text, args[1]);
            text = concat(&scratch, text, S("\\fR"));
        }
        text = concat(&scratch, text, S("]"));
        textline(c, text, scratch);
    } break;

    case M_MR: {
        Str text = S("\\fI");
        if (nargs) text = concat(&scratch, text, args[0]);
        text = concat(&scratch, text, S("\\fR("));
        if (nargs > 1) text = concat(&scratch, text, args[1]);
        text = concat(&scratch, text, S(")"));
        if (nargs > 2) text = concat(&scratch, text, args[2]);
        textline(c, text, scratch);
    } break;

    case M_DT:
        ts->ntabs = 0;
        break;

    case M_IGNORE:
        break;
    }
}


// Requests

typedef struct {
    Strs  args;     // args.data[0] is the macro name
    Flags quoted;   // per argument
} MacroArgs;

// Split macro arguments, honoring quotes. With cells, each tab is an
// argument of its own: "\t", or "\t " when spaces follow it, and a word
// directly beside a tab is escaped as text.
static MacroArgs macroargs(Str name, Str s, b32 cells, Arena *a)
{
    MacroArgs r = {0};
    *push(a, &r.args)   = name;
    *push(a, &r.quoted) = 0;
    b32 tab = 0;  // directly after a tab
    for (;;) {
        iz skip = 0;
        for (; skip<s.len && whitespace(s.data[skip]); skip++) {
            if (cells && s.data[skip]=='\t') break;
        }
        s = cuthead(s, skip);
        tab &= !skip;
        if (!s.len) {
            break;
        }
        Str arg = {0};
        b32 isq = s.data[0] == '"';
        if (s.data[0] == '\t') {
            arg = takehead(s, s.len>1 && s.data[1]==' ' ? 2 : 1);
            s = cuthead(s, 1);
        } else if (isq) {
            Buf b = {0};
            iz i = 1;
            for (; i < s.len; i++) {
                if (s.data[i] == '"') {
                    if (i+1<s.len && s.data[i+1]=='"') {
                        bappendu8(&b, '"', a);
                        i++;
                        continue;
                    }
                    break;
                }
                bappendu8(&b, s.data[i], a);
                if (s.data[i]=='\\' && i+1<s.len) {
                    bappendu8(&b, s.data[++i], a);
                }
            }
            arg = bstr(b);
            if (!arg.data) arg = S("");
            s = cuthead(s, i<s.len ? i+1 : i);
        } else {
            iz i = 0;
            for (; i<s.len && !whitespace(s.data[i]); i++) {
                if (s.data[i]=='\\' && i+1<s.len) i++;
            }
            arg = takehead(s, i);
            s = cuthead(s, i);
            if (cells && arg.len>1 && (tab || (s.len && s.data[0]=='\t'))) {
                arg = concat(a, S("\\&"), arg);  // like mandoc, never a macro
            }
        }
        tab = !isq && arg.data[0]=='\t';
        *push(a, &r.args)   = arg;
        *push(a, &r.quoted) = isq;
    }
    return r;
}

static Str nextword(Str *s)
{
    Str t = trimleft(*s);
    iz i = 0;
    for (; i<t.len && !whitespace(t.data[i]); i++) {}
    *s = cuthead(t, i);
    return takehead(t, i);
}

static i32 countbraces(Str s)
{
    i32 n = 0;
    for (iz i = 0; i+1 < s.len; i++) {
        if (s.data[i] == '\\') {
            if (s.data[i+1] == '{') n++;
            if (s.data[i+1] == '}') n--;
            i++;
        }
    }
    return n;
}

typedef struct {
    Str rest;
    b32 value;
} Cond;

// Prepare a string comparison operand. As in groff on a terminal, a
// change to a font that does not exist (e.g. CB) vanishes, but a real
// font change makes the strings differ.
static Str plaintext(Str s, Arena *a)
{
    Buf b = {0};
    for (iz i = 0; i < s.len; i++) {
        if (s.data[i]=='\\' && i+1<s.len) {
            u8 e = s.data[i+1];
            if (e == 'f') {
                Arg n = escname(cuthead(s, i+2));
                Str f = n.name;
                b32 real = !f.len || equals(f, S("R")) || equals(f, S("I")) ||
                           equals(f, S("B")) || equals(f, S("BI")) ||
                           equals(f, S("P")) || (f.len==1 && digit(f.data[0]));
                if (n.ok && !real) {
                    i = n.rest.data - s.data - 1;
                    continue;
                }
            } else if (e=='&' || e=='|' || e=='^') {
                i++;
                continue;
            }
        }
        bappendu8(&b, s.data[i], a);
    }
    Str r = bstr(b);
    return r.data ? r : S("");
}

static Cond evalcond(Ctx *c, Str s, Arena *a)
{
    Cond r = {0};
    s = trimleft(s);
    b32 neg = 0;
    for (; s.len && s.data[0]=='!'; s = cuthead(s, 1)) {
        neg = !neg;
    }
    if (!s.len) {
        r.rest = s;
        return r;
    }

    u8 ch = s.data[0];
    b32 term = s.len==1 || whitespace(s.data[1]) || s.data[1]=='\\';
    if (term && (ch=='n' || ch=='o')) {
        r.value = 1;
        r.rest  = cuthead(s, 1);
    } else if (term && (ch=='t' || ch=='e' || ch=='v' || ch=='h')) {
        r.value = 0;
        r.rest  = cuthead(s, 1);
    } else if ((ch=='d' || ch=='r' || ch=='c' || ch=='m' || ch=='F' || ch=='S') &&
               s.len>1 && whitespace(s.data[1])) {
        Str rest = cuthead(s, 1);
        Str name = nextword(&rest);
        r.rest = rest;
        if (ch == 'd') {
            r.value = find(c->strings, name) || find(c->macros, name) ||
                      find(c->kinds, name);
        } else if (ch == 'r') {
            r.value = find(c->regs, name) || equals(name, S("an-margin")) ||
                      (name.len && name.data[0]=='.');
        } else if (ch == 'c') {
            r.value = 1;
        } else {
            r.value = 0;
        }
    } else if (digit(ch) || ch=='(' || ch=='+' || ch=='-' || ch=='|' || ch=='.') {
        Num n = evalexpr(s, 'u', 0);
        r.value = n.ok && n.value>0;
        r.rest  = n.ok ? n.rest : s;
        // Skip any unparsed remainder of the expression
        for (; r.rest.len && !whitespace(r.rest.data[0]); r.rest = cuthead(r.rest, 1)) {
            if (r.rest.data[0] == '\\') break;
        }
    } else {
        // String comparison: 'a'b'
        u8 d = ch;
        iz i = 1, j = -1, k = -1;
        for (; i < s.len; i++) {
            if (s.data[i]=='\\' && i+1<s.len) {
                i++;
            } else if (s.data[i] == d) {
                if (j < 0) {
                    j = i;
                } else {
                    k = i;
                    break;
                }
            }
        }
        if (k < 0) {
            r.rest = (Str){s.data+s.len, 0};
            r.value = 0;
        } else {
            Str x = plaintext(span(s.data+1, s.data+j), a);
            Str y = plaintext(span(s.data+j+1, s.data+k), a);
            r.value = equals(x, y);
            r.rest  = cuthead(s, k+1);
        }
    }
    r.value ^= neg;
    return r;
}

static void pushie(Ctx *c, b32 value)
{
    c->iestack = c->iestack<<1 | (u64)!!value;
    if (c->iedepth < 64) c->iedepth++;
}

static b32 popie(Ctx *c)
{
    if (!c->iedepth) {
        return 1;
    }
    b32 r = (b32)(c->iestack & 1);
    c->iestack >>= 1;
    c->iedepth--;
    return r;
}

static void doif(Ctx *c, i32 kind, Str args, Arena scratch, i32 depth)
{
    b32 t;
    Str rest;
    if (kind == R_EL) {
        t = !popie(c);
        rest = args;
    } else {
        Cond cd = evalcond(c, args, &scratch);
        t = cd.value;
        rest = cd.rest;
        if (kind == R_IE) {
            pushie(c, t);
        }
    }
    rest = trimleft(rest);
    if (!t) {
        i32 d = countbraces(rest);
        if (d > 0) {
            c->skip = d;
        }
        return;
    }
    while (startswith(rest, S("\\{"))) {
        rest = trimleft(cuthead(rest, 2));
    }
    if (rest.len && depth<MAXDEPTH) {
        processline(c, rest, scratch, 1, depth+1);
    }
}

static b32 isterminator(Str line, Str end)
{
    if (!line.len || (line.data[0]!='.' && line.data[0]!='\'')) {
        return 0;
    }
    Str t = trimleft(cuthead(line, 1));  // ".  ." also ends a macro
    if (!startswith(t, end)) {
        return 0;
    }
    t = cuthead(t, end.len);
    return !t.len || whitespace(t.data[0]) || t.data[0]=='\\';
}

static void invoke(Ctx *c, Str name, Str body, Str argstr, Arena scratch)
{
    if (c->depth >= MAXDEPTH) {
        warnf(c, S("macro nesting too deep"), name);
        return;
    }
    // Arguments may view the input line, which does not outlive the
    // call, so copy them to the high end of perm until the frame pops
    byte *end  = c->perm.end;
    Strs  raw  = macroargs(name, argstr, 0, &scratch).args;
    Str  *args = allochigh(&c->perm, raw.len, sizeof(Str), _Alignof(Str));
    args[0] = name;
    for (iz i = 1; i < raw.len; i++) {
        Str s = copymode(raw.data[i], &scratch);
        args[i].data = allochigh(&c->perm, s.len, 1, 1);
        args[i].len  = s.len;
        if (s.len) xcpy(args[i].data, s.data, touz(s.len));
    }
    Frame *f = newframe(c);
    f->src   = body;
    f->args  = args;
    f->nargs = (i32)raw.len;
    f->name  = name;
    f->macro = 1;
    f->end   = end;
    f->next  = c->frame;
    c->frame = f;
    c->depth++;
}

static void mdocline(Ctx *c, i32 kind, Str name, Str args, Arena scratch);

static void request(Ctx *c, i32 kind, Str args, b32 nobreak, Arena scratch, i32 depth)
{
    Typeset *ts = c->ts;
    switch (kind) {
    case R_IGNORE:
        break;

    case R_BR:
        if (!nobreak) tsbreak(ts);
        break;

    case R_SP: {
        Scaled v = scaled(args, 'v');
        i32 n = v.ok ? vlines(v.value) : 1;
        if (nobreak) {
            if (n > 0 && !ts->nospace) {
                ts->vspace = ts->vspace+n>MAXV ? MAXV : ts->vspace+n;
            }
        } else {
            tsvspace(ts, n<0 ? 0 : n);
        }
    } break;

    case R_NF:
        if (!nobreak) tsbreak(ts);
        ts->fill = 0;
        break;

    case R_FI:
        if (!nobreak) tsbreak(ts);
        ts->fill = 1;
        break;

    case R_AD: {
        // Adjustment applies at the next output line
        Str a = trim(args);
        ts->adjust |= 1;
        if (a.len) {
            switch (a.data[0]) {
            case 'l': ts->adjust = AD_L; break;
            case 'r': ts->adjust = AD_R; break;
            case 'c': ts->adjust = AD_C; break;
            case 'b': case 'n': ts->adjust = AD_B; break;
            default: {
                Num n = evalexpr(a, 'u', 0);
                if (n.ok && n.value>=0 && n.value<=AD_R) {
                    ts->adjust = (i32)n.value;
                }
            } break;
            }
        }
    } break;

    case R_NA:
        ts->adjust &= ~1;
        break;

    case R_CE: {
        if (!nobreak) tsbreak(ts);
        Scaled v = scaled(args, 'u');
        ts->center = v.ok ? v.value : 1;
        if (ts->center < 0) ts->center = 0;
    } break;

    case R_IN: {
        if (!nobreak) tsbreak(ts);
        Scaled v = scaled(args, 'm');
        i32 previous = ts->indent;
        if (!v.ok) {
            ts->indent = ts->previndent;
        } else {
            ts->indent = hcols(v.rel ? ts->indent*U_PER_N + v.value : v.value);
        }
        ts->previndent = previous;
        if (ts->indent < 0) ts->indent = 0;
        if (ts->indent > 200) ts->indent = 200;
    } break;

    case R_TI: {
        if (!nobreak) tsbreak(ts);
        Scaled v = scaled(args, 'm');
        if (v.ok) {
            ts->tindent = hcols(v.rel ? ts->indent*U_PER_N + v.value : v.value);
            if (ts->tindent < 0) ts->tindent = 0;
            if (ts->tindent > 200) ts->tindent = 200;
        }
    } break;

    case R_LL: {
        Scaled v = scaled(args, 'm');
        if (v.ok) {
            i32 cols = hcols(v.value);
            i32 ll = v.rel ? ts->ll + cols : cols;
            if (ll>=10 && ll<=1000) ts->ll = ll;
        }
    } break;

    case R_TA: {
        ts->ntabs = 0;
        i32 prev = 0;
        Str rest = args;
        while (ts->ntabs < countof(ts->tabs)) {
            Str w = nextword(&rest);
            if (!w.len) break;
            if (w.data[w.len-1]=='L' || w.data[w.len-1]=='R' || w.data[w.len-1]=='C') {
                w.len--;
            }
            if (equals(w, S("T"))) continue;
            Scaled v = scaled(w, 'm');
            if (!v.ok) continue;
            i32 col = hcols(v.value);
            col = v.rel>0 ? prev + col : col;
            if (col <= prev && ts->ntabs) continue;
            ts->tabs[ts->ntabs++] = col;
            prev = col;
        }
    } break;

    case R_FT: {
        Str f = trim(args);
        setfont(c, parsefont(c, nextword(&f)));
    } break;

    case R_NS:
        ts->nospace = 1;
        break;

    case R_SS: {
        // Only the additional sentence space matters on a terminal
        Str rest = args;
        nextword(&rest);
        Str m = nextword(&rest);
        Scaled v = scaled(m, 'u');
        if (v.ok && v.value>=0 && !v.rel) ts->sentspace = v.value;
    } break;

    case R_RS:
        ts->nospace = 0;
        break;

    case R_DS: {
        Str rest = args;
        Str key = nextword(&rest);
        if (!key.len) break;
        Str v = trimleft(rest);
        if (v.len && v.data[0]=='"') v = cuthead(v, 1);
        setstring(c, key, copymode(v, &scratch));
    } break;

    case R_AS: {
        Str rest = args;
        Str key = nextword(&rest);
        if (!key.len) break;
        Str v = trimleft(rest);
        if (v.len && v.data[0]=='"') v = cuthead(v, 1);
        Str old = getstring(c, key);
        Str nv = concat(&scratch, old.data ? old : S(""), copymode(v, &scratch));
        setstring(c, key, nv);
    } break;

    case R_DE:
    case R_AM: {
        Str rest = args;
        Str key = nextword(&rest);
        Str end = nextword(&rest);
        if (!key.len) {
            c->ignoring = 1;
            c->igend = S(".");
            break;
        }
        Entry *e = lookup(&c->macros, key, &c->perm);
        c->defining = e;
        c->defend = clone(&c->perm, end.len ? end : S("."));
        c->defbody = (Buf){0};
        if (kind==R_AM && e->live) {
            bappend(&c->defbody, e->str, &c->perm);
        }
    } break;

    case R_IG: {
        Str rest = args;
        Str end = nextword(&rest);
        c->ignoring = 1;
        c->igend = clone(&c->perm, end.len ? end : S("."));
    } break;

    case R_RM: {
        Str rest = args;
        for (Str w; (w = nextword(&rest)).len;) {
            Entry *e = find(c->macros, w);
            if (e) e->live = 0;
            e = find(c->strings, w);
            if (e) e->live = 0;
        }
    } break;

    case R_RR: {
        Str rest = args;
        for (Str w; (w = nextword(&rest)).len;) {
            Entry *e = find(c->regs, w);
            if (e) e->live = 0;
        }
    } break;

    case R_RN:
    case R_ALS: {
        Str rest = args;
        Str a = nextword(&rest);
        Str b = nextword(&rest);
        Str from = kind==R_RN ? a : b;
        Str to   = kind==R_RN ? b : a;
        Entry *src = find(c->macros, from);
        Entry **map = &c->macros;
        if (!src) {
            src = find(c->strings, from);
            map = &c->strings;
        }
        if (!src || !to.len) break;
        Entry *dst = lookup(map, to, &c->perm);
        dst->str  = src->str;
        dst->live = 1;
        if (kind == R_RN) src->live = 0;
    } break;

    case R_NR: {
        Str rest = args;
        Str key = nextword(&rest);
        Str val = nextword(&rest);
        Str inc = nextword(&rest);
        if (!key.len) break;
        Scaled v = scaled(val, 'u');
        if (!v.ok) break;
        Entry *e = lookup(&c->regs, key, &c->perm);
        if (!e->live) e->num = 0;
        e->num  = v.rel ? clampi32((i64)e->num + v.value) : v.value;
        e->live = 1;
        if (inc.len) {
            Scaled iv = scaled(inc, 'u');
            if (iv.ok) e->inc = iv.value;
        }
    } break;

    case R_IF:
    case R_IE:
    case R_EL:
        doif(c, kind, args, scratch, depth);
        break;

    case R_NOP:
        if (depth < MAXDEPTH) {
            processline(c, trimleft(args), scratch, 1, depth+1);
        }
        break;

    case R_DO:
        if (depth < MAXDEPTH) {
            Str line = concat(&scratch, S("."), trimleft(args));
            processline(c, line, scratch, 1, depth+1);
        }
        break;

    case R_SHIFT: {
        Frame *f = argframe(c);
        Scaled v = scaled(args, 'u');
        i32 n = v.ok ? v.value : 1;
        if (f && n>0) {
            if (n > f->nargs-1) n = f->nargs - 1;
            for (i32 i = 1; i+n < f->nargs; i++) {
                f->args[i] = f->args[i+n];
            }
            f->nargs -= n;
        }
    } break;

    case R_SO: {
        Str path = trim(args);
        if (!path.len) break;
        Frame *file = c->frame;  // the file running this request
        for (; file && file->macro; file = file->next) {}
        i32 nest = file ? file->sonest+1 : 1;
        if (nest > MAXDEPTH) {
            warnf(c, S(".so nested too deeply"), path);
            break;
        }
        Str src = plt_load(c->plt, path, &c->perm);
        if (!src.data) {
            warnf(c, S("cannot open"), path);
            c->status = 1;
            break;
        }
        pushfile(c, clone(&c->perm, path), src);
        c->frame->sonest = nest;
    } break;

    case R_TR: {
        Str s = trim(args);
        while (s.len) {
            Str from = {0}, to = {0};
            for (i32 side = 0; side < 2; side++) {
                Str g = {0};
                if (!s.len) break;
                if (s.data[0]=='\\' && s.len>1 && (s.data[1]=='(' || s.data[1]=='[')) {
                    Arg n = escname(cuthead(s, 1));
                    if (!n.ok) { s = (Str){0}; break; }
                    g = glyphtext(c, n.name, &c->perm);
                    s = n.rest;
                } else if (s.data[0]=='\\' && s.len>1) {
                    g = takehead(cuthead(s, 1), 1);
                    s = cuthead(s, 2);
                } else {
                    Rune r = decode(s);
                    g = takehead(s, r.len);
                    s = cuthead(s, r.len);
                }
                if (side) to = g; else from = g;
            }
            if (!from.len) break;
            Entry *e = lookup(&c->trmap, from, &c->perm);
            e->str  = clone(&c->perm, to.len ? to : S(" "));
            e->live = 1;
        }
    } break;

    case R_TL: {
        if (!nobreak) tsbreak(ts);
        Str s = trim(args);
        if (!s.len) break;
        u8 d = s.data[0];
        Str parts[3] = {0};
        s = cuthead(s, 1);
        for (i32 i = 0; i < 3; i++) {
            Cut p = cut(s, d);
            parts[i] = p.head;
            s = p.tail;
        }
        b32 fill = ts->fill;
        ts->fill = 0;  // titles never wrap
        Sink k = {0};
        k.c = c;
        i32 marks[3];
        for (i32 i = 0; i < 3; i++) {
            k.part = ts->ntoks;
            tokenize(c, parts[i], &k, scratch);
            marks[i] = ts->ntoks;
        }
        tstitle(ts, marks[0], marks[1]);
        ts->fill = fill;
    } break;

    case R_TS:
        tsbreak(ts);
        c->tblactive = 1;
        c->tblsrc    = (Buf){0};
        break;

    case R_MSO:
        // Only www.tmac matters to manual pages: its link macros
        if (equals(trim(args), S("www.tmac"))) {
            static Str body = STR(
                ".ie '\\$2'' \\&\\$1\\$3\n"
                ".el \\&\\$2 <\\$1>\\$3\n"
            );
            Str names[] = {S("URL"), S("MTO")};
            for (i32 i = 0; i < countof(names); i++) {
                Entry *e = lookup(&c->macros, names[i], &c->perm);
                e->str  = body;
                e->live = 1;
            }
        }
        break;

    case R_EQ:
        c->ignoring = 1;
        c->igend = S("EN");
        break;

    case R_PS:
        c->ignoring = 1;
        c->igend = S("PE");
        break;
    }
}

static void tblline(Ctx *c, Str line, Arena scratch);

static void controlline(Ctx *c, Str line, Arena scratch, i32 depth)
{
    b32 nobreak = line.data[0] == '\'';
    Str s = cuthead(line, 1);
    s = trimleft(s);
    iz i = 0;
    for (; i<s.len && !whitespace(s.data[i]) && s.data[i]!='\\'; i++) {}
    Str name = takehead(s, i);
    Str args = cuthead(s, i);
    if (!name.len || name.data[0]=='"') {
        // Possibly ".\}", or a malformed comment
        return;
    }
    args = trimleft(args);

    Entry *m = find(c->macros, name);
    if (m) {
        invoke(c, m->key, m->str, args, scratch);
        return;
    }

    Entry *k = find(c->kinds, name);
    i32 kind = k ? k->num : K_NONE;
    if (kind>=M_TH && kind<=M_IGNORE) {
        Strs argv = macroargs(name, args, 0, &scratch).args;
        manmacro(c, kind, argv.data+1, (i32)argv.len-1, scratch);
        return;
    }
    if (kind>=D_DD && kind<=D_IGNORE) {
        if (!c->mdoc && (kind==D_DD || kind==D_DT || kind==D_OS) && !c->page) {
            c->mdoc = 1;
        }
        if (c->mdoc) {
            mdocline(c, kind, name, args, scratch);
            return;
        }
        kind = K_NONE;
    }
    if (kind == K_NONE) {
        warnf(c, S("unknown macro"), name);
        return;
    }
    request(c, kind, args, nobreak, scratch, depth);
}

static void processline(Ctx *c, Str line, Arena scratch, b32 expanded, i32 depth)
{
    if (c->ignoring) {
        if (isterminator(line, c->igend) || (equals(c->igend, S(".")) && isterminator(line, S(".")))) {
            c->ignoring = 0;
        }
        return;
    }
    if (c->defining) {
        if (isterminator(line, c->defend)) {
            Entry *e = c->defining;
            e->str  = bstr(c->defbody);
            if (!e->str.data) e->str = S("");
            e->live = 1;
            c->defining = 0;
        } else {
            bappend(&c->defbody, copymode(line, &scratch), &c->perm);
            bappendu8(&c->defbody, '\n', &c->perm);
        }
        return;
    }
    if (c->skip > 0) {
        c->skip += countbraces(line);
        if (c->skip < 0) c->skip = 0;
        return;
    }
    if (c->tblactive) {
        tblline(c, line, scratch);
        return;
    }

    if (!expanded) {
        line = expand(c, line, &scratch, 0);
    }
    if (line.len && (line.data[0]=='.' || line.data[0]=='\'')) {
        controlline(c, line, scratch, depth);
        return;
    }

    // A line holding only a block close is not text
    Str t = trim(line);
    while (startswith(t, S("\\}"))) {
        t = trimleft(cuthead(t, 2));
    }
    if (!t.len && line.len) {
        return;
    }
    textline(c, line, scratch);
}


// tbl: lines between .TS and .TE are collected, then laid out as a
// whole. Cells are formatted by the typesetter in capture mode, so
// escapes, fonts, and requests inside text blocks work as usual.

enum {
    TBL_MAXCOLS = 64,
    TBL_MAXFMTS = 256,
};

typedef struct {
    u8  key;        // l r c n a s ^ _ =
    i8  font;       // -1 to inherit
    b32 top;        // t: align multi-line rows to the top
    b32 equal;      // e: equal-width column
    i32 minw;       // w(): minimum width, columns, or -1
    i32 sep;        // separation after the column
    i32 vline;      // vertical lines after the column
    b32 expand;     // x: take the remaining line width
} TblSpec;

typedef struct {
    TblSpec spec[TBL_MAXCOLS];
    i32     n;
    i32     vleft;  // vertical lines before the first column
} TblFmt;

typedef struct {
    Capline *lines;
    i32      nlines;
    i32      width;
    i32      left;  // width before the numeric alignment point
    u8       rule;  // '_' or '=' for a ruled cell
    b32      vspan; // \^
    b32      block; // T{ text block
    Str      text;
} TblCell;

typedef struct {
    TblCell *cells;
    i32      ncells;
    i32      fmt;
    u8       rule;  // full-width rule row
    b32      keyed; // the rule is a format row: a row of its own
    i32      space; // blank lines requested before the row
} TblRow;

typedef struct {
    TblFmt *fmts;
    i32     nfmts;
    TblRow *rows;
    i32     nrows;
    i32     ncols;
    u8      tab;
    b32     box;
    b32     allbox;
    b32     center;
    b32     expand;
} Tbl;

static b32 tblkey(u8 ch)
{
    switch (ch) {
    case 'l': case 'r': case 'c': case 'n': case 'a': case 's': case '^':
    case '_': case '-': case '=':
        return 1;
    }
    return 0;
}

static u8 lower(u8 ch)
{
    return ch>='A' && ch<='Z' ? (u8)(ch + 32) : ch;
}

// Parse format rows from a line, appending to the table. Sets *done at
// the terminating period. Returns false on a syntax error.
static b32 tblparsefmt(Tbl *t, Str line, b32 *done, Arena *a)
{
    TblFmt *f = 0;
    for (iz i = 0; i < line.len; i++) {
        u8 ch = line.data[i];
        if (!f) {
            if (t->nfmts == TBL_MAXFMTS) return 0;
            if (!t->fmts) t->fmts = new(a, TBL_MAXFMTS, TblFmt);
            f = t->fmts + t->nfmts++;
            for (i32 j = 0; j < TBL_MAXCOLS; j++) {
                f->spec[j].font = -1;
                f->spec[j].minw = -1;
                f->spec[j].sep  = 3;
            }
        }
        if (whitespace(ch)) {
            continue;
        } else if (ch == ',') {
            f = 0;
        } else if (ch == '.') {
            *done = 1;
            return f->n > 0;
        } else if (ch == '|') {
            if (f->n) f->spec[f->n-1].vline++; else f->vleft++;
        } else if (tblkey(lower(ch))) {
            if (f->n == TBL_MAXCOLS) return 0;
            u8 k = lower(ch);
            f->spec[f->n++].key = k=='-' ? '_' : k;
        } else if (!f->n) {
            return 0;
        } else {
            // Modifiers of the most recent key letter
            TblSpec *s = f->spec + f->n - 1;
            switch (lower(ch)) {
            case 'b': s->font = (i8)(s->font<0 ? F_B : s->font|F_B); break;
            case 'i': s->font = (i8)(s->font<0 ? F_I : s->font|F_I); break;
            case 't': s->top = 1; break;
            // x excludes e and w, and the later modifier wins
            case 'e': s->equal = 1;  s->expand = 0; break;
            case 'x': s->expand = 1; s->equal  = 0; s->minw = -1; break;
            case 'u': case 'z': case 'd': break;
            case 'f': {
                iz j = i + 1;
                Str name = {0};
                if (j<line.len && line.data[j]=='(') {
                    name = span(line.data+j+1, line.data+(j+3<=line.len ? j+3 : line.len));
                    j += 3;
                } else if (j<line.len && line.data[j]=='[') {
                    iz k = j;
                    for (; k<line.len && line.data[k]!=']'; k++) {}
                    name = span(line.data+j+1, line.data+k);
                    j = k + 1;
                } else {
                    iz k = j;
                    for (; k<line.len && k<j+2 && ((line.data[k]>='A' && line.data[k]<='Z') || digit(line.data[k])); k++) {}
                    name = span(line.data+j, line.data+k);
                    j = k;
                }
                b32 bold = 0, ital = 0;
                for (iz k = 0; k < name.len; k++) {
                    bold |= name.data[k]=='B' || name.data[k]=='3';
                    ital |= name.data[k]=='I' || name.data[k]=='2';
                }
                s->font = (i8)((bold ? F_B : 0) | (ital ? F_I : 0));
                i = j - 1;
            } break;
            case 'w': {
                iz j = i + 1;
                Str w = {0};
                if (j<line.len && line.data[j]=='(') {
                    iz k = j;
                    for (; k<line.len && line.data[k]!=')'; k++) {}
                    w = span(line.data+j+1, line.data+k);
                    j = k + 1;
                } else {
                    iz k = j;
                    for (; k<line.len && (digit(line.data[k]) || line.data[k]=='.'); k++) {}
                    w = span(line.data+j, line.data+k);
                    j = k;
                }
                Scaled v = scaled(w, 'n');
                i32 cols = hcols(v.value);
                if (v.ok) s->minw = cols<0 ? 0 : cols>MAXW ? MAXW : cols;
                s->expand = 0;
                i = j - 1;
            } break;
            case 'p': case 'v': {
                iz j = i + 1;
                if (j<line.len && (line.data[j]=='+' || line.data[j]=='-')) j++;
                for (; j<line.len && digit(line.data[j]); j++) {}
                i = j - 1;
            } break;
            default:
                if (digit(ch)) {
                    i32 n = 0;
                    iz j = i;
                    for (; j<line.len && digit(line.data[j]) && n<100; j++) {
                        n = n*10 + line.data[j] - '0';
                    }
                    s->sep = n;
                    i = j - 1;
                } else {
                    return 0;
                }
            }
        }
    }
    return 1;
}

static void tbloptions(Tbl *t, Str line)
{
    for (iz i = 0; i < line.len; i++) {
        Str rest = cuthead(line, i);
        if (whitespace(rest.data[0]) || rest.data[0]==',' || rest.data[0]==';') {
            continue;
        }
        iz n = 0;
        for (; n<rest.len && ((rest.data[n]>='a' && rest.data[n]<='z') || (rest.data[n]>='A' && rest.data[n]<='Z')); n++) {}
        Str word = takehead(rest, n);
        iz end = n;
        if (n<rest.len && rest.data[n]=='(') {
            iz k = n;
            for (; k<rest.len && rest.data[k]!=')'; k++) {}
            Str arg = span(rest.data+n+1, rest.data+k);
            if (equals(word, S("tab")) && arg.len) {
                t->tab = arg.data[0];
            }
            end = k + 1;
        }
        for (iz k = 0; k < word.len; k++) {
            word.data[k] = lower(word.data[k]);  // safe: our own copy
        }
        if (equals(word, S("box")) || equals(word, S("frame")) ||
            equals(word, S("doublebox")) || equals(word, S("doubleframe"))) {
            t->box = 1;
        } else if (equals(word, S("allbox"))) {
            t->box = t->allbox = 1;
        } else if (equals(word, S("center")) || equals(word, S("centre"))) {
            t->center = 1;
        } else if (equals(word, S("expand"))) {
            t->expand = 1;
        }
        i += end>0 ? end-1 : 0;
    }
}

// Typesetter and context state saved around a table cell
typedef struct {
    i32 indent;
    i32 previndent;
    i32 tindent;
    b32 fill;
    i32 adjust;
    i32 center;
    i32 ll;
    i32 vspace;
    b32 nospace;
    i32 font;
    i32 prevfont;
    i32 trap;
    b32 cont;
} TsSave;

// Begin capturing typesetter output for a table cell into the arena.
static TsSave tsbegin(Ctx *c, Arena *a)
{
    Typeset *ts = c->ts;
    TsSave s = {0};
    s.indent     = ts->indent;
    s.previndent = ts->previndent;
    s.tindent    = ts->tindent;
    s.fill       = ts->fill;
    s.adjust     = ts->adjust;
    s.center     = ts->center;
    s.ll         = ts->ll;
    s.vspace     = ts->vspace;
    s.nospace    = ts->nospace;
    s.font       = c->font;
    s.prevfont   = c->prevfont;
    s.trap       = c->trap;
    s.cont       = c->cont;

    ts->capture  = 1;
    ts->caparena = a;
    ts->caps     = (Caplines){0};
    ts->indent   = 0;
    ts->tindent  = -1;
    ts->vspace   = 0;
    ts->nospace  = 0;
    ts->center   = 0;
    c->trap      = 0;
    c->cont      = 0;
    return s;
}

// End a capture started by tsbegin, restoring the saved state.
static void tsend(Ctx *c, TsSave s)
{
    Typeset *ts = c->ts;
    tsbreak(ts);
    ts->capture    = 0;
    ts->indent     = s.indent;
    ts->previndent = s.previndent;
    ts->tindent    = s.tindent;
    ts->fill       = s.fill;
    ts->adjust     = s.adjust;
    ts->center     = s.center;
    ts->ll         = s.ll;
    ts->vspace     = s.vspace;
    ts->nospace    = s.nospace;
    c->font        = s.font;
    c->prevfont    = s.prevfont;
    c->trap        = s.trap;
    c->cont        = s.cont;
}

static i32 caplinewidth(Capline *l)
{
    i32 n = l->len;
    for (; n>0 && !l->cells[n-1].data; n--) {}
    return n;
}

// Format one cell, either a single line or a filled text block.
static void tblrender(Ctx *c, TblCell *cell, TblSpec *spec, i32 width, Arena *perm, Arena scratch)
{
    Typeset *ts = c->ts;
    TsSave save = tsbegin(c, perm);
    if (spec->font >= 0) {
        setfont(c, spec->font);
    }
    if (cell->block) {
        ts->fill = 1;
        ts->ll   = width>0 ? width : 1;
        Str src = cell->text;
        while (src.len) {
            Cut l = cut(src, '\n');
            src = l.tail;
            processline(c, l.head, scratch, 0, 1);
        }
    } else {
        ts->fill = 0;
        ts->ll   = 10000;
        Str text = expand(c, cell->text, &scratch, 0);
        Sink k = {0};
        k.c = c;
        tokenize(c, text, &k, scratch);
    }
    tsend(c, save);
    cell->lines  = ts->caps.data;
    cell->nlines = (i32)ts->caps.len;
    for (; cell->nlines>0 && !caplinewidth(cell->lines+cell->nlines-1); cell->nlines--) {}
    cell->width = 0;
    for (i32 i = 0; i < cell->nlines; i++) {
        i32 w = caplinewidth(cell->lines + i);
        cell->width = w>cell->width ? w : cell->width;
    }

    // Numeric alignment point: after the last digit, or at a decimal
    // point adjacent to a digit. Items without digits are centered (-1).
    cell->left = -1;
    if (spec->key=='n' && cell->nlines==1) {
        Capline *l = cell->lines;
        i32 point = -1;
        for (i32 i = 0; i < l->len; i++) {
            Cell *x = l->cells + i;
            if (!x->data || x->len!=1) continue;
            if (digit(x->data[0])) {
                point = i + 1;
            }
        }
        for (i32 i = 0; i < l->len; i++) {
            Cell *x = l->cells + i;
            if (x->data && x->len==1 && x->data[0]=='.') {
                b32 before = i>0 && l->cells[i-1].data && digit(l->cells[i-1].data[0]);
                b32 after  = i+1<l->len && l->cells[i+1].data && digit(l->cells[i+1].data[0]);
                if (before || after) point = i;
            }
        }
        if (point >= 0) cell->left = point;
    }
}

static TblSpec *tblspec(Tbl *t, TblRow *row, i32 j)
{
    static TblSpec dflt = {'l', -1, 0, 0, -1, 3, 0, 0};
    if (!t->nfmts) return &dflt;
    TblFmt *f = t->fmts + row->fmt;
    return j<f->n ? f->spec+j : &dflt;
}

// Split one data line into cells, reading text blocks from the source.
// Like tbl, entries skip the columns of an s format. The row keeps only
// the cells it has, so that huge tables fit.
static void tblrow(Tbl *t, Str *src, Str line, Arena *a)
{
    TblRow *row = t->rows + t->nrows++;
    TblCell cells[TBL_MAXCOLS] = {0};
    for (;;) {
        for (; row->ncells<TBL_MAXCOLS && tblspec(t, row, row->ncells)->key=='s'; row->ncells++) {}
        if (row->ncells == TBL_MAXCOLS) break;
        TblCell *cell = cells + row->ncells++;
        Cut f = cut(line, t->tab);
        Str text = f.head;
        if (startswith(text, S("T{")) && !trim(cuthead(text, 2)).len) {
            // Text block: lines until one starting with T}
            Buf b = {0};
            Str rest = {0};
            while (src->len) {
                Cut l = cut(*src, '\n');
                *src = l.tail;
                Str bl = stripcr(l.head);
                if (startswith(bl, S("T}"))) {
                    rest = cuthead(bl, 2);
                    break;
                }
                bappend(&b, bl, a);
                bappendu8(&b, '\n', a);
            }
            cell->block = 1;
            cell->text  = bstr(b);
            if (rest.len && rest.data[0]==t->tab) {
                line = cuthead(rest, 1);
                continue;
            }
            break;
        }
        Str tt = trim(text);
        if (equals(tt, S("_")) || equals(tt, S("="))) {
            cell->rule = tt.data[0];
        } else if (equals(tt, S("\\_"))) {
            cell->rule = '_';
        } else if (equals(tt, S("\\^"))) {
            cell->vspan = 1;
        } else if (startswith(tt, S("\\R")) && tt.len>2) {
            cell->rule = tt.data[2];
        } else {
            cell->text = text;
        }
        if (!f.ok) break;
        line = f.tail;
    }
    row->cells = new(a, row->ncells, TblCell);
    xcpy(row->cells, cells, touz(row->ncells)*sizeof(TblCell));
}

// The rule drawn by a format row of only _ and = keys over every column,
// or zero if it takes data
static u8 tblfmtrule(Tbl *t, i32 i)
{
    TblFmt *f = t->fmts + i;
    for (i32 j = 0; j < f->n; j++) {
        if (f->spec[j].key!='_' && f->spec[j].key!='=') return 0;
    }
    for (i32 k = 0; k < t->nfmts; k++) {
        if (t->fmts[k].n > f->n) return 0;
    }
    return f->spec[0].key;
}

// Columns spanned by the cell in column j, counting the s after it
static i32 tblspan(Tbl *t, TblRow *row, i32 j)
{
    i32 span = 1;
    for (; j+span<t->ncols && tblspec(t, row, j+span)->key=='s'; span++) {}
    return span;
}

// Width of columns [j, j+span) and the separation between them
static i32 tblspanw(i32 *colw, i32 *sep, i32 j, i32 span)
{
    i32 w = 0;
    for (i32 k = j; k < j+span; k++) {
        w += colw[k] + (k<j+span-1 ? sep[k]*U_PER_N : 0);
    }
    return w;
}

static void tblequal(i32 *colw, b32 *ecol, i32 n)
{
    i32 w = 0;
    for (i32 j = 0; j < n; j++) {
        w = ecol[j] && colw[j]>w ? colw[j] : w;
    }
    for (i32 j = 0; j < n; j++) {
        colw[j] = ecol[j] ? w : colw[j];
    }
}

// Widen columns evenly for the widest cell of each span, ordered by last
// column then by first column descending, like tbl. A span over an
// equal-width or expanding column widens every column, like tbl.
static void tbldivide(Tbl *t, i32 *colw, i32 *sep, b32 *ecol, b32 *xcol, Arena scratch)
{
    i32  n     = t->ncols;
    i32 *spanw = new(&scratch, n*n, i32);
    for (i32 r = 0; r < t->nrows; r++) {
        TblRow *row = t->rows + r;
        for (i32 j = 0; j < row->ncells && j < n; j++) {
            i32 key  = tblspec(t, row, j)->key;
            i32 span = tblspan(t, row, j);
            if (span==1 || key=='s' || key=='^') continue;
            i32 *w = spanw + j*n + j+span-1;
            i32  cw = row->cells[j].width * U_PER_N;
            *w = cw>*w ? cw : *w;
        }
    }
    for (i32 e = 1; e < n; e++) {
        for (i32 s = e-1; s >= 0; s--) {
            i32 span = e - s + 1;
            i32 need = (spanw[s*n+e] - tblspanw(colw, sep, s, span))/span;
            b32 eq   = 0;
            for (i32 k = s; k <= e; k++) {
                eq |= ecol[k] || xcol[k];
            }
            for (i32 k = 0; need>0 && k<n; k++) {
                colw[k] += eq || (k>=s && k<=e) ? need : 0;
            }
        }
    }
}

static void tblplace(Typeset *ts, i32 x, Str s)
{
    if (x>=0 && x<ts->rowcap) {
        ts->row[x].data = s.data;
        ts->row[x].len  = (i32)s.len;
        ts->row[x].font = 0;
    }
}

// Draw a horizontal rule over [x0, xend) into a cleared row, with joints
// at the vertical lines. It leaves out the columns a vertical span holds
// open, and above and below mark the lines that spanning cells of the
// adjacent rows cut, null at the top or bottom of the table.
static void tblrule(Typeset *ts, Str ch, i32 x0, i32 xend, i32 *vline, i32 *vx, i32 n, b32 *open, b32 *above, b32 *below)
{
    // By the directions joined: up 8, down 4, left 2, right 1
    static Str joints[16] = {
        STR(""),             STR("\xe2\x94\x80"), STR("\xe2\x94\x80"), STR("\xe2\x94\x80"),
        STR("\xe2\x94\x82"), STR("\xe2\x94\x8c"), STR("\xe2\x94\x90"), STR("\xe2\x94\xac"),
        STR("\xe2\x94\x82"), STR("\xe2\x94\x94"), STR("\xe2\x94\x98"), STR("\xe2\x94\xb4"),
        STR("\xe2\x94\x82"), STR("\xe2\x94\x9c"), STR("\xe2\x94\xa4"), STR("\xe2\x94\xbc"),
    };
    clearrow(ts, xend+1);
    for (i32 x = x0; x < xend; x++) {
        tblplace(ts, x, ch);
    }
    for (i32 j = 0; open && j < n; j++) {
        if (open[j]) {
            i32 lo = vx[j] + (j || vline[0]) - (j && open[j-1] && !vline[j]);
            i32 hi = j<n-1 || vline[n] ? vx[j+1] : xend;
            for (i32 x = lo; x < hi; x++) {
                tblplace(ts, x, (Str){0});
            }
        }
    }
    for (i32 j = 0; j <= n; j++) {
        if (vline[j]) {
            i32 d = (above && !above[j])<<3 | (below && !below[j])<<2 |
                    (j>0 && !(open && open[j-1]))<<1 | (j<n && !(open && open[j]));
            Str s = joints[d];
            if (!ts->utf8) {
                s = d&3 && d&12 ? S("+") : d&3 ? S("-") : S("|");
            }
            tblplace(ts, vx[j], d ? s : (Str){0});
        }
    }
}

static void tbllayout(Ctx *c, Str src, Arena scratch)
{
    // The table lives in scratch, and tblrender gets the scratch past it
    // by value. Only the lines of rendered cells are kept in perm.
    Typeset *ts   = c->ts;
    Arena   *a    = &scratch;
    Arena   *perm = &c->perm;
    Tbl t = {0};
    t.tab = '\t';

    // Options and formats
    Str s = src;
    Cut first = cut(s, '\n');
    Str fl = trim(stripcr(first.head));
    if (fl.len && fl.data[fl.len-1]==';') {
        tbloptions(&t, clone(a, fl));
        s = first.tail;
    }
    Str fmtstart = s;
    b32 done = 0, ok = 1;
    while (s.len && !done && ok) {
        Cut l = cut(s, '\n');
        s = l.tail;
        ok = tblparsefmt(&t, stripcr(l.head), &done, a);
    }
    if (!ok || !done) {
        // Malformed format: treat everything as left-aligned data
        t.nfmts = 0;
        s = fmtstart;
    }
    i32 nfmt0 = t.nfmts;  // .T& formats cannot change x or e columns

    // Data rows, with .T& format changes. Before a data line, format rows
    // of rules alone, short of the last, are rules taking no data line.
    i32 maxrows = 1;
    for (iz i = 0; i < s.len; i++) maxrows += s.data[i]=='\n';
    t.rows = new(a, maxrows+TBL_MAXFMTS, TblRow);
    i32 section = 0, sectionrow = 0, pending = 0;
    while (s.len) {
        Cut l = cut(s, '\n');
        s = l.tail;
        Str line = stripcr(l.head);
        if (startswith(line, S(".T&"))) {
            section = t.nfmts;
            sectionrow = 0;
            b32 fdone = 0, fok = 1;
            while (s.len && !fdone && fok) {
                Cut fl2 = cut(s, '\n');
                s = fl2.tail;
                fok = tblparsefmt(&t, stripcr(fl2.head), &fdone, a);
            }
            continue;
        }
        if (line.len && (line.data[0]=='.' || line.data[0]=='\'')) {
            Str req = trim(cuthead(line, 1));
            if (startswith(req, S("sp"))) {
                Scaled v = scaled(cuthead(req, 2), 'v');
                pending += v.ok ? vlines(v.value) : 1;
                pending  = pending<0 ? 0 : pending>MAXV ? MAXV : pending;
            }
            continue;
        }
        // Rule-only format rows come before the next real data line
        Str tl = trim(line);
        b32 datarule = equals(tl, S("_")) || equals(tl, S("="));
        for (; !datarule && section+sectionrow<t.nfmts-1; sectionrow++) {
            u8 rule = tblfmtrule(&t, section+sectionrow);
            if (!rule) break;
            TblRow *row = t.rows + t.nrows++;
            row->fmt   = section + sectionrow;
            row->rule  = rule;
            row->keyed = 1;
            row->space = pending;
            pending = 0;
        }
        TblRow *row = t.rows + t.nrows;
        if (t.nfmts) {
            i32 last = t.nfmts - 1;
            row->fmt = section + sectionrow;
            row->fmt = row->fmt>last ? last : row->fmt;
        }
        if (datarule) {
            t.nrows++;
            row->rule = tl.data[0];
        } else {
            tblrow(&t, &s, line, a);
        }
        row->space = pending;
        pending = 0;
        if (!row->rule) sectionrow++;
    }

    // Column count
    for (i32 i = 0; i < t.nfmts; i++) {
        t.ncols = t.fmts[i].n>t.ncols ? t.fmts[i].n : t.ncols;
    }
    for (i32 r = 0; r < t.nrows; r++) {
        if (!t.nfmts) t.ncols = t.rows[r].ncells>t.ncols ? t.rows[r].ncells : t.ncols;
    }
    i32 n = t.ncols;
    if (!n) return;

    // Render cells and measure columns. Widths are in basic units, like
    // tbl's registers, so that shares of spans and of the expansion are
    // rounded to character positions like groff.
    i32 *colw  = new(a, n, i32);
    i32 *left  = new(a, n, i32);
    i32 *right = new(a, n, i32);
    i32 *sep   = new(a, n, i32);
    i32 *vline = new(a, n+1, i32);
    i32 *minw  = new(a, n, i32);  // the last w() of each column, or -1
    for (i32 j = 0; j < n; j++) {
        minw[j] = -1;
    }
    for (i32 f = 0; f < t.nfmts; f++) {
        for (i32 j = 0; j < t.fmts[f].n && j < n; j++) {
            i32 w = t.fmts[f].spec[j].minw;
            minw[j] = w>=0 ? w*U_PER_N : minw[j];
        }
    }
    for (i32 j = 0; j < n; j++) {
        sep[j]  = 3;
        colw[j] = minw[j]>=0 ? minw[j] : U_PER_N;  // 1n unless w() given
    }
    // Expanding and equal-width columns: x or e in any initial format row
    b32 *xcol = new(a, n, b32);
    b32 *ecol = new(a, n, b32);
    i32  nx   = 0;
    for (i32 f = 0; f < nfmt0; f++) {
        for (i32 j = 0; j < t.fmts[f].n && j < n; j++) {
            nx += t.fmts[f].spec[j].expand && !xcol[j];
            xcol[j] |= t.fmts[f].spec[j].expand;
            ecol[j] |= t.fmts[f].spec[j].equal;
        }
    }

    // Ordinary entries first: they set the initial column widths
    for (i32 r = 0; r < t.nrows; r++) {
        TblRow *row = t.rows + r;
        if (row->rule) continue;
        for (i32 j = 0; j < n; j++) {
            TblSpec *sp = tblspec(&t, row, j);
            if (j < n-1) sep[j] = sp->sep;
            if (j >= row->ncells) continue;
            TblCell *cell = row->cells + j;
            if (cell->rule || cell->vspan || cell->block || sp->key=='s' || sp->key=='^') {
                continue;
            }
            tblrender(c, cell, sp, 0, perm, scratch);
            if (tblspan(&t, row, j) > 1) {
                continue;  // spanning: handled below
            }
            if (sp->key=='n' && cell->nlines==1 && cell->left>=0) {
                left[j]  = cell->left>left[j] ? cell->left : left[j];
                i32 rw = cell->width - cell->left;
                right[j] = rw>right[j] ? rw : right[j];
            } else {
                i32 w = cell->width * U_PER_N;
                colw[j] = w>colw[j] ? w : colw[j];
            }
        }
    }
    for (i32 j = 0; j < n; j++) {
        i32 w = (left[j] + right[j]) * U_PER_N;
        colw[j] = w>colw[j] ? w : colw[j];
    }
    tblequal(colw, ecol, n);
    tbldivide(&t, colw, sep, ecol, xcol, scratch);

    // Text blocks, in order: each is filled to the larger of its width so
    // far and L*C/(N+1), or the w() widths if all its columns have one,
    // then may widen its column, like tbl. Blocks over expanding columns
    // wait for the expanded width.
    for (i32 r = 0; r < t.nrows; r++) {
        TblRow *row = t.rows + r;
        for (i32 j = 0; j < row->ncells && j < n; j++) {
            TblCell *cell = row->cells + j;
            TblSpec *sp = tblspec(&t, row, j);
            if (!cell->block || sp->key=='s' || sp->key=='^') continue;
            i32 span = tblspan(&t, row, j);
            b32 x    = 0;
            b32 allw = 1;
            for (i32 k = j; k < j+span; k++) {
                x    |= xcol[k];
                allw &= minw[k] >= 0;
            }
            if (x) continue;
            i32 cw = tblspanw(colw, sep, j, span);
            i32 bw = ts->ll*U_PER_N*span/(n + 1);
            bw = allw ? tblspanw(minw, sep, j, span) : bw;
            tblrender(c, cell, sp, hcols(cw>bw ? cw : bw), perm, scratch);
            i32 w = cell->width * U_PER_N;
            colw[j] = span==1 && w>colw[j] ? w : colw[j];
        }
    }
    tblequal(colw, ecol, n);
    tbldivide(&t, colw, sep, ecol, xcol, scratch);

    // Vertical lines
    for (i32 f = 0; f < t.nfmts; f++) {
        vline[0] = t.fmts[f].vleft>vline[0] ? t.fmts[f].vleft : vline[0];
        for (i32 j = 0; j < t.fmts[f].n && j < n; j++) {
            if (t.fmts[f].spec[j].vline) vline[j+1] = 1;
        }
    }
    if (t.allbox) {
        for (i32 j = 0; j <= n; j++) vline[j] = 1;
    }
    if (t.box) {
        vline[0] = vline[n] = 1;
    }
    i32 frame  = vline[0] ? 2 : 0;
    i32 rframe = vline[n] ? 2 : 0;

    // Expanding columns share what the others leave of the line, if that
    // exceeds their natural widths. Then their text blocks are filled to
    // that width, or spanning ones as above on a line less the frame.
    if (nx) {
        i32 rest = (ts->ll - ts->indent - frame - rframe) * U_PER_N;
        i32 nat  = 0;
        for (i32 j = 0; j < n; j++) {
            rest -= (xcol[j] ? 0 : colw[j]) + (j<n-1 ? sep[j]*U_PER_N : 0);
            nat  += xcol[j] ? colw[j] : 0;
        }
        i32 xw = rest>nat ? rest/nx : 0;
        for (i32 j = 0; j < n; j++) {
            colw[j] = xcol[j] && xw>colw[j] ? xw : colw[j];
        }
        i32 ll = ts->ll - (vline[0]>0) - (vline[n]>0);
        for (i32 r = 0; r < t.nrows; r++) {
            TblRow *row = t.rows + r;
            for (i32 j = 0; j < row->ncells && j < n; j++) {
                TblCell *cell = row->cells + j;
                TblSpec *sp = tblspec(&t, row, j);
                if (!cell->block || sp->key=='s' || sp->key=='^') continue;
                i32 span = tblspan(&t, row, j);
                b32 x    = 0;
                for (i32 k = j; k < j+span; k++) {
                    x |= xcol[k];
                }
                if (!x) continue;
                i32 cw = tblspanw(colw, sep, j, span);
                i32 bw = span>1 ? ll*U_PER_N*span/(n + 1) : 0;
                tblrender(c, cell, sp, hcols(cw>bw ? cw : bw), perm, scratch);
                i32 w = cell->width * U_PER_N;
                colw[j] = span==1 && w>colw[j] ? w : colw[j];
            }
        }
        tbldivide(&t, colw, sep, ecol, xcol, scratch);
    }

    // Horizontal positions, rounded from basic units like troff motions,
    // after which column widths are in character cells
    i32 total = (frame + rframe) * U_PER_N;
    for (i32 j = 0; j < n; j++) {
        total += colw[j] + (j<n-1 ? sep[j]*U_PER_N : 0);
    }
    i32 x0 = ts->indent;
    i32 avail = ts->ll - ts->indent;
    if (t.expand && avail*U_PER_N>total) {
        i32 extra = avail - hcols(total);
        for (i32 j = 0; j < n; j++) {
            i32 add = extra/(n - j);
            colw[j] += add * U_PER_N;
            extra -= add;
        }
    } else if (t.center) {
        // Measured from groff: a frame costs one more column, and an
        // overwide table may extend left into the indentation.
        x0 += (avail - hcols(total) - (vline[0]||vline[n] ? 1 : 0))/2;
        x0 = x0<0 ? 0 : x0;
    }
    i32 *colx = new(a, n+1, i32);
    i32 *vx   = new(a, n+1, i32);
    i32 *numu = new(a, n, i32);  // numeric alignment points, basic units
    i32  u    = (x0 + frame) * U_PER_N;
    vx[0] = x0;
    for (i32 j = 0; j < n; j++) {
        colx[j] = hcols(u);
        numu[j] = u + (colw[j] - (left[j] + right[j])*U_PER_N)/2 + left[j]*U_PER_N;
        u += colw[j];
        colw[j] = hcols(u) - colx[j];
        if (j < n-1) {
            vx[j+1] = hcols(u + sep[j]*U_PER_N/2);
            u += sep[j] * U_PER_N;
        }
    }
    i32 xend = hcols(u) + rframe;  // one past the right frame
    vx[n] = xend - 1;

    // Vertical spans: \^ or a ^ format continues the cell above, across
    // any data rule rows, while a format rule row continues as itself.
    // vorg is the row of the cell shown in each row and column, and vend
    // the last row of the span starting at each cell.
    i32 *vorg = new(a, (iz)t.nrows*n, i32);
    i32 *vend = new(a, (iz)t.nrows*n, i32);
    for (i32 r = 0, prev = -1; r < t.nrows; r++) {
        TblRow *row = t.rows + r;
        for (i32 j = 0; j < n; j++) {
            i32 k = r*n + j;
            vorg[k] = vend[k] = r;
            if (row->rule || prev<0) continue;
            if ((j<row->ncells && row->cells[j].vspan) || tblspec(&t, row, j)->key=='^') {
                i32 o = vorg[prev*n+j];
                for (i32 q = prev+1; q <= r; q++) {
                    vorg[q*n+j] = o;
                }
                vend[o*n+j] = r;
            }
        }
        prev = row->rule && !row->keyed ? prev : r;
    }

    // Lines of each row: the allbox rule below the row above, if that is
    // a data or format rule row and more data follows, blank lines, a
    // rule, then its own lines. Cells sit at the top of their row, and
    // spanning cells are centered over their rows unless t, the last row
    // growing to fit. Like tbl, a row ending no entry takes one line, and
    // others what their entries need, at least one for those of a single
    // row. In no-space mode, rows take no blank lines until one ends an
    // entry or is a rule.
    i32 *start  = new(a, t.nrows, i32);
    i32 *height = new(a, t.nrows, i32);
    i32  lastdata = -1;
    for (i32 r = 0; r < t.nrows; r++) {
        lastdata = t.rows[r].rule ? lastdata : r;
    }
    b32 ns = ts->nospace && !t.box;
    for (i32 r = 0, g = 0; r < t.nrows; r++) {
        TblRow *row = t.rows + r;
        b32     ab  = t.allbox && r>0 && (!row[-1].rule || row[-1].keyed) && r<=lastdata;
        row->space = ns ? 0 : row->space;
        g += ab + row->space + (row->rule>0);
        start[r] = g;
        i32 h    = 0;
        b32 ends = 0;
        for (i32 j = 0; !row->rule && j < n; j++) {
            i32 o = vorg[r*n+j];
            if (vend[o*n+j]!=r || tblspec(&t, t.rows+o, j)->key=='s') continue;
            // An empty entry takes a line, but not a format rule row
            i32 lines = j<t.rows[o].ncells ? t.rows[o].cells[j].nlines : 0;
            lines = lines>1 ? lines : !t.rows[o].rule;
            i32 need = lines - (g - start[o]);
            h    = need>h ? need : h;
            ends = 1;
        }
        height[r] = row->rule ? 0 : ends ? h : !ns;
        ns &= !row->rule && !ends;
        g += height[r];
    }

    b32 utf8 = ts->utf8;
    Str hz = utf8 ? S("\xe2\x94\x80") : S("-");
    Str vt = utf8 ? S("\xe2\x94\x82") : S("|");
    Str dh = utf8 ? S("\xe2\x95\x90") : S("=");

    // Column lines cut by the spanning cells shown in each row, then a
    // row of none. Like tbl, a rule row's are cut by the data rows on
    // either side.
    i32  nc    = n + 1;
    b32 *cross = new(a, (iz)(t.nrows+1)*nc, b32);
    for (i32 r = 0; r < t.nrows; r++) {
        for (i32 j = 0; !t.rows[r].rule && j < n; j++) {
            i32 span = tblspan(&t, t.rows+vorg[r*n+j], j);
            for (i32 k = j+1; k < j+span; k++) {
                cross[r*nc+k] = 1;
            }
        }
    }
    for (i32 r = 1; r < t.nrows; r++) {
        for (i32 k = 0; t.rows[r].rule && k < nc; k++) {
            cross[r*nc+k] |= cross[(r-1)*nc+k];
        }
    }
    for (i32 r = t.nrows-2; r >= 0; r--) {
        for (i32 k = 0; t.rows[r].rule && k < nc; k++) {
            cross[r*nc+k] |= cross[(r+1)*nc+k];
        }
    }
    b32 *none = cross + (iz)t.nrows*nc;
    b32 *last = t.nrows ? none-nc : none;

    if (t.box) {
        tblrule(ts, hz, x0, xend, vline, vx, n, 0, 0, cross);
        emitrow(ts, xend);
    }
    b32 *open = new(a, n, b32);
    i32  xr   = xend + !vline[n];  // like tbl, rules reach the right edge
    for (i32 r = 0, g = 0; r < t.nrows; r++) {
        TblRow *row = t.rows + r;
        i32  own = start[r] - g;  // first line of the row's own
        i32  ab  = own - row->space - (row->rule>0);  // allbox rule lines
        b32 *cut = cross + (iz)r*nc;
        for (i32 j = 0, k = 0; j < n; j++) {
            // A continued cell holds open the columns it spans
            i32 o = vorg[r*n+j];
            i32 e = o<r ? j+tblspan(&t, t.rows+o, j) : 0;
            k = e>k ? e : k;
            open[j] = j < k;
        }
        for (i32 line = 0; line < own+height[r]; line++, g++) {
            b32 rl = line<ab || (row->rule && line==own-1);
            if (line < ab) {
                // The allbox rule lies below the row above, and the line
                // to a rule just under it is cut as in that row
                b32 *below = row->rule && !row->space ? cut-nc : cut;
                tblrule(ts, hz, x0, xr, vline, vx, n, open, cut-nc, below);
            } else if (rl) {
                // A rule row lies between its neighbors
                b32 *above = !r ? none : line==ab ? cut-nc : cut;
                b32 *below = r<t.nrows-1 ? cut+nc : cut;
                Str  ch    = row->rule=='=' ? dh : hz;
                tblrule(ts, ch, x0, xr, vline, vx, n, open, above, below);
            } else {
                clearrow(ts, xend+1);
                for (i32 j = 0; j <= n; j++) {
                    if (vline[j] && !cut[j]) tblplace(ts, vx[j], vt);
                }
            }
            for (i32 j = 0; j < n; j++) {
                i32 o = vorg[r*n+j];
                if (o==r && line<own) continue;
                TblRow  *orow = t.rows + o;
                TblSpec *sp   = tblspec(&t, orow, j);
                if (sp->key=='s' || sp->key=='^') continue;
                i32 span = tblspan(&t, orow, j);
                i32 sw   = colx[j+span-1] + colw[j+span-1] - colx[j];
                if (sp->key=='_' || sp->key=='=' || (j<orow->ncells && orow->cells[j].rule)) {
                    // Ruled cell, or \Rx repeating a character
                    Str rc = sp->key=='=' ? dh : hz;
                    if (j<orow->ncells && orow->cells[j].rule) {
                        u8 *p = &orow->cells[j].rule;
                        rc = *p=='=' ? dh : *p=='_' ? hz : (Str){p, 1};
                    }
                    for (i32 xx = 0; o==r && line==own && xx<sw; xx++) {
                        tblplace(ts, colx[j]+xx, rc);
                    }
                    continue;
                }
                if (j >= orow->ncells) continue;
                TblCell *cell = orow->cells + j;
                i32 e   = vend[o*n+j];
                i32 top = start[o];
                if (e>o && !sp->top) {
                    top += (start[e] + height[e] - start[o] - cell->nlines)/2;
                }
                i32 li = g - top;
                if (li<0 || li>=cell->nlines) continue;
                Capline *cl = cell->lines + li;
                i32 w  = cell->block ? cell->width : caplinewidth(cl);  // blocks as a whole
                i32 cx = colx[j];
                switch (sp->key) {
                case 'r': cx += sw - w; break;
                case 'c': cx += (sw - w)/2; break;
                case 'n':
                    if (span==1 && cell->left>=0) {
                        cx = hcols(numu[j] - cell->left*U_PER_N);
                    } else {
                        cx += (sw - w)/2;
                    }
                    break;
                }
                for (i32 k = 0; k < cl->len; k++) {
                    if (cx+k>=0 && cx+k<ts->rowcap && cl->cells[k].data) {
                        ts->row[cx+k] = cl->cells[k];
                    }
                }
            }
            emitrow(ts, rl ? xr : xend);
        }
    }
    if (t.box) {
        tblrule(ts, hz, x0, xend, vline, vx, n, 0, last, 0);
        emitrow(ts, xend);
    }
}

static void tblline(Ctx *c, Str line, Arena scratch)
{
    if (isterminator(line, S("TE"))) {
        c->tblactive = 0;
        tbllayout(c, bstr(c->tblsrc), scratch);
        return;
    }
    bappend(&c->tblsrc, line, &c->perm);
    bappendu8(&c->tblsrc, '\n', &c->perm);
}


// mdoc(7), following mandoc's terminal layout

enum {
    MDIN = 5,   // body indent
    MDSS = 3,   // subsection heading indent
    MDDS = 6,   // "Ds" display indent
};

enum {
    MS_OTHER,
    MS_NAME,
    MS_SYNOPSIS,
    MS_LIBRARY,
    MS_SEEALSO,
    MS_AUTHORS,
};

enum {
    LT_TAG, LT_ITEM, LT_BULLET, LT_DASH, LT_ENUM, LT_HANG, LT_OHANG,
    LT_INSET, LT_DIAG, LT_COLUMN,
};

enum {
    DL_OPEN = 1,
    DL_CLOSE,
    DL_MIDDLE,
};

typedef struct {
    Str *args;
    b32 *quoted;
    i32  n;
    i32  i;
    b32  block;  // quoted arguments are words, not macros
} MdArgs;

static i32 mddelim(Str s)
{
    if (s.len != 1) {
        return 0;
    }
    switch (s.data[0]) {
    case '(': case '[':
        return DL_OPEN;
    case '.': case ',': case ':': case ';': case ')': case ']': case '?':
    case '!':
        return DL_CLOSE;
    case '|':
        return DL_MIDDLE;
    }
    return 0;
}

// The kind of a callable macro named by an argument, or zero.
static i32 mdcallable(Ctx *c, Str s)
{
    if (s.len<2 || s.len>3) {
        return 0;
    }
    Entry *e = find(c->kinds, s);
    return e && e->inc && e->num>=D_DD ? e->num : 0;
}

static Str glyph(Ctx *c, Str name)
{
    Entry *e = find(c->glyphmap, name);
    return e ? (c->ts->utf8 ? e->str : e->alt) : name;
}

// Emit a word in a font with mdoc spacing: no space after an opening
// delimiter, before a closing one, or after any word under .Sm off,
// and delimiters are never styled.
static void mdword(Ctx *c, Str w, i32 font, Arena scratch)
{
    Typeset *ts = c->ts;
    i32 d = mddelim(w);
    b32 space = !c->mdnospace && !c->cont && d!=DL_CLOSE;
    c->cont = 0;
    if (space && hasglyphs(ts, 0, ts->ntoks) && ts->toks[ts->ntoks-1].type!=TK_SPACE) {
        tsspace(ts, 1);
    } else if (!space && (d==DL_CLOSE || c->mdnospace)) {
        tsunspace(ts);
    }
    i32 saved = c->font, prev = c->prevfont;
    c->font = d ? F_R : font;
    Sink k = {0};
    k.c = c;
    c->nobreak = 1;
    tokenize(c, w.len ? w : S("\\&"), &k, scratch);  // "" holds its space
    c->nobreak = 0;
    c->font = saved;
    c->prevfont = prev;
    c->mdnospace = d==DL_OPEN || c->mdsmoff;
    c->mdeos   = d==DL_CLOSE && (w.data[0]=='.' || w.data[0]=='!' || w.data[0]=='?');
    c->mdwrote = 1;
}

static void mdmacro(Ctx *c, i32 kind, Str name, MdArgs *a, Arena scratch);

// Process the remaining arguments: words, delimiters, callable macros.
// A called macro parses the rest of the line, and unlike a block macro
// it also calls macros named by quoted arguments.
static void mdrest(Ctx *c, MdArgs *a, i32 font, Arena scratch)
{
    while (a->i < a->n) {
        i32 i = a->i++;
        Str w = a->args[i];
        i32 k = a->block && a->quoted[i] ? 0 : mdcallable(c, w);
        if (k && c->mddepth<MAXDEPTH) {
            a->block = 0;
            c->mddepth++;
            mdmacro(c, k, w, a, scratch);
            c->mddepth--;
        } else {
            mdword(c, w, font, scratch);
        }
    }
}

// Format arguments up to the next callable macro. Returns the number of
// non-delimiter words.
static i32 mdinline(Ctx *c, MdArgs *a, i32 font, Arena scratch)
{
    i32 n = 0;
    while (a->i < a->n) {
        Str w = a->args[a->i];
        if (mdcallable(c, w)) {
            break;
        }
        mdword(c, w, font, scratch);
        n += !mddelim(w);
        a->i++;
    }
    return n;
}

// Is the next argument missing or a macro? Like mandoc, then some
// in-line macros print nothing.
static b32 mdnoarg(Ctx *c, MdArgs *a)
{
    return a->i==a->n || mdcallable(c, a->args[a->i]);
}

// Does a word argument follow before the next callable macro?
static b32 mdhasword(Ctx *c, MdArgs *a)
{
    for (i32 i = a->i; i < a->n; i++) {
        if (mdcallable(c, a->args[i])) return 0;
        if (!mddelim(a->args[i])) return 1;
    }
    return 0;
}

// End of the arguments, before any trailing closing delimiters, which
// are never quoted.
static i32 mdcontentend(MdArgs *a)
{
    i32 end = a->n;
    for (; end>a->i && !a->quoted[end-1] && mddelim(a->args[end-1])==DL_CLOSE; end--) {}
    return end;
}

// Partial-implicit enclosure: the rest of the line, except trailing
// closing delimiters, inside open/close.
static void mdenclose(Ctx *c, MdArgs *a, Str open, Str close, i32 font, Arena scratch)
{
    i32 end = mdcontentend(a);
    for (; a->i<end && mddelim(a->args[a->i])==DL_OPEN; a->i++) {
        mdword(c, a->args[a->i], F_R, scratch);
    }
    mdword(c, open, F_R, scratch);
    c->mdnospace = 1;
    MdArgs sub = {a->args, a->quoted, end, a->i, 1};
    mdrest(c, &sub, font, scratch);
    a->i = end;
    c->mdnospace = 1;
    mdword(c, close, F_R, scratch);
}

static Str mdunbreakable(Str s, Arena *a)
{
    Buf b = {0};
    for (iz i = 0; i < s.len; i++) {
        if (s.data[i] == ' ') {
            bappend(&b, S("\\ "), a);
        } else {
            bappendu8(&b, s.data[i], a);
        }
    }
    return b.data ? bstr(b) : S("");
}

static void mdheader(Ctx *c, Arena scratch)
{
    if (c->page) {
        return;
    }
    Typeset *ts = c->ts;
    tsbreak(ts);
    c->page = 1;
    ts->fill = 0;  // titles never wrap
    Sink k = {0};
    k.c = c;
    i32 font = c->font;
    c->font = F_R;
    Str ref = concat(&scratch, c->mdtitle, S("("));
    ref = concat(&scratch, ref, c->mdsec);
    ref = concat(&scratch, ref, S(")"));
    tokenize(c, ref, &k, scratch);
    i32 left = ts->ntoks;
    tokenize(c, c->mdvol, &k, scratch);
    i32 center = ts->ntoks;
    tokenize(c, ref, &k, scratch);
    tstitle(ts, left, center);
    printu8(ts->out, '\n');
    c->font = font;
    ts->indent  = MDIN;
    ts->tindent = -1;
    ts->fill    = 1;
    ts->adjust  = AD_L;
    ts->tabw    = 8;
    ts->vcollapse = 1;
    ts->zerogap   = 1;
    ts->nospace = 1;
    ts->vspace  = 0;
}

static void mdfooter(Ctx *c, Arena scratch)
{
    Typeset *ts = c->ts;
    tsbreak(ts);
    ts->vspace = 1;
    ts->nospace = 0;
    b32 fill = ts->fill;
    ts->fill = 0;  // titles never wrap
    Sink k = {0};
    k.c = c;
    c->font = F_R;
    tokenize(c, c->mdos, &k, scratch);
    i32 left = ts->ntoks;
    tokenize(c, c->mddate, &k, scratch);
    i32 center = ts->ntoks;
    tokenize(c, c->mdos, &k, scratch);
    tstitle(ts, left, center);
    ts->fill = fill;
    c->page = 0;
}

static Str mdvolume(Str sec)
{
    if (sec.len != 1) {
        return defaultmanual(sec);
    }
    return sec.data[0]=='4' ? S("Device Drivers Manual") : defaultmanual(sec);
}

// A scaled width like "4n", or -1.
static i32 mdscaled(Str w)
{
    if (w.len && (digit(w.data[0]) || w.data[0]=='.')) {
        Num n = evalexpr(w, 'n', 0);
        if (n.ok && !n.rest.len) return hcols(n.value);
    }
    return -1;
}

// The -width argument of .Bl, in columns: a scaled width, "Ds", or the
// width of a string; mandoc then adds two columns of separation.
static i32 mdwidth(Ctx *c, Str w, Arena scratch)
{
    // Default widths of macros named as widths, from mandoc
    static struct { Str name; i32 width; } macros[] = {
        {STR("Ad"), 12}, {STR("Ao"), 12}, {STR("An"), 12}, {STR("Aq"), 12},
        {STR("Ar"), 12}, {STR("Bo"), 12}, {STR("Bq"), 12}, {STR("Cd"), 12},
        {STR("Cm"), 10}, {STR("Do"), 10}, {STR("Dq"), 12}, {STR("Dv"), 12},
        {STR("Eo"), 12}, {STR("Em"), 10}, {STR("Er"), 17}, {STR("Ev"), 15},
        {STR("Fa"), 12}, {STR("Fl"), 10}, {STR("Fo"), 16}, {STR("Fn"), 16},
        {STR("Ic"), 10}, {STR("Li"), 16}, {STR("Ms"), 6},  {STR("Nm"), 10},
        {STR("No"), 12}, {STR("Oo"), 10}, {STR("Op"), 14}, {STR("Pa"), 32},
        {STR("Pf"), 12}, {STR("Po"), 12}, {STR("Pq"), 12}, {STR("Ql"), 16},
        {STR("Qo"), 12}, {STR("So"), 12}, {STR("Sq"), 12}, {STR("Sy"), 6},
        {STR("Sx"), 16}, {STR("Tn"), 10}, {STR("Va"), 12}, {STR("Vt"), 12},
        {STR("Xr"), 10},
    };
    i32 r = mdscaled(w);
    for (i32 i = 0; r<0 && i<countof(macros); i++) {
        if (equals(w, macros[i].name)) r = macros[i].width;
    }
    if (r < 0) {
        r = equals(w, S("Ds")) ? MDDS : textwidth(c, w, scratch);
    }
    r = r<0 ? 0 : r>MAXIND ? MAXIND : r;
    return r + 2;
}

// The -offset argument of .Bl or .Bd, in columns.
static i32 mdoffset(Ctx *c, Str w, Arena scratch)
{
    if (equals(w, S("left")))       return 0;
    if (equals(w, S("indent")))     return MDIN + 1;
    if (equals(w, S("indent-two"))) return 2*(MDIN + 1);
    if (equals(w, S("Ds")))         return MDDS;
    i32 r = mdscaled(w);
    r = r>=0 ? r : textwidth(c, w, scratch);
    return r<0 ? 0 : r>MAXIND ? MAXIND : r;
}

static MdBlock *mdpush(Ctx *c, i32 kind)
{
    if (c->mdnblocks == countof(c->mdblocks)) {
        return 0;
    }
    MdBlock *b = c->mdblocks + c->mdnblocks++;
    xset(b, 0, sizeof(*b));
    b->kind = kind;
    b->saveindent = c->ts->indent;
    b->savefill = c->ts->fill;
    return b;
}

static MdBlock *mdtop(Ctx *c, i32 kind)
{
    for (i32 i = c->mdnblocks-1; i >= 0; i--) {
        if (c->mdblocks[i].kind == kind) {
            return c->mdblocks + i;
        }
    }
    return 0;
}

static void mdpop(Ctx *c, i32 kind)
{
    MdBlock *b = mdtop(c, kind);
    if (!b) {
        return;
    }
    c->ts->indent = b->saveindent;
    c->ts->fill   = b->savefill;
    c->mdnblocks  = (i32)(b - c->mdblocks);
}

// Note that a block's body begins here.
static void mdbody(Ctx *c, MdBlock *b)
{
    b->lines = c->ts->lines;
    b->body  = c->ts->ntoks;
}

// Is nothing added to the block's body since it began?
static b32 mdatbody(Ctx *c, MdBlock *b)
{
    return b && b->lines==c->ts->lines && b->body==c->ts->ntoks;
}

// Finish a list item's tag: body on the same line when it fits.
static void mdtagdone(Ctx *c, MdBlock *b)
{
    Typeset *ts = c->ts;
    tscommit(ts);
    i32 w = tspending(ts);
    b32 any = hasglyphs(ts, 0, ts->ntoks);
    b32 wrapped = ts->lines != b->lines;
    c->mdnospace = 0;
    c->mdwrote   = 0;  // the gap below replaces the line's end space
    switch (b->type) {
    case LT_TAG:
    case LT_HANG:
        // A tag fits beside the body when no wider than the width
        // argument itself (the width includes two columns of gap)
        if (any && !wrapped && (w+2<=b->width || b->type==LT_HANG)) {
            tsfreeze(ts);
            i32 gap = b->width - w;
            tsmotion(ts, gap>0 ? gap : 1);
            ts->word = ts->ntoks;
            ts->tindent = b->indent;
            c->mdnospace = 1;  // the gap is the body's leading space
        } else if (any) {
            tsbreak(ts);
        } else {
            ts->tindent = -1;  // the body of an empty tag starts at its indent
        }
        ts->indent = b->indent + b->width;
        break;
    case LT_OHANG:
        tsbreak(ts);
        ts->indent = b->indent;
        break;
    case LT_INSET:
    case LT_DIAG:
        // Like mandoc, one nonbreaking space after an inset head, two
        // after any diag head, whether or not it ends a sentence, and
        // the body's first word joins the head's last word
        if (any || b->type==LT_DIAG) {
            tsunspace(ts);
            tsreopen(ts);
            tsmotion(ts, b->type==LT_DIAG ? 2 : 1);
            c->mdnospace = 1;
        }
        ts->indent = b->indent;
        break;
    }
    mdbody(c, b);
}

// Does a macro read the rest of its line as words, even quoted macro
// names? Like mandoc, the enclosures do.
static b32 mdencloses(i32 kind)
{
    switch (kind) {
    case D_AQ: case D_BQ: case D_BRQ: case D_DQ: case D_OP: case D_PQ:
    case D_QL: case D_QQ: case D_SQ: case D_AO: case D_BO: case D_BRO:
    case D_DO: case D_EO: case D_OO: case D_PO: case D_QO: case D_SO:
    case D_XO:
        return 1;
    }
    return 0;
}

// Does an argument end a -column cell: a tab, or Ta? Like mandoc, a
// quoted Ta is a word unless the macro reading it calls macros.
static b32 mdcellend(MdArgs *a, i32 i, b32 called)
{
    Str s = a->args[i];
    if (a->quoted[i]) {
        return called && equals(s, S("Ta"));
    }
    return equals(s, S("Ta")) || equals(s, S("\t")) || equals(s, S("\t "));
}

static void mditem(Ctx *c, MdArgs *a, Arena scratch)
{
    Typeset *ts = c->ts;
    MdBlock *b = mdtop(c, D_BL);
    if (!b) {
        mdrest(c, a, F_R, scratch);
        return;
    }
    // Like mandoc, no blank line follows a -diag item without a body
    b32 nobody = b->type==LT_DIAG && mdatbody(c, b);
    tsbreak(ts);
    if (!b->compact && b->count && b->type!=LT_COLUMN && !nobody) {
        tsvspace(ts, 1);
    }
    b->count++;
    ts->indent  = b->indent;
    ts->tindent = -1;
    c->mdnospace = 0;
    c->mdwrote   = 0;
    mdbody(c, b);
    switch (b->type) {
    case LT_ITEM:
        break;
    case LT_BULLET:
    case LT_DASH:
    case LT_ENUM: {
        Str mark = b->type==LT_BULLET ? glyph(c, S("bu")) : S("-");
        if (b->type == LT_ENUM) {
            mark = concat(&scratch, fmti64(&scratch, b->count), S("."));
        }
        tsglyph(ts, mark, F_R, 0);
        i32 w = tspending(ts);
        tsfreeze(ts);
        tsmotion(ts, b->width>w ? b->width-w : 1);
        ts->word = ts->ntoks;
        ts->tindent = b->indent;
        ts->indent  = b->indent + b->width;
        c->mdnospace = 1;
        mdbody(c, b);
    } break;
    case LT_COLUMN: {
        // Cells separated by Ta or tabs, each starting at its column,
        // and later lines of a cell, or of the item body, align with it.
        // Macros end with their cell. Like mandoc, an overfull cell puts
        // the next on a new line, and cells past the declared columns
        // pack together.
        i32 n = a->n, col = 0, x = 0;
        i32 gap = b->ncols<5 ? 4 : b->ncols==5 ? 3 : 1;
        ts->tindent = b->indent;
        for (;;) {
            i32 end = a->i;
            for (b32 called = 0; end<n && !mdcellend(a, end, called); end++) {
                i32 k = !a->quoted[end] || called ? mdcallable(c, a->args[end]) : 0;
                called = k ? !mdencloses(k) : called;
            }
            a->n     = end;
            a->block = 1;  // each cell parses like a new .It line
            mdrest(c, a, F_R, scratch);
            a->n     = n;
            if (end == n) {
                return;
            }
            Str sep = a->args[end];
            a->i = end + 1;
            tscommit(ts);
            i32 cur = tsstart(ts) - b->indent + tspending(ts);
            b32 extra = col >= b->ncols;
            x += extra ? 0 : b->colw[col] + gap;
            col++;
            if (!extra && cur>=x) {
                tsbreak(ts);
            } else {
                ts->tindent = tsstart(ts);  // the pending line keeps its start
                tsfreeze(ts);
                if (x > cur) tsmotion(ts, x-cur);
                ts->word = ts->ntoks;
            }
            ts->indent = b->indent + x;
            c->mdnospace = !equals(sep, S("\t "));
            c->mdwrote   = 0;
            mdbody(c, b);
        }
    }
    default: {
        // Tagged forms: the arguments are the tag, and a long tag
        // wraps to the body indentation
        ts->tindent = b->indent;
        ts->indent  = b->indent + b->width;
        b32 diag = b->type == LT_DIAG;
        i32 saved = c->font;
        for (i32 i = a->i; i < a->n; i++) {
            if (!a->quoted[i] && equals(a->args[i], S("Xo"))) {
                c->mdintag = 1;
            }
        }
        mdrest(c, a, diag ? F_B : F_R, scratch);
        c->font = saved;
        if (!c->mdintag) {
            mdtagdone(c, b);
        }
        return;
    }
    }
    mdrest(c, a, F_R, scratch);
}

static Str mdstd(Ctx *c, Str arg, Arena *a)
{
    static struct { Str name, text; } stds[] = {
        {STR("-ansiC"), STR("ANSI X3.159-1989 (\\(lqANSI\\~C89\\(rq)")},
        {STR("-ansiC-89"), STR("ANSI X3.159-1989 (\\(lqANSI\\~C89\\(rq)")},
        {STR("-isoC"), STR("ISO/IEC 9899:1990 (\\(lqISO\\~C90\\(rq)")},
        {STR("-isoC-90"), STR("ISO/IEC 9899:1990 (\\(lqISO\\~C90\\(rq)")},
        {STR("-isoC-amd1"), STR("ISO/IEC 9899/AMD1:1995 (\\(lqISO\\~C90, Amendment 1\\(rq)")},
        {STR("-isoC-99"), STR("ISO/IEC 9899:1999 (\\(lqISO\\~C99\\(rq)")},
        {STR("-isoC-2011"), STR("ISO/IEC 9899:2011 (\\(lqISO\\~C11\\(rq)")},
        {STR("-iso9945-1-90"), STR("ISO/IEC 9945-1:1990 (\\(lqPOSIX.1\\(rq)")},
        {STR("-iso9945-1-96"), STR("ISO/IEC 9945-1:1996 (\\(lqPOSIX.1\\(rq)")},
        {STR("-iso9945-2-93"), STR("ISO/IEC 9945-2:1993 (\\(lqPOSIX.2\\(rq)")},
        {STR("-p1003.1"), STR("IEEE Std 1003.1 (\\(lqPOSIX.1\\(rq)")},
        {STR("-p1003.1-88"), STR("IEEE Std 1003.1-1988 (\\(lqPOSIX.1\\(rq)")},
        {STR("-p1003.1-90"), STR("IEEE Std 1003.1-1990 (\\(lqPOSIX.1\\(rq)")},
        {STR("-p1003.1-96"), STR("ISO/IEC 9945-1:1996 (\\(lqPOSIX.1\\(rq)")},
        {STR("-p1003.1b-93"), STR("IEEE Std 1003.1b-1993 (\\(lqPOSIX.1b\\(rq)")},
        {STR("-p1003.1c-95"), STR("IEEE Std 1003.1c-1995 (\\(lqPOSIX.1c\\(rq)")},
        {STR("-p1003.1g-2000"), STR("IEEE Std 1003.1g-2000 (\\(lqPOSIX.1g\\(rq)")},
        {STR("-p1003.1i-95"), STR("IEEE Std 1003.1i-1995 (\\(lqPOSIX.1i\\(rq)")},
        {STR("-p1003.1-2001"), STR("IEEE Std 1003.1-2001 (\\(lqPOSIX.1\\(rq)")},
        {STR("-p1003.1-2004"), STR("IEEE Std 1003.1-2004 (\\(lqPOSIX.1\\(rq)")},
        {STR("-p1003.1-2008"), STR("IEEE Std 1003.1-2008 (\\(lqPOSIX.1\\(rq)")},
        {STR("-p1003.1-2024"), STR("IEEE Std 1003.1-2024 (\\(lqPOSIX.1\\(rq)")},
        {STR("-p1003.2"), STR("IEEE Std 1003.2 (\\(lqPOSIX.2\\(rq)")},
        {STR("-p1003.2-92"), STR("IEEE Std 1003.2-1992 (\\(lqPOSIX.2\\(rq)")},
        {STR("-p1003.2a-92"), STR("IEEE Std 1003.2a-1992 (\\(lqPOSIX.2\\(rq)")},
        {STR("-svid4"), STR("System\\~V Interface Definition, Fourth Edition (\\(lqSVID4\\(rq)")},
        {STR("-susv1"), STR("Version\\~1 of the Single UNIX Specification (\\(lqSUSv1\\(rq)")},
        {STR("-susv2"), STR("Version\\~2 of the Single UNIX Specification (\\(lqSUSv2\\(rq)")},
        {STR("-susv3"), STR("Version\\~3 of the Single UNIX Specification (\\(lqSUSv3\\(rq)")},
        {STR("-susv4"), STR("Version\\~4 of the Single UNIX Specification (\\(lqSUSv4\\(rq)")},
        {STR("-xpg3"), STR("X/Open Portability Guide Issue\\~3 (\\(lqXPG3\\(rq)")},
        {STR("-xpg4"), STR("X/Open Portability Guide Issue\\~4 (\\(lqXPG4\\(rq)")},
        {STR("-xpg4.2"), STR("X/Open Portability Guide Issue\\~4, Version\\~2 (\\(lqXPG4.2\\(rq)")},
        {STR("-xsh5"), STR("X/Open System Interfaces and Headers Issue\\~5 (\\(lqXSH5\\(rq)")},
        {STR("-xcurses4.2"), STR("X/Open Curses Issue\\~4, Version\\~2 (\\(lqXCURSES4.2\\(rq)")},
        {STR("-ieee754"), STR("IEEE Std 754-1985")},
        {STR("-iso8601"), STR("ISO 8601")},
        {STR("-iso8802-3"), STR("ISO 8802-3: 1989")},
    };
    (void)c;
    (void)a;
    for (i32 i = 0; i < countof(stds); i++) {
        if (equals(arg, stds[i].name)) {
            return stds[i].text;
        }
    }
    return arg;
}

static Str mdatt(Str v, Arena *a)
{
    static struct { Str v, text; } vers[] = {
        {STR("v1"), STR("Version\\~1 AT&T UNIX")}, {STR("v2"), STR("Version\\~2 AT&T UNIX")},
        {STR("v3"), STR("Version\\~3 AT&T UNIX")}, {STR("v4"), STR("Version\\~4 AT&T UNIX")},
        {STR("v5"), STR("Version\\~5 AT&T UNIX")}, {STR("v6"), STR("Version\\~6 AT&T UNIX")},
        {STR("v7"), STR("Version\\~7 AT&T UNIX")}, {STR("32v"), STR("Version\\~32V AT&T UNIX")},
        {STR("III"), STR("AT&T System\\~III UNIX")}, {STR("V"), STR("AT&T System\\~V UNIX")},
        {STR("V.1"), STR("AT&T System\\~V Release\\~1 UNIX")},
        {STR("V.2"), STR("AT&T System\\~V Release\\~2 UNIX")},
        {STR("V.3"), STR("AT&T System\\~V Release\\~3 UNIX")},
        {STR("V.4"), STR("AT&T System\\~V Release\\~4 UNIX")},
    };
    (void)a;
    for (i32 i = 0; i < countof(vers); i++) {
        if (equals(v, vers[i].v)) return vers[i].text;
    }
    return S("AT&T UNIX");
}

// Library descriptions for .Lb, from mandoc
static Str mdlib(Str name)
{
    static struct { Str name, text; } libs[] = {
        {STR("lib80211"), STR("802.11 Wireless Network Management Library (lib80211, \\-l80211)")},
        {STR("libalias"), STR("Packet Aliasing Library (libalias, \\-lalias)")},
        {STR("libarchive"), STR("Streaming Archive Library (libarchive, \\-larchive)")},
        {STR("libarm"), STR("ARM Architecture Library (libarm, \\-larm)")},
        {STR("libarm32"), STR("ARM32 Architecture Library (libarm32, \\-larm32)")},
        {STR("libbe"), STR("Boot Environment Library (libbe, \\-lbe)")},
        {STR("libbluetooth"), STR("Bluetooth Library (libbluetooth, \\-lbluetooth)")},
        {STR("libbsdxml"), STR("eXpat XML parser library (libbsdxml, \\-lbsdxml)")},
        {STR("libbsm"), STR("Basic Security Module Library (libbsm, \\-lbsm)")},
        {STR("libc"), STR("Standard C\\~Library (libc, \\-lc)")},
        {STR("libc_r"), STR("Reentrant C\\~Library (libc_r, \\-lc_r)")},
        {STR("libcalendar"), STR("Calendar Arithmetic Library (libcalendar, \\-lcalendar)")},
        {STR("libcam"), STR("Common Access Method User Library (libcam, \\-lcam)")},
        {STR("libcasper"), STR("Casper Library (libcasper, \\-lcasper)")},
        {STR("libcdk"), STR("Curses Development Kit Library (libcdk, \\-lcdk)")},
        {STR("libcipher"), STR("FreeSec Crypt Library (libcipher, \\-lcipher)")},
        {STR("libcompat"), STR("Compatibility Library (libcompat, \\-lcompat)")},
        {STR("libcrypt"), STR("Crypt Library (libcrypt, \\-lcrypt)")},
        {STR("libcurses"), STR("Curses Library (libcurses, \\-lcurses)")},
        {STR("libcuse"), STR("Userland Character Device Library (libcuse, \\-lcuse)")},
        {STR("libdevattr"), STR("Device attribute and event library (libdevattr, \\-ldevattr)")},
        {STR("libdevctl"), STR("Device Control Library (libdevctl, \\-ldevctl)")},
        {STR("libdevinfo"), STR("Device and Resource Information Utility Library (libdevinfo, \\-ldevinfo)")},
        {STR("libdevstat"), STR("Device Statistics Library (libdevstat, \\-ldevstat)")},
        {STR("libdisk"), STR("Interface to Slice and Partition Labels Library (libdisk, \\-ldisk)")},
        {STR("libdl"), STR("Dynamic Linker Services Filter (libdl, \\-ldl)")},
        {STR("libdm"), STR("Device Mapper Library (libdm, \\-ldm)")},
        {STR("libdwarf"), STR("DWARF Access Library (libdwarf, \\-ldwarf)")},
        {STR("libedit"), STR("Command Line Editor Library (libedit, \\-ledit)")},
        {STR("libefi"), STR("EFI Runtime Services Library (libefi, \\-lefi)")},
        {STR("libelf"), STR("ELF Access Library (libelf, \\-lelf)")},
        {STR("libevent"), STR("Event Notification Library (libevent, \\-levent)")},
        {STR("libexecinfo"), STR("Backtrace Information Library (libexecinfo, \\-lexecinfo)")},
        {STR("libfetch"), STR("File Transfer Library (libfetch, \\-lfetch)")},
        {STR("libform"), STR("Curses Form Library (libform, \\-lform)")},
        {STR("libfsid"), STR("Filesystem Identification Library (libfsid, \\-lfsid)")},
        {STR("libftpio"), STR("FTP Connection Management Library (libftpio, \\-lftpio)")},
        {STR("libgeom"), STR("Userland API Library for Kernel GEOM subsystem (libgeom, \\-lgeom)")},
        {STR("libgpio"), STR("General-Purpose Input Output (GPIO) library (libgpio, \\-lgpio)")},
        {STR("libhammer"), STR("HAMMER Filesystem Userland Library (libhammer, \\-lhammer)")},
        {STR("libi386"), STR("i386 Architecture Library (libi386, \\-li386)")},
        {STR("libintl"), STR("Internationalized Message Handling Library (libintl, \\-lintl)")},
        {STR("libipsec"), STR("IPsec Policy Control Library (libipsec, \\-lipsec)")},
        {STR("libiscsi"), STR("iSCSI protocol library (libiscsi, \\-liscsi)")},
        {STR("libisns"), STR("Internet Storage Name Service Library (libisns, \\-lisns)")},
        {STR("libjail"), STR("Jail Library (libjail, \\-ljail)")},
        {STR("libkcore"), STR("Kernel Memory Core Access Library (libkcore, \\-lkcore)")},
        {STR("libkiconv"), STR("Kernel-side iconv Library (libkiconv, \\-lkiconv)")},
        {STR("libkse"), STR("N:M Threading Library (libkse, \\-lkse)")},
        {STR("libkvm"), STR("Kernel Data Access Library (libkvm, \\-lkvm)")},
        {STR("libm"), STR("Math Library (libm, \\-lm)")},
        {STR("libm68k"), STR("m68k Architecture Library (libm68k, \\-lm68k)")},
        {STR("libmagic"), STR("Magic Number Recognition Library (libmagic, \\-lmagic)")},
        {STR("libmandoc"), STR("Mandoc Macro Compiler Library (libmandoc, \\-lmandoc)")},
        {STR("libmd"), STR("Message Digest (MD4, MD5, etc.) Support Library (libmd, \\-lmd)")},
        {STR("libmemstat"), STR("Kernel Memory Allocator Statistics Library (libmemstat, \\-lmemstat)")},
        {STR("libmenu"), STR("Curses Menu Library (libmenu, \\-lmenu)")},
        {STR("libmj"), STR("Minimalist JSON library (libmj, \\-lmj)")},
        {STR("libnetgraph"), STR("Netgraph User Library (libnetgraph, \\-lnetgraph)")},
        {STR("libnetpgp"), STR("Netpgp Signing, Verification, Encryption and Decryption (libnetpgp, \\-lnetpgp)")},
        {STR("libnetpgpverify"), STR("Netpgp Verification (libnetpgpverify, \\-lnetpgpverify)")},
        {STR("libnpf"), STR("NPF Packet Filter Library (libnpf, \\-lnpf)")},
        {STR("libnv"), STR("Name/value pairs library (libnv, \\-lnv)")},
        {STR("libossaudio"), STR("OSS Audio Emulation Library (libossaudio, \\-lossaudio)")},
        {STR("libpam"), STR("Pluggable Authentication Module Library (libpam, \\-lpam)")},
        {STR("libpanel"), STR("Z-order for curses windows (libpanel, \\-lpanel)")},
        {STR("libpcap"), STR("Packet capture Library (libpcap, \\-lpcap)")},
        {STR("libpci"), STR("PCI Bus Access Library (libpci, \\-lpci)")},
        {STR("libpmc"), STR("Performance Counters Library (libpmc, \\-lpmc)")},
        {STR("libposix"), STR("POSIX Compatibility Library (libposix, \\-lposix)")},
        {STR("libposix1e"), STR("POSIX.1e Security API Library (libposix1e, \\-lposix1e)")},
        {STR("libppath"), STR("Property-List Paths Library (libppath, \\-lppath)")},
        {STR("libproc"), STR("Processor Monitoring and Analysis Library (libproc, \\-lproc)")},
        {STR("libprocstat"), STR("Process and Files Information Retrieval (libprocstat, \\-lprocstat)")},
        {STR("libprop"), STR("Property Container Object Library (libprop, \\-lprop)")},
        {STR("libpthread"), STR("POSIX Threads Library (libpthread, \\-lpthread)")},
        {STR("libpthread_dbg"), STR("POSIX Debug Threads Library (libpthread_dbg, \\-lpthread_dbg)")},
        {STR("libpuffs"), STR("puffs Convenience Library (libpuffs, \\-lpuffs)")},
        {STR("libquota"), STR("Disk Quota Access and Control Library (libquota, \\-lquota)")},
        {STR("libradius"), STR("RADIUS Client Library (libradius, \\-lradius)")},
        {STR("librefuse"), STR("File System in Userspace Convenience Library (librefuse, \\-lrefuse)")},
        {STR("libresolv"), STR("DNS Resolver Library (libresolv, \\-lresolv)")},
        {STR("librpcsec_gss"), STR("RPC GSS-API Authentication Library (librpcsec_gss, \\-lrpcsec_gss)")},
        {STR("librpcsvc"), STR("RPC Service Library (librpcsvc, \\-lrpcsvc)")},
        {STR("librt"), STR("POSIX Real\\-time Library (librt, \\-lrt)")},
        {STR("librtld_db"), STR("Debugging interface to the runtime linker Library (librtld_db, \\-lrtld_db)")},
        {STR("librumpclient"), STR("Clientside Stubs for rump Kernel Remote Protocols (librumpclient, \\-lrumpclient)")},
        {STR("libsaslc"), STR("Simple Authentication and Security Layer client library (libsaslc, \\-lsaslc)")},
        {STR("libsbuf"), STR("Safe String Composition Library (libsbuf, \\-lsbuf)")},
        {STR("libsdp"), STR("Bluetooth Service Discovery Protocol User Library (libsdp, \\-lsdp)")},
        {STR("libssp"), STR("Buffer Overflow Protection Library (libssp, \\-lssp)")},
        {STR("libstand"), STR("Standalone Applications Library (libstand, \\-lstand)")},
        {STR("libstdthreads"), STR("C11 Threads Library (libstdthreads, \\-lstdthreads)")},
        {STR("libsysdecode"), STR("System Argument Decoding Library (libsysdecode, \\-lsysdecode)")},
        {STR("libSystem"), STR("System Library (libSystem, \\-lSystem)")},
        {STR("libtacplus"), STR("TACACS+ Client Library (libtacplus, \\-ltacplus)")},
        {STR("libtcplay"), STR("TrueCrypt-compatible API library (libtcplay, \\-ltcplay)")},
        {STR("libtermcap"), STR("Termcap Access Library (libtermcap, \\-ltermcap)")},
        {STR("libterminfo"), STR("Terminal Information Library (libterminfo, \\-lterminfo)")},
        {STR("libthr"), STR("1:1 Threading Library (libthr, \\-lthr)")},
        {STR("libufs"), STR("UFS File System Access Library (libufs, \\-lufs)")},
        {STR("libugidfw"), STR("File System Firewall Interface Library (libugidfw, \\-lugidfw)")},
        {STR("libulog"), STR("User Login Record Library (libulog, \\-lulog)")},
        {STR("libusbhid"), STR("USB Human Interface Devices Library (libusbhid, \\-lusbhid)")},
        {STR("libutil"), STR("System Utilities Library (libutil, \\-lutil)")},
        {STR("libvgl"), STR("Video Graphics Library (libvgl, \\-lvgl)")},
        {STR("libx86_64"), STR("x86_64 Architecture Library (libx86_64, \\-lx86_64)")},
        {STR("libxo"), STR("Text, XML, JSON, and HTML Output Emission Library (libxo, \\-lxo)")},
        {STR("libz"), STR("Compression Library (libz, \\-lz)")},
    };
    for (i32 i = 0; i < countof(libs); i++) {
        if (equals(name, libs[i].name)) {
            return libs[i].text;
        }
    }
    return (Str){0};
}

// Position of an .Rs part in mandoc's output order
static i32 mdrefrank(u8 part)
{
    Str order = S("ATBIJRNVUPQCDO");
    i32 i = 0;
    for (; i<order.len && order.data[i]!=part; i++) {}
    return i;
}

// Begin a SYNOPSIS element on its own line, after a blank line if the
// previous line was an element other than its like or its function's
// type. Like mandoc, an .Nm block extends to the next .Nm, its first
// element continues the name's line, and the next .Nm gets a blank
// line only after a function or .Fd.
static void mdsynbreak(Ctx *c, i32 kind)
{
    Typeset *ts = c->ts;
    if (c->mdsection != MS_SYNOPSIS) {
        return;
    }
    i32 prev = c->mdsynprev;
    c->mdsynprev = kind;
    b32 nm = kind==D_NM && c->mdnmbody;  // ends an .Nm block
    if (!nm && prev==D_NM) {
        return;
    }
    tsbreak(ts);
    b32 fn    = prev==D_FD || prev==D_FN || prev==D_FO;
    b32 blank = nm ? fn :
                prev==kind ? kind==D_FT || kind==D_FN || kind==D_FO :
                prev==D_FT ? kind!=D_FN && kind!=D_FO :
                fn || prev==D_IN || prev==D_VT;
    if (blank) {
        tsvspace(ts, 1);
    }
}

// Left edge of SYNOPSIS elements: the body of an .Nm block, if any.
static i32 mdsynindent(Ctx *c)
{
    return c->mdnmbody ? c->mdnmbody : MDIN;
}

static void mdmacro(Ctx *c, i32 kind, Str name, MdArgs *a, Arena scratch)
{
    Typeset *ts = c->ts;
    b32 syn = c->mdsection == MS_SYNOPSIS;
    switch (kind) {
    case D_AD: case D_AR: case D_EM: case D_VA: case D_PA: case D_FA: case D_FT:
    case D_VT: {
        b32 line = a->i==1;  // first macro on the line
        if (syn && (kind==D_FT || (kind==D_VT && line))) {
            mdsynbreak(c, kind);
        }
        if (kind==D_FA && c->mdfo) {
            // Function arguments inside .Fo
            i32 end = mdcontentend(a);
            for (; a->i < end; a->i++) {
                if (c->mdfoargs++) {
                    mdword(c, S(","), F_R, scratch);
                }
                mdword(c, syn ? mdunbreakable(a->args[a->i], &scratch) : a->args[a->i], F_I, scratch);
            }
            break;
        }
        if (!mdhasword(c, a) && kind==D_AR) {
            mdword(c, S("file\\ ..."), F_I, scratch);
        } else if (!mdhasword(c, a) && kind==D_PA) {
            mdword(c, S("~"), F_I, scratch);
        }
        mdinline(c, a, F_I, scratch);
        if (syn && line && (kind==D_FT || kind==D_VT)) {
            mdrest(c, a, F_R, scratch);
            tsbreak(ts);
        }
    } break;

    case D_CM: case D_IC: case D_SY: case D_CD: case D_FD: case D_MS:
        if (syn && (kind==D_FD || kind==D_CD)) {
            mdsynbreak(c, kind);
        }
        mdinline(c, a, F_B, scratch);
        if (syn && (kind==D_FD || kind==D_CD)) {
            mdrest(c, a, F_R, scratch);
            tsbreak(ts);
        }
        break;

    case D_DV: case D_ER: case D_EV: case D_LI: case D_NO: case D_TN: case D_MT:
        mdinline(c, a, F_R, scratch);
        break;

    case D_SX:
        mdinline(c, a, F_R, scratch);
        break;

    case D_NM: {
        b32 line = a->i == 1;
        if (!c->mdname.len && a->i<a->n && !mddelim(a->args[a->i]) && !mdcallable(c, a->args[a->i])) {
            c->mdname = clone(&c->perm, a->args[a->i]);
        }
        if (syn && line) {
            // Synopsis line: hanging indent after the name
            mdsynbreak(c, D_NM);
            Str nm = a->i<a->n && !mddelim(a->args[a->i]) && !mdcallable(c, a->args[a->i])
                   ? a->args[a->i] : c->mdname;
            i32 w = textwidth(c, nm, scratch);
            c->mdnmbody = MDIN + w + 1;
            ts->tindent = MDIN;
            ts->indent  = c->mdnmbody;
        }
        if (!mdhasword(c, a) && c->mdname.len) {
            mdword(c, c->mdname, F_B, scratch);
        }
        mdinline(c, a, F_B, scratch);
    } break;

    case D_FL: {
        // Each argument gets a leading dash; no arguments: a lone dash
        b32 any = 0;
        while (a->i < a->n) {
            Str w = a->args[a->i];
            if (mdcallable(c, w)) break;
            if (mddelim(w)) {
                if (!any) {
                    mdword(c, S("\\-"), F_B, scratch);
                    any = 1;
                }
                mdword(c, w, F_R, scratch);
            } else {
                mdword(c, concat(&scratch, S("\\-"), w), F_B, scratch);
                any = 1;
            }
            a->i++;
        }
        if (!any) {
            // A bare flag joins a following macro: "Fl Fl x" is "--x"
            mdword(c, S("\\-"), F_B, scratch);
            c->mdnospace = a->i < a->n;
        }
    } break;

    case D_XR: {
        if (mdnoarg(c, a)) {
            break;
        }
        Str page = a->args[a->i++];
        Str sec  = a->i<a->n && !mddelim(a->args[a->i]) && !mdcallable(c, a->args[a->i])
                 ? a->args[a->i++] : (Str){0};
        Str text = page;
        if (sec.data) {
            text = concat(&scratch, page, S("("));
            text = concat(&scratch, text, sec);
            text = concat(&scratch, text, S(")"));
        }
        mdword(c, text, F_R, scratch);
    } break;

    case D_FN: {
        if (a->i == a->n) {
            break;  // its name is never a macro
        }
        mdsynbreak(c, D_FN);
        if (syn) {
            if (tsempty(ts)) ts->tindent = mdsynindent(c);  // not after .Nm's name
            ts->indent = mdsynindent(c) + 4;
        }
        // Arguments end at a delimiter or a callable macro
        Str fn = a->args[a->i++];
        i32 end = a->i;
        for (; end<a->n && !mddelim(a->args[end]) && !mdcallable(c, a->args[end]); end++) {}
        mdword(c, fn, F_B, scratch);
        c->mdnospace = 1;
        mdword(c, S("("), F_R, scratch);
        for (i32 k = 0; a->i < end; k++, a->i++) {
            if (k) mdword(c, S(","), F_R, scratch);
            mdword(c, syn ? mdunbreakable(a->args[a->i], &scratch) : a->args[a->i], F_I, scratch);
        }
        mdword(c, S(")"), F_R, scratch);
        if (syn) {
            mdword(c, S(";"), F_R, scratch);
            mdrest(c, a, F_R, scratch);
            tsbreak(ts);
            ts->indent = mdsynindent(c);
        }
    } break;

    case D_FO: {
        mdsynbreak(c, D_FO);
        if (syn) {
            if (tsempty(ts)) ts->tindent = mdsynindent(c);  // not after .Nm's name
            ts->indent = mdsynindent(c) + 4;
        }
        if (a->i < a->n) {
            mdword(c, a->args[a->i++], F_B, scratch);
        }
        c->mdnospace = 1;
        mdword(c, S("("), F_R, scratch);
        c->mdfo = 1;
        c->mdfoargs = 0;
    } break;

    case D_FC:
        c->mdfo = 0;
        mdword(c, S(")"), F_R, scratch);
        if (syn) {
            mdword(c, S(";"), F_R, scratch);
            mdrest(c, a, F_R, scratch);
            tsbreak(ts);
            ts->indent = mdsynindent(c);
        }
        break;

    case D_IN:
        if (mdnoarg(c, a)) {
            break;
        } else if (syn && a->i==1) {
            mdsynbreak(c, D_IN);
            Str f = a->args[a->i++];
            mdword(c, concat(&scratch, S("#include\\ <"), concat(&scratch, f, S(">"))), F_B, scratch);
        } else {
            Str f = a->args[a->i++];
            mdword(c, S("<"), F_R, scratch);
            c->mdnospace = 1;
            mdword(c, f, F_I, scratch);
            c->mdnospace = 1;
            mdword(c, S(">"), F_R, scratch);
        }
        break;

    case D_AN: {
        if (a->i<a->n && equals(a->args[a->i], S("-split"))) {
            c->mdsplit = c->mdsplitset = 1;
            a->i++;
            break;
        }
        if (a->i<a->n && equals(a->args[a->i], S("-nosplit"))) {
            c->mdsplit = 0;
            c->mdsplitset = 1;
            a->i++;
            break;
        }
        if (c->mdsection==MS_AUTHORS && a->i==1) {
            if (c->mdauthors && (c->mdsplit || !c->mdsplitset)) {
                tsbreak(ts);
            }
            c->mdauthors = 1;
        }
        mdinline(c, a, F_R, scratch);
    } break;

    case D_AP:
        c->mdnospace = 1;
        mdword(c, S("'"), F_R, scratch);
        c->mdnospace = 1;
        break;

    case D_NS:
        if (a->i == 1) {
            break;  // like mandoc, ignored first on a line
        }
        c->mdnospace = 1;
        if (ts->ntoks && ts->toks[ts->ntoks-1].type==TK_SPACE) {
            ts->ntoks--;
            ts->word = ts->ntoks;
        }
        break;

    case D_PF:
        if (a->i < a->n) {
            mdword(c, a->args[a->i++], F_R, scratch);
            c->mdnospace = 1;
        }
        break;

    case D_SM: {
        if (a->i<a->n && equals(a->args[a->i], S("on"))) {
            c->mdsmoff = 0;
            a->i++;
        } else if (a->i<a->n && equals(a->args[a->i], S("off"))) {
            c->mdsmoff = 1;
            a->i++;
        } else {
            c->mdsmoff = !c->mdsmoff;
        }
        // Like mandoc, on cancels a no-space, but not at the start of a
        // line or of an item body after the gap of its tag, mark, or cell
        MdBlock *b = mdtop(c, D_BL);
        b32 fresh = !ts->ntoks ||
                    (mdatbody(c, b) && b->type!=LT_INSET && b->type!=LT_DIAG);
        if (!fresh) {
            c->mdnospace &= c->mdsmoff;
        }
    } break;

    case D_AQ: {
        b32 mt = a->n-a->i>=2 && equals(a->args[a->i], S("Mt"));
        mdenclose(c, a, mt ? S("<") : glyph(c, S("la")),
                  mt ? S(">") : glyph(c, S("ra")), F_R, scratch);
    } break;
    case D_BQ:  mdenclose(c, a, S("["), S("]"), F_R, scratch); break;
    case D_BRQ: mdenclose(c, a, S("{"), S("}"), F_R, scratch); break;
    case D_DQ:  mdenclose(c, a, glyph(c, S("lq")), glyph(c, S("rq")), F_R, scratch); break;
    case D_OP:  mdenclose(c, a, S("["), S("]"), F_R, scratch); break;
    case D_PQ:  mdenclose(c, a, S("("), S(")"), F_R, scratch); break;
    case D_QQ:  mdenclose(c, a, S("\""), S("\""), F_R, scratch); break;
    case D_SQ:
    case D_QL:  mdenclose(c, a, glyph(c, S("oq")), glyph(c, S("cq")), F_R, scratch); break;

    case D_AO: case D_BO: case D_BRO: case D_DO: case D_OO: case D_PO:
    case D_QO: case D_SO: {
        Str open = kind==D_AO ? glyph(c, S("la")) : kind==D_BO ? S("[") :
                   kind==D_BRO ? S("{") : kind==D_DO ? glyph(c, S("lq")) :
                   kind==D_OO ? S("[") : kind==D_PO ? S("(") :
                   kind==D_QO ? S("\"") : glyph(c, S("oq"));
        mdword(c, open, F_R, scratch);
        c->mdnospace = 1;
        a->block = 1;
    } break;

    case D_AC: case D_BC: case D_BRC: case D_DC: case D_OC: case D_PC:
    case D_QC: case D_SC: {
        Str close = kind==D_AC ? glyph(c, S("ra")) : kind==D_BC ? S("]") :
                    kind==D_BRC ? S("}") : kind==D_DC ? glyph(c, S("rq")) :
                    kind==D_OC ? S("]") : kind==D_PC ? S(")") :
                    kind==D_QC ? S("\"") : glyph(c, S("cq"));
        c->mdnospace = 1;
        mdword(c, close, F_R, scratch);
    } break;

    case D_EO:
        if (a->i < a->n) {
            mdword(c, a->args[a->i++], F_R, scratch);
            c->mdnospace = 1;
        }
        a->block = 1;
        break;

    case D_EC:
        c->mdnospace = 1;
        if (a->i < a->n) {
            mdword(c, a->args[a->i++], F_R, scratch);
        }
        break;

    case D_XO:
        a->block = 1;
        break;

    case D_XC:
        if (c->mdintag) {
            c->mdintag = 0;
            mdrest(c, a, F_R, scratch);
            MdBlock *b = mdtop(c, D_BL);
            if (b) mdtagdone(c, b);
        }
        break;

    case D_AT:
        mdword(c, mdatt(a->i<a->n ? a->args[a->i++] : S(""), &scratch), F_R, scratch);
        break;

    case D_BSX: case D_BX: case D_DX: case D_FX: case D_NX: case D_OX:
    case D_UX: {
        Str os = kind==D_BSX ? S("BSD/OS") : kind==D_DX ? S("DragonFly") :
                 kind==D_FX ? S("FreeBSD") : kind==D_NX ? S("NetBSD") :
                 kind==D_OX ? S("OpenBSD") : kind==D_UX ? S("UNIX") : S("BSD");
        Str v = {0};
        if (a->i<a->n && !mddelim(a->args[a->i]) && !mdcallable(c, a->args[a->i])) {
            v = a->args[a->i++];
        }
        if (kind == D_BX) {
            Str text = v.data ? concat(&scratch, v, S("BSD")) : S("BSD");
            if (v.data && a->i<a->n && !mddelim(a->args[a->i]) && !mdcallable(c, a->args[a->i])) {
                text = concat(&scratch, text, S("-"));
                text = concat(&scratch, text, a->args[a->i++]);
            }
            mdword(c, text, F_R, scratch);
        } else if (v.data && kind!=D_UX) {
            mdword(c, concat(&scratch, os, concat(&scratch, S("\\~"), v)), F_R, scratch);
        } else {
            mdword(c, os, F_R, scratch);
            if (v.data) mdword(c, v, F_R, scratch);
        }
    } break;

    case D_ST:
        if (a->i < a->n) {
            mdword(c, mdstd(c, a->args[a->i++], &scratch), F_R, scratch);
        }
        break;

    case D_LK: {
        if (mdnoarg(c, a)) {
            break;
        }
        i32 end = mdcontentend(a);
        Str url = a->i<end ? a->args[a->i++] : S("");
        if (a->i < end) {
            for (; a->i < end; a->i++) {
                mdword(c, a->args[a->i], F_R, scratch);
            }
            c->mdnospace = 1;
            mdword(c, S(":"), F_R, scratch);
        }
        mdword(c, url, F_I, scratch);
    } break;

    case D_EX: {
        if (a->i<a->n && equals(a->args[a->i], S("-std"))) a->i++;
        i32 end = mdcontentend(a);
        i32 n = end - a->i;
        mdword(c, S("The"), F_R, scratch);
        if (!n && c->mdname.len) {
            mdword(c, c->mdname, F_B, scratch);
        }
        for (i32 k = 0; a->i < end; k++, a->i++) {
            if (k && n>2) mdword(c, S(","), F_R, scratch);
            if (k && k==n-1) mdword(c, S("and"), F_R, scratch);
            mdword(c, a->args[a->i], F_B, scratch);
        }
        mdword(c, n>1 ? S("utilities exit\\~0 on success, and\\~>0 if an error occurs.")
                      : S("utility exits\\~0 on success, and\\~>0 if an error occurs."),
               F_R, scratch);
    } break;

    case D_RV: {
        if (a->i<a->n && equals(a->args[a->i], S("-std"))) a->i++;
        i32 end = mdcontentend(a);
        i32 n = end - a->i;
        mdword(c, S("The"), F_R, scratch);
        if (!n) {
            mdword(c, concat(&scratch, c->mdname, S("()")), F_B, scratch);
        }
        for (i32 k = 0; a->i < end; k++, a->i++) {
            if (k && n>2) mdword(c, S(","), F_R, scratch);
            if (k && k==n-1) mdword(c, S("and"), F_R, scratch);
            mdword(c, concat(&scratch, a->args[a->i], S("()")), F_B, scratch);
        }
        mdword(c, n>1 ? S("functions return") : S("function returns"), F_R, scratch);
        mdword(c, S("the value\\~0 if successful; otherwise the value\\~-1 "
                    "is returned and the global variable"), F_R, scratch);
        mdword(c, S("errno"), F_I, scratch);
        mdword(c, S("is set to indicate the error."), F_R, scratch);
    } break;

    case D_LB: {
        if (a->i == a->n) {
            break;
        }
        b32 line = a->i==1;
        Str lib  = a->args[a->i++];
        Str text = mdlib(lib);
        if (!text.data) {
            text = concat(&scratch, S("library \\(lq"), lib);
            text = concat(&scratch, text, S("\\(rq"));
        }
        mdword(c, text, F_R, scratch);
        if (c->mdsection==MS_LIBRARY && line) {
            mdrest(c, a, F_R, scratch);
            tsbreak(ts);
        }
    } break;

    case D_TA:
        mdword(c, S("\\t"), F_R, scratch);
        break;

    case D_IGNORE:
        mdinline(c, a, F_R, scratch);
        break;

    default:
        // Block macros used as callable arguments: just their text
        (void)name;
        mdinline(c, a, F_R, scratch);
        break;
    }
}

static Str mddate(Str s, Arena *a)
{
    s = trim(s);
    if (startswith(s, S("$Mdocdate:"))) {
        s = trim(cuthead(s, 10));
        if (s.len && s.data[s.len-1]=='$') s = trim(takehead(s, s.len-1));
        // "Month D YYYY" -> "Month D, YYYY"
        iz sp = -1;
        for (iz i = s.len-1; i >= 0; i--) {
            if (s.data[i] == ' ') { sp = i; break; }
        }
        if (sp > 0 && s.data[sp-1] != ',') {
            s = concat(a, concat(a, takehead(s, sp), S(",")), cuthead(s, sp));
        }
    }
    return s;
}

static void mdocline(Ctx *c, i32 kind, Str name, Str args, Arena scratch)
{
    Typeset *ts = c->ts;
    MdBlock *bl = kind==D_IT ? mdtop(c, D_BL) : 0;
    b32 cells = bl && bl->type==LT_COLUMN;  // tabs separate cells
    // Index 0 is the macro itself, so a->i==1 means "first on the line"
    MacroArgs m = macroargs(name, args, cells, &scratch);
    Str *argv   = m.args.data + 1;
    i32  nargs  = (i32)m.args.len - 1;
    b32 *quoted = m.quoted.data;
    b32 block = kind==D_SH || kind==D_SS || kind==D_ND || kind==D_D1 ||
                kind==D_DL || kind==D_IT;
    MdArgs a = {m.args.data, quoted, nargs+1, 1, block};

    switch (kind) {
    case D_DD:
        c->mddate = clone(&c->perm, mddate(args, &scratch));
        return;

    case D_DT:
        c->mdtitle = clone(&c->perm, nargs>0 ? argv[0] : S("UNTITLED"));
        c->mdsec   = clone(&c->perm, nargs>1 ? argv[1] : S("1"));
        c->mdvol   = clone(&c->perm, mdvolume(c->mdsec));
        if (nargs > 2) {
            Str v = concat(&scratch, c->mdvol, S(" ("));
            v = concat(&scratch, v, argv[2]);
            c->mdvol = clone(&c->perm, concat(&scratch, v, S(")")));
        }
        return;

    case D_OS:
        c->mdos = clone(&c->perm, joinargs(argv, nargs, &scratch));
        return;

    case D_SH:
    case D_SS: {
        mdheader(c, scratch);
        tsbreak(ts);
        c->mdnblocks = 0;
        tsvspace(ts, 1);
        ts->fill    = 1;
        ts->indent  = MDIN;
        ts->tindent = kind==D_SH ? 0 : MDSS;
        if (kind == D_SH) {
            Str h = joinargs(argv, nargs, &scratch);
            c->mdsection = equals(h, S("NAME")) ? MS_NAME :
                           equals(h, S("SYNOPSIS")) ? MS_SYNOPSIS :
                           equals(h, S("LIBRARY")) ? MS_LIBRARY :
                           equals(h, S("SEE ALSO")) ? MS_SEEALSO :
                           equals(h, S("AUTHORS")) ? MS_AUTHORS : MS_OTHER;
            c->mdauthors = 0;
        }
        c->mdsynprev = 0;
        c->mdnmbody  = 0;
        c->mdnospace = 0;
        mdrest(c, &a, F_B, scratch);
        tsbreak(ts);
        ts->indent  = MDIN;
        ts->tindent = -1;
        ts->nospace = 1;
        c->font = F_R;
        return;
    }
    }

    mdheader(c, scratch);
    switch (kind) {
    case D_PP:
        tsvspace(ts, 1);
        if (c->mdsection == MS_SYNOPSIS) {
            ts->indent = mdsynindent(c);
        }
        return;

    case D_ND: {
        mdword(c, glyph(c, S("en")), F_R, scratch);
        mdrest(c, &a, F_R, scratch);
        tsendline(ts);
        return;
    }

    case D_D1:
    case D_DL: {
        tsbreak(ts);
        i32 indent = ts->indent;
        b32 fill = ts->fill;
        ts->indent += MDDS;
        if (kind == D_DL) ts->fill = 0;
        c->mdnospace = 0;
        mdrest(c, &a, F_R, scratch);
        tsbreak(ts);
        ts->indent = indent;
        ts->fill = fill;
        return;
    }

    case D_BD: {
        tsbreak(ts);
        b32 compact = 0, filled = 1;
        i32 offset = 0;
        for (i32 i = 0; i < nargs; i++) {
            Str w = argv[i];
            if (equals(w, S("-literal")) || equals(w, S("-unfilled"))) {
                filled = 0;
            } else if (equals(w, S("-filled")) || equals(w, S("-ragged")) || equals(w, S("-centered"))) {
                filled = 1;
            } else if (equals(w, S("-compact"))) {
                compact = 1;
            } else if (equals(w, S("-offset")) && i+1<nargs) {
                offset = mdoffset(c, argv[++i], scratch);
            }
        }
        if (!compact) tsvspace(ts, 1);
        MdBlock *b = mdpush(c, D_BD);
        if (!b) return;
        ts->indent = ts->indent+offset>MAXIND ? MAXIND : ts->indent+offset;
        ts->fill   = filled;
        b->indent  = ts->indent;
        b->lines   = ts->lines;
        return;
    }

    case D_ED:
        tsbreak(ts);
        mdpop(c, D_BD);
        return;

    case D_BL: {
        tsbreak(ts);
        MdBlock *b = mdpush(c, D_BL);
        if (!b) return;
        b->type = LT_TAG;
        b->width = -1;
        i32 offset = 0;
        for (i32 i = 0; i < nargs; i++) {
            Str w = argv[i];
            static struct { Str name; i32 type; } types[] = {
                {STR("-tag"), LT_TAG}, {STR("-item"), LT_ITEM},
                {STR("-bullet"), LT_BULLET}, {STR("-dash"), LT_DASH},
                {STR("-hyphen"), LT_DASH}, {STR("-enum"), LT_ENUM},
                {STR("-hang"), LT_HANG}, {STR("-ohang"), LT_OHANG},
                {STR("-inset"), LT_INSET}, {STR("-diag"), LT_DIAG},
                {STR("-column"), LT_COLUMN},
            };
            b32 matched = 0;
            for (i32 t = 0; t < countof(types); t++) {
                if (equals(w, types[t].name)) {
                    b->type = types[t].type;
                    matched = 1;
                }
            }
            if (matched) {
                continue;
            } else if (equals(w, S("-compact"))) {
                b->compact = 1;
            } else if (equals(w, S("-width")) && i+1<nargs) {
                b->width = mdwidth(c, argv[++i], scratch);
            } else if (equals(w, S("-offset")) && i+1<nargs) {
                offset = mdoffset(c, argv[++i], scratch);
            } else if (b->type==LT_COLUMN && b->ncols<countof(b->colw)) {
                // Plain strings: no scaled widths or macro names here
                i32 cw = textwidth(c, w, scratch);
                b->colw[b->ncols++] = cw<0 ? 0 : cw>MAXIND ? MAXIND : cw;
            }
        }
        if (b->width < 0) {
            b->width = b->type==LT_BULLET || b->type==LT_DASH ? 4 :
                       b->type==LT_ENUM ? 5 : MDDS+2;
        }
        if (!b->compact) tsvspace(ts, 1);
        b->indent = ts->indent+offset>MAXIND ? MAXIND : ts->indent+offset;
        return;
    }

    case D_EL:
        tsbreak(ts);
        mdpop(c, D_BL);
        ts->tindent = -1;
        return;

    case D_IT:
        mditem(c, &a, scratch);
        if (cells && !c->mdintag) {
            // Following lines continue the last cell, spaced as after
            // an in-line macro line; a cell without words starts flush
            if (!ts->fill) {
                tsbreak(ts);
            } else if (!c->mdnospace && !c->mdsmoff && c->mdwrote) {
                tsspace(ts, 1 + (c->mdeos ? (ts->sentspace + 6)/12 : 0));
            }
        } else if (!c->mdintag && !c->mdnospace && ts->ntoks && hasglyphs(ts, ts->word, ts->ntoks)) {
            // Text on the following lines continues the item body
            tsendline(ts);
        }
        return;

    case D_RS: {
        // Like mandoc, a blank line separates a reference in SEE ALSO
        // from anything before it in its section, item body, or display
        MdBlock *b = c->mdnblocks ? c->mdblocks+c->mdnblocks-1 : 0;
        if (c->mdsection==MS_SEEALSO && !mdatbody(c, b)) {
            tsvspace(ts, 1);
        }
        c->mdrefn = 0;
        mdpush(c, D_RS);
        return;
    }

    case D_RE: {
        mdpop(c, D_RS);
        i32 nauth = 0;
        b32 quote = 0;  // %T is quoted beside a book or journal
        for (i32 i = 0; i < c->mdrefn; i++) {
            nauth += c->mdrefs[i].part == 'A';
            quote |= c->mdrefs[i].part=='B' || c->mdrefs[i].part=='J';
        }
        Buf b = {0};
        for (i32 i = 0; i < c->mdrefn; i++) {
            MdRef r = c->mdrefs[i];
            if (i && i==nauth-1) {
                bappend(&b, nauth==2 ? S(" and ") : S(", and "), &scratch);
            } else if (i) {
                bappend(&b, S(", "), &scratch);
            }
            if (r.part=='T' && quote) {
                bappend(&b, S("\\(lq"), &scratch);
                bappend(&b, r.v, &scratch);
                bappend(&b, S("\\(rq"), &scratch);
            } else if (r.part=='T' || r.part=='B' || r.part=='I' || r.part=='J') {
                bappend(&b, S("\\fI"), &scratch);
                bappend(&b, r.v, &scratch);
                bappend(&b, S("\\fR"), &scratch);
            } else {
                bappend(&b, r.v, &scratch);
            }
        }
        c->mdrefn = 0;
        if (b.len) {
            bappend(&b, S("."), &scratch);
            Sink k = {0};
            k.c = c;
            mdword(c, S(""), F_R, scratch);
            tokenize(c, bstr(b), &k, scratch);
            tsendline(ts);
        }
        return;
    }

    case D_REF: {
        // Parts sort into mandoc's order and are formatted at .Re
        if (!nargs || c->mdrefn==countof(c->mdrefs)) {
            return;  // like mandoc, skip parts without arguments
        }
        MdRef r = {0};
        r.v    = clone(&c->perm, joinargs(argv, nargs, &scratch));
        r.part = name.len>1 ? name.data[1] : 'O';
        i32 i = c->mdrefn++;
        for (; i>0 && mdrefrank(c->mdrefs[i-1].part)>mdrefrank(r.part); i--) {
            c->mdrefs[i] = c->mdrefs[i-1];
        }
        c->mdrefs[i] = r;
        return;
    }

    case D_BK:
    case D_EK:
    case D_NOOP:
        return;

    case D_BF: {
        Str f = nargs ? argv[0] : S("");
        c->font = equals(f, S("-emphasis")) || equals(f, S("Em")) ? F_I :
                  equals(f, S("-symbolic")) || equals(f, S("Sy")) ? F_B : F_R;
        return;
    }

    case D_EF:
        c->font = F_R;
        return;
    }

    // In-line macro lines
    a.i = 1;
    c->mdwrote = 0;
    mdmacro(c, kind, name, &a, scratch);
    mdrest(c, &a, F_R, scratch);
    c->mdeos &= !quoted[nargs];  // a quoted "." ends no sentence
    if (!c->mdfo && kind!=D_FC) {
        c->mdsynprev = kind;  // .Fo through .Fc is one element
    }
    if (c->mdintag) {
        return;
    }
    if (!ts->fill) {
        // Like mandoc, .Sm off never joins unfilled lines
        if (!c->mdnospace || c->mdsmoff) tsbreak(ts);
    } else if (!c->mdnospace && !c->mdsmoff) {
        // Like mandoc, a sentence ends here only when the line's
        // last output is the sentence-ending delimiter itself. A line
        // without words keeps the space already pending.
        tscommit(ts);
        b32 pending = ts->ntoks && ts->toks[ts->ntoks-1].type==TK_SPACE;
        if (hasglyphs(ts, 0, ts->ntoks) && (c->mdwrote || !pending)) {
            tsspace(ts, 1 + (c->mdeos ? (ts->sentspace + 6)/12 : 0));
        }
    }
}


// Driver

static void initctx(Ctx *c)
{
    Arena *perm = &c->perm;
    for (i32 i = 0; i < countof(builtins); i++) {
        Entry *e = lookup(&c->kinds, builtins[i].name, perm);
        e->num  = builtins[i].kind;
        e->live = 1;
    }
    for (i32 i = 0; i < countof(mdocs); i++) {
        Entry *e = lookup(&c->kinds, mdocs[i].name, perm);
        e->num  = mdocs[i].kind;
        e->inc  = mdocs[i].callable;
        e->live = 1;
    }
    for (i32 i = 0; i < countof(glyphs); i++) {
        Entry *e = lookup(&c->glyphmap, glyphs[i].name, perm);
        e->str  = glyphs[i].utf8;
        e->alt  = glyphs[i].ascii;
        e->live = 1;
        e = lookup(&c->asciimap, glyphs[i].utf8, perm);
        if (!e->live) {
            e->str  = glyphs[i].ascii;
            e->live = 1;
        }
    }
    for (i32 i = 0; i < countof(translits); i++) {
        Entry *e = lookup(&c->asciimap, translits[i].utf8, perm);
        e->str  = translits[i].ascii;
        e->live = 1;
    }
    setstring(c, S("R"),  S("\\(rg"));
    setstring(c, S("Tm"), S("\\(tm"));
    setstring(c, S("lq"), S("\\(lq"));
    setstring(c, S("rq"), S("\\(rq"));
    setstring(c, S("S"),  S(""));
    setstring(c, S("HF"), S("B"));
    c->margin  = IN;
    c->pindent = IN;
    c->pd      = 1;
    c->ts->indent = hcols(IN);
    c->budget  = (i64)1<<20;  // the largest known page uses about 95k
}

static void run(Ctx *c, Arena scratch)
{
    for (;;) {
        Arena tmp = scratch;
        Str line = nextline(c, &tmp);
        if (!line.data) {
            break;
        }
        processline(c, line, tmp, 0, 0);
    }
    if (c->defining) {
        Entry *e = c->defining;
        e->str  = bstr(c->defbody);
        e->live = 1;
        c->defining = 0;
    }
    tsbreak(c->ts);
    endpage(c, scratch);
}

static void usage(Output *out)
{
    print(out, S(
    "usage: nroff [-man|-mdoc] [-Tascii|-Tutf8] [-rLL=Nn] [-dNAME=STR] [-w]"
    " [FILE...]\n"
    "  -m*      accepted and ignored; the format is detected\n"
    "  -t       accepted and ignored; tbl is built in\n"
    "  -Tascii  ASCII output (default)\n"
    "  -Tutf8   UTF-8 output\n"
    "  -rLL=N   line length (default: $MANWIDTH, $COLUMNS, or 78)\n"
    "  -dAD=l   ragged-right man(7) pages (also c, r; default b)\n"
    "  -w       warn about unsupported input\n"
    "  -h       print this message\n"
    "With no FILE, or when FILE is -, read standard input.\n"));
}

static i32 parsewidth(Str s)
{
    Scaled v = scaled(s, 'n');
    if (!v.ok || v.rel) {
        return 0;
    }
    i32 cols = hcols(v.value);
    return cols>=10 && cols<=1000 ? cols : 0;
}

static i32 nroff(i32 argc, u8 **argv, Plt *plt, Arena mem)
{
    iz    cap     = mem.end - mem.beg;
    Arena perm    = mem;
    Arena scratch = mem;
    perm.end    = mem.beg + cap/4*3;
    scratch.beg = perm.end;

    Output *out = newoutput(&perm, plt, 1);
    Output *err = newoutput(&perm, plt, 2);

    Ctx *c = new(&perm, 1, Ctx);
    c->plt = plt;
    c->err = err;
    c->ts  = newtypeset(&perm, out);
    c->perm = perm;  // from here on, allocate only from c->perm
    Typeset *ts = c->ts;

    ts->sgr = plt_sgr(plt) && !plt_getenv(plt, S("GROFF_NO_SGR"), &scratch).data;
    i32 width = parsewidth(plt_getenv(plt, S("MANWIDTH"), &scratch));
    if (!width) {
        width = parsewidth(plt_getenv(plt, S("COLUMNS"), &scratch));
    }

    i32 nfiles = 0;
    Str *files = new(&c->perm, argc+1, Str);
    b32 dashdash = 0;
    for (i32 i = 1; i < argc; i++) {
        Str arg = import(argv[i]);
        if (dashdash || arg.len<2 || arg.data[0]!='-') {
            files[nfiles++] = arg;
            continue;
        }
        if (equals(arg, S("--"))) {
            dashdash = 1;
        } else if (arg.data[1] == 'm' || equals(arg, S("-t")) ||
                   arg.data[1] == 'P') {
            // ignored
        } else if (arg.data[1] == 'T') {
            Str t = cuthead(arg, 2);
            if (!t.len && i+1<argc) {
                t = import(argv[++i]);
            }
            ts->utf8 = equals(t, S("utf8")) || equals(t, S("utf-8"));
        } else if (arg.data[1] == 'r') {
            Str r = cuthead(arg, 2);
            if (!r.len && i+1<argc) {
                r = import(argv[++i]);
            }
            Cut kv = cut(r, '=');
            Str key = kv.head, val = kv.tail;
            if (!kv.ok && key.len > 1) {
                // groff also accepts -rLL78n
                key = takehead(r, 2);
                val = cuthead(r, 2);
                if (key.len<2 || r.len<3) {
                    key = takehead(r, 1);
                    val = cuthead(r, 1);
                }
            }
            if (equals(key, S("LL"))) {
                i32 w = parsewidth(val);
                if (w) width = w;
            } else if (key.len) {
                Scaled v = scaled(val, 'u');
                if (v.ok) setreg(c, key, v.value);
            }
        } else if (arg.data[1] == 'd') {
            Str d = cuthead(arg, 2);
            if (!d.len && i+1<argc) {
                d = import(argv[++i]);
            }
            Cut kv = cut(d, '=');
            if (!kv.ok) {
                // groff: -dcs defines the one-character name c
                kv.head = takehead(d, 1);
                kv.tail = cuthead(d, 1);
            }
            if (kv.head.len) setstring(c, kv.head, kv.tail);
        } else if (equals(arg, S("-w")) || startswith(arg, S("-w")) ||
                   startswith(arg, S("-W"))) {
            c->warn = arg.data[1] == 'w';
        } else if (equals(arg, S("-h")) || equals(arg, S("--help"))) {
            usage(out);
            flush(out);
            return out->err;
        } else {
            print(err, S("nroff: unknown option: "));
            print(err, arg);
            print(err, S("\n"));
            usage(err);
            flush(err);
            return 2;
        }
    }
    ts->ll = width ? width : 78;

    initctx(c);
    if (!nfiles) {
        files[nfiles++] = S("-");
    }
    for (i32 i = nfiles-1; i >= 0; i--) {
        b32 isstdin = equals(files[i], S("-"));
        Str src = plt_load(plt, isstdin ? (Str){0} : files[i], &c->perm);
        if (!src.data) {
            print(err, S("nroff: cannot open: "));
            print(err, files[i]);
            print(err, S("\n"));
            flush(err);
            c->status = 1;
            continue;
        }
        pushfile(c, isstdin ? S("<stdin>") : files[i], src);
    }
    run(c, scratch);
    flush(out);
    flush(err);
    // A failed write (e.g. the pager quit) is quiet but not successful
    return c->status ? c->status : out->err;
}


#if TEST
// Table-driven tests: each case is rendered at 40 columns and compared
// with output verified against groff (man) or mandoc (mdoc).
#define __USE_MINGW_SETJMP_NON_SEH  // mingw: SEH longjmp fails at -O0
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

struct Plt {
    Str     input;
    b32     sgr;
    jmp_buf oom;
    iz      len;
    u8      out[1<<16];
};

static Str plt_load(Plt *plt, Str path, Arena *a)
{
    return path.data ? (Str){0} : clone(a, plt->input);
}

static b32 plt_write(Plt *plt, i32 fd, u8 *buf, i32 len)
{
    if (fd == 2) {
        fwrite(buf, 1, touz(len), stderr);
        return 1;
    }
    if (len > countof(plt->out) - plt->len) {
        return 0;
    }
    xcpy(plt->out+plt->len, buf, touz(len));
    plt->len += len;
    return 1;
}

static Str plt_getenv(Plt *plt, Str name, Arena *a)
{
    (void)plt;
    (void)name;
    (void)a;
    return (Str){0};
}

static b32 plt_sgr(Plt *plt)
{
    return plt->sgr;
}

static void plt_oom(Plt *plt)
{
    longjmp(plt->oom, 1);
}

typedef struct {
    char *name;
    char *input;
    char *want;
} Case;

// Render input with the given options, returning the exit status.
static i32 render(Plt *plt, byte *mem, iz cap, char *input, char **opts, i32 nopts)
{
    char *argv[8] = {"nroff"};
    i32   argc    = 1;
    for (i32 i = 0; i < nopts; i++) {
        argv[argc++] = opts[i];
    }
    plt->input = import((u8 *)input);
    plt->len   = 0;
    xset(mem, 0x55, touz(cap));  // catch reads of uninitialized memory
    Arena a = {plt, mem, mem+cap};
    if (setjmp(plt->oom)) {
        return -1;
    }
    return nroff(argc, (u8 **)argv, plt, a);
}

static b32 expect(Case *c, Plt *plt)
{
    Str got  = {plt->out, plt->len};
    Str want = import((u8 *)c->want);
    if (equals(got, want)) {
        return 1;
    }
    fprintf(stderr, "FAIL %s\n--- want\n%s--- got\n%.*s---\n",
            c->name, c->want, (int)got.len, (char *)got.data);
    return 0;
}

int main(void)
{
    static Case cases[] = {
        {
            "fill",
            ".TH T 1 2026-01-01 src\n"
            ".SH NAME\n"
            "t \\- test\n"
            ".SH DESCRIPTION\n"
            "One two three four five six seven eight nine ten eleven twelve.\n"
            "Thirteen.\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "NAME\n"
            "       t - test\n"
            "\n"
            "DESCRIPTION\n"
            "       One two three four five six seven\n"
            "       eight  nine  ten  eleven  twelve.\n"
            "       Thirteen.\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "sentence",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            "End here.\n"
            "Next \"q.\"\n"
            "Mid. line e.g.\\& not.\n"
            "And (x.)\n"
            "end\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       End  here.   Next \"q.\"  Mid. line\n"
            "       e.g. not.  And (x.)  end\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "adjust",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".ad l\n"
            "left adjusted words that wrap around the line for sure ok\n"
            ".ad b\n"
            "both adjusted words that wrap around the line for sure ok\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       left adjusted words that wrap\n"
            "       around  the line for sure ok both\n"
            "       adjusted words that  wrap  around\n"
            "       the line for sure ok\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "tags",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".TP 4\n"
            "ab\n"
            "fits\n"
            ".TP 4\n"
            "abcd\n"
            "next line\n"
            ".TP\n"
            ".B \\-x\n"
            "bold tag\n"
            ".IP \\(bu 2\n"
            "bullet\n"
            ".IP \"\" 4\n"
            "no tag\n"
            ".HP 4\n"
            "hanging paragraph text that wraps to the next line\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       ab  fits\n"
            "\n"
            "       abcd\n"
            "           next line\n"
            "\n"
            "       -x  bold tag\n"
            "\n"
            "       \xe2\x80\xa2"" bullet\n"
            "\n"
            "           no tag\n"
            "\n"
            "       hanging paragraph text that wraps\n"
            "           to the next line\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "adresume",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".ad c\n"
            ".na\n"
            ".ad\n"
            "centered\n"
            ".br\n"
            ".ad r\n"
            ".ad\n"
            "right\n"
            ".br\n"
            ".ad r\n"
            ".na\n"
            "left\n"
            ".br\n"
            ".ad c\n"
            ".nr J \\n(.j\n"
            ".ad l\n"
            "j=\\nJ\n"
            ".br\n"
            ".ad \\nJ\n"
            "centered\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "                   centered\n"
            "                                   right\n"
            "       left\n"
            "       j=3\n"
            "                   centered\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "rsneg",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            "top\n"
            ".RS -4\n"
            "out four\n"
            ".RE\n"
            ".RS 3\n"
            "in three\n"
            ".RS -2\n"
            "back two\n"
            ".RE\n"
            "in three\n"
            ".RE\n"
            ".RS foo\n"
            "not moved\n"
            ".RE\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       top\n"
            "   out four\n"
            "          in three\n"
            "        back two\n"
            "          in three\n"
            "       not moved\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "rs",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            "top\n"
            ".RS\n"
            "in one\n"
            ".RS 3.5\n"
            "in two\n"
            ".RE\n"
            "back one\n"
            ".RE\n"
            "back\n"
            ".SS Sub\n"
            "s\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       top\n"
            "              in one\n"
            "                 in two\n"
            "              back one\n"
            "       back\n"
            "\n"
            "   Sub\n"
            "       s\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "fonts",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            "\\fBb\\fR \\fIi\\fR \\f(BIbi\\fP r\n"
            ".BR bold roman\n"
            ".IR it ro\n"
            ".B\n"
            "next line bold\n"
            "plain\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       b i bi r boldroman itro next line\n"
            "       bold plain\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "escapes",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            "a\\-b \\e \\(em \\(lq\\(rq \\[u00E9] \\(co \\&.x a\\ b \\(bu \\(aq \\s-1small\\s0 x\\|y z\\c\n"
            "w\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       a-b  \\  \xe2\x80\x94"" \xe2\x80\x9c""\xe2\x80\x9d"" \xc3\xa9"" \xc2\xa9"" .x a b \xe2\x80\xa2"" ' small\n"
            "       xy zw\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "strings",
            ".ds X hello\n"
            ".ds Y \\\\*X world\n"
            ".nr N 5\n"
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            "\\*X \\*Y \\nN \\n(.g \\w'abc'\n"
            ".nr N +2\n"
            "\\nN\n"
            ".as X !\n"
            "\\*X\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       hello hello world 5 1 72 7 hello!\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "cond",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".if n yes-n\n"
            ".if t yes-t\n"
            ".ie \\n(.g>0 \\{\\\n"
            "block-true\n"
            ".\\}\n"
            ".el \\{\\\n"
            "block-false\n"
            ".\\}\n"
            ".if '\\fBa\\fR'a' strcmp\n"
            ".if !d ZZ undef\n"
            ".if (1+2*3)=9 math\n"
            ".if r .g dot-g\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       yes-n block-true undef math dot-g\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "macros",
            ".de GR\n"
            "Hello \\\\$1 and \\\\$2.\n"
            "..\n"
            ".de NEST\n"
            ".GR \\\\$@\n"
            "..\n"
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".GR one two\n"
            ".NEST \"three four\" five\n"
            "after\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       Hello  one  and two.  Hello three\n"
            "       four and five.  after\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "macroargs",
            ".de GR\n"
            "Hello \\\\$1 and \\\\$2.\n"
            "..\n"
            ".ds W world\n"
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".GR \\*W x\\*[W]y\n"
            ".GR \"\\*W\" z\n"
            "after\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       Hello  world  and xworldy.  Hello\n"
            "       world and z.  after\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "macroframes",
            ".de L0\n..\n"
            ".de L1\n.L0 \\\\$1\n.L0 \\\\$1\n.L0 \\\\$1\n.L0 \\\\$1\n..\n"
            ".de L2\n.L1 \\\\$1\n.L1 \\\\$1\n.L1 \\\\$1\n.L1 \\\\$1\n..\n"
            ".de L3\n.L2 \\\\$1\n.L2 \\\\$1\n.L2 \\\\$1\n.L2 \\\\$1\n..\n"
            ".de L4\n.L3 \\\\$1\n.L3 \\\\$1\n.L3 \\\\$1\n.L3 \\\\$1\n\\\\$2\n..\n"
            ".ds x xxxxxxxxxxxxxxxx\n"
            ".as x \\*x\n.as x \\*x\n.as x \\*x\n.as x \\*x\n"
            ".as x \\*x\n.as x \\*x\n.as x \\*x\n.as x \\*x\n"
            ".as x \\*x\n.as x \\*x\n.as x \\*x\n.as x \\*x\n"
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".L4 \\*x end\n"
            "ok\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       end ok\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "titles",
            ".TH \"A B\" 1 2026 \"src a b c\" \"x y z w\"\n"
            ".SH NAME\n"
            "text\n"
            ".RS 20\n"
            "indented\n",
            "A B(1)           x y z w          A B(1)\n"
            "\n"
            "NAME\n"
            "       text\n"
            "                           indented\n"
            "\n"
            "src a b c         2026            A B(1)\n",
        },
        {
            "titletabs",
            ".TH \"A\tB\" 1 \"d\te\" \"s\tr\" \"M\tN\"\n"
            ".SH NAME\n"
            "text\n",
            "A    B(1)        M    N        A    B(1)\n"
            "\n"
            "NAME\n"
            "       text\n"
            "\n"
            "s    r           d    e        A    B(1)\n",
        },
        {
            "titleabbrev",
            ".TH \"foo\\-bar\\-baz\\-qux\\-\\fBcorge\\fR\" 1 d s M\n"
            ".SH NAME\n"
            "text\n",
            "foo-bar...corge(1)  M foo-bar...corge(1)\n"
            "\n"
            "NAME\n"
            "       text\n"
            "\n"
            "s                   d foo-bar...corge(1)\n",
        },
        {
            "titletabstops",
            ".TH \"AAAAA\tBBBBBBBB\" 1 d \"s\tr\" M\n"
            ".SH NAME\n"
            ".ta 3n 6n\n"
            "text\n",
            "AAAAA...BBBBB(1)    M   AAAAA...BBBBB(1)\n"
            "\n"
            "NAME\n"
            "       text\n"
            "\n"
            "s    r              d   AAAAA...BBBBB(1)\n",
        },
        {
            "nofill",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".nf\n"
            "line  one\n"
            "\tTabbed\n"
            "  spaced\n"
            ".fi\n"
            "filled again\n"
            ".in 4\n"
            "indented\n"
            ".ti +2\n"
            "temp\n"
            "back to in\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       line  one\n"
            "            Tabbed\n"
            "         spaced\n"
            "       filled again\n"
            "    indented\n"
            "      temp back to in\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "center",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".ce 2\n"
            "center one\n"
            "center two\n"
            "not centered\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "                  center one\n"
            "                  center two\n"
            "       not centered\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "links",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            "See\n"
            ".UR https://e.com\n"
            "link\n"
            ".UE .\n"
            ".MT a@b.c\n"
            ".ME\n"
            ".MR ls 1 ,\n"
            ".OP \\-o file\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       See     link     \xe2\x9f\xa8""https://e.com\xe2\x9f\xa9"".\n"
            "       \xe2\x9f\xa8""a@b.c\xe2\x9f\xa9"" ls(1), [-o file]\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "hyphen",
            ".TH T 1\n"
            ".SH A\n"
            "xxxxxxxxxxxxxxxxxxxxxxxxx alpha-beta-gamma 8-bit-thing\n"
            "xxxxxxxxxxxxxxxxxxxxx (\\%discretionary)\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       xxxxxxxxxxxxxxxxxxxxxxxxx  alpha-\n"
            "       beta-gamma            8-bit-thing\n"
            "       xxxxxxxxxxxxxxxxxxxxx          (-\n"
            "       discretionary)\n"
            "\n"
            "                                    T(1)\n",
        },
        {
            "blank",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            "para one\n"
            "\n"
            "para two\n"
            ".PP\n"
            ".PP\n"
            "after two PP\n"
            ".sp 2\n"
            "after sp 2\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       para one\n"
            "\n"
            "       para two\n"
            "\n"
            "       after two PP\n"
            "\n"
            "\n"
            "       after sp 2\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "tbl",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".TS\n"
            "tab(;);\n"
            "l r c.\n"
            "left;right;center\n"
            "longer text;2;c\n"
            ".TE\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       left          right   center\n"
            "       longer text       2     c\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "tblbox",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".TS\n"
            "box center tab(;);\n"
            "l | n.\n"
            "Name;Value\n"
            "_\n"
            "alpha;1.5\n"
            "beta;10\n"
            ".TE\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "              \xe2\x94\x8c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xac""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x90""\n"
            "              \xe2\x94\x82"" Name  \xe2\x94\x82"" Value \xe2\x94\x82""\n"
            "              \xe2\x94\x9c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xbc""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xa4""\n"
            "              \xe2\x94\x82"" alpha \xe2\x94\x82""  1.5  \xe2\x94\x82""\n"
            "              \xe2\x94\x82"" beta  \xe2\x94\x82"" 10    \xe2\x94\x82""\n"
            "              \xe2\x94\x94""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xb4""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x98""\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "tblblock",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".TS\n"
            "allbox tab(:);\n"
            "lB l.\n"
            "T{\n"
            "Header\n"
            "T}:T{\n"
            "A longer text block that will need to wrap within its column\n"
            "T}\n"
            ".TE\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       \xe2\x94\x8c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xac""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x90""\n"
            "       \xe2\x94\x82"" Header \xe2\x94\x82"" A longer text \xe2\x94\x82""\n"
            "       \xe2\x94\x82""        \xe2\x94\x82"" block    that \xe2\x94\x82""\n"
            "       \xe2\x94\x82""        \xe2\x94\x82"" will need  to \xe2\x94\x82""\n"
            "       \xe2\x94\x82""        \xe2\x94\x82"" wrap   within \xe2\x94\x82""\n"
            "       \xe2\x94\x82""        \xe2\x94\x82"" its column    \xe2\x94\x82""\n"
            "       \xe2\x94\x94""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xb4""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x98""\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "tblexpand",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".TS\n"
            "allbox;\n"
            "lbx lb\n"
            "l l.\n"
            "Interface\tValue\n"
            "T{\n"
            ".BR open (),\n"
            ".BR openat (),\n"
            ".BR creat ()\n"
            "T}\tMT-Safe\n"
            ".TE\n"
            ".TS\n"
            "lx l lx.\n"
            "a\tbb\tc\n"
            ".TE\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       \xe2\x94\x8c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xac""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x90""\n"
            "       \xe2\x94\x82"" Interface           \xe2\x94\x82"" Value   \xe2\x94\x82""\n"
            "       \xe2\x94\x9c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xbc""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xa4""\n"
            "       \xe2\x94\x82"" open(),   openat(), \xe2\x94\x82"" MT-Safe \xe2\x94\x82""\n"
            "       \xe2\x94\x82"" creat()             \xe2\x94\x82""         \xe2\x94\x82""\n"
            "       \xe2\x94\x94""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xb4""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x98""\n"
            "       a              bb   c\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "tblxblock",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".TS\n"
            "allbox;\n"
            "lbx lb\n"
            "l l.\n"
            "Interface\tValue\n"
            "T{\n"
            ".BR cos (),\n"
            ".BR sin ()\n"
            "T}\tMT-Safe\n"
            "\\fBpthread_setname_np\\fP()\tMT-Safe\n"
            ".TE\n"
            ".TS\n"
            "lx lx l.\n"
            "aaaaaaaaaaaaaaaaaaaaaaaaaa\tb\tc\n"
            ".TE\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       \xe2\x94\x8c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xac""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x90""\n"
            "       \xe2\x94\x82"" Interface            \xe2\x94\x82"" Value   \xe2\x94\x82""\n"
            "       \xe2\x94\x9c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xbc""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xa4""\n"
            "       \xe2\x94\x82"" cos(), sin()         \xe2\x94\x82"" MT-Safe \xe2\x94\x82""\n"
            "       \xe2\x94\x9c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xbc""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xa4""\n"
            "       \xe2\x94\x82"" pthread_setname_np() \xe2\x94\x82"" MT-Safe \xe2\x94\x82""\n"
            "       \xe2\x94\x94""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xb4""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x98""\n"
            "       aaaaaaaaaaaaaaaaaaaaaaaaaa   b   c\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "tblspan",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".TS\n"
            "tab(;);\n"
            "l s s\n"
            "l l l.\n"
            "Spanning entry text;\n"
            "a;b;c\n"
            ".TE\n"
            ".TS\n"
            "allbox tab(;);\n"
            "l s\n"
            "lx l.\n"
            "T{\n"
            "A spanning block over an expanding column\n"
            "T}\n"
            "ab;cd\n"
            ".TE\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       Spanning entry text\n"
            "       a      b       c\n"
            "       \xe2\x94\x8c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x90""\n"
            "       \xe2\x94\x82"" A   spanning  block  over  an \xe2\x94\x82""\n"
            "       \xe2\x94\x82"" expanding column              \xe2\x94\x82""\n"
            "       \xe2\x94\x9c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xac""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xa4""\n"
            "       \xe2\x94\x82"" ab                       \xe2\x94\x82"" cd \xe2\x94\x82""\n"
            "       \xe2\x94\x94""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xb4""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x98""\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "tblvspan",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".TS\n"
            "allbox tab(;);\n"
            "l l l.\n"
            "a;T{\n"
            "one two three four five six\n"
            "T};top\n"
            "\\^;c;d\n"
            ".TE\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       \xe2\x94\x8c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xac""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xac""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x90""\n"
            "       \xe2\x94\x82""   \xe2\x94\x82"" one    two \xe2\x94\x82"" top \xe2\x94\x82""\n"
            "       \xe2\x94\x82""   \xe2\x94\x82"" three four \xe2\x94\x82""     \xe2\x94\x82""\n"
            "       \xe2\x94\x82"" a \xe2\x94\x82"" five six   \xe2\x94\x82""     \xe2\x94\x82""\n"
            "       \xe2\x94\x82""   \xe2\x94\x9c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xbc""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xa4""\n"
            "       \xe2\x94\x82""   \xe2\x94\x82"" c          \xe2\x94\x82"" d   \xe2\x94\x82""\n"
            "       \xe2\x94\x94""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xb4""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xb4""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x98""\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "tblxspan",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".TS\n"
            "tab(;);\n"
            "l lx s.\n"
            "aa;bbbbbbbbbbbbbbbb\n"
            "c;d\n"
            ".TE\n"
            ".TS\n"
            "tab(;);\n"
            "l s l.\n"
            "ab;c\n"
            ".TE\n"
            ".TS\n"
            "allbox tab(;);\n"
            "l s s\n"
            "n n n.\n"
            "hhhhhhhhhhhhhhhhhhhhhh\n"
            "1.5;22.25;3\n"
            "10;2;333.1\n"
            ".TE\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       aa        bbbbbbbbbbbbbbbb\n"
            "       c         d\n"
            "       ab      c\n"
            "       \xe2\x94\x8c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x90""\n"
            "       \xe2\x94\x82"" hhhhhhhhhhhhhhhhhhhhhh \xe2\x94\x82""\n"
            "       \xe2\x94\x9c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xac""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xac""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xa4""\n"
            "       \xe2\x94\x82""  1.5  \xe2\x94\x82"" 22.25  \xe2\x94\x82""   3   \xe2\x94\x82""\n"
            "       \xe2\x94\x9c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xbc""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xbc""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xa4""\n"
            "       \xe2\x94\x82"" 10    \xe2\x94\x82""  2     \xe2\x94\x82"" 333.1 \xe2\x94\x82""\n"
            "       \xe2\x94\x94""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xb4""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xb4""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x98""\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "tblvspans",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".TS\n"
            "allbox tab(;);\n"
            "c c s\n"
            "c ^ s\n"
            "l l l.\n"
            "X;Head\n"
            "Y\n"
            "a;b;c\n"
            ".TE\n"
            ".TS\n"
            "allbox tab(;);\n"
            "c c\n"
            "^ ^\n"
            "l l.\n"
            "Name;Value\n"
            "\n"
            "a;b\n"
            ".TE\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       \xe2\x94\x8c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xac""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x90""\n"
            "       \xe2\x94\x82"" X \xe2\x94\x82""       \xe2\x94\x82""\n"
            "       \xe2\x94\x9c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xa4"" Head  \xe2\x94\x82""\n"
            "       \xe2\x94\x82"" Y \xe2\x94\x82""       \xe2\x94\x82""\n"
            "       \xe2\x94\x9c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xbc""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xac""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xa4""\n"
            "       \xe2\x94\x82"" a \xe2\x94\x82"" b \xe2\x94\x82"" c \xe2\x94\x82""\n"
            "       \xe2\x94\x94""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xb4""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xb4""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x98""\n"
            "       \xe2\x94\x8c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xac""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x90""\n"
            "       \xe2\x94\x82"" Name \xe2\x94\x82"" Value \xe2\x94\x82""\n"
            "       \xe2\x94\x82""      \xe2\x94\x82""       \xe2\x94\x82""\n"
            "       \xe2\x94\x9c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xbc""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xa4""\n"
            "       \xe2\x94\x82"" a    \xe2\x94\x82"" b     \xe2\x94\x82""\n"
            "       \xe2\x94\x94""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xb4""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x98""\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "tblblockw",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".TS\n"
            "allbox;\n"
            "lbw8 lb\n"
            "l l.\n"
            "Name\tValue\n"
            "T{\n"
            ".BR foo (),\n"
            ".BR bar ()\n"
            "T}\tMT-Safe\n"
            ".TE\n"
            ".TS\n"
            "box;\n"
            "c.\n"
            "T{\n"
            "alpha beta gamma delta epsilon zeta\n"
            "T}\n"
            "wwwwwwwwwwwwwwwwwwwwwww\n"
            ".TE\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       \xe2\x94\x8c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xac""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x90""\n"
            "       \xe2\x94\x82"" Name     \xe2\x94\x82"" Value   \xe2\x94\x82""\n"
            "       \xe2\x94\x9c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xbc""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xa4""\n"
            "       \xe2\x94\x82"" foo(),   \xe2\x94\x82"" MT-Safe \xe2\x94\x82""\n"
            "       \xe2\x94\x82"" bar()    \xe2\x94\x82""         \xe2\x94\x82""\n"
            "       \xe2\x94\x94""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xb4""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x98""\n"
            "       \xe2\x94\x8c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x90""\n"
            "       \xe2\x94\x82"" alpha  beta gamma delta \xe2\x94\x82""\n"
            "       \xe2\x94\x82"" epsilon zeta            \xe2\x94\x82""\n"
            "       \xe2\x94\x82"" wwwwwwwwwwwwwwwwwwwwwww \xe2\x94\x82""\n"
            "       \xe2\x94\x94""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x98""\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "tblfmtrule",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            "text\n"
            ".TS\n"
            "tab(;);\n"
            "l c c l\n"
            "____\n"
            "lB c c l.\n"
            "Signal;Std;Action;Comment\n"
            ".sp\n"
            "SIGINT;P1990;Term;Interrupt\n"
            "SIGHUP;P1990;Term;Hangup\n"
            ".TE\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       text\n"
            "       Signal    Std    Action   Comment\n"
            "\n"
            "       \xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\n"
            "       SIGINT   P1990    Term    Interrupt\n"
            "       SIGHUP   P1990    Term    Hangup\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "tblallboxrule",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            "text\n"
            ".TS\n"
            "allbox tab(;);\n"
            "l\n"
            "^.\n"
            "X\n"
            "_\n"
            "\n"
            ".TE\n"
            ".TS\n"
            "allbox tab(;);\n"
            "l l.\n"
            "a;b\n"
            ".sp\n"
            "_\n"
            "c;d\n"
            ".TE\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       text\n"
            "       \xe2\x94\x8c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x90""\n"
            "       \xe2\x94\x82""   \xe2\x94\x82""\n"
            "       \xe2\x94\x82"" X \xe2\x94\x82""\n"
            "       \xe2\x94\x82""   \xe2\x94\x82""\n"
            "       \xe2\x94\x94""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x98""\n"
            "       \xe2\x94\x8c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xac""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x90""\n"
            "       \xe2\x94\x82"" a \xe2\x94\x82"" b \xe2\x94\x82""\n"
            "       \xe2\x94\x9c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xbc""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xa4""\n"
            "       \xe2\x94\x82""   \xe2\x94\x82""   \xe2\x94\x82""\n"
            "       \xe2\x94\x9c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xbc""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xa4""\n"
            "       \xe2\x94\x82"" c \xe2\x94\x82"" d \xe2\x94\x82""\n"
            "       \xe2\x94\x94""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xb4""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x98""\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "tblnospace",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".TS\n"
            "tab(;);\n"
            "l l l.\n"
            "a;b;c\n"
            "\\^;\\^;\\^\n"
            "\\^;\\^;\\^\n"
            "d;e;f\n"
            ".TE\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       a   b   c\n"
            "       d   e   f\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "tblw0",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            "text\n"
            ".TS\n"
            "tab(;);\n"
            "lw(0) l.\n"
            "T{\n"
            "alpha beta gamma\n"
            "T};x\n"
            ".TE\n"
            ".TS\n"
            "allbox tab(;);\n"
            "l lw(0) l.\n"
            "a;;b\n"
            ".TE\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       text\n"
            "       alpha   x\n"
            "       beta\n"
            "       gamma\n"
            "       \xe2\x94\x8c""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xac""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xac""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x90""\n"
            "       \xe2\x94\x82"" a \xe2\x94\x82""  \xe2\x94\x82"" b \xe2\x94\x82""\n"
            "       \xe2\x94\x94""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xb4""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\xb4""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x80""\xe2\x94\x98""\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "example",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            "text\n"
            ".EX\n"
            "code  here\n"
            ".EE\n"
            "after\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       text\n"
            "       code  here\n"
            "       after\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "spaces",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            "trailing spaces\n"
            "next\n"
            "  leading spaces\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       trailing spaces next\n"
            "         leading spaces\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
        {
            "pod",
            ".de Vb\n"
            ".ft CW\n"
            ".nf\n"
            ".ne \\\\$1\n"
            "..\n"
            ".de Ve\n"
            ".ft R\n"
            ".fi\n"
            "..\n"
            ".ie n \\{\\\n"
            ".    ds C` \"\"\n"
            ".    ds C' \"\"\n"
            "'br\\}\n"
            ".el\\{\\\n"
            ".    ds C`\n"
            ".    ds C'\n"
            "'br\\}\n"
            ".de IX\n"
            "..\n"
            ".TH POD 1 2026-01-01 pod\n"
            ".SH NAME\n"
            "pod \\- generated\n"
            ".SH OPTIONS\n"
            ".IP \"\\fB\\-o\\fR \\fIfile\\fR\" 4\n"
            ".IX Item \"-o file\"\n"
            "Write to \\f(CW\\*(C`file\\*(C'\\fR.\n"
            ".Vb 1\n"
            "\\&    verbatim  text\n"
            ".Ve\n",
            "POD(1)   General Commands Manual  POD(1)\n"
            "\n"
            "NAME\n"
            "       pod - generated\n"
            "\n"
            "OPTIONS\n"
            "       -o file\n"
            "           Write to \"file\".\n"
            "               verbatim  text\n"
            "\n"
            "pod            2026-01-01         POD(1)\n",
        },
        {
            "mdoc",
            ".Dd January 1, 2026\n"
            ".Dt TEST 1\n"
            ".Os W64\n"
            ".Sh NAME\n"
            ".Nm test\n"
            ".Nd a test page\n"
            ".Sh SYNOPSIS\n"
            ".Nm\n"
            ".Op Fl abc\n"
            ".Op Fl o Ar file\n"
            ".Ar input ...\n"
            ".Sh DESCRIPTION\n"
            "The\n"
            ".Nm\n"
            "utility does\n"
            ".Dq things .\n"
            "See\n"
            ".Xr ls 1 ,\n"
            ".Xr cp 1\n"
            "and\n"
            ".Pa /tmp .\n"
            ".Bl -tag -width Ds\n"
            ".It Fl a\n"
            "all\n"
            ".It Fl verylongflag\n"
            "long\n"
            ".El\n"
            ".Pp\n"
            ".Bl -bullet -compact\n"
            ".It\n"
            "one\n"
            ".It\n"
            "two\n"
            ".El\n"
            ".Sh AUTHORS\n"
            ".An Al Author Aq Mt al@example.com\n",
            "TEST(1)  General Commands Manual TEST(1)\n"
            "\n"
            "NAME\n"
            "     test \xe2\x80\x93"" a test page\n"
            "\n"
            "SYNOPSIS\n"
            "     test [-abc] [-o file] input ...\n"
            "\n"
            "DESCRIPTION\n"
            "     The test utility does \xe2\x80\x9c""things\xe2\x80\x9d"".\n"
            "     See ls(1), cp(1) and /tmp.\n"
            "\n"
            "     -a      all\n"
            "\n"
            "     -verylongflag\n"
            "             long\n"
            "\n"
            "     \xe2\x80\xa2""   one\n"
            "     \xe2\x80\xa2""   two\n"
            "\n"
            "AUTHORS\n"
            "     Al Author <al@example.com>\n"
            "\n"
            "W64          January 1, 2026         W64\n",
        },
        {
            "mdocfn",
            ".Dd January 1, 2026\n"
            ".Dt LIB 3\n"
            ".Os W64\n"
            ".Sh NAME\n"
            ".Nm f\n"
            ".Nd function\n"
            ".Sh SYNOPSIS\n"
            ".In stdio.h\n"
            ".Ft int\n"
            ".Fn func \"const char *name\" \"int flags\"\n"
            ".Sh DESCRIPTION\n"
            ".Fn func\n"
            "returns\n"
            ".Dv NULL .\n"
            ".Bd -literal -offset indent\n"
            "literal\ttab\n"
            ".Ed\n",
            "LIB(3)  Library Functions Manual  LIB(3)\n"
            "\n"
            "NAME\n"
            "     f \xe2\x80\x93"" function\n"
            "\n"
            "SYNOPSIS\n"
            "     #include <stdio.h>\n"
            "\n"
            "     int\n"
            "     func(const char *name, int flags);\n"
            "\n"
            "DESCRIPTION\n"
            "     func() returns NULL.\n"
            "\n"
            "           literal tab\n"
            "\n"
            "W64          January 1, 2026         W64\n",
        },
        {
            "mdoccolumn",
            ".Dd January 1, 2026\n"
            ".Dt TEST 1\n"
            ".Os W64\n"
            ".Sh DESCRIPTION\n"
            "Columns:\n"
            ".Bl -column Ar Value\n"
            ".It Sy Key Ta Sy Value\n"
            ".It one\ttwo\n"
            ".It Fl x Ta a long last cell that wraps to its own column\n"
            ".El\n",
            "TEST(1)  General Commands Manual TEST(1)\n"
            "\n"
            "DESCRIPTION\n"
            "     Columns:\n"
            "\n"
            "     Key   Value\n"
            "     one   two\n"
            "     -x    a long last cell that wraps\n"
            "           to its own column\n"
            "\n"
            "W64          January 1, 2026         W64\n",
        },
        {
            "mdocspacing",
            ".Dd January 1, 2026\n"
            ".Dt TEST 1\n"
            ".Os W64\n"
            ".Sh DESCRIPTION\n"
            ".Fl L\n"
            ".Sm off\n"
            ".Oo Ar addr : Oc\n"
            ".Ar port\n"
            ".Sm on\n"
            "and\n"
            ".Ar x Ns\n"
            ".Tg x\n"
            "y\n"
            ".Tg\n"
            "z\n"
            ".Ql \",\"\n"
            ".Dq \"Ar\" .\n"
            ".Bl -inset\n"
            ".It Inset head\n"
            "body\n"
            ".El\n"
            ".Bl -diag\n"
            ".It Diag head\n"
            "body\n"
            ".El\n",
            "TEST(1)  General Commands Manual TEST(1)\n"
            "\n"
            "DESCRIPTION\n"
            "     -L [addr:]port and xy z \xe2\x80\x98"",\xe2\x80\x99"" \xe2\x80\x9c""Ar\xe2\x80\x9d"".\n"
            "\n"
            "     Inset head body\n"
            "\n"
            "     Diag head  body\n"
            "\n"
            "W64          January 1, 2026         W64\n",
        },
        {
            "mdocrefs",
            ".Dd January 1, 2026\n"
            ".Dt LIB 3\n"
            ".Os W64\n"
            ".Sh LIBRARY\n"
            ".Lb libc\n"
            ".Lb libnone\n"
            ".Sh SYNOPSIS\n"
            ".In unistd.h\n"
            ".Vt extern char *optarg;\n"
            ".Vt extern int optind;\n"
            ".Ft int\n"
            ".Fn getopt\n"
            ".Sh DESCRIPTION\n"
            "See\n"
            ".In stdio.h .\n"
            ".Sh SEE ALSO\n"
            ".Xr ls 1\n"
            ".Rs\n"
            ".%T The C Book\n"
            ".%A Al\n"
            ".%A Bo\n"
            ".%I Pub\n"
            ".%D 1988\n"
            ".Re\n"
            ".Rs\n"
            ".%A Al\n"
            ".%A Bo\n"
            ".%A Cy\n"
            ".%T Report\n"
            ".%J Journal\n"
            ".Re\n",
            "LIB(3)  Library Functions Manual  LIB(3)\n"
            "\n"
            "LIBRARY\n"
            "     Standard C Library (libc, -lc)\n"
            "     library \xe2\x80\x9c""libnone\xe2\x80\x9d""\n"
            "\n"
            "SYNOPSIS\n"
            "     #include <unistd.h>\n"
            "\n"
            "     extern char *optarg;\n"
            "     extern int optind;\n"
            "\n"
            "     int\n"
            "     getopt();\n"
            "\n"
            "DESCRIPTION\n"
            "     See <stdio.h>.\n"
            "\n"
            "SEE ALSO\n"
            "     ls(1)\n"
            "\n"
            "     Al and Bo, The C Book, Pub, 1988.\n"
            "\n"
            "     Al, Bo, and Cy, \xe2\x80\x9c""Report\xe2\x80\x9d"", Journal.\n"
            "\n"
            "W64          January 1, 2026         W64\n",
        },
        {
            "mdoccells",
            ".Dd January 1, 2026\n"
            ".Dt TEST 1\n"
            ".Os W64\n"
            ".Sh DESCRIPTION\n"
            ".Bl -column xxxx yyyy -compact\n"
            ".It a\t Sy b\tc\n"
            ".It a\tTa b\n"
            ".It a Ta\tb\n"
            ".It Sy x Ta \"Ar\" y Ta z\n"
            ".El\n",
            "TEST(1)  General Commands Manual TEST(1)\n"
            "\n"
            "DESCRIPTION\n"
            "     a        b      c\n"
            "     a       Ta b\n"
            "     a Ta    b\n"
            "     x       Ar y    z\n"
            "\n"
            "W64          January 1, 2026         W64\n",
        },
        {
            "mdocwords",
            ".Dd January 1, 2026\n"
            ".Dt TEST 1\n"
            ".Os W64\n"
            ".Sh DESCRIPTION\n"
            ".Ar foo \"\"\n"
            "bar\n"
            ".No \"\"\n"
            "M\n"
            ".Bl -tag -width Ds\n"
            ".It Fl e\n"
            ".Sm on\n"
            ".Ar x\n"
            ".El\n"
            ".Sh SEE ALSO\n"
            "Text first.\n"
            ".Bd -filled -compact\n"
            ".Rs\n"
            ".%A\n"
            ".%A Two\n"
            ".%T T2\n"
            ".Re\n"
            ".Ed\n",
            "TEST(1)  General Commands Manual TEST(1)\n"
            "\n"
            "DESCRIPTION\n"
            "     foo  bar  M\n"
            "\n"
            "     -e      x\n"
            "\n"
            "SEE ALSO\n"
            "     Text first.\n"
            "     Two, T2.\n"
            "\n"
            "W64          January 1, 2026         W64\n",
        },
        {
            "mdocsynnm",
            ".Dd January 1, 2026\n"
            ".Dt TEST 1\n"
            ".Os W64\n"
            ".Sh SYNOPSIS\n"
            ".Nm foo\n"
            ".In stdio.h\n"
            ".Nm bar\n"
            ".Fl b\n"
            ".Pp\n"
            ".Ft int\n"
            ".Fn f \"int a\"\n"
            ".Nm baz\n"
            ".Vt int x;\n"
            ".Nm qux\n"
            ".Fn g\n"
            ".Nm end\n",
            "TEST(1)  General Commands Manual TEST(1)\n"
            "\n"
            "SYNOPSIS\n"
            "     foo #include <stdio.h>\n"
            "     bar -b\n"
            "\n"
            "         int\n"
            "         f(int a);\n"
            "\n"
            "     baz int x;\n"
            "     qux g();\n"
            "\n"
            "     end\n"
            "\n"
            "W64          January 1, 2026         W64\n",
        },
        {
            "mdoctitles",
            ".Dd January 2, 2026\n"
            ".Dt T 1\n"
            ".Os a b c d e\n"
            ".Sh NAME\n"
            ".Nm t\n"
            ".Nd test\n"
            ".Bd -offset 20n\n"
            "x y\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "NAME\n"
            "     t \xe2\x80\x93 test\n"
            "\n"
            "                         x y\n"
            "\n"
            "a b c d e    January 2, 2026   a b c d e\n",
        },
        {
            "mdocgaps",
            ".Dd January 1, 2026\n"
            ".Dt TEST 1\n"
            ".Os W64\n"
            ".Sh DESCRIPTION\n"
            ".Ar a\\h'1n' Ns\n"
            ".Sm on\n"
            ".Ar b\n"
            ".Sm off\n"
            ".Ar c\\h'2n'\n"
            ".Sm on\n"
            ".Ar d\n"
            ".Ar x \"\" y\n"
            ".Bd -literal\n"
            ".Ar \"e \" Ns\n"
            ".Sm on\n"
            ".Ar f\n"
            ".Ed\n"
            ".Bl -inset\n"
            ".It Fl g\n"
            ".Sm on\n"
            ".Ar h\n"
            ".It Fl i\n"
            ".Ns\n"
            ".Ar j\n"
            ".It Fl aaaa Ar bbbbbbbbbbbbbbbbbbbbbbbbbb\n"
            "cccc\n"
            ".El\n"
            ".Bl -diag\n"
            ".It\n"
            "k\n"
            ".El\n",
            "TEST(1)  General Commands Manual TEST(1)\n"
            "\n"
            "DESCRIPTION\n"
            "     a  b c   d x  y\n"
            "\n"
            "     e\n"
            "     f\n"
            "\n"
            "     -g  h\n"
            "\n"
            "     -i j\n"
            "\n"
            "     -aaaa\n"
            "     bbbbbbbbbbbbbbbbbbbbbbbbbb cccc\n"
            "\n"
            "       k\n"
            "\n"
            "W64          January 1, 2026         W64\n",
        },
        {
            "mdocitems",
            ".Dd January 1, 2026\n"
            ".Dt TEST 1\n"
            ".Os W64\n"
            ".Sh SYNOPSIS\n"
            ".In a.h\n"
            ".Op Ar x\n"
            ".In b.h\n"
            "text\n"
            ".Sh DESCRIPTION\n"
            ".Bl -column xxxx yyyy -compact\n"
            ".It a Sy\tb\tc\n"
            ".It Fl x Ar\tb\tc\n"
            ".It Ar x \"Ta\" y Ta z\n"
            ".It a \"Ta\" b Ta z\n"
            ".It a Ta b \"\"\n"
            "more\n"
            ".It a Ta \"b \"\n"
            "more\n"
            ".El\n"
            ".Sh SEE ALSO\n"
            ".Bl -tag -width Ds\n"
            ".It x\n"
            "Text.\n"
            ".Rs\n"
            ".%A One\n"
            ".Re\n"
            ".It y\n"
            ".Rs\n"
            ".%A Two\n"
            ".Re\n"
            ".Rs\n"
            ".%A Three\n"
            ".Re\n"
            ".El\n",
            "TEST(1)  General Commands Manual TEST(1)\n"
            "\n"
            "SYNOPSIS\n"
            "     #include <a.h> [x]\n"
            "     #include <b.h> text\n"
            "\n"
            "DESCRIPTION\n"
            "     a Sy    b       c\n"
            "     -x -Ar  b       c\n"
            "     x       y       z\n"
            "     a Ta b  z\n"
            "     a       b  more\n"
            "     a       b  more\n"
            "\n"
            "SEE ALSO\n"
            "     x       Text.\n"
            "\n"
            "             One.\n"
            "\n"
            "     y       Two.\n"
            "\n"
            "             Three.\n"
            "\n"
            "W64          January 1, 2026         W64\n",
        },
        {
            "mdocempty",
            ".Dd January 1, 2026\n"
            ".Dt TEST 1\n"
            ".Os W64\n"
            ".Sh DESCRIPTION\n"
            ".Xr\n"
            ".Pp\n"
            ".Lk\n"
            "text\n"
            ".Fn\n"
            ".Fo\n"
            ".Fa a\n"
            ".Fc\n"
            ".Ar b\n"
            ".Ns\n"
            "more\n"
            ".Pp\n"
            ".Ar xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx \"\" yyyy\n"
            ".Bl -column xxxx yyyy -compact\n"
            ".It Op x \"Ta\" y Ta z\n"
            ".El\n"
            ".Bl -tag -width 4n\n"
            ".It\n"
            ".Ar z\n"
            ".El\n"
            ".Bl -diag\n"
            ".It\n"
            ".It x\n"
            "body\n"
            ".El\n",
            "TEST(1)  General Commands Manual TEST(1)\n"
            "\n"
            "DESCRIPTION\n"
            "     text(a) b more\n"
            "\n"
            "     xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n"
            "     yyyy\n"
            "     [x Ta y]\n"
            "             z\n"
            "\n"
            "           z\n"
            "\n"
            "\n"
            "     x  body\n"
            "\n"
            "W64          January 1, 2026         W64\n",
        },
        {
            "edges",
            ".TH T 1 2026-01-01 src\n"
            ".SH A\n"
            ".nf\n"
            "abcdefghijklmnopqrstuvwxyz0123456789\n"
            ".tl |\\h'-100n'|||\n"
            ".fi\n"
            "x\\[u89ABCDEF]y \\[u1F600]\n"
            ".ds a b\n"
            ".ds b c\n"
            "\\*[\\*[a]] \\n[\\*[a]]\n"
            ".nr c 7\n"
            "\\n[\\*[b]]\n"
            "a\\\n"
            "b\\\\\n"
            "c\\\\\\\n"
            "d\n",
            "T(1)     General Commands Manual    T(1)\n"
            "\n"
            "A\n"
            "       abcdefghijklmnopqrstuvwxyz0123456789\n"
            "\n"
            "       xy \xf0\x9f\x98\x80"" c 0 7 ab\\ c\\d\n"
            "\n"
            "src            2026-01-01           T(1)\n",
        },
    };

    iz    cap   = (iz)1<<24;
    byte *mem   = malloc(touz(cap));
    Plt  *plt   = calloc(1, sizeof(*plt));
    i32   fails = 0;
    i32   count = 0;

    char *opts[] = {"-Tutf8", "-rLL=40n"};
    for (i32 i = 0; i < countof(cases); i++, count++) {
        render(plt, mem, cap, cases[i].input, opts, countof(opts));
        fails += !expect(cases+i, plt);
    }

    // Emphasis: bold and underline, each line closing its own SGR
    count++;
    plt->sgr = 1;
    render(plt, mem, cap, ".SH A\n\\fBb\\fR \\fIi\\fR\n", opts, countof(opts));
    Case sgr = {
        "sgr", 0,
        "\x1b[1mA\x1b[22m\n"
        "       \x1b[1mb\x1b[22m \x1b[4mi\x1b[24m\n"
    };
    fails += !expect(&sgr, plt);
    plt->sgr = 0;

    // ASCII output substitutes special characters
    count++;
    char *ascii[] = {"-Tascii", "-rLL=40n"};
    render(plt, mem, cap, "\\(em \\(lq\\(rq \\(co \\[u00E9] \xc3\xa9\n", ascii, countof(ascii));
    Case asc = {"ascii", 0, "       -- \"\" (C) e e\n"};
    fails += !expect(&asc, plt);

    // Runaway recursion terminates
    count++;
    render(plt, mem, cap, ".de A\n.A\n..\n.A\nok\n", opts, countof(opts));
    Case rec = {"recursion", 0, "       ok\n"};
    fails += !expect(&rec, plt);

    // Out of memory is reported, not a crash
    count++;
    i32 r = render(plt, mem, 1<<16, ".TH T 1\n.SH A\ntext\n", opts, countof(opts));
    if (r != -1) {
        fprintf(stderr, "FAIL oom: status %d\n", r);
        fails++;
    }

    // Unknown option
    count++;
    char *bad[] = {"-q"};
    r = render(plt, mem, cap, "", bad, countof(bad));
    if (r != 2) {
        fprintf(stderr, "FAIL option: status %d\n", r);
        fails++;
    }

    // A failed write (output past 64 KiB) is quiet but sets the status
    count++;
    r = render(plt, mem, cap,
        ".ds a x x x x\n"
        ".as a \\*a\n.as a \\*a\n.as a \\*a\n.as a \\*a\n.as a \\*a\n"
        ".as a \\*a\n.as a \\*a\n.as a \\*a\n.as a \\*a\n.as a \\*a\n"
        ".as a \\*a\n.as a \\*a\n.as a \\*a\n.as a \\*a\n"
        "\\*a\n", opts, countof(opts));
    if (r != 1) {
        fprintf(stderr, "FAIL write: status %d\n", r);
        fails++;
    }

    printf("%d tests, %d failed\n", count, fails);
    return fails != 0;
}


#elif __AFL_COMPILER
// Fuzz the whole formatter: arbitrary input must neither crash nor
// hang. A small arena makes pathological inputs fail fast.
//   $ afl-gcc-fast -g3 -fsanitize=undefined -fsanitize-trap nroff.c
//   $ afl-fuzz -i corpus -o results ./a.out
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

__AFL_FUZZ_INIT();

struct Plt {
    Str     input;
    jmp_buf oom;
};

static Str plt_load(Plt *plt, Str path, Arena *a)
{
    return path.data ? (Str){0} : clone(a, plt->input);
}

static b32 plt_write(Plt *plt, i32 fd, u8 *buf, i32 len)
{
    (void)plt;
    (void)fd;
    (void)buf;
    (void)len;
    return 1;
}

static Str plt_getenv(Plt *plt, Str name, Arena *a)
{
    (void)plt;
    (void)name;
    (void)a;
    return (Str){0};
}

static b32 plt_sgr(Plt *plt)
{
    (void)plt;
    return 1;
}

static void plt_oom(Plt *plt)
{
    longjmp(plt->oom, 1);
}

int main(void)
{
    __AFL_INIT();
    iz    cap = (iz)1<<24;
    byte *mem = malloc(touz(cap));
    u8   *buf = __AFL_FUZZ_TESTCASE_BUF;
    u8   *src = 0;
    static Plt plt;
    while (__AFL_LOOP(10000)) {
        i32 len = __AFL_FUZZ_TESTCASE_LEN;
        src = realloc(src, touz(len));
        memcpy(src, buf, touz(len));
        plt.input = (Str){src, len};
        char *argv[] = {"nroff", "-Tutf8", "-w", 0};
        Arena a = {&plt, mem, mem+cap};
        if (!setjmp(plt.oom)) {
            nroff(3, (u8 **)argv, &plt, a);
        }
    }
}


#elif _WIN32
typedef uint16_t    c16;

enum {
    CP_UTF8         = 65001,
    GENERIC_READ    = (i32)0x80000000,
    FILE_SHARE_ALL  = 7,
    OPEN_EXISTING   = 3,
    MEM_COMMIT      = 0x1000,
    MEM_RESERVE     = 0x2000,
    PAGE_READWRITE  = 4,
    VT_PROCESSING   = 4,
    ERROR_BROKEN_PIPE      = 109,
    ERROR_ENVVAR_NOT_FOUND = 203,
};

#define W32(r)  __declspec(dllimport) r __stdcall
W32(c16 **) CommandLineToArgvW(c16 *, i32 *);
W32(b32)    CloseHandle(uz);
W32(uz)     CreateFileW(c16 *, i32, i32, uz, i32, i32, uz);
W32(void)   ExitProcess(i32);
W32(c16 *)  GetCommandLineW(void);
W32(b32)    GetConsoleMode(uz, i32 *);
W32(i32)    GetEnvironmentVariableW(c16 *, c16 *, i32);
W32(i32)    GetLastError(void);
W32(uz)     GetStdHandle(i32);
W32(i32)    MultiByteToWideChar(i32, i32, u8 *, i32, c16 *, i32);
W32(b32)    ReadFile(uz, u8 *, i32, i32 *, uz);
W32(b32)    SetConsoleMode(uz, i32);
W32(byte *) VirtualAlloc(uz, iz, i32, i32);
W32(i32)    WideCharToMultiByte(i32, i32, c16 *, i32, u8 *, i32, uz, uz);
W32(b32)    WriteConsoleW(uz, c16 *, i32, i32 *, uz);
W32(b32)    WriteFile(uz, u8 *, i32, i32 *, uz);

struct Plt {
    uz  out[3];
    b32 console[3];
    b32 sgr;
    u8  pending[3][4];  // incomplete UTF-8 sequence per console
    i32 npending[3];
    u8  utf8[1<<12];
    c16 wide[1<<12];    // at most one unit per UTF-8 byte
};

static i32 trunc32(iz n)
{
    return n>maxof(i32) ? maxof(i32) : (i32)n;
}

static c16 *towide(Str s, Arena *a)
{
    i32  len = trunc32(s.len);
    i32  n   = MultiByteToWideChar(CP_UTF8, 0, s.data, len, 0, 0);
    c16 *w   = new(a, n+1, c16);
    MultiByteToWideChar(CP_UTF8, 0, s.data, len, w, n);
    return w;
}

static Str plt_load(Plt *plt, Str path, Arena *a)
{
    (void)plt;
    uz h = GetStdHandle(-10);
    b32 close = 0;
    if (path.data) {
        Arena scratch = *a;
        c16 *wpath = towide(path, &scratch);
        h = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_ALL, 0, OPEN_EXISTING, 0, 0);
        if (h == (uz)-1) {
            return (Str){0};
        }
        close = 1;
    }

    // Read into the free space of the arena, then commit
    Str r = {0};
    r.data = (u8 *)a->beg;
    iz avail = a->end - a->beg;
    for (;;) {
        i32 want = trunc32(avail - r.len);
        if (want > 1<<20) want = 1<<20;
        if (want <= 0) {
            plt_oom(plt);
        }
        i32 got = 0;
        if (!ReadFile(h, r.data+r.len, want, &got, 0)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) break;  // pipe EOF
            if (close) CloseHandle(h);
            return (Str){0};
        } else if (!got) {
            break;
        }
        r.len += got;
    }
    if (close) {
        CloseHandle(h);
    }
    a->beg += r.len;
    return r;
}

static b32 plt_write(Plt *plt, i32 fd, u8 *buf, i32 len)
{
    uz h = plt->out[fd];
    if (!plt->console[fd]) {
        for (i32 off = 0; off < len;) {
            i32 n = 0;
            if (!WriteFile(h, buf+off, len-off, &n, 0) || !n) {
                return 0;
            }
            off += n;
        }
        return 1;
    }

    // Console: convert complete UTF-8 sequences to UTF-16
    u8 *pending = plt->pending[fd];
    while (len > 0) {
        u8 *tmp = plt->utf8;
        i32 n   = 0;
        for (; n < plt->npending[fd]; n++) {
            tmp[n] = pending[n];
        }
        plt->npending[fd] = 0;
        i32 cap  = (i32)countof(plt->utf8);
        i32 take = len < cap-n ? len : cap-n;
        xcpy(tmp+n, buf, touz(take));
        n   += take;
        buf += take;
        len -= take;

        // Hold back an incomplete trailing sequence
        i32 end = n;
        i32 back = 0;
        for (; back<3 && end-back-1>=0 && (tmp[end-back-1]&0xc0)==0x80; back++) {}
        if (end-back-1 >= 0) {
            u8 lead = tmp[end-back-1];
            i32 need = lead>=0xf0 ? 4 : lead>=0xe0 ? 3 : lead>=0xc0 ? 2 : 1;
            if (need > back+1) {
                i32 hold = back + 1;
                for (i32 i = 0; i < hold; i++) {
                    pending[i] = tmp[end-hold+i];
                }
                plt->npending[fd] = hold;
                end -= hold;
            }
        }
        i32 wlen = MultiByteToWideChar(CP_UTF8, 0, tmp, end, plt->wide,
                                       (i32)countof(plt->wide));
        for (i32 off = 0; off < wlen;) {
            i32 w = 0;
            if (!WriteConsoleW(h, plt->wide+off, wlen-off, &w, 0) || !w) {
                return 0;
            }
            off += w;
        }
    }
    return 1;
}

static Str plt_getenv(Plt *plt, Str name, Arena *a)
{
    Arena scratch = *a;
    c16 *wname = towide(name, &scratch);
    c16 *value = plt->wide;  // free outside plt_write
    i32  len   = GetEnvironmentVariableW(wname, value, (i32)countof(plt->wide));
    if (!len) {
        // Distinguish an empty variable from an unset one
        return GetLastError()==ERROR_ENVVAR_NOT_FOUND ? (Str){0} : S("");
    } else if (len >= (i32)countof(plt->wide)) {
        return (Str){0};
    }
    i32 n = WideCharToMultiByte(CP_UTF8, 0, value, len, 0, 0, 0, 0);
    Str r = {new(a, n, u8), n};
    WideCharToMultiByte(CP_UTF8, 0, value, len, r.data, n, 0, 0);
    return r;
}

static b32 plt_sgr(Plt *plt)
{
    return plt->sgr;
}

static void plt_oom(Plt *plt)
{
    static u8 msg[] = "nroff: out of memory\n";
    plt->console[2] = 0;
    plt_write(plt, 2, msg, (i32)countof(msg)-1);
    ExitProcess(2);
    affirm(0);
}

void __stdcall mainCRTStartup(void)
{
    static Plt plt;
    for (i32 fd = 0; fd < 3; fd++) {
        i32 mode;
        plt.out[fd] = GetStdHandle(-10 - fd);
        plt.console[fd] = fd && GetConsoleMode(plt.out[fd], &mode);
    }
    plt.sgr = 1;
    i32 mode;
    if (GetConsoleMode(plt.out[1], &mode)) {
        plt.sgr = (mode&VT_PROCESSING) || SetConsoleMode(plt.out[1], mode|VT_PROCESSING);
    }

    iz    cap = (iz)1 << (sizeof(void *)==8 ? 28 : 27);
    byte *mem = VirtualAlloc(0, cap, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if (!mem) {
        cap = (iz)1 << 25;
        mem = VirtualAlloc(0, cap, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
        if (!mem) {
            plt_oom(&plt);
        }
    }
    Arena a = {&plt, mem, mem+cap};

    i32   argc  = 0;
    c16 **wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    u8  **argv  = new(&a, argc+1, u8 *);
    for (i32 i = 0; i < argc; i++) {
        i32 len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, 0, 0, 0, 0);
        argv[i] = new(&a, len, u8);
        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, argv[i], len, 0, 0);
    }

    i32 r = nroff(argc, argv, &plt, a);
    ExitProcess(r);
    affirm(0);
}


#else  // POSIX
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

struct Plt {
    i32 unused;
};

static Str plt_load(Plt *plt, Str path, Arena *a)
{
    i32 fd = 0;
    if (path.data) {
        Arena scratch = *a;
        Str cpath = concat(&scratch, path, S("\0"));
        fd = open((char *)cpath.data, O_RDONLY);
        if (fd < 0) {
            return (Str){0};
        }
    }
    Str r = {0};
    r.data = (u8 *)a->beg;
    for (;;) {
        iz avail = a->end - a->beg - r.len;
        if (avail <= 0) {
            plt_oom(plt);
        }
        iz want = avail<(1<<20) ? avail : (1<<20);
        iz got  = read(fd, r.data+r.len, touz(want));
        if (got < 0) {
            if (fd) close(fd);
            return (Str){0};  // e.g. a directory
        } else if (!got) {
            break;
        }
        r.len += got;
    }
    if (fd) {
        close(fd);
    }
    a->beg += r.len;
    return r;
}

static b32 plt_write(Plt *plt, i32 fd, u8 *buf, i32 len)
{
    (void)plt;
    for (i32 off = 0; off < len;) {
        iz n = write(fd, buf+off, touz(len-off));
        if (n <= 0) {
            return 0;
        }
        off += (i32)n;
    }
    return 1;
}

static Str plt_getenv(Plt *plt, Str name, Arena *a)
{
    (void)plt;
    Arena scratch = *a;
    Str cname = concat(&scratch, name, S("\0"));
    char *v = getenv((char *)cname.data);
    return v ? clone(a, import((u8 *)v)) : (Str){0};
}

static b32 plt_sgr(Plt *plt)
{
    (void)plt;
    return 1;
}

static void plt_oom(Plt *plt)
{
    (void)plt;
    static char msg[] = "nroff: out of memory\n";
    write(2, msg, sizeof(msg)-1);
    _exit(2);
}

int main(int argc, char **argv)
{
    static Plt plt;
    iz    cap = (iz)1<<28;
    byte *mem = mmap(0, touz(cap), PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        plt_oom(&plt);
    }
    Arena a = {&plt, mem, mem+cap};
    return nroff(argc, (u8 **)argv, &plt, a);
}
#endif
