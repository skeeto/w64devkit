/* Tiny, standalone launcher for w64devkit
 * cc -s -Os -nostdlib -ffreestanding -o w64devkit.exe w64devkit.c -lkernel32
 * This avoids running a misbehaving monitor cmd.exe in the background.
 */
#include <windows.h>

#define COUNTOF(a) (sizeof(a) / sizeof(0[a]))
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
    lmemmove(tail, bin, COUNTOF(bin));
    size_t binlen = tail - path + COUNTOF(bin) - 1;

    /* Preprend bin/ path to $PATH */
    GetEnvironmentVariableW(L"PATH", path+binlen, MAX_VAR);
    if (!SetEnvironmentVariableW(L"PATH", path)) {
        return fatal(ERR_PATH);
    }

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
