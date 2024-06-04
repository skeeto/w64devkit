// PE export/import table listing
//
//   $ peports c:/windows/system32/kernel32.dll
//   $ peports -i main.exe    >imports.txt
//   $ peports -e library.dll >exports.txt
//
// Compilation requires GCC or Clang. Behaves like "dumpbin /exports"
// and "dumpbin /imports" from MSVC, but open source, standalone, and
// much faster. For C++ symbols, consider piping output through c++filt
// or vc++filt.
//
// Dynamic linking only permits ASCII for module and symbol names, and
// both the MSVC and GNU toolchains sometimes choke on non-ASCII names.
// Therefore wide console output is unnecessary. All standard output is
// ASCII, and non-ASCII name bytes are escape-printed. Angle brackets
// in names are escaped, so brackets appearing in output are delimiters.
// Command line argument paths may be wide, though, since these are not
// so restricted.
//
// THIS IS NOT A SECURITY TOOL! While this program does not misbehave
// given arbitrary, untrusted input, its interpretation of PE data may
// not precisely match the Windows loader, which is itself inconsistent
// between releases. For specially-crafted inputs, outputs may differ
// from Windows' parsing of the same input. This is first and foremost a
// debugging tool.
//
// Porting note: The platform layer implements osload and oswrite. To
// run the application, it calls peports with command line arguments and
// a scratch arena. The application calls osload and oswrite as needed
// for reading file and writing output.
//
// Roadmap:
// * Alternate format options? (e.g. DEF, a better gendef)
// * A recursive option, to behave like a dependency walker?
// * An option to automatically demangle C++ symbols?
//
// This is free and unencumbered software released into the public domain.

#define assert(c)       while (!(c)) __builtin_unreachable()
#define countof(a)      (iz)(sizeof(a) / sizeof(*(a)))
#define new(a, n, t)    (t *)alloc(a, n, sizeof(t), _Alignof(t))
#define s8(s)           (s8){(u8 *)s, countof(s)-1}
#define catch(e)        __builtin_setjmp((e)->jmp)

typedef unsigned char       u8;
typedef unsigned short      u16;
typedef   signed int        b32;
typedef   signed int        i32;
typedef unsigned int        u32;
typedef unsigned short      char16_t;
typedef          char16_t   c16;
typedef __PTRDIFF_TYPE__    iz;
typedef __SIZE_TYPE__       uz;
typedef          char       byte;

typedef struct {
    u8 *data;
    iz  len;
} s8;

typedef struct {
    void *jmp[5];
    s8    err;
} escape;

__attribute((noreturn))
static void throw(escape *e, s8 reason)
{
    e->err = reason;
    __builtin_longjmp(e->jmp, 1);
}

typedef struct {
    byte   *beg;
    byte   *end;
    escape *esc;
} arena;

// Read an entire file into memory. Returns a null string on error. The
// information we care about is probably at the very beginning of the
// file, so this function does not fill the whole arena but truncates as
// necessary to leave an 8th or so of the remaining space for parsing.
// Truncation is not an error. The special path "-" is standard input.
static s8 osload(arena *, s8);

// Write some bytes to standard output (1) or standard error (2).
static b32 oswrite(i32, u8 *, i32);

static byte *alloc(arena *a, iz count, iz size, iz align)
{
    assert(count >= 0);
    iz pad = -(uz)a->beg & (align - 1);
    if (count >= (a->end - a->beg - pad)/size) {
        throw(a->esc, s8("out of memory"));
    }
    byte *r = a->beg + pad;
    a->beg += pad + count*size;
    return __builtin_memset(r, 0, count*size);
}

static s8 span(u8 *beg, u8 *end)
{
    assert(beg <= end);
    s8 r = {0};
    r.data = beg;
    r.len = end - beg;
    return r;
}

static b32 equals(s8 a, s8 b)
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

static s8 slice3(s8 s, iz beg, iz end, escape *e)
{
    if (beg<0 || beg>end || end>s.len) {
        throw(e, s8("unexpected end of input (slice)"));
    }
    s.data += beg;
    s.len = end - beg;
    return s;
}

