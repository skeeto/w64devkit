// Tiny, standalone launcher for w64devkit
// * Sets $W64DEVKIT to the release version (-DVERSION)
// * Sets $W64DEVKIT_HOME to the install location
// * Maybe sets $HOME according to w64devkit.ini
// * Maybe sets $PATH according to w64devkit.ini
// * Starts a login shell with "sh -l"
//
// $ gcc -DVERSION="$VERSION" -nostartfiles -o w64devkit.exe
//       w64devkit.c -lmemory
//
// This is free and unencumbered software released into the public domain.

#define countof(a)    (iz)(sizeof(a) / sizeof(*(a)))
#define assert(c)     while (!(c)) __builtin_unreachable()
#define new(a, t, n)  (t *)alloc(a, sizeof(t), _Alignof(t), n)

typedef unsigned char    u8;
typedef unsigned short   u16;
typedef   signed int     i32;
typedef   signed int     b32;
typedef unsigned int     u32;
typedef __PTRDIFF_TYPE__ iz;
typedef __SIZE_TYPE__    uz;
typedef unsigned short   char16_t;  // for GDB
typedef char16_t         c16;

typedef struct {
    u32 cb;
    uz  a, b, c;
    i32 d, e, f, g, h, i, j, k;
    u16 l, m;
    uz  n, o, p, q;
} Si;

typedef struct {
    uz  process;
    uz  thread;
    u32 pid;
    u32 tid;
} Pi;

enum : i32 {
    MAX_ENVVAR     = 32767,
    CP_UTF8        = 65001,
    PAGE_READWRITE = 0x04,
    MEM_COMMIT     = 0x1000,
    MEM_RESERVE    = 0x2000,
    MEM_RELEASE    = 0x8000,
    GENERIC_READ   = (i32)0x80000000,
    OPEN_EXISTING  = 3,
    FILE_SHARE_ALL = 7,
};

#define W32 [[gnu::dllimport, gnu::stdcall]]
W32 b32    CloseHandle(uz);
W32 uz     CreateFileW(c16 *, i32, i32, void *, i32, i32, uz);
W32 b32    CreateProcessW(c16*,c16*,void*,void*,i32,i32,c16*,c16*,Si*,Pi*);
W32 void   ExitProcess[[noreturn]](i32);
W32 u32    ExpandEnvironmentStringsW(c16 *, c16 *, u32);
W32 u32    GetEnvironmentVariableW(c16 *, c16 *, u32);
W32 i32    GetExitCodeProcess(uz, i32 *);
W32 u32    GetFullPathNameW(c16 *, u32, c16 *, c16 *);
W32 u32    GetModuleFileNameW(uz, c16 *, u32);
W32 i32    GetVersion();
W32 u32    GetWindowsDirectoryW(c16 *, u32);
W32 i32    MessageBoxW(uz, c16 *, c16 *, i32);
W32 i32    MultiByteToWideChar(i32, i32, u8 *, i32, c16 *, i32);
W32 b32    ReadFile(uz, u8 *, i32, i32 *, void *);
W32 b32    SetConsoleTitleW(c16 *);
W32 b32    SetCurrentDirectoryW(c16 *);
W32 b32    SetEnvironmentVariableW(c16 *, c16 *);
W32 u8    *VirtualAlloc(u8 *, iz, i32, i32);
W32 b32    VirtualFree(u8 *, uz, i32);
W32 i32    WaitForSingleObject(uz, i32);

#define S(s) (s8){(u8 *)s, countof(s)-1}
typedef struct {
    u8 *s;
    iz  len;
} s8;

