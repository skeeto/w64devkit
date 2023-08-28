// Tiny, standalone launcher for w64devkit
// * Sets $W64DEVKIT to the release version (-DVERSION)
// * Sets $W64DEVKIT_HOME to the install location
// * Maybe sets $HOME according to w64devkit.ini
// * Starts a login shell with "sh -l"
//
// $ gcc -DVERSION="$VERSION" -nostartfiles -fno-builtin
//       -o w64devkit.exe w64devkit.c
//
// This is free and unencumbered software released into the public domain.

#define sizeof(a)    (size)(sizeof(a))
#define alignof(a)   (size)(_Alignof(a))
#define countof(a)   (sizeof(a) / sizeof(*(a)))
#define lengthof(s)  (countof(s) - 1)

#define new(...)            newx(__VA_ARGS__, new4, new3, new2)(__VA_ARGS__)
#define newx(a,b,c,d,e,...) e
#define new2(a, t)          (t *)alloc(a, sizeof(t), alignof(t), 1, 0)
#define new3(a, t, n)       (t *)alloc(a, sizeof(t), alignof(t), n, 0)
#define new4(a, t, n, f)    (t *)alloc(a, sizeof(t), alignof(t), n, f)

typedef unsigned char    byte;
typedef __UINT8_TYPE__   u8;
typedef unsigned short   u16;
typedef   signed int     i32;
typedef   signed int     b32;
typedef unsigned int     u32;
typedef __UINTPTR_TYPE__ uptr;
typedef __PTRDIFF_TYPE__ size;
typedef __SIZE_TYPE__    usize;
typedef unsigned short   char16_t;  // for GDB
typedef char16_t         c16;

typedef struct {} *handle;

typedef struct {
    u32 cb;
    uptr a, b, c;
    i32 d, e, f, g, h, i, j, k;
    u16 l, m;
    uptr n, o, p, q;
} si;

typedef struct {
    handle process;
    handle thread;
    u32 pid;
    u32 tid;
} pi;

#define MAX_PATH       260
#define MAX_ENVVAR     32767
#define MAX_CMDLINE    32767
#define MAX_INI        (1<<18)
#define CP_UTF8        65001
#define PAGE_READWRITE 0x04
#define MEM_COMMIT     0x1000
#define MEM_RESERVE    0x2000
#define MEM_RELEASE    0x8000
#define GENERIC_READ   0x80000000
#define OPEN_EXISTING  3
#define FILE_SHARE_ALL 7

#define W32 __attribute((dllimport,stdcall))
W32 b32    CloseHandle(handle);
W32 handle CreateFileW(c16 *, u32, u32, void *, u32, u32, handle);
W32 b32    CreateProcessW(c16*,c16*,void*,void*,i32,u32,c16*,c16*,si*,pi*);
W32 void   ExitProcess(u32) __attribute((noreturn));
W32 u32    ExpandEnvironmentStringsW(c16 *, c16 *, u32);
W32 u32    GetEnvironmentVariableW(c16 *, c16 *, u32);
W32 i32    GetExitCodeProcess(handle, u32 *);
W32 u32    GetFullPathNameW(c16 *, u32, c16 *, c16 *);
W32 u32    GetModuleFileNameW(handle, c16 *, u32);
W32 i32    MessageBoxW(handle, c16 *, c16 *, u32);
W32 i32    MultiByteToWideChar(u32, u32, u8 *, i32, c16 *, i32);
W32 b32    ReadFile(handle, u8 *, u32, u32 *, void *);
W32 b32    SetConsoleTitleW(c16 *);
W32 b32    SetCurrentDirectoryW(c16 *);
W32 b32    SetEnvironmentVariableW(c16 *, c16 *);
W32 byte  *VirtualAlloc(byte *, usize, u32, u32);
W32 b32    VirtualFree(byte *, usize, u32);
W32 u32    WaitForSingleObject(handle, u32);

#define S(s) (s8){(u8 *)s, lengthof(s)}
typedef struct {
    u8  *s;
    size len;
} s8;