static s8 slice2(s8 s, iz beg, escape *e)
{
    return slice3(s, beg, s.len, e);
}

static u16 readu16(s8 s, iz off, escape *e)
{
    if (off > s.len-2) {
        throw(e, s8("unexpected end of input (uint16)"));
    }
    u8 *p = s.data + off;
    return (u16)((u16)p[1]<<8 | p[0]);
}

static u32 readu32(s8 s, iz off, escape *e)
{
    if (off > s.len-4) {
        throw(e, s8("unexpected end of input (uint32)"));
    }
    u8 *p = s.data + off;
    return (u32)p[3]<<24 | (u32)p[2]<<16 | (u32)p[1]<<8 | p[0];
}

static s8 nullterm(s8 s)
{
    if (!s.data) return s;
    iz len = 0;
    for (; len<s.len && s.data[len]; len++) {}
    s.len = len;
    return s;
}

typedef struct {
    u8 *buf;
    i32 len;
    i32 cap;
    i32 fd;
    b32 err;
} u8buf;

static u8buf newu8buf(arena *a, i32 fd)
{
    u8buf b = {0};
    b.cap = 1<<12;
    b.buf = new(a, b.cap, u8);
    b.fd  = fd;
    return b;
}

static void flush(u8buf *b)
{
    if (!b->err && b->len) {
        b->err |= !oswrite(b->fd, b->buf, b->len);
        b->len = 0;
    }
}

static void print(u8buf *b, s8 s)
{
    for (iz off = 0; !b->err && off<s.len;) {
        i32 avail = b->cap - b->len;
        i32 count = avail<s.len-off ? avail : (i32)(s.len-off);
        __builtin_memcpy(b->buf+b->len, s.data+off, count);
        off += count;
        b->len += count;
        if (b->len == b->cap) {
            flush(b);
        }
    }
}

static void printu32(u8buf *b, u32 x)
{
    u8  buf[16];
    u8 *end = buf + countof(buf);
    u8 *beg = end;
    do {
        *--beg = (u8)(x%10) + '0';
    } while (x /= 10);
    return print(b, span(beg, end));
}

// Escape-print a module or symbol name.
static void printname(u8buf *b, s8 s)
{
    if (!s.len) {
        print(b, s8("<>"));  // signify empty using brackets
    } else {
        for (iz i = 0; i < s.len; i++) {
            u8 c = s.data[i];
            if (c<0x20 || c>0x7f || c=='<' || c=='>' || c=='\\') {
                u8 encode[4] = "\\x..";
                encode[2] = "0123456789abcdef"[c>>4];
                encode[3] = "0123456789abcdef"[c&15];
                print(b, span(encode, encode+countof(encode)));
            } else {
                print(b, span(&c, &c+1));
            }
        }
    }
}

typedef struct {
    u32 beg;
    u32 end;
    s8  mem;
} region;

typedef struct {
    region *regions;
    i32     len;
} vm;

// Parsing a PE means simulating a loader. A virtual memory (vm) object
// represents sections mapped into a virtual address space, and this
// function reads regions out of that address space.
static s8 loadrva(vm m, u32 vaddr, escape *e)
{
    s8 r = {0};
    for (i32 i = 0; i < m.len; i++) {
        region s = m.regions[i];
        if (vaddr>=s.beg && vaddr<s.end) {
            r = slice3(s.mem, vaddr-s.beg, s.mem.len, e);
            break;
        }
    }
    return r;
}

// Throw an error if the result overflows. It would be better not to do
// arithmetic on unsigned operands, but PE requires it.
static u32 checkadd(u32 a, u32 b, escape *e)
{
    if (a > 0xffffffff-b) {
        throw(e, s8("overflow computing 32-bit offset"));
    }
    return a + b;
}

static void usage(u8buf *b)
{
    print(b, s8(
        "usage: peports [-ehi] [files...]\n"
        "  -e    print the export table\n"
        "  -h    print this message\n"
        "  -i    print the import table\n"
        "Prints export and import tables of EXEs and DLLs.\n"
        "Given no arguments, reads data from standard input.\n"
    ));
}

typedef struct {
    u8buf *out;
    u8buf *err;
    i32    optind;
    b32    exports;
    b32    imports;
} config;

