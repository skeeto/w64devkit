# Portable C and C++ Development Kit for x64 Windows

w64devkit is a Dockerfile that builds from source a small, portable
development suite for creating C and C++ applications on and for x64
Windows. It is the highest quality native toolchain for C, C++, and
Fortran currently available on Windows.

Included tools:

* [Mingw-w64 GCC][w64] : compilers, linker, assembler
* [GDB][gdb] : debugger
* [GNU Make][make] : standard build tool
* [busybox-w32][bb] : standard unix utilities, including sh
* [Vim][vim] : powerful text editor
* [Universal Ctags][ctags] : source navigation
* [NASM][nasm] : x86 assembler

The compilers support pthreads, C++11 threads, and OpenMP. All included
libraries are static. Docker is not required to use the development kit.
It's merely a reliable, clean environment for building the kit itself.

## Build

First build the image, then run it to produce a distribution .zip file:

    docker build -t w64devkit .
    docker run --rm w64devkit >w64devkit.zip

This takes about half an hour on modern systems. You will need an
internet connection during the first couple minutes of the build.

## Usage

The final .zip file contains tools in a typical unix-like configuration.
Unzip the contents anywhere. Inside is `w64devkit.exe` (or `activate.bat`)
that launches a console window with the environment configured and ready
to go. It is the easiest way to enter the development environment, and
requires no system changes.

Alternatively, add the `bin/` directory to your path. For example, while
inside a console or batch script:

    set PATH=c:\path\to\w64devkit\bin;%PATH%

To start an interactive unix shell:

    sh -l

## Best of class

What makes w64devkit the best? It is the only production-grade, native
toolchain for Windows which:

* Does not require installation. Run it anywhere as any user.

* Does not require internet access during installation. The installers for
  other toolchains are actually downloaders, or otherwise call home, and
  so must be online for at least part of their installation process.

It's one of a few that:

* Supports C99 by default. Most others have incomplete support or require
  esoteric configurations in order to enable it.

* It's one of the few that supports static linking for the entire runtime.

Finally it's by far the easiest toolchain to bootstrap, meaning it's the
easiest to tweak and adjust for your own requirements.

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

Due to [an old GCC bug][bug], we must build a cross-compiler to
cross-compile GCC itself because, due to host contamination, GCC can
only be correctly and safely cross-compiled by a matching version.

I'd love to include Git, but unfortunately Git's build system doesn't
quite support cross-compilation, and it's hostile to installation-free
.zip distribution (lots of symlinks). A decent backup solution would be
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
[bug]: https://gcc.gnu.org/legacy-ml/gcc/2017-05/msg00219.html
[ctags]: https://github.com/universal-ctags/ctags
[gdb]: https://www.gnu.org/software/gdb/
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
[zip]: https://tanzu.vmware.com/content/blog/barriers-to-deterministic-reproducible-zip-files