#define U(s) (s16){s, lengthof(s)}
typedef struct {
    c16 *s;
    size len;
} s16;

static s8 s8span(u8 *beg, u8 *end)
{
    s8 s = {};
    s.s = beg;
    s.len = end - beg;
    return s;
}

static b32 s8equals(s8 a, s8 b)
{
    if (a.len != b.len) {
        return 0;
    }
    for (size i = 0; i < a.len; i++) {
        if (a.s[i] != b.s[i]) {
            return 0;
        }
    }
    return 1;
}

static void fatal(c16 *msg)
{
    MessageBoxW(0, msg, u"w64devkit launcher", 0x10);
    ExitProcess(2);
}

typedef struct {
    byte *mem;
    size  cap;
    size  off;
} arena;

static arena *newarena(size cap)
{
    arena *a = 0;
    byte *mem = VirtualAlloc(0, cap, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (mem) {
        a = (arena *)mem;
        a->mem = mem;
        a->cap = cap;
        a->off = sizeof(arena);
    }
    return a;
}

static void freearena(arena *a)
{
    VirtualFree(a->mem, 0, MEM_RELEASE);
}

#define NOZERO   (1<<0)
#define SOFTFAIL (1<<1)
__attribute((malloc))
__attribute((alloc_align(3)))
__attribute((alloc_size(2, 4)))
static byte *alloc(arena *a, size objsize, size align, size count, i32 flags)
{
    size avail = a->cap - a->off;
    size pad   = -a->off & (align - 1);
    if (count > (avail - pad)/objsize) {
        if (flags & SOFTFAIL) {
            return 0;
        }
        fatal(u"Out of memory");
    }
    size total = count*objsize;
    byte *p = a->mem + a->off + pad;
    if (!(flags & NOZERO)) {
        for (size i = 0; i < total; i++) {
            p[i] = 0;
        }
    }
    a->off += pad + total;
    return p;
}

static arena splitarena(arena *a, i32 div)
{
    size avail = a->cap - a->off;
    size cap = avail / div;
    arena sub = {};
    sub.mem = alloc(a, 1, 32, cap, NOZERO);
    sub.cap = cap;
    return sub;
}

typedef enum {
    INI_eof,
    INI_section,
    INI_key,
    INI_value
} initype;

typedef struct {
    s8      name;
    initype type;
} initoken;

typedef struct {
    u8 *beg;
    u8 *end;
    b32 invalue;
} iniparser;

static b32 inidone(iniparser *p)
{
    return p->beg == p->end;
}

static u8 ininext(iniparser *p)
{
    return *p->beg++;
}

static b32 iniblank(u8 c)
{
    return c==' ' || c=='\t' || c=='\r';
}

static void iniskip(iniparser *p)
{
    for (; !inidone(p) && iniblank(*p->beg); p->beg++) {}
}

static u8 *initok(iniparser *p, u8 term)
{
    u8 *end = p->beg;
    while (!inidone(p)) {
        u8 c = ininext(p);
        if (c == term) {
            return end;
        } else if (c == '\n') {
            break;
        } else if (!iniblank(c)) {
            end = p->beg;
        }
    }
    return term=='\n' ? end : 0;
}

static b32 iniquoted(s8 s)
{
    return s.len>1 && s.s[0]=='"' && s.s[s.len-1]=='"';
}

// Parses like GetPrivateProfileString except sections cannot contain
// newlines. Invalid input lines are ignored. Comment lines begin with
// ';' following any whitespace. No trailing comments. Trims leading and
// trailing whitespace from sections, keys, and values. To preserve
// whitespace, values may be double-quoted. No quote escapes. Content on
// a line following a closing section ']' is ignored. Token encoding
// matches input encoding. An INI_value always follows an INI_key key.
static initoken iniparse(iniparser *p)
{
    initoken r = {};

    if (p->invalue) {
        p->invalue = 0;
        iniskip(p);
        u8 *beg = p->beg;
        u8 *end = initok(p, '\n');
        r.name = s8span(beg, end);
        r.type = INI_value;
        if (iniquoted(r.name)) {
            r.name.s++;
            r.name.len -= 2;
        }
        return r;
    }

    for (;;) {
        iniskip(p);
        if (inidone(p)) {
            return r;
        }

        u8 *end;
        u8 *beg = p->beg;
        switch (ininext(p)) {
        case ';':
            while (!inidone(p) && ininext(p)!='\n') {}
            break;

        case '[':
            iniskip(p);
            beg = p->beg;
            end = initok(p, ']');
            if (end) {
                // skip over anything else on the line
                while (!inidone(p) && ininext(p)!='\n') {}
                r.name = s8span(beg, end);
                r.type = INI_section;
                return r;
            }
            break;

        case '\n':
            break;

        default:
            end = initok(p, '=');
            if (end) {
                p->invalue = 1;
                r.name = s8span(beg, end);
                r.type = INI_key;
                return r;
            }
        }
    }
}

typedef enum {
    sym_null = 0,
    sym_w64devkit,
    sym_home,
} symbol;

static symbol intern(s8 s)
{
    static struct {
        s8     name;
        symbol symbol;
    } symbols[] = {
        {S("w64devkit"), sym_w64devkit},
        {S("home"),      sym_home},
    };
    for (size i = 0; i < countof(symbols); i++) {
        if (s8equals(symbols[i].name, s)) {
            return symbols[i].symbol;
        }
    }
    return sym_null;
}

static u8 *makecstr(arena *a, s8 s)
{
    u8 *r = new(a, u8, s.len+1);
    for (size i = 0; i < s.len; i++) {
        r[i] = s.s[i];
    }
    return r;
}

// Read and process "w64devkit.home" from "w64devkit.ini". Environment
// variables are expanded, and if relative, the result is converted into
// an absolute path. Returns null on error.
//
// Before calling, the current working directory must be changed to the
// location of w64devkit.exe.
static c16 *homeconfig(arena *perm, arena scratch)
{
    handle h = CreateFileW(
        u"w64devkit.ini",
        GENERIC_READ,
        FILE_SHARE_ALL,
        0,
        OPEN_EXISTING,
        0,
        0
    );
    if (h == (handle)-1) {
        return 0;
    }

    iniparser *p = new(&scratch, iniparser);
    p->beg = new(&scratch, u8, MAX_INI, NOZERO);
    u32 inilen;
    b32 r = ReadFile(h, p->beg, MAX_INI, &inilen, 0);
    CloseHandle(h);
    if (!r || inilen == MAX_INI) {
        return 0;
    }
    p->end = p->beg + inilen;

    u8 *home = 0;
    u32 len = 0;
    for (symbol section = 0, key = 0;;) {
        initoken t = iniparse(p);
        switch (t.type) {
        case INI_eof:
            break;
        case INI_section:
            section = intern(t.name);
            continue;
        case INI_key:
            key = intern(t.name);
            continue;
        case INI_value:
            if (!home && section==sym_w64devkit && key==sym_home) {
                home = makecstr(&scratch, t.name);
                len = (u32)(t.name.len + 1);  // include terminator
            }
            continue;
        }
        break;
    }

    c16 *whome = new(&scratch, c16, len);
    if (!MultiByteToWideChar(CP_UTF8, 0, home, len, whome, len)) {
        return 0;
    }

    // Process INI string into a final HOME path. Allocate a bit more
    // than MAX_PATH, because GetFullPathNameW could technically reduce
    // it to within MAX_PATH if there are lots of relative components.
    u32 cap = MAX_PATH*4;
    c16 *expanded = new(&scratch, c16, cap);
    len = ExpandEnvironmentStringsW(whome, expanded, cap);
    if (!len || len>cap) {
        return 0;
    }

    // The final result must fit within MAX_PATH in order to be useful.
    c16 *path = new(perm, c16, MAX_PATH);
    len = GetFullPathNameW(expanded, MAX_PATH, path, 0);
    if (!len || len>=MAX_PATH) {
        return 0;
    }
    return path;
}

typedef struct {
    c16 *buf;
    size cap;
    size len;
    b32  err;
} buf16;

static buf16 newbuf16(arena *a, size cap)
{
    buf16 buf = {};
    buf.buf = new(a, c16, cap, NOZERO);
    buf.cap = cap;
    return buf;
}

static void buf16cat(buf16 *buf, s16 s)
{
    size avail = buf->cap - buf->len;
    size count = s.len<avail ? s.len : avail;
    c16 *dst = buf->buf + buf->len;
    for (size i = 0; i < count; i++) {
        dst[i] = s.s[i];
    }
    buf->len += count;
    buf->err |= count < s.len;
}

static void buf16c16(buf16 *buf, c16 c)
{
    s16 s = {&c, 1};
    buf16cat(buf, s);
}

static void buf16moduledir(buf16 *buf, arena scratch)
{
    c16 *path = new(&scratch, c16, MAX_PATH);
    size len = GetModuleFileNameW(0, path, MAX_PATH);
    for (; len; len--) {
        switch (path[len-1]) {
        case  '/':
        case '\\': buf16cat(buf, (s16){path, len-1});
                   return;
        }
    }
}

static void buf16getenv(buf16 *buf, c16 *key, arena scratch)
{
    s16 var = {};
    var.s = new(&scratch, c16, MAX_ENVVAR, NOZERO);
    u32 len = GetEnvironmentVariableW(key, var.s, MAX_ENVVAR);
    var.len = len>=MAX_ENVVAR ? 0 : len;
    buf16cat(buf, var);
}

static void toslashes(c16 *path)
{
    for (size i = 0; i < path[i]; i++) {
        path[i] = path[i]=='\\' ? '/' : path[i];
    }
}

static u32 w64devkit(void)
{
    arena *perm = newarena(1<<22);
    if (!perm) {
        fatal(u"Out of memory on startup");
    }
    arena scratch = splitarena(perm, 2);

    // First load the module directory into the fresh buffer, and use it
    // for a few different operations.
    buf16 path = newbuf16(perm, MAX_ENVVAR);
    buf16moduledir(&path, scratch);
    buf16 moduledir = path;  // to truncate back to the module dir

    buf16c16(&path, 0);  // null terminator
    SetEnvironmentVariableW(u"W64DEVKIT_HOME", path.buf);  // ignore errors

    // Maybe set HOME from w64devkit.ini
    if (SetCurrentDirectoryW(path.buf)) {
        c16 *home = homeconfig(perm, scratch);
        if (home) {
            toslashes(home);
            SetEnvironmentVariableW(u"HOME", home);  // ignore errors
        }
    }

    // Continue building PATH
    path = moduledir;
    buf16cat(&path, U(u"\\bin;"));
    buf16getenv(&path, u"PATH", scratch);
    buf16c16(&path, 0);  // null terminator
    if (path.err || !SetEnvironmentVariableW(u"PATH", path.buf)) {
        fatal(u"Failed to configure $PATH");
    }

    #ifdef VERSION
    #define LSTR(s) XSTR(s)
    #define XSTR(s) u ## # s
    SetEnvironmentVariableW(u"W64DEVKIT", LSTR(VERSION));  // ignore errors
    #endif

    // Set the console title as late as possible, but not after starting
    // the shell because .profile might change it.
    SetConsoleTitleW(u"w64devkit");  // ignore errors

    path = moduledir;
    buf16cat(&path, U(u"\\bin\\busybox.exe"));
    buf16c16(&path, 0);  // null terminator

    // Start a BusyBox login shell
    si si = {};
    si.cb = sizeof(si);
    pi pi;
    c16 cmdline[] = u"sh -l";  // NOTE: must be mutable!
    if (!CreateProcessW(path.buf, cmdline, 0, 0, 1, 0, 0, 0, &si, &pi)) {
        fatal(u"Failed to launch a login shell");
    }

    // Wait for shell to exit
    freearena(perm);
    u32 ret;
    WaitForSingleObject(pi.process, -1);
    GetExitCodeProcess(pi.process, &ret);
    return ret;
}

__attribute((force_align_arg_pointer))
void mainCRTStartup(void)
{
    u32 r = w64devkit();
    ExitProcess(r);
}