enum {OPT_OK, OPT_EXIT, OPT_ERR};

static i32 parseopts(config *c, i32 argc, s8 *argv)
{
    for (c->optind = !!argc; c->optind < argc; c->optind++) {
        s8 arg = argv[c->optind];
        if (!arg.len || arg.data[0]!='-') break;
        for (iz i = 1; i < arg.len; i++) {
            u8 x = arg.data[i];
            switch (x) {
            case 'e':
                c->exports = 1;
                break;
            case 'h':
                usage(c->out);
                flush(c->out);
                return c->out->err ? OPT_ERR : OPT_EXIT;
            case 'i':
                c->imports = 1;
                break;
            default:
                print(c->err, s8("peports: unknown option: -"));
                print(c->err, span(&x, &x+1));
                print(c->err, s8("\n"));
                usage(c->err);
                flush(c->err);
                return OPT_ERR;
            }
        }
    }

    if (!c->imports && !c->exports) {
        c->imports = c->exports = 1;
    }
    return OPT_OK;
}

static void processpe(s8 dll, config conf, arena scratch)
{
    u8buf  *out = conf.out;
    escape *esc = scratch.esc;

    u32 peoff = readu32(dll, 0x3c, esc);
    s8  pe    = slice2(dll, peoff, esc);
    s8  pehdr = slice3(pe, 0, 4, esc);
    if (!equals(s8("PE\0\0"), pehdr)) {
        throw(esc, s8("not a PE file"));
    }

    u16 nsections = readu16(pe, 4+ 2, esc);
    u16 hdrsize   = readu16(pe, 4+16, esc);

    enum { PE32, PE64 };
    u16 magic = readu16(pe, 4+20, esc);
    i32 type = -1;
    switch (magic) {
    default:     throw(esc, s8("unknown PE magic"));
    case 0x010b: type = PE32; break;
    case 0x020b: type = PE64; break;
    }

    vm map       = {0};
    i32 loadlen  = nsections>96 ? 96 : nsections;
    map.regions  = new(&scratch, loadlen, region);
    s8  sections = slice2(pe, 4+20+hdrsize, esc);
    for (i32 i = 0; i < loadlen; i++) {
        u32 vsize = readu32(sections, 40*i+ 8, esc);
        u32 vaddr = readu32(sections, 40*i+12, esc);
        u32 rsize = readu32(sections, 40*i+16, esc);
        u32 raddr = readu32(sections, 40*i+20, esc);

        i32 r = map.len++;
        map.regions[r].beg = vaddr;
        map.regions[r].end = checkadd(vaddr, vsize, esc);
        if (r) {
            region prev = map.regions[r-1];
            if (prev.beg>=vaddr || prev.end>vaddr) {
                throw(esc, s8("invalid section order"));
            }
        }

        u32 rend = checkadd(raddr, rsize, esc);
        map.regions[r].mem = slice3(dll, raddr, rend, esc);
        if (vsize > rsize) {
            // Padded sections (e.g. .bss) unlikely interesting: discard
            map.len--;
        } else if (vsize < rsize) {
            // Truncated sections *are* usually interesting. Go figure.
            map.regions[r].mem.len = vsize;
        }
    }

    u32 edataoff = readu32(pe, 24+(type==PE32 ?  96 : 112), esc);
    u32 edatalen = readu32(pe, 24+(type==PE32 ? 100 : 116), esc);
    u32 edataend = checkadd(edataoff, edatalen, esc);
    s8  edata    = loadrva(map, edataoff, esc);

    if (conf.exports && edatalen) {
        print(out, s8("EXPORTS\n"));

        u32 ordbase = readu32(edata, 4*4, esc);
        i32 naddrs  = readu32(edata, 5*4, esc);
        i32 nnames  = readu32(edata, 6*4, esc);
        if (naddrs<0 || nnames<0) {
            throw(esc, s8("invalid export count"));
        }

        // If naddrs is huge, this will fail here with OOM
        u8 *seen = new(&scratch, naddrs, u8);

        u32 addrsoff = readu32(edata, 7*4, esc);
        s8  addrs    = loadrva(map, addrsoff, esc);
        u32 namesoff = readu32(edata, 8*4, esc);
        s8  names    = loadrva(map, namesoff, esc);
        u32 ordsoff  = readu32(edata, 9*4, esc);
        s8  ordinals = loadrva(map, ordsoff, esc);

        // If nnames is huge, the loop will EOF before overflow
        for (i32 i = 0; i < nnames; i++) {
            u16 ordinal = readu16(ordinals, i*2, esc);
            if (ordinal >= naddrs) {
                throw(esc, s8("invalid export ordinal"));
            }
            seen[ordinal] = 1;

            // If RVA points in .edata it's a forwarder name
            s8 module = {0};
            u32 addr = readu32(addrs, ordinal*4, esc);
            if (addr>=edataoff && addr<edataend) {
                module = nullterm(loadrva(map, addr, esc));
            }

            print(out, s8("\t"));
            u32 ordend = checkadd(ordinal, ordbase, esc);
            printu32(out, ordend);
            print(out, s8("\t"));

            u32 off = readu32(names, i*4, esc);
            s8 name = nullterm(loadrva(map, off, esc));
            printname(out, name);
            if (module.data) {
                print(out, s8(" <"));
                printname(out, module);
                print(out, s8(">"));
            }
            print(out, s8("\n"));
        }

        for (i32 i = 0; i < naddrs; i++) {
            if (!seen[i]) {
                print(out, s8("\t"));
                printu32(out, checkadd(i, ordbase, esc));
                print(out, s8("\t<NONAME>\n"));
            }
        }
    }

    u32 idataoff = readu32(pe, 24+(type==PE32 ? 104 : 120), esc);
    u32 idatalen = readu32(pe, 24+(type==PE32 ? 108 : 124), esc);
    s8  idata    = loadrva(map, idataoff, esc);

    if (conf.imports && idatalen) {
        // The PE specification says the last import directory table
        // entry is all zeros, indicating the directory end. However,
        // MSVC link.exe is buggy and does not reliably produce this
        // null entry. Instead the directory runs into import lookup
        // tables and string table, causing the directory to read as
        // garbage. We have two workarounds:
        //
        // 1. Track the earliest import lookup table RVA, and stop
        //    reading if the directory would overlap it.
        // 2. Don't treat garbage RVA fields as errors, just stop
        //    reading the table (Binutils strategy).
        //
        // This issue was crashing objdump back in 2005. See Binutils
        // commit a50b216054a4.
        u32 firsttable = -1;

        for (i32 i = 0;; i++) {
            if (idataoff + i*20 == firsttable) {
                // Probably the link.exe bug. We're now overlapping an
                // import lookup table, so stop reading the import
                // directory. The left-side sum might overflow, but
                // that's fine. This is just a heuristic.
                break;
            }

            u32 tableoff = readu32(idata, i*20+ 0, esc);
            u32 nameoff  = readu32(idata, i*20+12, esc);
            if (!tableoff || !nameoff) break;

            s8 name = nullterm(loadrva(map, nameoff, esc));
            if (!name.data) break;  // ignore link.exe bug

            printname(out, name);
            print(out, s8("\n"));

            s8 table = loadrva(map, tableoff, esc);
            if (!table.data) break;  // ignore link.exe bug
            firsttable = firsttable<tableoff ? firsttable : tableoff;

            for (i32 j = 0;; j++) {
                u32 addr = 0;
                switch (type) {
                case PE32:
                    addr  = readu32(table, j*4, esc);
                    break;
                case PE64:
                    addr  = readu32(table, j*8+4, esc);
                    addr |= readu32(table, j*8+0, esc);
                    break;
                }
                if (!addr) break;

                print(out, s8("\t"));
                if (addr>>31) {
                    printu32(out, addr&0x7fffffff);
                    print(out, s8("\t<NONAME>"));
                } else {
                    s8 entry = loadrva(map, addr, esc);
                    u16 hint = readu16(entry, 0, esc);
                    printu32(out, hint);
                    print(out, s8("\t"));
                    s8  name = nullterm(slice2(entry, 2, esc));
                    printname(out, name);
                }
                print(out, s8("\n"));
            }
        }
    }
}

