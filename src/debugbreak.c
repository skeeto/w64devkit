// debugbreak - raise a breakpoint exception in all Win32 debuggees
//
// Graphical programs with windows can rely on the F12 hotkey to break in
// the attached debugger, but console programs have no such hotkey. This
// program fills that gap, breaking your console program mid-execution,
// such as when it's stuck in an infinite loop.
//
// Mingw-w64:
//   gcc -Os -fno-asynchronous-unwind-tables -Wl,--gc-sections -s -nostdlib
//       -o debugbreak.exe debugbreak.c -lkernel32
//
// MSVC:
//   cl /GS- /Os debugbreak.c
//
// This is free and unencumbered software released into the public domain.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#if defined(_MSC_VER)
#  pragma comment(lib, "kernel32")
#  pragma comment(linker, "/subsystem:console")
#endif

// Try to put data in .text to pack the binary even smaller.
#if __GNUC__
#  define STATIC __attribute__((section(".text.data"))) static const
#else
#  define STATIC static const
#endif

STATIC char usage[] =
"Usage: debugbreak\n"
"  raise a breakpoint exception in all Win32 debuggees\n";

int
mainCRTStartup(void)
{
    // Skip argv[0] and space separator. This avoids linking shell32.dll
    // for CommandLineToArgvW just to count the arguments.
    wchar_t *cmd = GetCommandLineW();
    switch (*cmd) {
    default : for (; *cmd && *cmd != '\t' && *cmd != ' '; cmd++);
              break;
    case '"': for (cmd++; *cmd && *cmd != '"'; cmd++);
              cmd += !!*cmd;
    }
    for (; *cmd == '\t' || *cmd == ' '; cmd++);

    // Print usage and fail if argc > 1. The program's purpose will be
    // more discoverable, including responding to -h/--help.
    if (*cmd) {
        DWORD n;
        HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
        WriteFile(h, usage, sizeof(usage)-1, &n, 0);
        return 1;
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
            // If the process has no debugger attached, nothing happens.
            // Otherwise this would require CheckRemoteDebuggerPresent to
            // avoid terminating normal processes.
            DebugBreakProcess(h);
            CloseHandle(h);
        }
    }
    return GetLastError() != ERROR_NO_MORE_FILES;
}
