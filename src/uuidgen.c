// uuidgen: drop-in replacement for Microsoft uuidgen, for widl
// Chris Wellons <wellons@nullprogram.com>
//
// w64dk: $ cc -nostartfiles -Oz -s -o uuidgen.exe uuidgen.c -lmemory
// unix:  $ cc               -Oz -s -o uuidgen     uuidgen.c
//
// Features versus the original uuidgen:
// * Better license
// * ~10x smaller, ~10x faster
// * Supports wide and long paths
// * Fewer bugs; detects write errors
// * Extremely portable
//
// It generates UUIDs by encrypting a 128-bit counter with Speck128/128,
// keyed at startup from the operating system CSPRNG, falling back on a
// high resolution absolute timestamp and ASLR.
//
// This is free and unencumbered software released into the public domain.
#include <stddef.h>
#include <stdint.h>

#define N "\r\n"

#define countof(a)      (iz)(sizeof(a) / sizeof(*(a)))
#define affirm(c)       while (!(c)) unreachable()
#define S(s)            (Str){(u8 *)s, sizeof(s)-1}
#define new(a, n, t)    (t *)alloc(a, n, sizeof(t), _Alignof(t))
#define maxof(t)        ((t)-1<1 ? (((t)1<<(sizeof(t)*8-2))-1)*2+1 : (t)-1)

typedef uint8_t     u8;
typedef int32_t     b32;
typedef int32_t     i32;
typedef int64_t     i64;
typedef uint64_t    u64;
typedef ptrdiff_t   iz;
typedef size_t      uz;

typedef struct { u8 *beg, *end; } Arena;

typedef struct Plt Plt;
static b32  plt_write(Plt *, i32, u8 *, i32);   // like write(2)
static void plt_printpath(Plt *, u8 *, Arena);  // display a file system path
static b32  plt_redirect(Plt *, u8 *, Arena);   // redirect standard output

static uz touz(iz n)
{
    affirm(n >= 0);
    return (uz)n;
}

static void *alloc(Arena *a, iz count, iz size, iz align)
{
    iz pad = (iz)-(uz)a->beg & (align - 1);
    affirm(count < (a->end - a->beg - pad)/size);
    u8 *r = a->beg + pad;
    a->beg += pad + count*size;
    return __builtin_memset(r, 0, touz(size*count));
}

typedef struct {
    u8 *data;
    iz  len;
} Str;

static Str import(u8 *s)
{
    Str r = {};
    r.data = s;
    for (; r.data[r.len]; r.len++) {}
    return r;
}

static Str span(u8 *beg, u8 *end)
{
    affirm(beg <= end);
    return (Str){beg, end-beg};
}

typedef struct { u64 k[32]; } Context;
typedef struct { u64 x, y; } Block;

static Context speck_init(Block b)
{
    Context ctx = {};
    for (i32 i = 0; i < 31; i++) {
        b.x = b.x>>8 | b.x<<56;
        b.x += b.y;
        b.x ^= (u64)i;
        b.y = b.y<<3 | b.y>>61;
        b.y ^= b.x;
        ctx.k[i+1] = b.y;
    }
    return ctx;
}

static Block speck_encrypt(Context ctx, Block b)
{
    for (i32 i = 0; i < 32; i++) {
        b.x = b.x>>8 | b.x<<56;
        b.x += b.y;
        b.x ^= ctx.k[i];
        b.y = b.y<<3 | b.y>>61;
        b.y ^= b.x;
    }
    return b;
}

typedef struct {
    Plt *plt;
    i32  len;
    i32  fd;
    b32  err;
    u8   buf[1<<14];
} Output;

static Output *newoutput(Arena *a, Plt *plt, i32 fd)
{
    Output *out = new(a, 1, Output);
    out->fd  = fd;
    out->plt = plt;
    return out;
}

static void flush(Output *out)
{
    if (!out->err && out->len) {
        out->err = !plt_write(out->plt, out->fd, out->buf, out->len);
        out->len = 0;
    }
}

static void print(Output *out, Str s)
{
    for (iz off = 0; !out->err && off<s.len;) {
        i32 avail = (i32)countof(out->buf) - out->len;
        i32 count = avail<s.len-off ? avail : (i32)(s.len-off);
        __builtin_memcpy(out->buf+out->len, s.data+off, touz(count));
        off += count;
        out->len += count;
        if (out->len == countof(out->buf)) {
            flush(out);
        }
    }
}

static void printhex(Output *out, u8 *hex, i32 w, u64 x)
{
    affirm(w>=0 && w<=16);
    u8  buf[16] = {};
    u8 *end = buf + sizeof(buf);
    u8 *beg = end;
    for (i32 i = 0; i < w; i++) {
        *--beg = hex[x&15];
        x >>= 4;
    }
    print(out, span(beg, end));
}

