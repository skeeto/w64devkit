FROM debian:buster-slim

ARG VERSION=1.6.0
ARG PREFIX=/w64devkit

ARG BINUTILS_VERSION=2.36.1
ARG BUSYBOX_VERSION=FRP-3812-g12e14ebba
ARG CTAGS_VERSION=20200824
ARG GCC_VERSION=10.2.0
ARG GDB_VERSION=10.1
ARG GMP_VERSION=6.2.0
ARG MAKE_VERSION=4.2
ARG MINGW_VERSION=7.0.0
ARG MPC_VERSION=1.2.1
ARG MPFR_VERSION=4.1.0
ARG NASM_VERSION=2.15.05
ARG VIM_VERSION=8.2

RUN apt-get update && apt-get install --yes --no-install-recommends \
  build-essential curl file libgmp-dev libmpc-dev libmpfr-dev m4 texinfo zip

# Download, verify, and unpack

RUN curl --insecure --location --remote-name-all \
    https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VERSION.tar.xz \
    https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.xz \
    https://ftp.gnu.org/gnu/gdb/gdb-$GDB_VERSION.tar.xz \
    https://ftp.gnu.org/gnu/gmp/gmp-$GMP_VERSION.tar.xz \
    https://ftp.gnu.org/gnu/mpc/mpc-$MPC_VERSION.tar.gz \
    https://ftp.gnu.org/gnu/mpfr/mpfr-$MPFR_VERSION.tar.xz \
    https://ftp.gnu.org/gnu/make/make-$MAKE_VERSION.tar.gz \
    https://frippery.org/files/busybox/busybox-w32-$BUSYBOX_VERSION.tgz \
    http://ftp.vim.org/pub/vim/unix/vim-$VIM_VERSION.tar.bz2 \
    https://www.nasm.us/pub/nasm/releasebuilds/$NASM_VERSION/nasm-$NASM_VERSION.tar.xz \
    http://deb.debian.org/debian/pool/main/u/universal-ctags/universal-ctags_0+git$CTAGS_VERSION.orig.tar.gz \
    https://downloads.sourceforge.net/project/mingw-w64/mingw-w64/mingw-w64-release/mingw-w64-v$MINGW_VERSION.tar.bz2
COPY src/SHA256SUMS $PREFIX/src/
RUN sha256sum -c $PREFIX/src/SHA256SUMS \
 && tar xJf binutils-$BINUTILS_VERSION.tar.xz \
 && tar xzf busybox-w32-$BUSYBOX_VERSION.tgz \
 && tar xzf universal-ctags_0+git$CTAGS_VERSION.orig.tar.gz \
 && tar xJf gcc-$GCC_VERSION.tar.xz \
 && tar xJf gdb-$GDB_VERSION.tar.xz \
 && tar xJf gmp-$GMP_VERSION.tar.xz \
 && tar xzf mpc-$MPC_VERSION.tar.gz \
 && tar xJf mpfr-$MPFR_VERSION.tar.xz \
 && tar xzf make-$MAKE_VERSION.tar.gz \
 && tar xjf mingw-w64-v$MINGW_VERSION.tar.bz2 \
 && tar xJf nasm-$NASM_VERSION.tar.xz \
 && tar xjf vim-$VIM_VERSION.tar.bz2
COPY src/alias.c $PREFIX/src/

# Build cross-compiler

WORKDIR /binutils-$BINUTILS_VERSION
RUN sed -ri 's/(bfd_boolean insert_timestamp = )/\1!/' ld/emultempl/pe*.em
WORKDIR /x-binutils
RUN /binutils-$BINUTILS_VERSION/configure \
        --prefix=/bootstrap \
        --with-sysroot=/bootstrap \
        --target=x86_64-w64-mingw32 \
        --disable-nls \
        --enable-static \
        --disable-shared \
        --disable-multilib
RUN make -j$(nproc)
RUN make install

WORKDIR /x-mingw-headers
RUN /mingw-w64-v$MINGW_VERSION/mingw-w64-headers/configure \
        --prefix=/bootstrap/x86_64-w64-mingw32 \
        --host=x86_64-w64-mingw32
RUN make -j$(nproc)
RUN make install

WORKDIR /bootstrap
RUN ln -s x86_64-w64-mingw32 mingw

WORKDIR /x-gcc
RUN /gcc-$GCC_VERSION/configure \
        --prefix=/bootstrap \
        --with-sysroot=/bootstrap \
        --target=x86_64-w64-mingw32 \
        --enable-static \
        --disable-shared \
        --enable-languages=c,c++ \
        --enable-libgomp \
        --enable-threads=posix \
        --enable-version-specific-runtime-libs \
        --disable-dependency-tracking \
        --disable-nls \
        --disable-multilib \
        CFLAGS="-Os" \
        CXXFLAGS="-Os" \
        LDFLAGS="-s"
