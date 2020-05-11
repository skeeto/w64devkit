# Portable C and C++ Development Kit for x64 Windows

w64devkit is a Dockerfile that builds from source a small, portable
development suite for writing C and C++ applications on and for x64
Windows. Docker is not needed to use the tools themselves. It's merely
used as reliable, clean environment for compilation and linking.
Included tools:

* [Mingw-w64 GCC][w64] : compilers, linker, assembler
* GDB : debugger
* GNU Make : standard build tool
* [busybox-w32][bb] : standard unix utilities, including sh
* [Vim][vim]
* [NASM][nasm]

## Build

First build the image, then run it to produce a release .zip file:

    docker build -t w64devkit .
    docker run --rm w64devkit >w64devkit.zip

This takes about half an hour on modern systems. You will need an
internet connection during the first couple minutes of the build.

## Usage

The final .zip file contains tools in a typical unix-like configuration.
Unzip the contents anywhere and add its `bin/` directory to your path.
For example, while inside a console or batch script:

    set PATH=c:\path\to\w64devkit\bin;%PATH%

Then to access a small unix environment:

    busybox sh -l

This will expose the rest of busybox's commands without further action.
However, the unix environment will not be available to other tools such
as `make` without "installing" BusyBox into the `bin/` directory:

    busybox --install

The distribution contains `activate.bat` that launches a console window
with the path pre-configured and ready to go. It's an easy way to enter
the development environment.

## Notes

Due to [an old GCC bug][bug], we must build a cross-compiler to
cross-compile GCC itself because, due to host contamination, GCC can
only be correctly and safely cross-compiled by a matching version.

Since the development kit is intended to be flexible, light, and
portable — i.e. run from anywhere, in place, and no installation is
necessary — the binaries are all optimized for size, not speed.

I'd love to include Git, but unfortunately Git's build system is a
disaster and doesn't support cross-compilation. It's also got weird
dependencies like Perl. Git may be a fantastic and wonderful tool, but
it's also kind of a mess.

It would be nice to have a better shell like Bash. BusyBox Ash is
limited, and the Windows port is even more limited and quite quirky.
Unfortunately Bash's build system is a total mess and does not support
cross-compilation.

Emacs does not support cross-compilation, particularly due to its
[fragile dumper][dumper]. There has been a "portable dumper" in the
works for years that, once stable, may eventually resolve this issue.
Even then, the parts of the build system that target Windows needlessly
assumes a very specific environment (msys), and much of [Emacs' source
very brittle][fpending]. Besides all that, Emacs is *huge* and including
it would triple the size of the release. So Emacs will not be included.

Since the build environment is so stable and predicable, it would be
great for the .zip to be reproducible, i.e. builds by different people
are bit-for-bit identical. There are multiple reasons why this is not
currently the case, the least of which are [timestamps in the .zip
file][zip].


[bb]: https://frippery.org/busybox/
[bug]: https://gcc.gnu.org/legacy-ml/gcc/2017-05/msg00219.html
[dumper]: https://lwn.net/Articles/707615/
[fpending]: http://git.savannah.gnu.org/cgit/emacs.git/tree/lib/fpending.c?h=emacs-26.3&id=96dd0196c28bc36779584e47fffcca433c9309cd
[nasm]: https://www.nasm.us/
[vim]: https://www.vim.org/
[w64]: http://mingw-w64.org/
[zip]: https://tanzu.vmware.com/content/blog/barriers-to-deterministic-reproducible-zip-files