static void processpath(s8 path, config conf, arena scratch)
{
    s8 dll = osload(&scratch, path);
    if (!dll.data) {
        throw(scratch.esc, s8("could not load file"));
    }
    processpe(dll, conf, scratch);
}

static b32 peports(i32 argc, s8 *argv, arena scratch)
{
    b32   ok     = 1;
    u8buf out[1] = {newu8buf(&scratch, 1)};
    u8buf err[1] = {newu8buf(&scratch, 2)};

    config conf = {0};
    conf.out = out;
    conf.err = err;
    switch (parseopts(&conf, argc, argv)) {
    case OPT_OK:   break;
    case OPT_EXIT: return 1;
    case OPT_ERR:  return 0;
    }

    if (conf.optind == argc) {
        static s8 fakeargv[] = {s8("peports"), s8("-")};
        argc = countof(fakeargv);
        argv = fakeargv;
        conf.optind = 1;
    }

    escape esc = {0};
    scratch.esc = &esc;
    for (i32 i = conf.optind; i < argc; i++) {
        if (catch(&esc)) {
            flush(out);
            print(err, s8("peports: "));
            print(err, esc.err);
            print(err, s8(": "));
            print(err, argv[i]);  // NOTE: UTF-8
            print(err, s8("\n"));
            flush(err);
            ok &= 1;
            continue;
        }
        processpath(argv[i], conf, scratch);
    }

    flush(out);
    ok &= !out->err;
    return ok;
}