#define U(s) (s16){s, countof(s)-1}
typedef struct {
    c16 *s;
    iz   len;
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
    for (iz i = 0; i < a.len; i++) {
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
    u8 *beg;
    u8 *end;
} Arena;

static void outofmemory()
{
    fatal(u"Out of Memory");
}

static u8 *alloc(Arena *a, iz size, iz align, iz count)
{
    iz padding = (iz)a->end & (align - 1);
    if (count > (a->end - a->beg - padding)/size) {
        outofmemory();
    }
    u8 *r = a->end -= count*size + padding;
    return __builtin_memset(r, 0, (uz)(count*size));
}

typedef struct {
    u8   *base;
    Arena perm;
    Arena scratch;
} Memory;

static Memory newmemory(iz cap)
{
    Memory r = {};
    r.base = VirtualAlloc(0, cap, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (!r.base) return r;
    r.perm.beg = r.base;
    r.perm.end = r.base + cap/2;
    r.scratch.beg = r.base + cap/2;
    r.scratch.end = r.base + cap;
    return r;
}

static void freememory(Memory m)
{
    VirtualFree(m.base, 0, MEM_RELEASE);
}

static i32 truncsize(iz len)
{
    i32 max = 0x7fffffff;
    return len>max ? max : (i32)len;
}

static u32 tou32(i32 len)
{
    assert(len >= 0);
    return (u32)len;
}

typedef enum {
    INI_eof,
    INI_section,
    INI_key,
    INI_value
} IniType;

typedef struct {
    s8      name;
    IniType type;
} IniToken;

typedef struct {
    u8 *beg;
    u8 *end;
    b32 invalue;
} IniParser;

static b32 inidone(IniParser *p)
{
    return p->beg == p->end;
}

static u8 ininext(IniParser *p)
{
    return *p->beg++;
}

static b32 iniblank(u8 c)
{
    return c==' ' || c=='\t' || c=='\r';
}

static void iniskip(IniParser *p)
{
    for (; !inidone(p) && iniblank(*p->beg); p->beg++) {}
}

static u8 *initok(IniParser *p, u8 term)
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
static IniToken iniparse(IniParser *p)
{
    IniToken r = {};

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
    sym_title,
    sym_path_type,
    sym_inherit,
    sym_minimal,
    sym_strict,
} Symbol;

static Symbol intern(s8 s)
{
    static struct {
        s8     name;
        Symbol symbol;
    } symbols[] = {
        {S("w64devkit"), sym_w64devkit},
        {S("home"),      sym_home},
        {S("title"),     sym_title},
        {S("path type"), sym_path_type},
        {S("inherit"),   sym_inherit},
        {S("minimal"),   sym_minimal},
        {S("strict"),    sym_strict},
    };
    for (iz i = 0; i < countof(symbols); i++) {
        if (s8equals(symbols[i].name, s)) {
            return symbols[i].symbol;
        }
    }
    return sym_null;
}

static c16 *expandvalue(s8 value, Arena *perm, Arena scratch)
{
    assert(value.len < 0x7fffffff);

    // First temporarily convert to a null-terminated wide string
    i32 len = MultiByteToWideChar(CP_UTF8, 0, value.s, (i32)value.len, 0, 0);
    if (len == 0x7fffffff) outofmemory();
    s16 w = {};
    w.len = len + 1;  // append null terminator
    w.s = new(&scratch, c16, w.len);
    MultiByteToWideChar(CP_UTF8, 0, value.s, (i32)value.len, w.s, len);

    len = (i32)ExpandEnvironmentStringsW(w.s, 0, 0);
    if (len < 0) outofmemory();
    c16 *r = new(perm, c16, len);
    ExpandEnvironmentStringsW(w.s, r, tou32(len));
    return r;
}

// Expand to a full path via GetFullPathNameW.
static c16 *tofullpath(c16 *path, Arena *perm)
{
    i32 len = (i32)GetFullPathNameW(path, 0, 0, 0);
    if (len < 0) outofmemory();
    c16 *r = new(perm, c16, len);
    GetFullPathNameW(path, tou32(len), r, 0);
    return r;
}

static s8 loadfile(c16 *path, Arena *perm)
{
    s8 r = {};

    uz h = CreateFileW(
        path,
        GENERIC_READ,
        FILE_SHARE_ALL,
        0,
        OPEN_EXISTING,
        0,
        0
    );
    if (h == (uz)-1) {
        return r;
    }

    r.s = (u8 *)perm->beg;
    r.len = truncsize(perm->end - perm->beg);
    i32 len;
    b32 ok = ReadFile(h, r.s, (i32)r.len, &len, 0);
    CloseHandle(h);
    r.len = len;
    r.s = ok ? r.s : 0;
    perm->beg += r.len;
    return r;
}

typedef struct {
    c16   *home;
    c16   *title;
    Symbol path_type;
    b32    ok;
} Config;

static Config newconfig()
{
    Config r = {};
    r.title = u"w64devkit";
    r.path_type = sym_inherit;
    return r;
}

// Read entries from w64devkit.ini. Expands environment variables, and
// if "home" is relative, converts it to an absolute path. Before the
// call, the working directory must be location of w64devkit.exe.
static Config loadconfig(Arena *perm, Arena scratch)
{
    Config conf = newconfig();

    s8 ini = loadfile(u"w64devkit.ini", &scratch);
    if (!ini.s) return conf;

    IniParser *p = new(&scratch, IniParser, 1);
    p->beg = ini.s;
    p->end = ini.s + ini.len;

    for (Symbol section = 0, key = 0;;) {
        IniToken t = iniparse(p);
        switch (t.type) {
        case INI_eof:
            conf.ok = 1;
            return conf;
        case INI_section:
            section = intern(t.name);
            break;
        case INI_key:
            key = intern(t.name);
            break;
        case INI_value:
            if (section == sym_w64devkit) {
                if (!conf.home && key==sym_home) {
                    Arena temp = scratch;
                    conf.home = expandvalue(t.name, &temp, *perm);
                    conf.home = tofullpath(conf.home, perm);
                } else if (key == sym_title) {
                    conf.title = expandvalue(t.name, perm, scratch);
                } else if (key == sym_path_type) {
                    conf.path_type = intern(t.name);
                }
            }
            break;
        }
    }
}

typedef struct {
    c16 *buf;
    iz   cap;
    iz   len;
    b32  err;
} Buf16;

static Buf16 newbuf16(Arena *a, iz cap)
{
    Buf16 buf = {};
    buf.buf = new(a, c16, cap);
    buf.cap = cap;
    return buf;
}

static void buf16cat(Buf16 *buf, s16 s)
{
    iz avail = buf->cap - buf->len;
    iz count = s.len<avail ? s.len : avail;
    c16 *dst = buf->buf + buf->len;
    for (iz i = 0; i < count; i++) {
        dst[i] = s.s[i];
    }
    buf->len += count;
    buf->err |= count < s.len;
}

static void buf16c16(Buf16 *buf, c16 c)
{
    s16 s = {&c, 1};
    buf16cat(buf, s);
}

static void buf16moduledir(Buf16 *buf, Arena scratch)
{
    // GetFullPathNameW does not allow querying the output size, instead
    // indicating whether or not the buffer was large enough. So simply
    // offer the entire scratch buffer, then crop out the actual result.
    scratch.beg += -(uz)scratch.beg & (sizeof(c16) - 1);  // align
    i32 len = truncsize((scratch.end - scratch.beg)/2);
    s16 path = {};
    path.s = (c16 *)scratch.beg;
    path.len = (i32)GetModuleFileNameW(0, path.s, tou32(len));
    if (len == path.len) outofmemory();
    for (; path.len; path.len--) {
        switch (path.s[path.len-1]) {
        case  '/':
        case '\\': path.len--;
                   buf16cat(buf, path);
                   return;
        }
    }
}

static void buf16getenv(Buf16 *buf, c16 *key, Arena scratch)
{
    i32 len = (i32)GetEnvironmentVariableW(key, 0, 0);
    if (len < 0) outofmemory();
    s16 var = {};
    var.s = new(&scratch, c16, len);
    var.len = (i32)GetEnvironmentVariableW(key, var.s, tou32(len));
    buf16cat(buf, var);
}

static s16 getwindir(Arena *perm)
{
    i32 len = (i32)GetWindowsDirectoryW(0, 0);
    if (len < 0) outofmemory();
    s16 r = {};
    r.s   = new(perm, c16, len);
    r.len = (i32)GetWindowsDirectoryW(r.s, tou32(len));
    return r;
}

static void buf16minpath(Buf16 *buf, Arena scratch)
{
    s16 windir = getwindir(&scratch);

    buf16cat(buf, windir);
    buf16cat(buf, U(u"\\System32;"));

    buf16cat(buf, windir);
    buf16cat(buf, U(u";"));

    buf16cat(buf, windir);
    buf16cat(buf, U(u"\\System32\\Wbem"));

    // PowerShell directory first appears in Windows 7
    u16 version = (u16)GetVersion();
    version = (u16)(version>>8 | version<<8);
    if (version > 0x0600) {
        buf16cat(buf, U(u";"));
        buf16cat(buf, windir);
        buf16cat(buf, U(u"\\System32\\WindowsPowerShell\\v1.0"));
    }
}

static void toslashes(c16 *path)
{
    for (iz i = 0; i < path[i]; i++) {
        path[i] = path[i]=='\\' ? '/' : path[i];
    }
}

static i32 w64devkit()
{
    Memory mem = newmemory(1<<22);
    if (!mem.base) {
        fatal(u"Out of Memory on startup");
    }
    Arena *perm = &mem.perm;
    Arena  scratch = mem.scratch;

    // First load the module directory into the fresh buffer, and use it
    // for a few different operations.
    Buf16 path = newbuf16(perm, MAX_ENVVAR);
    buf16moduledir(&path, scratch);
    Buf16 moduledir = path;  // to truncate back to the module dir

    buf16c16(&path, 0);  // null terminator
    SetEnvironmentVariableW(u"W64DEVKIT_HOME", path.buf);  // ignore errors

    #ifdef VERSION
    #define LSTR(s) XSTR(s)
    #define XSTR(s) u ## # s
    SetEnvironmentVariableW(u"W64DEVKIT", LSTR(VERSION));  // ignore errors
    #endif

    // Maybe set HOME from w64devkit.ini
    Config conf = newconfig();
    if (SetCurrentDirectoryW(path.buf)) {
        conf = loadconfig(perm, scratch);
        if (conf.home) {
            toslashes(conf.home);
            SetEnvironmentVariableW(u"HOME", conf.home);  // ignore errors
        }
    }

    // Continue building PATH
    path = moduledir;
    buf16cat(&path, U(u"\\bin"));
    switch (conf.path_type) {
    case sym_inherit:
        buf16cat(&path, U(u";"));
        buf16getenv(&path, u"PATH", scratch);
        break;
    case sym_minimal:
        buf16cat(&path, U(u";"));
        buf16minpath(&path, scratch);
        break;
    case sym_strict:
        break;
    default:
        fatal(u"w64devkit.ini: 'path type' must be inherit|minimal|strict");
    }
    buf16c16(&path, 0);  // null terminator
    if (path.err || !SetEnvironmentVariableW(u"PATH", path.buf)) {
        fatal(u"Failed to configure $PATH");
    }

    // Set the console title as late as possible, but not after starting
    // the shell because .profile might change it.
    if (conf.title) {
        SetConsoleTitleW(conf.title);  // ignore errors
    }

    path = moduledir;
    buf16cat(&path, U(u"\\bin\\busybox.exe"));
    buf16c16(&path, 0);  // null terminator

    // Start a BusyBox login shell
    Si si = {};
    si.cb = sizeof(si);
    Pi pi;
    c16 cmdline[] = u"sh -l";  // NOTE: must be mutable!
    if (!CreateProcessW(path.buf, cmdline, 0, 0, 1, 0, 0, 0, &si, &pi)) {
        fatal(u"Failed to launch a login shell");
    }

    // Wait for shell to exit
    freememory(mem);
    i32 ret;
    WaitForSingleObject(pi.process, -1);
    GetExitCodeProcess(pi.process, &ret);
    return ret;
}

[[gnu::stdcall]]
void mainCRTStartup()
{
    i32 r = w64devkit();
    ExitProcess(r);
}
