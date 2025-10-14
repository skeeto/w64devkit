// compdb-cc: JSON Compilation Database build wrapper
//
// Generates a compile_commands.json for arbitrary build systems by wrapping
// the compiler program and logging commands to a database. The database is
// UTF-8-encoded (assuming UTF-8 inputs on POSIX) with LF line endings on all
// platforms. Supports parallel builds.
//
// w64dk: $ cc -nostartfiles -o compdb-cc.exe compdb-cc.c
// unix:  $ cc -o compdb-cc compdb-cc.c
// msvc:  $ cl compdb-cc.c
//
// First, delete any existing database. This tool cannot update arbitrary
// databases, only those it has created and exclusively modified. Then run
// the build with $COMPDB_PATH set to the absolute path of the database:
//
//   $ rm -f compile_commands.json
//   $ COMPDB_PATH="$PWD"/compile_commands.json make -B CC=compdb-cc
//
// The wrapper may be called with different working directories by the build
// system, in which case an absolute path will be necessary in order to know
// the proper location. If $COMPDB_PATH is unset or empty, the wrapper runs
// the command without and database updates, and so it can be harmlessly left
// as the compiler during development.
//
// The underlying compiler is derived from the suffix, so compdb-cc runs cc,
// compdb-c++ runs c++, etc. Rename or symlink appropriately.
//
// Ref: https://clang.llvm.org/docs/JSONCompilationDatabase.html
// This is free and unencumbered software released into the public domain.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if _MSC_VER
#  define unreachable() __assume(0)
#  pragma comment(linker, "/subsystem:console")
#  pragma comment(lib, "kernel32")
#  pragma comment(lib, "shell32")
#  pragma comment(lib, "libvcruntime")
#endif

#define lenof(a)        (iz)(sizeof(a) / sizeof(*(a)))
#define affirm(c)       while (!(c)) unreachable()
#define S(s)            (Str){(u8 *)s, lenof(s)-1}
#define new(a, n, t)    (t *)alloc(a, n, sizeof(t), _Alignof(t))

typedef uint8_t     u8;
typedef int16_t     i16;
typedef int32_t     b32;
typedef int32_t     i32;
typedef int64_t     i64;
typedef ptrdiff_t   iz;
typedef size_t      uz;


// Platform API
//
// Platform layer opens $COMPDB_PATH, locks it, seeks SEEK_SIZE bytes
// from the end (if possible), and calls compdb(). The application layer
// calls plt_write() zero or more times. If returns with STATUS_OK, the
// platform layer unlocks the file and execs the remaining command line.

enum {
    STATUS_OK       = 0,
    STATUS_USAGE    = 200,
    STATUS_EXEC     = 201,
    STATUS_OPEN     = 202,
    STATUS_WRITE    = 203,
    STATUS_OOM      = 204,

    SEEK_SIZE   = -3,
};

typedef struct Plt Plt;
static b32  plt_write(Plt *, i32 fd, u8 *, i32);
static void plt_oom();


// Application layer

static uz touz(iz n)
{
    affirm(n >= 0);
    return (uz)n;
}

typedef struct {
    u8 *beg;
    u8 *end;
} Arena;

static void *alloc(Arena *a, iz count, iz size, iz align)
{
    iz pad = (iz)-(uz)a->beg & (align - 1);
    if (count >= (a->end - a->beg - pad)/size) {
        plt_oom();
    }
    u8 *r = a->beg + pad;
    a->beg += pad + count*size;
    return memset(r, 0, touz(size*count));
}

typedef struct {
    u8 *data;
    iz  len;
} Str;

static Str import(u8 *s)
{
    Str r = {0};
    r.data = s;
    for (; r.data[r.len]; r.len++) {}
    return r;
}

static b32 equals(Str a, Str b)
{
    return a.len==b.len && !memcmp(a.data, b.data, touz(a.len));
}

static Str substring(Str s, iz i)
{
    affirm(i>=0 && i<=s.len);
    s.data += i;
    s.len  -= i;
    return s;
}

static b32 endswith(Str s, Str suf)
{
    return s.len>=suf.len && equals(substring(s, s.len-suf.len), suf);
}

typedef struct {
    Plt *plt;
    i32  len;
    i32  fd;
    b32  err;
    u8   buf[1<<12];
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
        i32 avail = (i32)lenof(b->buf) - b->len;
        i32 count = avail<s.len-off ? avail : (i32)(s.len-off);
        memcpy(b->buf+b->len, s.data+off, touz(count));
        off    += count;
        b->len += count;
        if (b->len == lenof(b->buf)) {
            flush(b);
        }
    }
}

static void printu8(Output *b, u8 c)
{
    print(b, (Str){&c, 1});
}

