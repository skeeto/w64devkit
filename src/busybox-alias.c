// busybox-w32 command alias
// Starts the adjacent busybox.exe with an unmodified command line.
// $ cc -nostartfiles -fno-builtin -o COMMAND busybox-alias.c
// This is free and unencumbered software released into the public domain.

#define sizeof(x)   (i32)sizeof(x)
#define countof(a)  (sizeof(a) / sizeof(*(a)))
#define lengthof(s) (countof(s) - 1)

typedef __UINT8_TYPE__ u8;
typedef   signed short i16;
typedef   signed int   b32;
typedef   signed int   i32;
typedef unsigned int   u32;
typedef unsigned short char16_t;  // for GDB
typedef char16_t       c16;

#define MAX_PATH 260

typedef struct {} *handle;

typedef b32 __stdcall handler(i32);

typedef struct {
    u32 cb;
    void *a, *b, *c;
    i32 d, e, f, g, h, i, j, k;
    i16 l, m;
    void *n, *o, *p, *q;
} si;

typedef struct {
    handle process;
    handle thread;
    u32 pid;
    u32 tid;
} pi;

#define W32 __attribute((dllimport, stdcall))
W32 b32    CreateProcessW(c16*,c16*,void*,void*,b32,u32,c16*,c16*,si*,pi*);
W32 void   ExitProcess(u32) __attribute((noreturn));
W32 c16   *GetCommandLineW(void);
W32 i32    GetExitCodeProcess(handle, u32 *);
W32 u32    GetModuleFileNameW(handle, c16 *, u32);
W32 handle GetStdHandle(u32);
W32 b32    SetConsoleCtrlHandler(handler, b32);
W32 u32    WaitForSingleObject(handle, u32);
W32 b32    WriteFile(handle, u8 *, u32, u32 *, void *);

typedef struct {
    c16 *buf;
    i32  len;
    i32  cap;
    b32  err;
} c16buf;

static void append(c16buf *b, c16 *buf, i32 len)
{
    i32 avail = b->cap - b->len;
    i32 count = avail<len ? avail : len;
    c16 *dst = b->buf + b->len;
    for (i32 i = 0; i < count; i++) {
        dst[i] = buf[i];
    }
    b->len += count;
    b->err |= count < len;
}

static void getmoduledir(c16buf *b)
{
    c16 path[MAX_PATH];
    u32 len = GetModuleFileNameW(0, path, countof(path));
    for (; len; len--) {
        switch (path[len-1]) {
        case '/': case '\\':
            append(b, path, len-1);
            return;
        }
    }
}

static u32 fatal(u8 *msg, i32 len)
{
    handle stderr = GetStdHandle(-12);
    u32 dummy;
    WriteFile(stderr, msg, len, &dummy, 0);
    return 0x17e;
}

static b32 __stdcall ignorectrlc(i32)
{
    return 1;
}

static u32 run(void)
{
    c16 buf[MAX_PATH];
    c16buf exe = {};
    exe.buf = buf;
    exe.cap = countof(buf);

    getmoduledir(&exe);
    c16 busybox[] = u"\\busybox.exe";
    append(&exe, busybox, countof(busybox));
    if (exe.err) {
        static u8 msg[] = "w64devkit: busybox.exe path too long\n";
        return fatal(msg, lengthof(msg));
    }

    si si = {};
    si.cb = sizeof(si);
    pi pi;
    c16 *cmdline = GetCommandLineW();
    SetConsoleCtrlHandler(ignorectrlc, 1);  // NOTE: set as late a possible
    if (!CreateProcessW(exe.buf, cmdline, 0, 0, 1, 0, 0, 0, &si, &pi)) {
        static u8 msg[] = "w64devkit: could not start busybox.exe\n";
        return fatal(msg, lengthof(msg));
    }

    u32 ret;
    WaitForSingleObject(pi.process, -1);
    GetExitCodeProcess(pi.process, &ret);
    return ret;
}

__attribute((force_align_arg_pointer))
void mainCRTStartup(void)
{
    u32 r = run();
    ExitProcess(r);
}
