# Portable C and C++ Development Kit for x64 Windows

[w64devkit][] is a Dockerfile that builds from source a small, portable
development suite for creating C and C++ applications on and for x64
Windows. See "Releases" for pre-built, ready-to-use kits.

Included tools:

* [Mingw-w64 GCC][w64] : compilers, linker, assembler
* [GDB][gdb] : debugger
* [GNU Make][make] : standard build tool
* [busybox-w32][bb] : standard unix utilities, including sh
* [Vim][vim] : powerful text editor
* [Universal Ctags][ctags] : source navigation
* [NASM][nasm] : x86 assembler
* [Cppcheck][cppcheck] : static code analysis

The toolchain includes pthreads, C++11 threads, and OpenMP. All included
runtime components are static. **Docker/Podman is not required to use the
development kit**. It's merely a reliable, clean environment for building
the kit itself.

## Build

First build the image, then run it to produce a distribution .zip file:

    docker build -t w64devkit .
    docker run --rm w64devkit >w64devkit.zip

This takes about half an hour on modern systems. You will need an internet
connection during the first few minutes of the build. **Note:** Do not use
PowerShell because it lacks file redirection.

## Usage

The final .zip file contains tools in a typical unix-like configuration.
Unzip the contents anywhere. Inside is `w64devkit.exe`, which launches a
console window with the environment configured and ready to go. It is the
easiest way to enter the development environment, and requires no system
changes. It also sets two extra environment variables: `W64DEVKIT_HOME` to
the installation root and `W64DEVKIT` to the version.

Alternatively, add the `bin/` directory to your path. For example, inside
a `cmd.exe` console or batch script:

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

* [Complements Go](https://nullprogram.com/blog/2021/06/29/) for cgo and
  bootstrapping.

## Optimized for size

The language runtimes in w64devkit are optimized for size, so it produces
particularly small binaries when programs are also optimized for size
(`-Os`) during compilation. If your program only uses the `printf` family
of functions with MSVC-compatible directives (i.e. limited to C89), and
you want even smaller binaries, you can avoid embedding the Mingw-w64's
improved implementation by setting `__USE_MINGW_ANSI_STDIO` to 0 before
including any headers.

    $ cc -Os -D__USE_MINGW_ANSI_STDIO=0 ...

## Fortran support

Only C and C++ are included by default, but w64devkit also has full
support for Fortran. To build a Fortran compiler, add `fortran` to the
`--enable-languages` lines in the Dockerfile.

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

* [C and C++ Standards (drafts)][doc-std] (PDF), for figuring out how
  corner cases are intended to work.

* [Intel Intrinsics Guide][doc-intr] (interactive HTML), a great resource
  when working with SIMD intrinsics. (Search for "Download" on the left.)

* [GNU Make manual][doc-make] (PDF, HTML)

* [GNU Binutils manuals][doc-ld] (PDF, HTML), particularly `ld` and `as`.

* [GDB manual][doc-gdb] (PDF)

* [BusyBox man pages][doc-bb] (TXT), though everything here is also
  available via `-h` option inside w64devkit.

* [NASM manual][doc-nasm] (PDF)

* [Intel Software Developer Manuals][doc-intel] (PDF), for referencing x86
  instructions, when either studying compiler output with `objdump`, or
  writing assembly with `nasm` or `as`.

## Library installation

Except for the standard libraries and Win32 import libraries, w64devkit
does not include libraries, but you can install additional libraries such
that the toolchain can find them naturally. There are three options:

1. Install it under the sysroot at `w64devkit/$ARCH/`. The easiest option,
   but will require re-installation after upgrading w64devkit. If it
   defines `.pc` files, the `pkg-config` command will automatically find
   and use them.

2. Append its installation directory to your `CPATH` and `LIBRARY_PATH`
   environment variables. Use `;` to delimit directories. You would likely
   do this in your `.profile`.

3. If it exists, append its `pkgconfig` directory to the `PKG_CONFIG_PATH`
   environment variable, then use the `pkg-config` command as usual. Use
   `;` to delimit directories

Both (1) and (3) are designed to work correctly even if w64devkit or the
libraries have paths containing spaces.

## Cppcheck tips

Use `--library=windows` for programs calling the Win32 API directly, which
adds additional checks. In general, the following configuration is a good
default for programs developed using w64devkit:

    $ cppcheck --quiet -j$(nproc) --library=windows \
               --suppress=uninitvar --enable=portability,performance .

A "strict" check that is more thorough, but more false positives:

    $ cppcheck --quiet -j$(nproc) --library=windows \
          --enable=portability,performance,style \
          --suppress=uninitvar --suppress=unusedStructMember \
          --suppress=constVariable --suppress=shadowVariable \
          --suppress=variableScope --suppress=constParameter \
          --suppress=shadowArgument --suppress=knownConditionTrueFalse .

## Notes

`$HOME` can be set through the adjacent `w64devkit.ini` configuration, and
may even be relative to the `w64devkit/` directory. This is useful for
encapsulating the entire development environment, with home directory, on
removable, even read-only, media. Use a `.profile` in the home directory
to configure the environment further.

I'd love to include Git, but unfortunately Git's build system doesn't
quite support cross-compilation. A decent alternative would be
[Quilt][quilt], but it's written in Bash and Perl.

Neither Address Sanitizer (ASan) nor Thread Sanitizer (TSan) [has been
ported to Mingw-w64][san] ([also][san2]), but Undefined Behavior Sanitizer
(UBSan) works perfectly under GDB. With both `-fsanitize=undefined` and
`-fsanitize-trap`, GDB will [break precisely][break] on undefined
behavior, and it does not require linking with libsanitizer.

The kit includes a unique [`debugbreak` command][debugbreak]. It causes
all debugee processes to break in the debugger, like using Windows' F12
debugger hotkey. This is especially useful for console subsystem programs.

Since the build environment is so stable and predicable, it would be
great for the .zip to be reproducible, i.e. builds by different people
are bit-for-bit identical. There are multiple reasons why this is not
currently the case, the least of which are [timestamps in the .zip
file][zip].

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
[cppcheck]: https://cppcheck.sourceforge.io/
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
[doc-nasm]: https://www.nasm.us/docs.php
[doc-std]: https://stackoverflow.com/a/83763
[doc-win32]: https://web.archive.org/web/20220922051031/http://www.laurencejackson.com/win32/
[gdb]: https://www.gnu.org/software/gdb/
[go]: https://nullprogram.com/blog/2021/06/29/
[gpl]: https://www.gnu.org/licenses/gcc-exception-3.1.en.html
[lic1]: https://sourceforge.net/p/mingw-w64/mingw-w64/ci/master/tree/COPYING.MinGW-w64-runtime/COPYING.MinGW-w64-runtime.txt
[lic2]: https://sourceforge.net/p/mingw-w64/mingw-w64/ci/master/tree/mingw-w64-libraries/winpthreads/COPYING
[make]: https://www.gnu.org/software/make/
[nasm]: https://www.nasm.us/
[quilt]: http://savannah.nongnu.org/projects/quilt
[san]: http://mingw-w64.org/doku.php/contribute#sanitizers_asan_tsan_usan
[san2]: https://groups.google.com/forum/#!topic/address-sanitizer/q0e5EBVKZT4
[vim]: https://www.vim.org/
[w64]: http://mingw-w64.org/
[w64devkit]: https://github.com/skeeto/w64devkit
[zip]: https://tanzu.vmware.com/content/blog/barriers-to-deterministic-reproducible-zip-files