static void printuuid(Output *out, u8 *hex, Block uuid)
{
    printhex(out, hex,  8, uuid.x>>32);  print(out, S("-"));
    printhex(out, hex,  4, uuid.x>>16);  print(out, S("-"));
    printhex(out, hex,  4, uuid.x>> 0);  print(out, S("-"));
    printhex(out, hex,  4, uuid.y>> 0);  print(out, S("-"));
    printhex(out, hex, 12, uuid.y>>16);
}

static void printidl(Output *out, u8 *hex, Block uuid)
{
    print(out, S("[" N "uuid("));
    printuuid(out, hex, uuid);
    print(out, S(
        ")," N
        "version(1.0)" N
        "]" N
        "interface INTERFACENAME" N
        "{" N
        "" N
        "}" N
    ));
}

static void printstruct(Output *out, u8 *hex, Block uuid)
{
    print(out, S("INTERFACENAME = { /* "));
    printuuid(out, hex, uuid);
    print(out, S(" */" N "    0x"));
    printhex(out, hex,  8, uuid.x>>32);  print(out, S("," N "    0x"));
    printhex(out, hex,  4, uuid.x>>16);  print(out, S("," N "    0x"));
    printhex(out, hex,  4, uuid.x>> 0);  print(out, S("," N "    {0x"));
    printhex(out, hex,  2, uuid.y>> 8);  print(out, S(", 0x"));
    printhex(out, hex,  2, uuid.y>> 0);  print(out, S(", 0x"));
    printhex(out, hex,  2, uuid.y>>56);  print(out, S(", 0x"));
    printhex(out, hex,  2, uuid.y>>48);  print(out, S(", 0x"));
    printhex(out, hex,  2, uuid.y>>40);  print(out, S(", 0x"));
    printhex(out, hex,  2, uuid.y>>32);  print(out, S(", 0x"));
    printhex(out, hex,  2, uuid.y>>24);  print(out, S(", 0x"));
    printhex(out, hex,  2, uuid.y>>16);
    print(out, S("}" N "  };" N));
}

static Block generate(Context ctx, i64 i)
{
    Block r = {(u64)i, 0};
    r = speck_encrypt(ctx, r);
    r.x &= (u64)~0xb000;
    r.x |= (u64) 0x4000;
    r.y &= (u64)~0x4000;
    r.y |= (u64) 0x8000;
    return r;
}

static i64 parse(u8 *s)
{
    i64 r   = 0;
    iz  len = 0;
    s += *s == '+';
    for (; s[len]; len++) {
        i64 digit = s[len] - '0';
        if (digit<0 || digit>9) {
            break;
        } else if (r > (maxof(i64) - digit)/10) {
            // NOTE: Microsoft uuidgen uses atoi() without validation
            // nor error checking. Instead, saturate to an integer too
            // large to practically complete, simulating a bigint.
            return maxof(i64);  // saturate
        }
        r = r*10 + digit;
    }
    return len ? r : -1;
}

static void usage(Output *out)
{
    print(out, S(
        "usage: uuidgen [-chinosvx?]" N
        "    -c       upper case UUIDs" N
        "    -h/-?    print this message" N
        "    -i       IDL interface template" N
        "    -n<num>  number of UUIDs to generate" N
        "    -o<path> output to a file" N
        "    -s       C struct template" N
        "    -v       print version" N
        "    -x       ignored for compatability" N
    ));
}

static void version(Output *out)
{
    print(out, S(
        "w64devkit UUID Generator v1.01 public domain "
        "[like Windows SDK uuidgen]" N N
    ));
}

