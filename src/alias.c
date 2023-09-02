// Well-behaved command line aliases for w64devkit
//
// Unlike batch script aliases, this program will not produce an annoying
// and useless "Terminate batch job (Y/N)" prompt. When compiling, define
// EXE as the target executable (relative or absolute path), and define CMD
// as the argv[0] replacement, including additional arguments.
//
//   $ gcc -DEXE="target.exe" -DCMD="argv0 argv1"
//         -nostartfiles -fno-builtin -o alias.exe alias.c
//
// This is free and unencumbered software released into the public domain.

#define sizeof(x)   (i32)sizeof(x)
#define alignof(x)  (i32)_Alignof(x)
#define countof(a)  (sizeof(a) / sizeof(*(a)))
#define lengthof(s) (countof(s) - 1)

#define LSTR(s) XSTR(s)
#define XSTR(s) u ## # s
#define LEXE LSTR(EXE)
#define LCMD LSTR(CMD)

typedef __UINT8_TYPE__   u8;
typedef   signed short   i16;
typedef   signed int     b32;
typedef   signed int     i32;
typedef unsigned int     u32;
typedef unsigned char    byte;
typedef __UINTPTR_TYPE__ uptr;
typedef __SIZE_TYPE__    usize;
typedef unsigned short   char16_t;  // for GDB
typedef char16_t         c16;

// Win32

#define MAX_PATH    260
#define MAX_CMDLINE (1<<15)

typedef struct {} *handle;

typedef struct {
    u32 cb;
    uptr a, b, c;
    i32 d, e, f, g, h, i, j, k;
    i16 l, m;
    uptr n, o, p, q;
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
W32 u32    GetFullPathNameW(c16 *, u32, c16 *, c16 *);
W32 u32    GetModuleFileNameW(handle, c16 *, u32);
W32 handle GetStdHandle(u32);
W32 byte  *VirtualAlloc(byte *, usize, u32, u32);
W32 b32    VirtualFree(byte *, usize, u32);
W32 u32    WaitForSingleObject(handle, u32);
W32 b32    WriteFile(handle, u8 *, u32, u32 *, void *);

// Application

#define ERR(s) "w64devkit (alias): " s "\n"

#define new(h, t, n) (t *)alloc(h, sizeof(t), alignof(t), n)
__attribute((malloc))
__attribute((alloc_align(3)))
__attribute((alloc_size(2, 4)))
static byte *alloc(byte **heap, i32 size, i32 align, i32 count)
{
    *heap += -(uptr)*heap & (align - 1);
    byte *p = *heap;
    *heap += size * count;
    return p;
}

typedef struct {
    c16 *buf;
    i32  len;
    i32  cap;
    b32  err;
} c16buf;

static void append(c16buf *b, c16 *s, i32 len)
{
    i32 avail = b->cap - b->len;
    i32 count = avail<len ? avail : len;
    for (i32 i = 0; i < count; i++) {
        b->buf[b->len+i] = s[i];
    }
    b->len += count;
    b->err |= avail < len;
}

// Find the end of argv[0].
static c16 *findargs(c16 *s)
{
    if (s[0] == '"') {
        for (s++;; s++) {  // quoted argv[0]
            switch (*s) {
            case   0: return s;
            case '"': return s + 1;
            }
        }
    } else {
        for (;; s++) {  // unquoted argv[0]
            switch (*s) {
            case    0:
            case '\t':
            case  ' ': return s;
            }
        }
    }
}

static i32 c16len(c16 *s)
{
    i32 len = 0;
    for (; s[len]; len++) {}
    return len;
}

static u32 fatal(u8 *msg, i32 len)
{
    handle stderr = GetStdHandle(-12);
    u32 dummy;
    WriteFile(stderr, msg, len, &dummy, 0);
    return 0x17e;
}

static void getmoduledir(c16buf *b)
{
    c16 path[MAX_PATH];
    u32 len = GetModuleFileNameW(0, path, countof(path));
    for (; len; len--) {
        switch (path[len-1]) {
        case '/': case '\\':
            append(b, path, len);
            return;
        }
    }
}

static si *newstartupinfo(byte **heap)
{
    si *s = new(heap, si, 1);
    s->cb = sizeof(si);
    return s;
}

static i32 aliasmain(void)
{
    byte *heap_start = VirtualAlloc(0, 1<<18, 0x3000, 4);
    byte *heap = heap_start;
    if (!heap) {
        static const u8 msg[] = ERR("out of memory");
        return fatal((u8 *)msg, lengthof(msg));
    }

    // Construct a path to the .exe
    c16buf *exe = new(&heap, c16buf, 1);
    exe->cap = 2*MAX_PATH;  // concatenating two paths
    exe->buf = new(&heap, c16, exe->cap);
    if (LEXE[1] != ':') {  // relative path?
        getmoduledir(exe);
    }
    append(exe, LEXE, countof(LEXE));
    if (exe->err) {
        static const u8 msg[] = ERR(".exe path too long");
        return fatal((u8 *)msg, lengthof(msg));
    }

    // Try to collapse relative components
    if (LEXE[0] == '.') {  // relative components?
        c16buf *tmp = new(&heap, c16buf, 1);
        tmp->buf = new(&heap, c16, MAX_PATH);
        tmp->cap = MAX_PATH;
        tmp->len = GetFullPathNameW(exe->buf, tmp->cap, tmp->buf, 0);
        if (tmp->len>0 && tmp->len<tmp->cap) {
            tmp->len++;  // include null terminator
            *exe = *tmp;
        }
    }

    // Construct a new command line string
    c16buf *cmd = new(&heap, c16buf, 1);
    cmd->cap = MAX_CMDLINE;
    cmd->buf = new(&heap, c16, cmd->cap);
    append(cmd, LCMD, lengthof(LCMD));
    c16 *args = findargs(GetCommandLineW());
    append(cmd, args, c16len(args)+1);
    if (cmd->err) {
        static const u8 msg[] = ERR("command line too long");
        return fatal((u8 *)msg, lengthof(msg));
    }

    si *si = newstartupinfo(&heap);
    pi pi;
    if (!CreateProcessW(exe->buf, cmd->buf, 0, 0, 1, 0, 0, 0, si, &pi)) {
        static const u8 msg[] = ERR("could not start process\n");
        return fatal((u8 *)msg, lengthof(msg));
    }

    // Wait for child to exit
    VirtualFree(heap_start, 0, 0x8000);
    u32 ret;
    WaitForSingleObject(pi.process, -1);
    GetExitCodeProcess(pi.process, &ret);
    return ret;
}

__attribute((force_align_arg_pointer))
void mainCRTStartup(void)
{
    u32 r = aliasmain();
    ExitProcess(r);
}
