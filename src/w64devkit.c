/* Tiny, standalone launcher for w64devkit
 * This avoids running a misbehaving monitor cmd.exe in the background.
 *
 * $ gcc -DVERSION="$VERSION" \
 *       -mno-stack-arg-probe -Xlinker --stack=0x10000,0x10000 \
 *       -Os -fno-asynchronous-unwind-tables -Wl,--gc-sections \
 *       -s -nostdlib -o w64devkit.exe w64devkit.c -lkernel32
 *
 * This is free and unencumbered software released into the public domain.
 */
#define WIN32_LEAN_AND_MEAN
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

/* Read and process "w64devkit.home" from "w64devkit.ini". Environment
 * variables are expanded, and if relative, the result is converted into
 * an absolute path. The destination length must be MAX_PATH. If this
 * fails for any reason, the string will be zero length.
 *
 * Before calling, the current working directory must be changed to the
 * location of w64devkit.exe.
 */
static void
homeconfig(WCHAR *path)
{
    char home[MAX_PATH*3];    /* UTF-8 MAX_PATH */
    WCHAR whome[MAX_PATH*2];  /* extra room for variables */
    WCHAR expanded[MAX_PATH];

    /* If anything fails, leave empty. */
    path[0] = 0;

    /* Windows thinks this is a narrow, "ANSI"-encoded file, but it's
     * really UTF-8. It's decoded to UTF-16 after reading. This means
     * the INI path must be a narrow string, hence the requirement of
     * changing the current directory before this call, in case the
     * INI's absolute path contains non-ASCII characters.
     *
     * NOTE(Chris): This Win32 function works fine for now, but in the
     * future I may replace it with a simple, embedded INI parser. I
     * already have one written and available in my scratch repository.
     */
    GetPrivateProfileStringA(
        "w64devkit",
        "home",
        0,
        home,
        sizeof(home),
        "./w64devkit.ini"
    );
    if (!MultiByteToWideChar(CP_UTF8, 0, home, -1, whome, MAX_PATH*2)) {
        return;
    }

    /* Process INI string into a final HOME path */
    if (ExpandEnvironmentStringsW(whome, expanded, MAX_PATH) > MAX_PATH) {
        return;
    }
    GetFullPathNameW(expanded, MAX_PATH, path, 0);
}

int
mainCRTStartup(void)
{
    WCHAR path[MAX_PATH + MAX_VAR];

    /* Construct a path to bin/ directory */
    static const WCHAR bin[] = L"bin;";
    GetModuleFileNameW(0, path, MAX_PATH);
    WCHAR *tail = findfile(path);
    {
        /* Set W64DEVKIT_HOME to this module's directory */
        int save = tail[-1];
        tail[-1] = 0;
        SetEnvironmentVariableW(L"W64DEVKIT_HOME", path); // ignore errors

        /* Maybe set HOME from w64devkit.ini */
        if (SetCurrentDirectoryW(path)) {
            WCHAR home[MAX_PATH];
            homeconfig(home);
            if (home[0]) {
                SetEnvironmentVariableW(L"HOME", home); // ignore errors
            }
        }
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