static i32 uuidgen(i32 argc, u8 **argv, Plt *plt, Arena scratch, Block seed)
{
    Output *out = newoutput(&scratch, plt, 1);
    Output *err = newoutput(&scratch, plt, 2);

    enum { MODE_PLAIN, MODE_IDL, MODE_C } mode = 0;
    u8 *hex   = S("0123456789abcdef").data;
    i64 count = 1;
    u8 *path  = 0;

    for (i32 i = 1; i < argc; i++) {
        u8 dash = argv[i][0];
        u8 opt  = dash=='-' || dash=='/' ? argv[i][1] : 0;
        switch (opt) {
        case 'c':
            hex = S("0123456789ABCDEF").data;
            break;

        case 'h':
        case '?':
            version(out);
            usage(out);
            flush(out);
            return out->err;

        case 'i':
            mode = MODE_IDL;
            break;

        case 'n':
            // NOTE: Microsoft uuidgen does not allow a count of zero,
            // which is silly and pointless. We'll allow it here.
            count = parse(argv[i]+2);
            if (count < 0) {
                print(err, S(
                    "Argument to uuidgen must be a non-negative integer." N N
                ));
                version(err);
                usage(err);
                flush(err);
                return 1;
            }
            break;

        case 'o':
            path = argv[i] + 2;
            break;

        case 's':
            mode = MODE_C;
            break;

        case 'v':
            version(out);
            flush(out);
            return out->err;

        case 'x':  // ignored
            break;

        default:
            print(err, S("Invalid Switch Usage: "));
            print(err, import(argv[i]));
            print(err, S(N N));
            version(err);
            usage(err);
            flush(err);
            return 1;
        }
    }

    if (path) {
        if (!plt_redirect(plt, path, scratch)) {
            print(err, S("Cannot open output file: "));
            flush(err);
            plt_printpath(plt, path, scratch);
            print(err, S(N N));
            flush(err);
            return 1;
        }
    }

    Context ctx = speck_init(seed);
    for (i64 i = 0; i < count; i++) {
        Block uuid = generate(ctx, i);
        switch (mode) {
        case MODE_PLAIN:
            printuuid(out, hex, uuid);
            print(out, S(N));
            break;
        case MODE_IDL:
            printidl(out, hex, uuid);
            break;
        case MODE_C:
            printstruct(out, hex, uuid);
            break;
        }
    }

    flush(out);
    return out->err;
}


#if _WIN32
typedef uint16_t    char16_t;
typedef char16_t    c16;

#define W32 [[gnu::dllimport, gnu::stdcall]]
W32 c16   **CommandLineToArgvW(c16 *, i32 *);
W32 uz      CreateFileW(c16 *, i32, i32, uz, i32, i32, uz);
W32 void    ExitProcess[[noreturn]](i32);
W32 c16    *GetCommandLineW();
W32 b32     GetConsoleMode(uz, i32 *);
W32 uz      GetProcAddress(uz, char *);
W32 uz      GetStdHandle(i32);
W32 void    GetSystemTimeAsFileTime(u64 *);
W32 uz      LoadLibraryA(char *);
W32 i32     MultiByteToWideChar(i32, i32, u8 *, i32, c16 *, i32);
W32 i32     WideCharToMultiByte(i32, i32, c16 *, i32, u8 *, i32, uz, uz);
W32 b32     WriteConsoleW(uz, c16 *, i32, i32 *, uz);
W32 b32     WriteFile(uz, u8 *, i32, i32 *, uz);

enum {
    CP_UTF8                 = 65001,
    CREATE_ALWAYS           = 2,
    FILE_ATTRIBUTE_NORMAL   = 0x80,
    FILE_SHARE_ALL          = 7,
    GENERIC_READ            = (i32)0x8000'0000,
    GENERIC_WRITE           = (i32)0x4000'0000,
};

static i32 trunc32(iz n)
{
    return n>maxof(i32) ? maxof(i32) : (i32)n;
}

static c16 *towide(u8 *path, Arena scratch)
{
    i32  avail = trunc32((scratch.end - scratch.beg)/2);
    i32  pad   = (i32)-(uz)scratch.beg & 1;
    c16 *wpath = (c16 *)(scratch.beg + pad);
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, avail)) {
        return 0;
    }
    return wpath;
}

struct Plt { uz h[3]; };

static b32 plt_write(Plt *plt, i32 fd, u8 *buf, i32 len)
{
    return WriteFile(plt->h[fd], buf, len, &(i32){}, 0);
}

static void plt_printpath(Plt *plt, u8 *path, Arena scratch)
{
    if (GetConsoleMode(plt->h[2], &(i32){})) {
        c16 *wpath = towide(path, scratch);
        iz   len   = 0;
        for (; wpath[len]; len++) {}
        WriteConsoleW(plt->h[2], wpath, trunc32(len), &(i32){}, 0);
    } else {
        Str s = import(path);
        WriteFile(plt->h[2], s.data, trunc32(s.len), &(i32){}, 0);
    }
}

static b32 plt_redirect(Plt *plt, u8 *path, Arena scratch)
{
    plt->h[1] = (uz)-1;

    c16 *wpath = towide(path, scratch);
    if (!wpath) {
        return 0;
    }

    i32 access = GENERIC_WRITE;
    i32 share  = FILE_SHARE_ALL;
    i32 create = CREATE_ALWAYS;
    i32 attr   = FILE_ATTRIBUTE_NORMAL;
    plt->h[1] = CreateFileW(wpath, access, share, 0, create, attr, 0);
    return plt->h[1] != (uz)-1;
}

