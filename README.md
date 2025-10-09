# Portable C, C++, and Fortran Development Kit for x64 and x86 Windows

[w64devkit][] is a Dockerfile that builds from source a small, portable
development suite for creating C and C++ applications on and for x86 and
x64 Windows. See "Releases" for pre-built, ready-to-use kits.

Included tools:

* [Mingw-w64 GCC][w64] : compilers, linker, assembler
* [GDB][gdb] : debugger
* [GNU Make][make] : standard build tool
* [busybox-w32][bb] : standard unix utilities, including sh
* [Vim][vim] : powerful text editor
* [Universal Ctags][ctags] : source navigation

It is an MSVCRT toolchain with pthreads, C++11 threads, and OpenMP. All
included runtime components are static. **Docker/Podman is not required to
use the development kit**. It's merely a reliable, clean environment for
building the kit itself.

## Build

Build the image, then run it to produce a self-extracting 7z archive:

    docker build -t w64devkit .
    docker run --rm w64devkit >w64devkit-x64.exe

This takes about 15 minutes on modern systems. You will need an internet
connection during the first few minutes of the build. **Note:** Do not use
PowerShell because it lacks file redirection.

## Usage

The self-extracting 7z archive contains tools in a typical unix-like
configuration. Extract wherever is convenient. Inside is `w64devkit.exe`,
which launches a console window with the environment configured and ready
to go. It is the easiest way to enter the development environment, and
requires no system changes. It also sets two extra environment variables:
`W64DEVKIT_HOME` to the installation root and `W64DEVKIT` to the version.

Alternatively, add the `w64devkit/bin` directory to your path. For
example, inside a `cmd.exe` console or batch script:

    set PATH=c:\path\to\w64devkit\bin;%PATH%

Then to start an interactive unix shell:

    sh -l

## Main features

* No installation required. Run it anywhere as any user. Simply delete
  when no longer needed.

* Fully offline. No internet access is ever required or attempted.

* A focus on static linking all runtime components. The runtime is
  optimized for size.

* Trivial to build from source, meaning it's easy to tweak and adjust any
  part of the kit for your own requirements.

## Operating system support

The x64 kit requires Windows 7 or later, though some tools only support
Unicode ("wide") paths, inputs, and outputs on Windows 10 or later. The
toolchain targets Windows 7 by default.

The x86 kit requires Windows XP or later and an SSE2-capable processor
(e.g. at least Pentium 4); limited Unicode support. The toolchain targets
the same by default. Runtimes contain SSE2 instructions, so GCC `-march`
will not reliably target less capable processors when runtimes are linked
(exceptions: `-lmemory`, `-lchkstk`).

## Optimized for size

Runtime components are optimized for size, leading to smaller application
executables. Unique to w64devkit, `libmemory.a` is a library of `memset`,
`memcpy`, `memmove`, `memcmp`, and `strlen` implemented as x86 string
instructions. When [not linking a CRT][crt], linking `-lmemory` provides
tiny definitions, particularly when GCC requires them.

Also unique to w64devkit, `libchkstk.a` has a leaner, faster definition of
`___chkstk_ms` than GCC (`-lgcc`), as well as `__chkstk`, sometimes needed
when linking MSVC artifacts. Both are in the public domain and so, unlike
default implementations, do not involve complex licensing. When required
in a `-nostdlib` build, link `-lchkstk`.

Unlike traditional toolchains, import tables are not populated with junk
ordinal hints. If an explicit hint is not provided (i.e. via a DEF file),
then the hint is zeroed: "no data." Eliminating this random data makes
binaries more compressible and *theoretically* faster loading. See also:
`peports`.

## Recommended downloadable, offline documentation

With a few exceptions, such as Vim's built-in documentation (`:help`),
w64devkit does not include documentation. However, you need not forgo
offline documentation alongside your offline development tools. This is a
list of recommended, no-cost, downloadable documentation complementing
w64devkit's capabilities. In rough order of importance:

* [cppreference][doc-cpp] (HTML), friendly documentation for the C and C++
  standard libraries.

* [GCC manuals][doc-gcc] (PDF, HTML), to reference GCC features,
  especially built-ins, intrinsics, and command line switches.

* [Win32 Help File][doc-win32] (CHM) is old, but official, Windows API
  documentation. Unfortunately much is missing, such as Winsock. (Offline
  Windows documentation has always been very hard to come by.)

* [C Standards][doc-std-c] and [C++ Standards][doc-std-cpp] (drafts), for
  figuring out how corner cases are intended to work.

* [Intel Intrinsics Guide][doc-intr] (interactive HTML), a great resource
  when working with SIMD intrinsics. (Search for "Download" on the left.)

* [GNU Make manual][doc-make] (PDF, HTML)

* [GNU Binutils manuals][doc-ld] (PDF, HTML), particularly `ld` and `as`.

* [GDB manual][doc-gdb] (PDF)

* [BusyBox man pages][doc-bb] (TXT), though everything here is also
  available via `-h` option inside w64devkit.

* [Intel Software Developer Manuals][doc-intel] (PDF), for referencing x86
  instructions, when either studying compiler output with `objdump` or
  writing assembly.

## Library installation