RUN make -j$(nproc) all-gcc
RUN make install-gcc

ENV PATH="/bootstrap/bin:${PATH}"

WORKDIR /x-mingw-crt
RUN /mingw-w64-v$MINGW_VERSION/mingw-w64-crt/configure \
        --prefix=/bootstrap/x86_64-w64-mingw32 \
        --with-sysroot=/bootstrap/x86_64-w64-mingw32 \
        --host=x86_64-w64-mingw32 \
        --disable-dependency-tracking \
        --disable-lib32 \
        CFLAGS="-Os" \
        LDFLAGS="-s"
RUN make -j$(nproc)
RUN make install

WORKDIR /x-winpthreads
RUN /mingw-w64-v$MINGW_VERSION/mingw-w64-libraries/winpthreads/configure \
        --prefix=/bootstrap/x86_64-w64-mingw32 \
        --with-sysroot=/bootstrap/x86_64-w64-mingw32 \
        --host=x86_64-w64-mingw32 \
        --enable-static \
        --disable-shared \
        CFLAGS="-Os" \
        LDFLAGS="-s"
RUN make -j$(nproc)
RUN make install

WORKDIR /x-gcc
RUN make -j$(nproc)
RUN make install

# Cross-compile GCC

WORKDIR /
WORKDIR /binutils
RUN /binutils-$BINUTILS_VERSION/configure \
        --prefix=$PREFIX \
        --with-sysroot=$PREFIX \
        --host=x86_64-w64-mingw32 \
        --target=x86_64-w64-mingw32 \
        --disable-nls \
        --enable-static \
        --disable-shared \
        CFLAGS="-Os" \
        LDFLAGS="-s"
RUN make -j$(nproc)
RUN make install

WORKDIR /
WORKDIR /gmp
RUN /gmp-$GMP_VERSION/configure \
        --prefix=/deps \
        --host=x86_64-w64-mingw32 \
        --disable-assembly \
        --enable-static \
        --disable-shared \
        CFLAGS="-Os" \
        CXXFLAGS="-Os" \
        LDFLAGS="-s"
RUN make -j$(nproc)
RUN make install

WORKDIR /
WORKDIR /mpfr
RUN /mpfr-$MPFR_VERSION/configure \
        --prefix=/deps \
        --host=x86_64-w64-mingw32 \
        --with-gmp-include=/deps/include \
        --with-gmp-lib=/deps/lib \
        --enable-static \
        --disable-shared \
        CFLAGS="-Os" \
        LDFLAGS="-s"
RUN make -j$(nproc)
RUN make install

WORKDIR /
WORKDIR /mpc
RUN /mpc-$MPC_VERSION/configure \
        --prefix=/deps \
        --host=x86_64-w64-mingw32 \
        --with-gmp-include=/deps/include \
        --with-gmp-lib=/deps/lib \
        --with-mpfr-include=/deps/include \
        --with-mpfr-lib=/deps/lib \
        --enable-static \
        --disable-shared \
        CFLAGS="-Os" \
        LDFLAGS="-s"
RUN make -j$(nproc)
RUN make install

WORKDIR /
WORKDIR /mingw-headers
RUN /mingw-w64-v$MINGW_VERSION/mingw-w64-headers/configure \
        --prefix=$PREFIX/x86_64-w64-mingw32 \
        --host=x86_64-w64-mingw32
RUN make -j$(nproc)
RUN make install

WORKDIR /mingw-crt
RUN /mingw-w64-v$MINGW_VERSION/mingw-w64-crt/configure \
        --prefix=$PREFIX/x86_64-w64-mingw32 \
        --with-sysroot=$PREFIX/x86_64-w64-mingw32 \
        --host=x86_64-w64-mingw32 \
        --disable-dependency-tracking \
        --disable-lib32 \
        CFLAGS="-Os" \
        LDFLAGS="-s"
RUN make -j$(nproc)
RUN make install

