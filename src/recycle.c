// recycle: send files and folders to the recycle bin without confirmation
// $ cc -nostartfiles -o recycle.exe recycle.c
// $ recycle [PATHS...]
// Tip: Enable storage sense to automatically clear old recycle bin items
// This is free and unencumbered software released into the public domain.
#include <stddef.h>
#include <stdint.h>
#include <uchar.h>

#define lenof(a)        (ptrdiff_t)(sizeof(a) / sizeof(*(a)))
#define affirm(c)       while (!(c)) __builtin_trap()
#define new(a, n, t)    (t *)alloc(a, n, sizeof(t), _Alignof(t))

typedef struct {
  uintptr_t wnd;
  int32_t   func;
  char16_t *from;
  char16_t *to;
  int16_t   flags;
  int32_t   abort_flags;
  uintptr_t mappings;
  uintptr_t title;
} FileOp;

enum : int32_t {
    FO_DELETE = 0x0003,
};

enum : int16_t {
    FOF_ALLOWUNDO       = 0x0040,
    FOF_NOCONFIRMATION  = 0x0010,
    FOF_NOERRORUI       = 0x0400,
    FOF_SILENT          = 0x0004,
};

enum : int32_t {
    FORMAT_MESSAGE_ALLOCATE_BUFFER  =   0x00000100,
    FORMAT_MESSAGE_FROM_SYSTEM      =   0x00001000,
    FORMAT_MESSAGE_IGNORE_INSERTS   =   0x00000200,
};

#define W32 [[gnu::dllimport, gnu::stdcall]]
W32 char16_t  **CommandLineToArgvW(char16_t *, int32_t *);
W32 void        ExitProcess(int32_t);
W32 int32_t     FormatMessageW(int32_t,uintptr_t,int32_t,int32_t,char16_t**,int32_t,uintptr_t);
W32 char16_t   *GetCommandLineW();
W32 int32_t     GetFullPathNameW(char16_t *, int32_t, char16_t *, char16_t **);
W32 uintptr_t   GetStdHandle(int32_t);
W32 int32_t     SHFileOperationW(FileOp *);
W32 int32_t     WriteConsoleW(uintptr_t,char16_t*,int32_t,int32_t *,uintptr_t);

typedef struct {
    char *beg;
    char *end;
} Arena;

static size_t tousize(ptrdiff_t x)
{
    affirm(x >= 0);
    return (size_t)x;
}

static char *alloc(Arena *a, ptrdiff_t count, ptrdiff_t size, ptrdiff_t align)
{
    ptrdiff_t pad = (ptrdiff_t)-(uintptr_t)a->beg & (align - 1);
    affirm(count < (a->end - a->beg - pad)/size);
    char *r = a->beg + pad;
    a->beg += pad + count*size;
    return __builtin_memset(r, 0, tousize(count*size));
}

static int32_t len16(char16_t *s)
{
    int32_t len = 0;
    for (; s[len]; len++) {}
    return len;
}

static char16_t *geterror(int32_t err)
{
    int32_t flags =
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_IGNORE_INSERTS;
    char16_t *r = {};
    FormatMessageW(flags, 0, err, 0, &r, 0, 0);
    return r;
}

static int32_t isshellerr(int32_t code)
{
    code &= ~0x10000;  // strip ERRORONDEST
    return (code >= 0x71 && code <= 0x88) ||
            code == 0xB7                  ||
            code == 0x402;
}

static void report(char16_t *path, int32_t code)
{
    uintptr_t h = GetStdHandle(-12);
    static char16_t pre[] = u"recycle: ";
    WriteConsoleW(h, pre, lenof(pre)-1, &(int32_t){}, 0);
    WriteConsoleW(h, path, len16(path), &(int32_t){}, 0);
    static char16_t mid[] = u": ";
    WriteConsoleW(h, mid, lenof(mid)-1, &(int32_t){}, 0);
    if (isshellerr(code)) {
        static char16_t msg[] = u"Could not recycle\n";
        WriteConsoleW(h, msg, lenof(msg)-1, &(int32_t){}, 0);
    } else {
        char16_t *err = geterror(code);
        WriteConsoleW(h, err, len16(err), &(int32_t){}, 0);
    }
}

static int32_t recycle(int32_t argc, char16_t **argv, Arena scratch)
{
    int32_t errors = 0;
    for (int32_t i = 1; i < argc; i++) {
        Arena a = scratch;

        ptrdiff_t len  = GetFullPathNameW(argv[i], 0, 0, 0);
        char16_t *full = new(&a, len+2, char16_t);  // double terminator
        GetFullPathNameW(argv[i], len, full, 0);

        FileOp op = {};
        op.func  = FO_DELETE;
        op.from  = full;
        op.flags =
            FOF_ALLOWUNDO |
            FOF_NOCONFIRMATION |
            FOF_NOERRORUI |
            FOF_SILENT;

        int32_t r = SHFileOperationW(&op);
        if (r) {
            report(argv[i], r);
            errors++;
        }
    }
    return errors > 0;
}

[[gnu::stdcall]]
void mainCRTStartup()
{
    static char mem[1<<18];
    char16_t   *cmd  = GetCommandLineW();
    int32_t     argc = {};
    char16_t  **argv = CommandLineToArgvW(cmd, &argc);
    int32_t     stat = recycle(argc, argv, (Arena){mem, mem+lenof(mem)});
    ExitProcess(stat);
    __builtin_unreachable();
}
