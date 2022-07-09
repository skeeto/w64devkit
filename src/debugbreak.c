// debugbreak - raise a breakpoint exception in all Win32 debuggees
//
// Graphical programs with windows can rely on the F12 hotkey to break in
// the attached debugger, but console programs have no such hotkey. This
// program fills that gap, breaking your console program mid-execution,
// such as when it's stuck in an infinite loop.
//
// Mingw-w64:
//   gcc -Os -fno-ident -fno-asynchronous-unwind-tables -s -nostdlib
//       -o debugbreak.exe debugbreak.c -lkernel32 -lshell32
//
// MSVC:
//   cl /GS- /Os debugbreak.c
//
// This is free and unencumbered software released into the public domain.
#include <windows.h>
#include <tlhelp32.h>
#if defined(_MSC_VER)
#  pragma comment(lib, "kernel32")
#  pragma comment(lib, "shell32")
#  pragma comment(linker, "/subsystem:console")
#endif

static const char usage[] =
"Usage: debugbreak [-h] [-k] \n"
"  raise a breakpoint exception in all Win32 debuggees\n"
"  -h    print this usage message\n"
"  -k    terminate debugees rather than breakpoint\n";

struct wgetopt {
    wchar_t *optarg;
    int optind, optopt, optpos;
};

static int
wgetopt(struct wgetopt *x, int argc, wchar_t **argv, char *optstring)
{
    wchar_t *arg = argv[!x->optind ? (x->optind += !!argc) : x->optind];
    if (arg && arg[0] == '-' && arg[1] == '-' && !arg[2]) {
        x->optind++;
        return -1;
    } else if (!arg || arg[0] != '-' || ((arg[1] < '0' || arg[1] > '9') &&
                                         (arg[1] < 'A' || arg[1] > 'Z') &&
                                         (arg[1] < 'a' || arg[1] > 'z'))) {
        return -1;
    } else {
        while (*optstring && arg[x->optpos+1] != *optstring) {
            optstring++;
        }
        x->optopt = arg[x->optpos+1];
        if (!*optstring) {
            return '?';
        } else if (optstring[1] == ':') {
            if (arg[x->optpos+2]) {
                x->optarg = arg + x->optpos + 2;
                x->optind++;
                x->optpos = 0;
                return x->optopt;
            } else if (argv[x->optind+1]) {
                x->optarg = argv[x->optind+1];
                x->optind += 2;
                x->optpos = 0;
                return x->optopt;
            } else {
                return ':';
            }
        } else {
            if (!arg[++x->optpos+1]) {
                x->optind++;
                x->optpos = 0;
            }
            return x->optopt;
        }
    }
}

int
mainCRTStartup(void)
{
    DWORD n;
    HANDLE h;
    wchar_t **argv;
    int option, argc;
    struct wgetopt wgo = {0, 0, 0, 0};
    enum {MODE_BREAK, MODE_TERM} mode = MODE_BREAK;

    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    while ((option = wgetopt(&wgo, argc, argv, "hk")) != -1) {
        switch (option) {
        case 'k': mode = MODE_TERM;
                  break;
        case 'h': h = GetStdHandle(STD_OUTPUT_HANDLE);
                  return !WriteFile(h, usage, sizeof(usage)-1, &n, 0);
        case '?': h = GetStdHandle(STD_ERROR_HANDLE);
                  WriteFile(h, usage, sizeof(usage)-1, &n, 0);
                  return 1;
        }
    }

    // Cannot fail with this configuration
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    PROCESSENTRY32W p = {sizeof(p)};
    for (BOOL r = Process32FirstW(s, &p); r; r = Process32NextW(s, &p)) {
        // 64-bit only requires PROCESS_CREATE_THREAD. 32-bit additionally
        // requires PROCESS_VM_OPERATION and PROCESS_VM_WRITE. Since it's
        // not clearly documented, just ask for PROCESS_ALL_ACCESS in case
        // it changes in the future.
        HANDLE h = OpenProcess(PROCESS_ALL_ACCESS, 0, p.th32ProcessID);
        if (h) {
            BOOL b;
            switch (mode) {
            case MODE_BREAK:
                // If the process has no debugger attached, nothing happens.
                // Otherwise this would require CheckRemoteDebuggerPresent to
                // avoid terminating normal processes.
                DebugBreakProcess(h);
                break;
            case MODE_TERM:
                if (CheckRemoteDebuggerPresent(h, &b) && b) {
                    TerminateProcess(h, 1);
                }
                break;
            }
            CloseHandle(h);
        }
    }
    return GetLastError() != ERROR_NO_MORE_FILES;
}
