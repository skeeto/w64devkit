/* Well-behaved command line aliases for w64devkit
 *
 * Unlike batch script aliases, this program will not produce an annoying
 * and useless "Terminate batch job (Y/N)" prompt. When compiling, define
 * EXE as the target executable, and define CMD as the argv[0] replacement,
 * including additional arguments. Example:
 *
 *   $ gcc -DEXE='L"target.exe"' -DCMD='L"argv0 argv1..."' \
 *         -s -Os -nostdlib -ffreestanding -o alias.exe alias.c -lkernel32
 *
 * Program is compiled freestanding in order to be as small as possible.
 */
#include <windows.h>

#define FATAL "fatal: w64devkit alias failed\n"
#define COUNTOF(a) (sizeof(a) / sizeof(0[a]))

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
        s++;
        for (int e = 0; ; s++) {
            switch (*s) {
            case  0: return s;
            case 34: if (!e) return s + 1; break;
            case 92: e = !e; break;
            default: e = 0;
            }
        }
    } else {
        /* unquoted argv[0] */
        for (;; s++) {
            switch (*s) {
            case  0:
            case  9:
            case 10:
            case 13:
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

int WINAPI
mainCRTStartup(void)
{
    /* Replace alias module with adjacent target. */
    WCHAR exe[MAX_PATH + COUNTOF(EXE)];
    GetModuleFileNameW(0, exe, MAX_PATH);
    WCHAR *file = findfile(exe);
    xmemcpy(file, EXE, sizeof(EXE));

    /* Produce a new command line string with new argv[0]. */
    WCHAR *args = findargs(GetCommandLineW());
    size_t argslen = xstrlen(args);
    size_t cmdlen = COUNTOF(CMD) + argslen - 1;
    WCHAR *cmd = HeapAlloc(GetProcessHeap(), 0, sizeof(WCHAR)*cmdlen);
    xmemcpy(cmd, CMD, sizeof(WCHAR)*(COUNTOF(CMD) - 1));
    xmemcpy(cmd + COUNTOF(CMD) - 1, args, sizeof(WCHAR)*argslen);

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
