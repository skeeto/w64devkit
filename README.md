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

The toolchain includes pthreads, C++11 threads, and OpenMP. All included
runtime components are static. **Docker/Podman is not required to use the
development kit**. It's merely a reliable, clean environment for building
the kit itself.

## Build

First build the image, then run it to produce a distribution .zip file:

    docker build -t w64devkit .
    docker run --rm w64devkit >w64devkit.zip

This takes about half an hour on modern systems. You will need an
internet connection during the first couple minutes of the build.

## Usage

The final .zip file contains tools in a typical unix-like configuration.
Unzip the contents anywhere. Inside is `w64devkit.exe` that launches a
console window with the environment configured and ready to go. It is the
easiest way to enter the development environment, and requires no system
changes.

Alternatively, add the `bin/` directory to your path. For example, while
inside a console or batch script:

    set PATH=c:\path\to\w64devkit\bin;%PATH%

To start an interactive unix shell:

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
of functions with MSVC-compatable directivies (i.e. limited to C89), and
you want even smaller binaries, you can avoid embdedding the Mingw-w64's
improved implementation by setting `__USE_MINGW_ANSI_STDIO` to 0 before
including any headers.

    $ cc -Os -D__USE_MINGW_ANSI_STDIO=0 ...

## Fortran support

Only C and C++ are included by default, but w64devkit also has full
support for Fortran. To build a Fortran compiler, add `fortran` to the
`--enable-languages` lines in the Dockerfile.

## Notes

Since the development kit is intended to be flexible, light, and
portable — i.e. run from anywhere, in place, and no installation is
necessary — the binaries are all optimized for size, not speed.

I'd love to include Git, but unfortunately Git's build system doesn't
quite support cross-compilation. A decent alternative would be
[Quilt][quilt], but it's written in Bash and Perl.

What about sanitizer support? That would be fantastic, but unfortunately
libsanitizer [has not yet been ported from MSVC to Mingw-w64][san]
([also][san2]).

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
[bs]: https://www.rdegges.com/2016/i-dont-give-a-shit-about-licensing/
[ctags]: https://github.com/universal-ctags/ctags
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
