/* Well-behaved command line aliases for w64devkit
 *
 * Unlike batch script aliases, this program will not produce an annoying
 * and useless "Terminate batch job (Y/N)" prompt. When compiling, define
 * EXE as the target executable (relative or absolute path), and define CMD
 * as the argv[0] replacement, including additional arguments. Example:
 *
 *   $ gcc -DEXE="target.exe" -DCMD="argv0 argv1" \
 *         -Os -fno-asynchronous-unwind-tables \
 *         -Wl,--gc-sections -s -nostdlib \
 *         -o alias.exe alias.c -lkernel32
 *
 * This program is compiled CRT-free in order to be as small as possible.
 *
 * This is free and unencumbered software released into the public domain.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define FATAL "fatal: w64devkit alias failed\n"
#define COUNTOF(a) (sizeof(a) / sizeof(0[a]))
#define LSTR(s) XSTR(s)
#define XSTR(s) L ## # s
#define LEXE LSTR(EXE)
#define LCMD LSTR(CMD)

static size_t
xstrlen(WCHAR *s)
{
    size_t n = 1;
    while (*s++) n++;
    return n;
}

static void
xmemcpy(void *dst, void *src, size_t len)
{
    unsigned char *d = dst, *s = src;
    for (size_t i = 0; i < len; i++) d[i] = s[i];
}

/* Find the end of argv[0]. */
static WCHAR *
findargs(WCHAR *s)
{
    if (s[0] == 34) {
        /* quoted argv[0] */
        for (s++;; s++) {
            switch (*s) {
            case  0: return s;
            case 34: return s + 1;
            }
        }
    } else {
        /* unquoted argv[0] */
        for (;; s++) {
            switch (*s) {
            case  0:
            case  9:
            case 32: return s;
            }
        }
    }
}

/* Find the final file component. */
static WCHAR *
findfile(WCHAR *s)
{
    for (WCHAR *r = s; ; s++) {
        switch (*s) {
        case  0: return r;
        case 47:
        case 92: r = s + 1;
        }
    }
}

int
mainCRTStartup(void)
{
    /* Replace alias module with adjacent target. */
    WCHAR exe[MAX_PATH + COUNTOF(LEXE)];
    if (LEXE[1] != ':') {
        /* EXE looks like a relative path */
        GetModuleFileNameW(0, exe, MAX_PATH);
        WCHAR *file = findfile(exe);
        xmemcpy(file, LEXE, sizeof(LEXE));
    } else {
        /* EXE looks like an absolute path */
        xmemcpy(exe, LEXE, sizeof(LEXE));
    }

    /* Produce a new command line string with new argv[0]. */
    WCHAR *args = findargs(GetCommandLineW());
    size_t argslen = xstrlen(args);
    size_t cmdlen = COUNTOF(LCMD) + argslen - 1;
    WCHAR *cmd = HeapAlloc(GetProcessHeap(), 0, sizeof(WCHAR)*cmdlen);
    xmemcpy(cmd, LCMD, sizeof(WCHAR)*(COUNTOF(LCMD) - 1));
    xmemcpy(cmd + COUNTOF(LCMD) - 1, args, sizeof(WCHAR)*argslen);

    /* Run target with identical startup but with above changes. */
    STARTUPINFOW si;
    GetStartupInfoW(&si);
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(exe, cmd, 0, 0, TRUE, 0, 0, 0, &si, &pi)) {
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dummy;
        WriteFile(out, FATAL, sizeof(FATAL), &dummy, 0);
        return 2;
    }

    /* Wait for target to exit. */
    DWORD ret;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &ret);
    return ret;
}