WORKDIR /
RUN sed -i 's#=/mingw/include#=/include#' gcc-$GCC_VERSION/gcc/config.gcc
WORKDIR /gcc
RUN /gcc-$GCC_VERSION/configure \
        --prefix=$PREFIX \
        --with-sysroot=$PREFIX \
        --target=x86_64-w64-mingw32 \
        --host=x86_64-w64-mingw32 \
        --enable-static \
        --disable-shared \
        --with-gmp-include=/deps/include \
        --with-gmp-lib=/deps/lib \
        --with-mpc-include=/deps/include \
        --with-mpc-lib=/deps/lib \
        --with-mpfr-include=/deps/include \
        --with-mpfr-lib=/deps/lib \
        --enable-languages=c,c++ \
        --enable-libgomp \
        --enable-threads=posix \
        --enable-version-specific-runtime-libs \
        --disable-dependency-tracking \
        --disable-multilib \
        --disable-nls \
        --enable-mingw-wildcard \
        CFLAGS="-Os" \
        CXXFLAGS="-Os" \
        LDFLAGS="-s"
RUN make -j$(nproc)
RUN make install
RUN rm -rf $PREFIX/x86_64-w64-mingw32/bin/ $PREFIX/bin/x86_64-w64-mingw32-* \
        $PREFIX/bin/ld.bfd.exe $PREFIX/bin/c++.exe
RUN x86_64-w64-mingw32-gcc -DEXE=g++.exe -DCMD=c++ \
        -s -Os -nostdlib -ffreestanding -o $PREFIX/bin/c++.exe \
        $PREFIX/src/alias.c -lkernel32

WORKDIR /winpthreads
RUN /mingw-w64-v$MINGW_VERSION/mingw-w64-libraries/winpthreads/configure \
        --prefix=$PREFIX/x86_64-w64-mingw32 \
        --with-sysroot=$PREFIX/x86_64-w64-mingw32 \
        --host=x86_64-w64-mingw32 \
        --enable-static \
        --disable-shared \
        CFLAGS="-Os" \
        LDFLAGS="-s"
RUN make -j$(nproc)
RUN make install

RUN x86_64-w64-mingw32-gcc -DEXE=gcc.exe -DCMD=cc \
        -s -Os -nostdlib -ffreestanding -o $PREFIX/bin/cc.exe \
        $PREFIX/src/alias.c -lkernel32
RUN x86_64-w64-mingw32-gcc -DEXE=gcc.exe -DCMD="cc -std=c99" \
        -s -Os -nostdlib -ffreestanding -o $PREFIX/bin/c99.exe \
        $PREFIX/src/alias.c -lkernel32

# Enable non-broken, standards-compliant formatted output by default
RUN sed -i '1s/^/#ifndef __USE_MINGW_ANSI_STDIO\n#  define __USE_MINGW_ANSI_STDIO 1\n#endif\n/' \
        $PREFIX/x86_64-w64-mingw32/include/_mingw.h

# Build some extra development tools

WORKDIR /gdb
RUN /gdb-$GDB_VERSION/configure \
        --host=x86_64-w64-mingw32 \
        CFLAGS="-Os" \
        CXXFLAGS="-Os" \
        LDFLAGS="-s"
RUN make -j$(nproc)
RUN cp gdb/gdb.exe $PREFIX/bin/

WORKDIR /make
RUN /make-$MAKE_VERSION/configure \
        --host=x86_64-w64-mingw32 \
        --disable-nls \
        CFLAGS="-I/make-$MAKE_VERSION/glob -Os" \
        LDFLAGS="-s"
RUN make -j$(nproc)
RUN cp make.exe $PREFIX/bin/
RUN x86_64-w64-mingw32-gcc -DEXE=make.exe -DCMD=make \
        -s -Os -nostdlib -ffreestanding -o $PREFIX/bin/mingw32-make.exe \
        $PREFIX/src/alias.c -lkernel32

WORKDIR /busybox-w32
RUN make mingw64_defconfig
RUN sed -ri 's/^(CONFIG_(XXD|AR|STRINGS|DPKG\w*|TEST2|RPM\w*|VI|FTP\w*))=y/\1=n/' \
        .config
RUN sed -i '/\\007/d' libbb/lineedit.c
RUN make -j$(nproc)
RUN cp busybox.exe $PREFIX/bin/

# Create BusyBox command aliases (like "busybox --install")
RUN printf '%s\n' arch ash awk base32 base64 basename bash bunzip2 bzcat \
      bzip2 cal cat chattr chmod cksum clear cmp comm cp cpio cut date dc \
      dd df diff dirname dos2unix du echo ed egrep env expand expr factor \
      false fgrep find fold fsync getopt grep groups gunzip gzip hd head \
      hexdump httpd iconv id inotifyd install ipcalc kill killall less link \
      ln logname ls lsattr lzcat lzma lzop lzopcat man md5sum mkdir mktemp \
      mv nc nl od paste patch pgrep pidof pipe_progress pkill printenv \
      printf ps pwd readlink realpath reset rev rm rmdir sed seq sh sha1sum \
      sha256sum sha3sum sha512sum shred shuf sleep sort split ssl_client \
      stat su sum tac tail tar tee test time timeout touch tr true truncate \
      ts ttysize uname uncompress unexpand uniq unix2dos unlink unlzma \
      unlzop unxz unzip usleep uudecode uuencode watch wc wget which whoami \
      whois xargs xz xzcat yes zcat \
    | xargs -I{} -P$(nproc) \
          x86_64-w64-mingw32-gcc -DEXE=busybox.exe -DCMD={} \
            -s -Os -nostdlib -ffreestanding -o $PREFIX/bin/{}.exe \
            $PREFIX/src/alias.c -lkernel32