// Print a JSON-quoted representation of the string.
static void printq(Output *b, Str s)
{
    printu8(b, '"');
    for (iz i = 0; i < s.len; i++) {
        u8 c = s.data[i];
        if (c == '\n') {
            print(b, S("\\n"));
        } else if (c == '\t') {
            print(b, S("\\t"));
        } else if (c == '\r') {
            print(b, S("\\r"));
        } else if (c=='\\' || c=='"') {
            printu8(b, '\\');
            printu8(b, c);
        } else if (c < ' ') {
            print(b, S("\\u00"));
            static u8 hex[] = "0123456789abcdef";
            printu8(b, hex[c>>4]);
            printu8(b, hex[c&15]);
        } else {
            printu8(b, c);
        }
    }
    printu8(b, '"');
}

typedef struct {
    i32  argc;
    Str *argv;
    Str  cwd;
} Config;

static void perfile(Output *out, Config conf, Str file, b32 first)
{
    printu8(out, first ? '[' : ',');
    print(out, S("\n    {"));

    print(out, S("\"file\": "));
    printq(out, file);

    print(out, S(", \"directory\": "));
    printq(out, conf.cwd);

    print(out, S(", \"arguments\": ["));
    for (i32 i = 0; i < conf.argc; i++) {
        if (i) {
            print(out, S(", "));
        }
        printq(out, conf.argv[i]);
    }
    print(out, S("]}"));
}

static b32 match(Str arg)
{
    return endswith(arg, S(".c"))
        || endswith(arg, S(".C"))
        || endswith(arg, S(".cc"))
        || endswith(arg, S(".cpp"))
        || endswith(arg, S(".s"))
        || endswith(arg, S(".S"));
}

static Str cmd_suffix(Str s)
{
    iz cut = s.len;
    for (; cut && s.data[cut-1]!='-'; cut--) {}
    return (Str){s.data+cut, s.len-cut};
}

static u8 usage[] =
"usage: compdb-cc [CMD ...]\n"
"example: COMPDB_PATH=\"$PWD\"/compile_commands.json make -B CC=compdb-cc\n"
"  When $COMPDB_PATH is set, updates the named file with build commands.\n";

static i32 compdb(Plt *plt, Arena *a, i32 argc, u8 **argv, Str cwd, b32 first)
{
    Output *out = newoutput(a, plt, 1);
    Output *err = newoutput(a, plt, 2);

    Config conf = {};
    conf.argc = argc;
    conf.argv = new(a, argc, Str);
    conf.cwd  = cwd;
    for (i32 i = 0; i < argc; i++) {
        conf.argv[i] = import(argv[i]);
        if (i == 0) {
            conf.argv[i] = cmd_suffix(conf.argv[i]);
        }
    }

    if (argc < 2) {
        print(err, S(usage));
        if (conf.argc) {
            print(err, S("  This program wraps the compiler named \""));
            print(err, conf.argv[0]);
            print(err, S("\".\n"));
        }
        flush(err);
        return STATUS_USAGE;
    }

    i32 count = 0;
    for (i32 i = 1; i < argc; i++) {
        Str arg = conf.argv[i];
        if (match(arg)) {
            count++;
            perfile(out, conf, arg, first);
            first = 0;
        }
    }

    if (count) {
        print(out, S("\n]\n"));
    }
    flush(out);
    return out->err ? STATUS_WRITE : STATUS_OK;
}


// Platform layer

#if _WIN32
typedef uint16_t    char16_t;
typedef char16_t    c16;

enum {
    CP_UTF8                 = 65001,
    OPEN_ALWAYS             = 4,
    FILE_ATTRIBUTE_NORMAL   = 0x80,
    FILE_SHARE_ALL          = 7,
    GENERIC_WRITE           = 0x40000000,
    FILE_END                = 2,
    LOCKFILE_EXCLUSIVE_LOCK = 2,
};

typedef struct {
    i32 cb;
    uz  a, b, c;
    i32 d, e, f, g, h, i, j, k;
    i16 l, m;
    uz  n, o, p, q;
} si;

typedef struct {
    uz  process;
    uz  thread;
    i32 pid;
    i32 tid;
} pi;