[[gnu::stdcall]]
i32 mainCRTStartup()
{
    static u8 mem[1<<21];
    Arena a = {mem, mem+countof(mem)};

    c16  *cmd   = GetCommandLineW();
    i32   argc  = 0;
    c16 **wargv = CommandLineToArgvW(cmd, &argc);
    u8  **argv  = new(&a, argc+1, u8 *);
    for (i32 i = 0; i < argc; i++) {
        i32 len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, 0, 0, 0, 0);
        argv[i] = new(&a, len, u8);
        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, argv[i], len, 0, 0);
    }

    Block seed = {};
    uz dll  = LoadLibraryA("advapi32");
    uz proc = GetProcAddress(dll, "SystemFunction036");
    u8 (*RtlGenRandom)(void *, i32) = (u8 (*)(void *, i32))proc;
    if (!RtlGenRandom || !RtlGenRandom(&seed, sizeof(seed))) {
        GetSystemTimeAsFileTime(&seed.x);
        seed.y = (uz)mainCRTStartup;  // ASLR
    }

    Plt *plt = new(&a, 1, Plt);
    plt->h[1] = GetStdHandle(-11);
    plt->h[2] = GetStdHandle(-12);
    i32 r = uuidgen(argc, argv, plt, a, seed);
    ExitProcess(r);
    unreachable();
}


#elif __linux && __amd64 && !__STDC_HOSTED__  // mostly for fun
// $ cc -static -ffreestanding -nostdlib -Oz -s -o uuidgen uuidgen.c

enum {
    SYS_write   = 1,
    SYS_open    = 2,
    SYS_exit    = 60,
    O_WRONLY    = 0x001,
    O_CREAT     = 0x040,
    O_TRUNC     = 0x200,
};

struct Plt { i32 fd[3]; };

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

static b32 plt_write(Plt *plt, i32 fd, u8 *buf, i32 len)
{
    return syscall3(SYS_write, (uz)plt->fd[fd], (uz)buf, touz(len)) == len;
}

static void plt_printpath(Plt *, u8 *path, Arena)
{
    iz len = 0;
    for (; path[len]; len++) {}
    syscall3(SYS_write, 2, (uz)path, touz(len));
}

static b32 plt_redirect(Plt *plt, u8 *path, Arena)
{
    i32 flags = O_CREAT | O_TRUNC | O_WRONLY;
    plt->fd[1] = (i32)syscall3(SYS_open, (uz)path, (uz)flags, 0666);
    return plt->fd[1] >= 0;
}

static u64 rand64()
{
    u64 r;
    asm volatile (
        "0:  rdrand %0\n"
        "    jnc    0b\n"
        : "=r"(r)
    );
    return r;
}

[[gnu::used]]
static void entrypoint(uz *stack)
{
    static u8 mem[1<<21];
    Arena a    = (Arena){mem, mem+countof(mem)};

    i32   argc = (i32)*stack;
    u8  **argv = (u8 **)(stack+1);

    Block seed = {rand64(), rand64()};

    i32 r = uuidgen(argc, argv, &(Plt){{0, 1, 2}}, a, seed);
    asm volatile ("syscall" :: "a"(SYS_exit), "D"(r));
    __builtin_unreachable();
}

asm (
    "        .globl _start\n"
    "_start: mov   %rsp, %rdi\n"
    "        call  entrypoint\n"
);


#else  // POSIX
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

struct Plt { int fd[3]; };

static b32 plt_write(Plt *plt, i32 fd, u8 *buf, i32 len)
{
    return write(plt->fd[fd], buf, touz(len)) == len;
}

static void plt_printpath(Plt *, u8 *path, Arena)
{
    iz len = 0;
    for (; path[len]; len++) {}
    write(2, path, touz(len));
}

static b32 plt_redirect(Plt *plt, u8 *path, Arena)
{
    plt->fd[1] = open((char *)path, O_CREAT|O_TRUNC|O_WRONLY, 0666);
    return plt->fd[1] >= 0;
}

int main(int argc, char **argv)
{
    Block seed = {};
    int rnd = open("/dev/random", O_RDONLY);
    if (rnd == -1 || read(rnd, &seed, sizeof(seed)) != (iz)sizeof(seed)) {
        struct timespec ts = {};
        clock_gettime(CLOCK_REALTIME, &ts);
        seed.x ^= (u64)ts.tv_sec*1'000'000'000 + (u64)ts.tv_nsec;
        seed.y ^= (uz)main;  // ASLR
    }

    static u8 mem[1<<21];
    Arena a = (Arena){mem, mem+countof(mem)};
    return uuidgen(argc, (u8 **)argv, &(Plt){{0, 1, 2}}, a, seed);
}
#endif
