/* Tiny, standalone launcher for w64devkit
 * This avoids running a misbehaving monitor cmd.exe in the background.
 *
 * $ gcc -DVERSION="$VERSION" -Os -ffreestanding -s -nostdlib \
 *       -o w64devkit.exe w64devkit.c -lkernel32
 *
 * This is free and unencumbered software released into the public domain.
 */
#include <windows.h>

#define COUNTOF(a) (sizeof(a) / sizeof(0[a]))
#define LSTR(s) XSTR(s)
#define XSTR(s) L ## # s
#define MAX_VAR 32767

enum err {ERR_PATH, ERR_EXEC};
static const char err_path[] = "w64devkit: failed to set $PATH\n";
static const char err_exec[] = "w64devkit: failed to start shell\n";

static int
fatal(enum err e)
{
    size_t len = 0;
    const char *msg = 0;
    switch (e) {
    case ERR_PATH: msg = err_path; len = sizeof(err_path) - 1; break;
    case ERR_EXEC: msg = err_exec; len = sizeof(err_exec) - 1; break;
    }
    HANDLE out = GetStdHandle(STD_ERROR_HANDLE);
    DWORD dummy;
    WriteFile(out, msg, len, &dummy, 0);
    return 2;
}

static void
lmemmove(WCHAR *dst, const WCHAR *src, size_t len)
{
    for (size_t i = 0; i < len; i++) dst[i] = src[i];
}

static WCHAR *
findfile(WCHAR *s)
{
    for (WCHAR *r = s; ; s++) {
        switch (*s) {
        case    0: return r;
        case  '/':
        case '\\': r = s + 1;
        }
    }
}

int WINAPI
mainCRTStartup(void)
{
    static WCHAR path[MAX_PATH + MAX_VAR];

    /* Construct a path to bin/ directory */
    static const WCHAR bin[] = L"bin;";
    GetModuleFileNameW(0, path, MAX_PATH);
    WCHAR *tail = findfile(path);
    {
        /* Set W64DEVKIT_HOME to this module's directory */
        int save = tail[-1];
        tail[-1] = 0;
        SetEnvironmentVariableW(L"W64DEVKIT_HOME", path); // ignore errors
        tail[-1] = save;
    }
    lmemmove(tail, bin, COUNTOF(bin));
    size_t binlen = tail - path + COUNTOF(bin) - 1;

    /* Preprend bin/ path to $PATH */
    GetEnvironmentVariableW(L"PATH", path+binlen, MAX_VAR);
    if (!SetEnvironmentVariableW(L"PATH", path)) {
        return fatal(ERR_PATH);
    }

    #ifdef VERSION
    SetEnvironmentVariableW(L"W64DEVKIT", LSTR(VERSION)); // ignore errors
    #endif

    /* Start a BusyBox login shell */
    STARTUPINFOW si;
    GetStartupInfoW(&si);
    PROCESS_INFORMATION pi;
    static const WCHAR busybox[] = L"bin\\busybox.exe";
    lmemmove(tail, busybox, COUNTOF(busybox));
    if (!CreateProcessW(path, L"sh -l", 0, 0, TRUE, 0, 0, 0, &si, &pi)) {
        return fatal(ERR_EXEC);
    }

    /* Wait for shell to exit */
    DWORD ret;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &ret);
    return ret;
}