#define W32(r) __declspec(dllimport) r __stdcall
W32(b32)    CloseHandle(uz);
W32(c16 **) CommandLineToArgvW(c16 *, i32 *);
W32(uz)     CreateFileW(c16 *, i32, i32, uz, i32, i32, uz);
W32(b32)    CreateProcessW(c16*,c16*,void*,void*,b32,i32,c16*,c16*,si*,pi*);
W32(void)   ExitProcess(i32);
W32(c16 *)  GetCommandLineW();
W32(i32)    GetCurrentDirectoryW(i32, c16 *);
W32(i32)    GetEnvironmentVariableW(c16 *, c16 *, i32);
W32(i32)    GetExitCodeProcess(uz, i32 *);
W32(uz)     GetStdHandle(i32);
W32(b32)    LockFileEx(uz, i32, i32, i32, i32, uz[4]);
W32(b32)    SetFilePointerEx(uz, i64, i64 *, i32);
W32(i32)    WaitForSingleObject(uz, i32);
W32(i32)    WideCharToMultiByte(i32, i32, c16 *, i32, u8 *, i32, uz, uz);
W32(b32)    WriteFile(uz, u8 *, i32, i32 *, uz);

struct Plt {
    uz handle[3];
    uz err;
};

static b32 plt_write(Plt *plt, i32 fd, u8 *buf, i32 len)
{
    return WriteFile(plt->handle[fd], buf, len, &(i32){}, 0);
}

static void plt_oom()
{
    ExitProcess(STATUS_OOM);
    unreachable();
}

#define U(s)    (S16){s, lenof(s)-1}
typedef struct {
    c16 *data;
    iz   len;
} S16;

static S16 wimport(c16 *s)
{
    S16 r = {};
    r.data = s;
    for (; r.data[r.len]; r.len++) {}
    return r;
}

static S16 wclone(Arena *a, S16 s)
{
    S16 r = s;
    r.data = new(a, s.len, c16);
    for (iz i = 0; i < r.len; i++) {
        r.data[i] = s.data[i];
    }
    return r;
}

static S16 wconcat(Arena *a, S16 head, S16 tail)
{
    if (a->beg != (u8 *)(head.data+head.len)) {
        head = wclone(a, head);
    }
    head.len += wclone(a, tail).len;
    return head;
}

static S16 wslice(S16 s, iz beg, iz end)
{
    affirm(beg >= 0 && beg <= end && end <= s.len);
    s.data += beg;
    s.len = end - beg;
    return s;
}

typedef struct {
    S16 cmd;
    S16 args;
} Args;

static Args split(S16 s)
{
    Args r   = {};
    iz   beg = 0;
    iz   cut = 0;

    if (s.len && s.data[0] == '"') {
        cut = beg = 1;
        for (; cut < s.len; cut++) {
            if (s.data[cut] == '"') {
                break;
            }
        }
        r.cmd  = wslice(s, beg, cut);
        cut += cut < s.len;
        r.args = wslice(s, cut, s.len);

    } else {
        for (; cut < s.len; cut++) {
            if (s.data[cut]==' ' || s.data[cut]=='\t') {
                break;
            }
        }
        r.cmd  = wslice(s, beg, cut);
        r.args = wslice(s, cut, s.len);
    }
    return r;
}

static S16 wcmd_suffix(S16 s)
{
    iz cut = s.len;
    for (; cut && s.data[cut-1]!='-'; cut--) {}
    return wslice(s, cut, s.len);
}

static Str encode(Arena *a, c16 *s)
{
    Str r   = {};
    i32 len = WideCharToMultiByte(CP_UTF8, 0, s, -1, 0, 0, 0, 0);
    if (len) {
        r.data = new(a, len, u8);
        r.len  = len - 1;
        WideCharToMultiByte(CP_UTF8, 0, s, -1, r.data, len, 0, 0);
    }
    return r;
}

static Str workingdir(Arena *a)
{
    i32  len = GetCurrentDirectoryW(0, 0);
    c16 *buf = new(a, len, c16);
    GetCurrentDirectoryW(len, buf);
    return encode(a, buf);
}

static c16 *envvar(Arena *a, c16 *key)
{
    i32 len = GetEnvironmentVariableW(key, 0, 0);
    if (!len) {
        return 0;
    }

    c16 *buf = new(a, len, c16);
    GetEnvironmentVariableW(key, buf, len);
    return buf;
}

static i32 exec(uz err, c16 *cmd)
{
    if (!*cmd) {
        return STATUS_OK;
    }

    si si = {sizeof(si)};
    pi pi = {};
    if (!CreateProcessW(0, cmd, 0, 0, 1, 0, 0, 0, &si, &pi)) {
        Str msg = S("compdb-cc: CreateProcessW() failed to exec command\n");
        WriteFile(err, msg.data, (i32)msg.len, &(i32){}, 0);
        return STATUS_EXEC;
    }

    WaitForSingleObject(pi.process, -1);

    i32 r;
    GetExitCodeProcess(pi.process, &r);
    return r;
}

