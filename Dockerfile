FROM debian:buster-slim

ARG VERSION=1.0.1
ARG PREFIX=/w64devkit-$VERSION

ARG BINUTILS_VERSION=2.34
ARG BUSYBOX_VERSION=FRP-3445-g10e14d5eb
ARG GCC_VERSION=10.1.0
ARG GMP_VERSION=6.2.0
ARG MAKE_VERSION=4.2
ARG MINGW_VERSION=6.0.0
ARG MPC_VERSION=1.1.0
ARG MPFR_VERSION=4.0.2
ARG NASM_VERSION=2.14.02
ARG VIM_VERSION=8.1

RUN apt-get update && apt-get install --yes --no-install-recommends \
  build-essential curl file libgmp-dev libmpc-dev libmpfr-dev m4 texinfo zip

# Download, verify, and unpack

RUN curl --insecure --location --remote-name-all \
    https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VERSION.tar.xz \
    https://ftp.gnu.org/gnu/gcc/gcc-10.1.0/gcc-$GCC_VERSION.tar.xz \
    https://ftp.gnu.org/gnu/gmp/gmp-$GMP_VERSION.tar.xz \
    https://ftp.gnu.org/gnu/mpc/mpc-$MPC_VERSION.tar.gz \
    https://ftp.gnu.org/gnu/mpfr/mpfr-$MPFR_VERSION.tar.xz \
    https://ftp.gnu.org/gnu/make/make-$MAKE_VERSION.tar.gz \
    https://frippery.org/files/busybox/busybox-w32-$BUSYBOX_VERSION.tgz \
    http://ftp.vim.org/pub/vim/unix/vim-$VIM_VERSION.tar.bz2 \
    https://www.nasm.us/pub/nasm/releasebuilds/2.14.02/nasm-$NASM_VERSION.tar.xz \
    https://downloads.sourceforge.net/project/mingw-w64/mingw-w64/mingw-w64-release/mingw-w64-v$MINGW_VERSION.tar.bz2
COPY SHA256SUMS .
RUN sha256sum -c SHA256SUMS \
 && tar xJf binutils-$BINUTILS_VERSION.tar.xz \
 && tar xzf busybox-w32-$BUSYBOX_VERSION.tgz \
 && tar xJf gcc-$GCC_VERSION.tar.xz \
 && tar xJf gmp-$GMP_VERSION.tar.xz \
 && tar xzf mpc-$MPC_VERSION.tar.gz \
 && tar xJf mpfr-$MPFR_VERSION.tar.xz \
 && tar xzf make-$MAKE_VERSION.tar.gz \
 && tar xjf mingw-w64-v$MINGW_VERSION.tar.bz2 \
 && tar xJf nasm-$NASM_VERSION.tar.xz \
 && tar xjf vim-$VIM_VERSION.tar.bz2

# Build cross-compiler

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
        --enable-threads=win32 \
        --enable-version-specific-runtime-libs \
        --disable-nls \
        --disable-multilib
RUN make -j$(nproc) all-gcc
RUN make install-gcc

ENV PATH="/bootstrap/bin:${PATH}"

WORKDIR /x-mingw-crt
RUN /mingw-w64-v$MINGW_VERSION/mingw-w64-crt/configure \
        --prefix=/bootstrap/x86_64-w64-mingw32 \
        --with-sysroot=/bootstrap/x86_64-w64-mingw32 \
        --host=x86_64-w64-mingw32
RUN make -j$(nproc)
RUN make install

WORKDIR /x-winpthreads
RUN /mingw-w64-v$MINGW_VERSION/mingw-w64-libraries/winpthreads/configure \
        --prefix=/bootstrap/x86_64-w64-mingw32 \
        --with-sysroot=/bootstrap/x86_64-w64-mingw32 \
        --host=x86_64-w64-mingw32 \
        --enable-static \
        --disable-shared
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
        --enable-threads=win32 \
        --enable-version-specific-runtime-libs \
        --disable-multilib \
        --disable-nls \
        CFLAGS="-Os" \
        LDFLAGS="-s"
RUN make -j$(nproc)
RUN make install

WORKDIR /winpthreads
RUN /mingw-w64-v$MINGW_VERSION/mingw-w64-libraries/winpthreads/configure \
        --prefix=$PREFIX/x86_64-w64-mingw32 \
        --with-sysroot=$PREFIX/x86_64-w64-mingw32 \
        --host=x86_64-w64-mingw32 \
        --enable-static \
        --disable-shared
RUN make -j$(nproc)
RUN make install

RUN echo '@"%~dp0/gcc.exe" %*' >$PREFIX/bin/cc.bat

# Build some extra development tools

WORKDIR /make
RUN /make-$MAKE_VERSION/configure \
        --host=x86_64-w64-mingw32 \
        --disable-nls \
        CFLAGS="-I/make-$MAKE_VERSION/glob -Os" \
        LDFLAGS="-s"
RUN make -j$(nproc)
RUN cp make.exe $PREFIX/bin/

WORKDIR /busybox-w32
RUN make mingw64_defconfig
RUN sed -i '/\\007/d' libbb/lineedit.c
RUN make -j$(nproc)
RUN cp busybox.exe $PREFIX/bin/

# TODO: Either somehow use $VIM_VERSION or normalize the workdir
WORKDIR /vim81/src
RUN make -j$(nproc) -f Make_ming.mak \
        ARCH=x86-64 OPTIMIZE=SIZE STATIC_STDCPLUS=yes \
        UNDER_CYGWIN=yes CROSS=yes CROSS_COMPILE=x86_64-w64-mingw32- \
        FEATURES=HUGE OLE=no IME=no NETBEANS=no
RUN make -j$(nproc) -f Make_ming.mak \
        ARCH=x86-64 OPTIMIZE=SIZE STATIC_STDCPLUS=yes \
        UNDER_CYGWIN=yes CROSS=yes CROSS_COMPILE=x86_64-w64-mingw32- \
        FEATURES=HUGE OLE=no IME=no NETBEANS=no \
        GUI=no vim.exe
RUN cp -r ../runtime $PREFIX/share/vim
RUN cp gvim.exe vim.exe $PREFIX/share/vim/
RUN cp vimrun.exe xxd/xxd.exe $PREFIX/bin
RUN echo '@"%~dp0/../share/vim/gvim.exe" %*' >$PREFIX/bin/gvim.bat
RUN echo '@"%~dp0/../share/vim/vim.exe" %*' >$PREFIX/bin/vim.bat

# NOTE: nasm's configure script is broken, so no out-of-source build
WORKDIR /nasm-$NASM_VERSION
RUN ./configure \
        --host=x86_64-w64-mingw32 \
        CFLAGS="-Os" \
        LDFLAGS="-s"
RUN make -j$(nproc)
RUN cp nasm.exe ndisasm.exe $PREFIX/bin

# Pack up a release

WORKDIR /
COPY README.md Dockerfile SHA256SUMS $PREFIX/
ENV PREFIX=${PREFIX}
CMD zip -q9Xr - $PREFIX
