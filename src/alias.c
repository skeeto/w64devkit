// Well-behaved command line aliases for w64devkit
//
// Unlike batch script aliases, this program will not produce an annoying
// and useless "Terminate batch job (Y/N)" prompt. When compiling, define
// EXE as the target executable (relative or absolute path), and define CMD
// as the argv[0] replacement, including additional arguments. Example:
//
//   $ gcc -DEXE="target.exe" -DCMD="argv0 argv1"
//         -Os -fno-asynchronous-unwind-tables
//         -s -nostartfiles -Wl,--gc-sections -o alias.exe alias.c
//
// This is free and unencumbered software released into the public domain.

// Win32 declarations
// NOTE: Parsing windows.h was by far the slowest part of the build, and
// this program is compiled hundreds times for w64devkit. So instead,
// define just what's needed.

#define MAX_PATH 260

typedef __SIZE_TYPE__ size_t;
typedef unsigned short char16_t;

typedef struct {
  int cb;
  void *a, *b, *c;
  int d, e, f, g, h, i, j, k;
  short l, m;
  void *n, *o, *p, *q;
} StartupInfo;

typedef struct {
    void *process;
    void *thread;
    int pid;
    int tid;
} ProcessInformation;

int CreateProcessW(
    void *, void *, void *, void *, int, int, void *, void *, void *, void *
) __attribute((dllimport,stdcall));
char16_t *GetCommandLineW(void)
    __attribute((dllimport,stdcall));
int GetExitCodeProcess(void *, int *)
    __attribute((dllimport,stdcall));
int GetModuleFileNameW(void *, char16_t *, int)
    __attribute((dllimport,stdcall));
void *GetStdHandle(int)
    __attribute((dllimport,stdcall));
int lstrlenW(void *)
    __attribute((dllimport,stdcall));
void *VirtualAlloc(void *, size_t, int, int)
    __attribute__((dllimport,stdcall,malloc));
int WriteFile(void *, void *, int, int *, void *)
    __attribute((dllimport,stdcall));
int WaitForSingleObject(void *, int)
    __attribute((dllimport,stdcall));

// Application

#define FATAL "fatal: w64devkit alias failed: "
#define TOOLONG "command too long\n"
#define OOM "out of memory\n"
#define CREATEPROC "cannot create process\n"
#define COUNTOF(a) (int)(sizeof(a) / sizeof(0[a]))
#define LSTR(s) XSTR(s)
#define XSTR(s) L ## # s
#define LEXE LSTR(EXE)
#define LCMD LSTR(CMD)

#define STRBUF(buf, cap) {buf, cap, 0, 0}
typedef struct {
    char16_t *buf;
    int cap;
    int len;
    int error;
} StrBuf;

static void append(StrBuf *b, char16_t *s, int len)
{
    int avail = b->cap - b->len;
    int count = len<avail ? len : avail;
    for (int i = 0; i < count; i++) {
        b->buf[b->len+i] = s[i];
    }
    b->len += count;
    b->error |= len > avail;
}

// Find the end of argv[0].
static char16_t *findargs(char16_t *s)
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

// Return the directory component length including the last slash.
static int dirname(char16_t *s)
{
    int len = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i]=='/' || s[i]=='\\') {
            len = i + 1;
        }
    }
    return len;
}

static void fail(char *reason, int len)
{
    int dummy;
    void *out = GetStdHandle(-12);
    WriteFile(out, FATAL, COUNTOF(FATAL), &dummy, 0);
    WriteFile(out, reason, len, &dummy, 0);
}

#if __i386
__attribute((force_align_arg_pointer))
#endif
__attribute((externally_visible))
int mainCRTStartup(void)
{
    // Replace alias module with adjacent target
    char16_t exebuf[MAX_PATH];
    StrBuf exe = STRBUF(exebuf, COUNTOF(exebuf));
    if (LEXE[1] == ':') {
        // EXE looks like an absolute path
        append(&exe, LEXE, COUNTOF(LEXE));
    } else {
        // EXE looks like a relative path
        char16_t module[MAX_PATH];
        GetModuleFileNameW(0, module, COUNTOF(module));
        int len = dirname(module);
        append(&exe, module, len);
        append(&exe, LEXE, COUNTOF(LEXE));
    }

    // Construct a new command line string
    int cmdcap = 1<<15;
    char16_t *cmdbuf = VirtualAlloc(0, 2*cmdcap, 0x3000, 4);
    if (!cmdbuf) {
        fail(OOM, COUNTOF(OOM)-1);
        return -2;
    }
    StrBuf cmd = STRBUF(cmdbuf, cmdcap);
    append(&cmd, LCMD, COUNTOF(LCMD)-1);
    char16_t *args = findargs(GetCommandLineW());
    append(&cmd, args, lstrlenW(args)+1);

    if (exe.error || cmd.error) {
        fail(TOOLONG, COUNTOF(TOOLONG)-1);
        return -3;
    }

    StartupInfo si = {0};
    si.cb = sizeof(si);
    ProcessInformation pi;
    if (!CreateProcessW(exebuf, cmdbuf, 0, 0, 1, 0, 0, 0, &si, &pi)) {
        fail(CREATEPROC, COUNTOF(CREATEPROC)-1);
        return -4;
    }

    int ret;
    WaitForSingleObject(pi.process, -1);
    GetExitCodeProcess(pi.process, &ret);
    return ret;
}