# TODO: Either somehow use $VIM_VERSION or normalize the workdir
WORKDIR /vim82/src
COPY src/vim-markdown-italics.patch $PREFIX/src/
RUN patch -d.. -p1 <$PREFIX/src/vim-markdown-italics.patch
RUN make -j$(nproc) -f Make_ming.mak \
        ARCH=x86-64 OPTIMIZE=SIZE STATIC_STDCPLUS=yes HAS_GCC_EH=no \
        UNDER_CYGWIN=yes CROSS=yes CROSS_COMPILE=x86_64-w64-mingw32- \
        FEATURES=HUGE OLE=no IME=no NETBEANS=no WINDRES_FLAGS=
RUN make -j$(nproc) -f Make_ming.mak \
        ARCH=x86-64 OPTIMIZE=SIZE STATIC_STDCPLUS=yes HAS_GCC_EH=no \
        UNDER_CYGWIN=yes CROSS=yes CROSS_COMPILE=x86_64-w64-mingw32- \
        FEATURES=HUGE OLE=no IME=no NETBEANS=no WINDRES_FLAGS= \
        GUI=no vim.exe
RUN rm -rf ../runtime/tutor/tutor.*
RUN cp -r ../runtime $PREFIX/share/vim
RUN cp gvim.exe vim.exe $PREFIX/share/vim/
RUN cp vimrun.exe xxd/xxd.exe $PREFIX/bin
RUN printf '@set SHELL=\r\n@"%%~dp0/../share/vim/gvim.exe" %%*\r\n' \
        >$PREFIX/bin/gvim.bat
RUN printf '@set SHELL=\r\n@"%%~dp0/../share/vim/vim.exe" %%*\r\n' \
        >$PREFIX/bin/vim.bat
RUN printf '@set SHELL=\r\n@"%%~dp0/../share/vim/vim.exe" %%*\r\n' \
        >$PREFIX/bin/vi.bat
RUN printf '@vim -N -u NONE "+read %s" "+write" "%s"\r\n' \
        '$VIMRUNTIME/tutor/tutor' '%TMP%/tutor%RANDOM%' \
        >$PREFIX/bin/vimtutor.bat

# NOTE: nasm's configure script is broken, so no out-of-source build
WORKDIR /nasm-$NASM_VERSION
RUN ./configure \
        --host=x86_64-w64-mingw32 \
        CFLAGS="-Os" \
        LDFLAGS="-s"
RUN make -j$(nproc)
RUN cp nasm.exe ndisasm.exe $PREFIX/bin

WORKDIR /ctags-master
RUN make -j$(nproc) -f mk_mingw.mak CC=gcc packcc.exe
RUN make -j$(nproc) -f mk_mingw.mak \
        CC=x86_64-w64-mingw32-gcc WINDRES=x86_64-w64-mingw32-windres \
        OPT= CFLAGS=-Os LDFLAGS=-s
RUN cp ctags.exe $PREFIX/bin/

# Pack up a release

WORKDIR /
RUN rm -rf $PREFIX/share/man/ $PREFIX/share/info/ $PREFIX/share/gcc-*
COPY README.md Dockerfile $PREFIX/
RUN cp /mingw-w64-v$MINGW_VERSION/COPYING.MinGW-w64-runtime/COPYING.MinGW-w64-runtime.txt \
        $PREFIX/
RUN printf "\n===========\nwinpthreads\n===========\n\n" \
        >>$PREFIX/COPYING.MinGW-w64-runtime.txt .
RUN cat /mingw-w64-v$MINGW_VERSION/mingw-w64-libraries/winpthreads/COPYING \
        >>$PREFIX/COPYING.MinGW-w64-runtime.txt
RUN printf '@set PATH=%%~dp0bin;%%PATH%%\r\n@busybox sh -l\r\n' \
        >$PREFIX/activate.bat
RUN echo $VERSION >$PREFIX/VERSION.txt
ENV PREFIX=${PREFIX}
CMD zip -qXr - $PREFIX