static i32 run(Arena *a)
{
    uz err = GetStdHandle(-12);

    c16  *cmd   = GetCommandLineW();
    i32   argc  = 0;
    c16 **wargv = CommandLineToArgvW(cmd, &argc);
    u8  **argv  = new(a, argc+1, u8 *);
    for (i32 i = 0; i < argc; i++) {
        argv[i] = encode(a, wargv[i]).data;
    }

    Plt *plt = new(a, 1, Plt);
    plt->handle[1] = (uz)-1;
    plt->handle[2] = err;

    if (argc < 2) {
        return compdb(plt, a, argc, argv, (Str){}, 0);
    }

    Args args = split(wimport(cmd));
    S16  cc   = wcmd_suffix(args.cmd);
    if (cc.len == args.cmd.len) {
        Str msg = S(
            "compdb-cc: could not determine actual compiler from name\n"
        );
        WriteFile(err, msg.data, (i32)msg.len, &(i32){}, 0);
        return STATUS_EXEC;
    }
    S16 realcmd = wconcat(a, cc, args.args);
    realcmd = wconcat(a, realcmd, U(u"\0"));

    c16 *path = envvar(a, u"COMPDB_PATH");
    if (!path || !*path) {
        return exec(err, realcmd.data);
    }

    Str cwd = workingdir(a);

    i32  access = GENERIC_WRITE;
    i32  share  = FILE_SHARE_ALL;
    i32  create = OPEN_ALWAYS;
    i32  attr   = FILE_ATTRIBUTE_NORMAL;
    plt->handle[1] = CreateFileW(path, access, share, 0, create, attr, 0);
    if (plt->handle[1] == (uz)-1) {
        Str msg = S("compdb-cc: CreateFileW() failed to open database\n");
        WriteFile(err, msg.data, (i32)msg.len, &(i32){}, 0);
        return STATUS_OPEN;
    }
    LockFileEx(plt->handle[1], LOCKFILE_EXCLUSIVE_LOCK, 0, -1, -1, (uz[4]){});

    b32 first  = !SetFilePointerEx(plt->handle[1], SEEK_SIZE, 0, FILE_END);
    i32 status = compdb(plt, a, argc, argv, cwd, first);
    if (status) {
        return status;
    }

    CloseHandle(plt->handle[1]);  // unlock
    return exec(err, realcmd.data);
}

i32 __stdcall mainCRTStartup()
{
    static u8 mem[1<<21];
    Arena a = {mem, mem+lenof(mem)};
    i32   r = run(&a);
    ExitProcess(r);
    unreachable();
}


#else  // POSIX
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <unistd.h>

struct Plt {
    int fds[3];
};

static b32 plt_write(Plt *plt, i32 fd, u8 *buf, i32 len)
{
    return write(plt->fds[fd], buf, touz(len)) == len;
}

static void plt_oom()
{
    exit(STATUS_OOM);
}

static int exec(int argc, char **argv)
{
    if (argc < 1) {
        return STATUS_OK;
    }
    execvp(argv[0], argv);
    fprintf(stderr, "compdb-cc: execvp(%s): %s\n", argv[1], strerror(errno));
    return STATUS_EXEC;
}

static Str workingdir(Arena *a)
{
    Str r = {};
    if (getcwd((char *)a->beg, touz(a->end-a->beg))) {
        r = import(a->beg);
        a->beg += r.len + 1;
    }
    return r;
}

int main(int argc, char **argv)
{
    static u8 mem[1<<21];
    Arena a = {mem, mem+lenof(mem)};

    Plt plt = {{0, 1, 2}};

    if (argc < 2) {
        return compdb(&plt, &a, argc, (u8 **)argv, (Str){}, 0);
    }

    Str argv0 = import((u8 *)argv[0]);
    Str cc    = cmd_suffix(argv0);
    if (cc.len == argv0.len) {
        fprintf(
            stderr,
            "compdb-cc: could not determine actual compiler from name, %s\n",
            argv[0]
        );
        return STATUS_EXEC;
    }

    char **new_argv = new(&a, argc+1, char *);
    new_argv[0] = (char *)cc.data;
    for (int i = 1; i < argc; i++) {
        new_argv[i] = argv[i];
    }

    char *path = getenv("COMPDB_PATH");
    if (!path || !*path) {
        return exec(argc, new_argv);
    }

    Str cwd = workingdir(&a);

    plt.fds[1] = open(path, O_CREAT|O_RDWR, 0666);
    if (plt.fds[1] == -1) {
        fprintf(stderr, "compdb-cc: open(%s): %s\n", path, strerror(errno));
        return STATUS_OPEN;
    }
    flock(plt.fds[1], LOCK_EX);

    b32 first  = lseek(plt.fds[1], SEEK_SIZE, SEEK_END) == -1;
    i32 status = compdb(&plt, &a, argc, (u8 **)argv, cwd, first);
    if (status) {
        return status;
    }

    close(plt.fds[1]);  // unlock
    return exec(argc, new_argv);
}
#endif