#if _WIN32
// $ gcc -nostartfiles -o peports.exe peports.c
// $ clang-cl peports.c /link /subsystem:console
//            kernel32.lib shell32.lib libvcruntime.lib

#define W32(r) __declspec(dllimport) r __stdcall
W32(b32)    CloseHandle(uz);
W32(c16 **) CommandLineToArgvW(c16 *, i32 *);
W32(b32)    CreateFileW(c16 *, i32, i32, uz, i32, i32, uz);
W32(void)   ExitProcess(i32) __attribute((noreturn));
W32(c16 *)  GetCommandLineW(void);
W32(uz)     GetStdHandle(i32);
W32(i32)    MultiByteToWideChar(i32, i32, u8 *, i32, c16 *, i32);
W32(b32)    ReadFile(uz, u8 *, i32, i32 *, uz);
W32(i32)    WideCharToMultiByte(i32, i32, c16 *, i32, u8 *, i32, uz, uz);
W32(b32)    WriteFile(uz, u8 *, i32, i32 *, uz);

static i32 truncsize(iz len, i32 max)
{
    return max<len ? max : (i32)len;
}

static s8 osload(arena *a, s8 path)
{
    enum {
        FILE_ATTRIBUTE_NORMAL   = 0x80,
        FILE_SHARE_ALL          = 7,
        GENERIC_READ            = 0x80000000,
        OPEN_EXISTING           = 3,
    };
    s8 r       = {0};
    b32 close  = 0;
    uz  handle = -1;

    if (equals(path, s8("-"))) {
        handle = GetStdHandle(-10);

    } else {
        arena scratch = *a;

        assert((i32)path.len == path.len);
        i32  len   = (i32)path.len;
        i32  wlen  = MultiByteToWideChar(65001, 0, path.data, len, 0, 0);
        c16 *wpath = new(&scratch, wlen+1, c16);
        MultiByteToWideChar(65001, 0, path.data, len, wpath, wlen);

        handle = CreateFileW(
            wpath,
            GENERIC_READ,
            FILE_SHARE_ALL,
            0,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            0
        );
        if (handle == (uz)-1) {
            return r;
        }
        close = 1;
    }

    r.data = (u8 *)a->beg;
    iz avail = a->end - a->beg;
    avail -= avail / 8;  // don't fill the arena to the brim
    for (;;) {
        i32 max = 1<<21;
        i32 len;
        ReadFile(handle, r.data+r.len, truncsize(avail-r.len, max), &len, 0);
        if (len < 1) break;
        r.len += len;
    }
    a->beg += r.len;

    if (close) CloseHandle(handle);
    return r;
}