Except for the standard libraries and Win32 import libraries, w64devkit
does not include libraries, but you can install additional libraries such
that the toolchain can find them naturally. There are three options:

1. The w64devkit installation directory is a sysroot (`lib/`, `include/`,
   etc.), so you can install the library directly into w64devkit the usual
   unix-like way. It's the easiest option, but requires re-installation
   after each w64devkit upgrade. If the library defines `.pc` files, the
   `pkg-config` command will automatically find and use them.

2. Append its installation directory to your `CPATH` and `LIBRARY_PATH`
   environment variables. Use `;` to delimit directories. You would likely
   do this in your `.profile`.

3. If it exists, append its `pkgconfig` directory to the `PKG_CONFIG_PATH`
   environment variable, then use the `pkg-config` command as usual. Use
   `;` to delimit directories

Both (1) and (3) are designed to work correctly even if w64devkit or the
libraries have paths containing spaces.

## Unique command-line programs

* `peports`: displays export and import tables of EXEs and DLLs. Like MSVC
  `dumpbin` options `/exports` and `/imports`; narrower and more precise
  than Binutils `objdump -p`. Useful for checking if exports and imports
  match your expectations. Complemented by `c++filt` and `vc++filt`, i.e.
  in a pipeline. Pronounced like *purports*.

* `vc++filt`: a `c++filt` for [Visual C++ name decorations][names]. Used
  to examine GCC-incompatible binaries, potentially to make some use of
  them anyway.

* [`debugbreak`][debugbreak]: causes all debugee processes to break in the
  debugger, like using Windows' F12 debugger hotkey. Especially useful for
  console subsystem programs.

## Notes

`$HOME` can be set through the adjacent `w64devkit.ini` configuration, and
may even be relative to the `w64devkit/` directory. This is useful for
encapsulating the entire development environment, with home directory, on
removable, even read-only, media. Use a `.profile` in the home directory
to configure the environment further.

Neither Address Sanitizer (ASan) nor Thread Sanitizer (TSan) [has been
ported to Mingw-w64][san] ([also][san2]), but Undefined Behavior Sanitizer
(UBSan) works perfectly under GDB. With both `-fsanitize=undefined` and
`-fsanitize-trap`, GDB will [break precisely][break] on undefined
behavior, and it does not require linking with libsanitizer.

## Licenses

When distributing binaries built using w64devkit, your .exe will include
parts of this distribution. For the GCC runtime, including OpenMP, you're
covered by the [GCC Runtime Library Exception][gpl] so you do not need to
do anything. However the Mingw-w64 runtime [has the usual software license
headaches][bs] and you may need to comply with various BSD-style licenses
depending on the functionality used by your program: [MinGW-w64 runtime
licensing][lic1] and [winpthreads license][lic2]. To make this easy,
w64devkit includes the concatenated set of all licenses in the file
`COPYING.MinGW-w64-runtime.txt`, which should be distributed with your
binaries.


[bb]: https://frippery.org/busybox/
[break]: https://nullprogram.com/blog/2022/06/26/
[bs]: https://www.rdegges.com/2016/i-dont-give-a-shit-about-licensing/
[crt]: https://nullprogram.com/blog/2023/02/15/
[ctags]: https://github.com/universal-ctags/ctags
[debugbreak]: https://nullprogram.com/blog/2022/07/31/
[doc-bb]: https://busybox.net/downloads/BusyBox.txt
[doc-cpp]: https://en.cppreference.com/w/Cppreference:Archives
[doc-gcc]: https://gcc.gnu.org/onlinedocs/
[doc-gdb]: https://sourceware.org/gdb/current/onlinedocs/gdb.pdf
[doc-intel]: https://software.intel.com/content/www/us/en/develop/articles/intel-sdm.html
[doc-intr]: https://software.intel.com/sites/landingpage/IntrinsicsGuide/
[doc-ld]: https://sourceware.org/binutils/docs/
[doc-make]: https://www.gnu.org/software/make/manual/
[doc-std-c]: https://en.cppreference.com/w/c/links
[doc-std-cpp]: https://en.cppreference.com/w/cpp/links
[doc-win32]: https://web.archive.org/web/20220922051031/http://www.laurencejackson.com/win32/
[gdb]: https://www.gnu.org/software/gdb/
[gpl]: https://www.gnu.org/licenses/gcc-exception-3.1.en.html
[lic1]: https://sourceforge.net/p/mingw-w64/mingw-w64/ci/master/tree/COPYING.MinGW-w64-runtime/COPYING.MinGW-w64-runtime.txt
[lic2]: https://sourceforge.net/p/mingw-w64/mingw-w64/ci/master/tree/mingw-w64-libraries/winpthreads/COPYING
[make]: https://www.gnu.org/software/make/
[names]: https://learn.microsoft.com/en-us/cpp/build/reference/decorated-names
[san]: http://mingw-w64.org/doku.php/contribute#sanitizers_asan_tsan_usan
[san2]: https://groups.google.com/forum/#!topic/address-sanitizer/q0e5EBVKZT4
[vim]: https://www.vim.org/
[w64]: http://mingw-w64.org/
[w64devkit]: https://github.com/skeeto/w64devkit
bruh