static b32 oswrite(i32 fd, u8 *buf, i32 len)
{
    uz h = GetStdHandle(-10 - fd);
    return WriteFile(h, buf, len, &len, 0);
}

__attribute((force_align_arg_pointer))
void mainCRTStartup(void)
{
    static byte mem[sizeof(uz)<<26];  // 256/512 MiB
    arena scratch = {0};
    scratch.beg = mem;
    asm ("" : "+r"(scratch.beg));  // launder the pointer
    scratch.end = scratch.beg + countof(mem);

    c16  *cmd   = GetCommandLineW();
    i32   argc  = 0;
    c16 **argvw = CommandLineToArgvW(cmd, &argc);
    s8   *argv  = new(&scratch, argc, s8);
    for (i32 i = 0; i < argc; i++) {
        i32 len = WideCharToMultiByte(65001, 0, argvw[i], -1, 0, 0, 0, 0);
        argv[i].data = new(&scratch, len, u8);
        argv[i].len  = len ? len-1 : len;
        WideCharToMultiByte(65001, 0, argvw[i], -1, argv[i].data, len, 0, 0);
    }

    b32 ok = peports(argc, argv, scratch);
    ExitProcess(!ok);
}


#elif __AFL_COMPILER
// $ afl-gcc-fast -g3 -fsanitize=undefined peports.c
// $ mkdir i
// $ cp corpus.dll i/
// $ afl-fuzz -ii -oo ./a.out
#include <stdlib.h>
#include <unistd.h>

__AFL_FUZZ_INIT();

static b32 oswrite(i32, u8 *, i32) { return 1; }
static s8  osload(arena *, s8)     { __builtin_trap(); }

int main(void)
{
    __AFL_INIT();

    iz cap  = 1<<20;
    arena a = {0};
    a.beg   = malloc(cap);
    a.end   = a.beg + cap;

    config c  = {0};
    c.exports = 1;
    c.imports = 1;
     c.out    = new(&a, 1, u8buf);
    *c.out    = newu8buf(&a, 1);
     c.err    = new(&a, 1, u8buf);
    *c.err    = newu8buf(&a, 2);

    s8 dll   = {0};
    dll.data = __AFL_FUZZ_TESTCASE_BUF;
    while (__AFL_LOOP(10000)) {
        dll.len = __AFL_FUZZ_TESTCASE_LEN;
        a.esc = &(escape){0};
        if (!catch(a.esc)) {
            processpe(dll, c, a);
        }
    }
}


#else  // POSIX-ish?
// $ cc -o peports peports.c
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static s8 osload(arena *a, s8 path)
{
    s8  r       = {0};
    int fd      = 0;
    b32 closeit = 1;

    if (equals(path, s8("-"))) {
        closeit = 0;
    } else {
        // NOTE: Assume the path is null-terminated because it came
        // straight from argv. This program does not construct paths.
        char *cpath = (char *)path.data;
        fd = open(cpath, O_RDONLY);
        if (fd == -1) return r;
    }

    r.data = (u8 *)a->beg;
    iz avail = a->end - a->beg;
    avail -= avail / 8;  // don't fill the arena to the brim
    while (r.len < avail) {
        iz len = read(fd, r.data+r.len, avail-r.len);
        if (len < 1) break;
        r.len += len;
    }
    a->beg += r.len;

    if (closeit) close(fd);
    return r;
}

static b32 oswrite(i32 fd, u8 *buf, i32 len)
{
    for (i32 off = 0; off < len;) {
        i32 r = (i32)write(fd, buf+off, len-off);
        if (r < 1) return 0;
        off += r;
    }
    return 1;
}

int main(int argc, char **argv)
{
    static byte mem[sizeof(uz)<<26];  // 256/512 MiB
    arena scratch = {0};
    scratch.beg = mem;
    asm ("" : "+r"(scratch.beg));  // launder the pointer
    scratch.end = scratch.beg + countof(mem);

    s8 *args = new(&scratch, argc, s8);
    for (int i = 0; i < argc; i++) {
        args[i].data = (u8 *)argv[i];
        args[i].len  = strlen(argv[i]);
    }
    b32 ok = peports(argc, args, scratch);
    return !ok;
}
#endif
