ARG VERSION=2.6.0 \
    PREFIX=/w64devkit

FROM debian:trixie-slim AS base
ARG PREFIX
ENV PREFIX=$PREFIX

RUN apt-get update && apt-get install --yes --no-install-recommends \
  build-essential cmake curl libgmp-dev libmpc-dev libmpfr-dev m4 p7zip-full

COPY src/w64devkit.ico src/alias.c $PREFIX/src/

# Download stages: each independently downloads, verifies, and unpacks its
# own sources into /dl/. Version+hash ARGs are local to each stage so that
# changing one dependency only invalidates its own download stage cache.
# Source directories are normalized (no version in the directory name).

FROM base AS dl-cross
ARG BINUTILS_VERSION=2.46.0 \
    BINUTILS_SHA256=d75a94f4d73e7a4086f7513e67e439e8fcdcbb726ffe63f4661744e6256b2cf2 \
    GCC_VERSION=15.2.0 \
    GCC_SHA256=438fd996826b0c82485a29da03a72d71d6e3541a83ec702df4271f6fe025d24e \
    GMP_VERSION=6.3.0 \
    GMP_SHA256=a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898 \
    MINGW_VERSION=13.0.0 \
    MINGW_SHA256=5afe822af5c4edbf67daaf45eec61d538f49eef6b19524de64897c6b95828caf \
    MPC_VERSION=1.3.1 \
    MPC_SHA256=ab642492f5cf882b74aa0cb730cd410a81edcdbec895183ce930e706c1c759b8 \
    MPFR_VERSION=4.2.2 \
    MPFR_SHA256=b67ba0383ef7e8a8563734e2e889ef5ec3c3b898a01d00fa0a6869ad81c6ce01
WORKDIR /dl
RUN curl --insecure --location --remote-name-all --remote-header-name \
    https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VERSION.tar.xz \
    https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.xz \
    https://ftp.gnu.org/gnu/gmp/gmp-$GMP_VERSION.tar.xz \
    https://ftp.gnu.org/gnu/mpc/mpc-$MPC_VERSION.tar.gz \
    https://ftp.gnu.org/gnu/mpfr/mpfr-$MPFR_VERSION.tar.xz \
    https://downloads.sourceforge.net/project/mingw-w64/mingw-w64/mingw-w64-release/mingw-w64-v$MINGW_VERSION.tar.bz2 \
 && printf '%s  %s\n' \
      $BINUTILS_SHA256 binutils-$BINUTILS_VERSION.tar.xz \
      $GCC_SHA256 gcc-$GCC_VERSION.tar.xz \
      $GMP_SHA256 gmp-$GMP_VERSION.tar.xz \
      $MPC_SHA256 mpc-$MPC_VERSION.tar.gz \
      $MPFR_SHA256 mpfr-$MPFR_VERSION.tar.xz \
      $MINGW_SHA256 mingw-w64-v$MINGW_VERSION.tar.bz2 \
    | sha256sum -c \
 && mkdir binutils \
 && tar xJf binutils-$BINUTILS_VERSION.tar.xz -C binutils --strip-components=1 \
 && mkdir gcc \
 && tar xJf gcc-$GCC_VERSION.tar.xz -C gcc --strip-components=1 \
 && mkdir gmp \
 && tar xJf gmp-$GMP_VERSION.tar.xz -C gmp --strip-components=1 \
 && mkdir mpc \
 && tar xzf mpc-$MPC_VERSION.tar.gz -C mpc --strip-components=1 \
 && mkdir mpfr \
 && tar xJf mpfr-$MPFR_VERSION.tar.xz -C mpfr --strip-components=1 \
 && mkdir mingw \
 && tar xjf mingw-w64-v$MINGW_VERSION.tar.bz2 -C mingw --strip-components=1

FROM base AS dl-gdb
ARG GDB_VERSION=17.1 \
    GDB_SHA256=14996f5f74c9f68f5a543fdc45bca7800207f91f92aeea6c2e791822c7c6d876 \
    EXPAT_VERSION=2.7.4 \
    EXPAT_SHA256=9e9cabb457c1e09de91db2706d8365645792638eb3be1f94dbb2149301086ac0 \
    LIBICONV_VERSION=1.19 \
    LIBICONV_SHA256=88dd96a8c0464eca144fc791ae60cd31cd8ee78321e67397e25fc095c4a19aa6
WORKDIR /dl
RUN curl --insecure --location --remote-name-all --remote-header-name \
    https://ftp.gnu.org/gnu/gdb/gdb-$GDB_VERSION.tar.xz \
    https://github.com/libexpat/libexpat/releases/download/R_$(echo $EXPAT_VERSION | tr . _)/expat-$EXPAT_VERSION.tar.xz \
    https://ftp.gnu.org/gnu/libiconv/libiconv-$LIBICONV_VERSION.tar.gz \
 && printf '%s  %s\n' \
      $GDB_SHA256 gdb-$GDB_VERSION.tar.xz \
      $EXPAT_SHA256 expat-$EXPAT_VERSION.tar.xz \
      $LIBICONV_SHA256 libiconv-$LIBICONV_VERSION.tar.gz \
    | sha256sum -c \
 && mkdir gdb \
 && tar xJf gdb-$GDB_VERSION.tar.xz -C gdb --strip-components=1 \
 && mkdir expat \
 && tar xJf expat-$EXPAT_VERSION.tar.xz -C expat --strip-components=1 \
 && mkdir libiconv \
 && tar xzf libiconv-$LIBICONV_VERSION.tar.gz -C libiconv --strip-components=1

FROM base AS dl-ncurses
ARG NCURSES_VERSION=6.6 \
    NCURSES_SHA256=355b4cbbed880b0381a04c46617b7656e362585d52e9cf84a67e2009b749ff11
WORKDIR /dl
RUN curl --insecure --location --remote-name-all --remote-header-name \
    https://invisible-island.net/archives/ncurses/ncurses-$NCURSES_VERSION.tar.gz \
 && printf '%s  %s\n' $NCURSES_SHA256 ncurses-$NCURSES_VERSION.tar.gz \
    | sha256sum -c \
 && mkdir ncurses \
 && tar xzf ncurses-$NCURSES_VERSION.tar.gz -C ncurses --strip-components=1

FROM base AS dl-make
ARG MAKE_VERSION=4.4.1 \
    MAKE_SHA256=dd16fb1d67bfab79a72f5e8390735c49e3e8e70b4945a15ab1f81ddb78658fb3
WORKDIR /dl
RUN curl --insecure --location --remote-name-all --remote-header-name \
    https://ftp.gnu.org/gnu/make/make-$MAKE_VERSION.tar.gz \
 && printf '%s  %s\n' $MAKE_SHA256 make-$MAKE_VERSION.tar.gz | sha256sum -c \
 && mkdir make \
 && tar xzf make-$MAKE_VERSION.tar.gz -C make --strip-components=1

FROM base AS dl-busybox
ARG BUSYBOX_VERSION=FRP-5857-g3681e397f \
    BUSYBOX_SHA256=3a1b3ecc813036d1be42aa71e8c4da9e2c8d9d1d6203d99e48a831b7a6647145
WORKDIR /dl
RUN curl --insecure --location --remote-name-all --remote-header-name \
    https://frippery.org/files/busybox/busybox-w32-$BUSYBOX_VERSION.tgz \
 && printf '%s  %s\n' $BUSYBOX_SHA256 busybox-w32-$BUSYBOX_VERSION.tgz \
    | sha256sum -c \
 && mkdir busybox \
 && tar xzf busybox-w32-$BUSYBOX_VERSION.tgz -C busybox --strip-components=1

FROM base AS dl-vim
ARG VIM_VERSION=9.0 \
    VIM_SHA256=a6456bc154999d83d0c20d968ac7ba6e7df0d02f3cb6427fb248660bacfb336e
WORKDIR /dl
RUN curl --insecure --location --remote-name-all --remote-header-name \
    https://mirror.math.princeton.edu/pub/vim/unix/vim-$VIM_VERSION.tar.bz2 \
 && printf '%s  %s\n' $VIM_SHA256 vim-$VIM_VERSION.tar.bz2 | sha256sum -c \
 && mkdir vim \
 && tar xjf vim-$VIM_VERSION.tar.bz2 -C vim --strip-components=1

FROM base AS dl-ctags
ARG CTAGS_VERSION=6.2.1 \
    CTAGS_SHA256=f56829e9a576025e98955597ee967099a871987b3476fbd8dbbc2b9dc921f824
WORKDIR /dl
RUN curl --insecure --location --remote-name-all --remote-header-name \
    https://github.com/universal-ctags/ctags/archive/refs/tags/v$CTAGS_VERSION.tar.gz \
 && printf '%s  %s\n' $CTAGS_SHA256 ctags-$CTAGS_VERSION.tar.gz | sha256sum -c \
 && mkdir ctags \
 && tar xzf ctags-$CTAGS_VERSION.tar.gz -C ctags --strip-components=1

FROM base AS dl-ccache
ARG CCACHE_VERSION=4.13.1 \
    CCACHE_SHA256=85638df95c4d3907d9dd686583f2e0b2bd4c232d36e025a5c48e91524b491c4b \
    XXHASH_VERSION=0.8.3 \
    XXHASH_SHA256=aae608dfe8213dfd05d909a57718ef82f30722c392344583d3f39050c7f29a80 \
    ZSTD_VERSION=1.5.7 \
    ZSTD_SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
WORKDIR /dl
RUN curl --insecure --location --remote-name-all --remote-header-name \
    https://github.com/ccache/ccache/releases/download/v$CCACHE_VERSION/ccache-$CCACHE_VERSION.tar.xz \
    https://github.com/Cyan4973/xxhash/archive/refs/tags/v$XXHASH_VERSION.tar.gz \
    https://github.com/facebook/zstd/releases/download/v$ZSTD_VERSION/zstd-$ZSTD_VERSION.tar.gz \
 && printf '%s  %s\n' \
      $CCACHE_SHA256 ccache-$CCACHE_VERSION.tar.xz \
      $XXHASH_SHA256 xxHash-$XXHASH_VERSION.tar.gz \
      $ZSTD_SHA256 zstd-$ZSTD_VERSION.tar.gz \
    | sha256sum -c \
 && mkdir ccache \
 && tar xJf ccache-$CCACHE_VERSION.tar.xz -C ccache --strip-components=1 \
 && mkdir xxhash \
 && tar xzf xxHash-$XXHASH_VERSION.tar.gz -C xxhash --strip-components=1 \
 && mkdir zstd \
 && tar xzf zstd-$ZSTD_VERSION.tar.gz -C zstd --strip-components=1

FROM base AS dl-ninja
ARG NINJA_VERSION=1.13.2 \
    NINJA_SHA256=974d6b2f4eeefa25625d34da3cb36bdcebe7fbce40f4c16ac0835fd1c0cbae17
WORKDIR /dl
RUN curl --insecure --location --remote-name-all --remote-header-name \
    https://github.com/ninja-build/ninja/archive/refs/tags/v$NINJA_VERSION.tar.gz \
 && printf '%s  %s\n' $NINJA_SHA256 ninja-$NINJA_VERSION.tar.gz | sha256sum -c \
 && mkdir ninja \
 && tar xzf ninja-$NINJA_VERSION.tar.gz -C ninja --strip-components=1

FROM base AS dl-cmake
ARG CMAKE_VERSION=4.2.3 \
    CMAKE_SHA256=7efaccde8c5a6b2968bad6ce0fe60e19b6e10701a12fce948c2bf79bac8a11e9
WORKDIR /dl
RUN curl --insecure --location --remote-name-all --remote-header-name \
    https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION.tar.gz \
 && printf '%s  %s\n' $CMAKE_SHA256 cmake-$CMAKE_VERSION.tar.gz | sha256sum -c \
 && mkdir cmake \
 && tar xzf cmake-$CMAKE_VERSION.tar.gz -C cmake --strip-components=1

FROM base AS dl-7z
ARG Z7_VERSION=2301 \
    Z7_SHA256=356071007360e5a1824d9904993e8b2480b51b570e8c9faf7c0f58ebe4bf9f74
WORKDIR /dl
RUN curl --insecure --location --remote-name-all --remote-header-name \
    https://downloads.sourceforge.net/project/sevenzip/7-Zip/23.01/7z$Z7_VERSION-src.tar.xz \
 && printf '%s  %s\n' $Z7_SHA256 7z$Z7_VERSION-src.tar.xz | sha256sum -c \
 && mkdir 7z \
 && tar xJf 7z$Z7_VERSION-src.tar.xz -C 7z

# Build cross-compiler

FROM dl-cross AS cross
ARG ARCH=x86_64-w64-mingw32
ENV ARCH=$ARCH

WORKDIR /dl/binutils
COPY src/binutils-*.patch $PREFIX/src/
RUN sed -ri 's/(static bool insert_timestamp = )/\1!/' ld/emultempl/pe*.em \
 && sed -ri 's/(int pe_enable_stdcall_fixup = )/\1!!/' ld/emultempl/pe*.em \
 && sed -ri 's/(static int use_big_obj = )/\1!/' gas/config/tc-i386.c \
 && cat $PREFIX/src/binutils-*.patch | patch -p1
WORKDIR /x-binutils
RUN /dl/binutils/configure \
        --prefix=/bootstrap \
        --with-sysroot=/bootstrap \
        --target=$ARCH \
        --disable-nls \
        --with-static-standard-libraries \
        --disable-multilib \
 && make MAKEINFO=true -j$(nproc) \
 && make MAKEINFO=true install

# Fixes i686 Windows XP regression
# https://sourceforge.net/p/mingw-w64/bugs/821/
RUN sed -i /OpenThreadToken/d /dl/mingw/mingw-w64-crt/lib32/kernel32.def

WORKDIR /x-mingw-headers
RUN echo '#include <crtdefs.h>' \
      >/dl/mingw/mingw-w64-headers/crt/stddef.h \
 && /dl/mingw/mingw-w64-headers/configure \
        --prefix=/bootstrap \
        --host=$ARCH \
        --with-default-msvcrt=msvcrt-os \
 && make -j$(nproc) \
 && make install

WORKDIR /bootstrap
RUN ln -s /bootstrap mingw

WORKDIR /x-gcc
COPY src/gcc-*.patch $PREFIX/src/
RUN cat $PREFIX/src/gcc-*.patch | patch -d/dl/gcc -p1 \
 && /dl/gcc/configure \
        --prefix=/bootstrap \
        --with-sysroot=/bootstrap \
        --target=$ARCH \
        --enable-static \
        --disable-shared \
        --with-pic \
        --enable-languages=c,c++,fortran \
        --enable-libgomp \
        --enable-threads=posix \
        --enable-version-specific-runtime-libs \
        --disable-libstdcxx-verbose \
        --disable-dependency-tracking \
        --disable-nls \
        --disable-lto \
        --disable-multilib \
        CFLAGS_FOR_TARGET="-O2" \
        CXXFLAGS_FOR_TARGET="-O2" \
        LDFLAGS_FOR_TARGET="-s" \
        CFLAGS="-O2" \
        CXXFLAGS="-O2" \
        LDFLAGS="-s" \
 && make -j$(nproc) all-gcc \
 && make install-gcc

ENV PATH="/bootstrap/bin:${PATH}"

COPY src/libmemory.c src/libchkstk.S $PREFIX/src/
RUN mkdir -p $PREFIX/lib \
 && CC=$ARCH-gcc AR=$ARCH-ar DESTDIR=$PREFIX/lib/ \
        sh $PREFIX/src/libmemory.c \
 && ln $PREFIX/lib/libmemory.a /bootstrap/lib/ \
 && CC=$ARCH-gcc AR=$ARCH-ar DESTDIR=$PREFIX/lib/ \
        sh $PREFIX/src/libchkstk.S \
 && ln $PREFIX/lib/libchkstk.a /bootstrap/lib/

WORKDIR /x-mingw-crt
RUN /dl/mingw/mingw-w64-crt/configure \
        --prefix=/bootstrap \
        --with-sysroot=/bootstrap \
        --host=$ARCH \
        --with-default-msvcrt=msvcrt-os \
        --disable-dependency-tracking \
        --disable-lib32 \
        --enable-lib64 \
        CFLAGS="-O2" \
        LDFLAGS="-s" \
 && make -j$(nproc) \
 && make install

WORKDIR /x-winpthreads
RUN /dl/mingw/mingw-w64-libraries/winpthreads/configure \
        --prefix=/bootstrap \
        --with-sysroot=/bootstrap \
        --host=$ARCH \
        --enable-static \
        --disable-shared \
        CFLAGS="-O2" \
        LDFLAGS="-s" \
 && make -j$(nproc) \
 && make install

WORKDIR /x-gcc
RUN make -j$(nproc) \
 && make install

# Cross-compile GCC

WORKDIR /binutils
RUN /dl/binutils/configure \
        --prefix=$PREFIX \
        --with-sysroot=$PREFIX \
        --host=$ARCH \
        --target=$ARCH \
        --disable-nls \
        --with-static-standard-libraries \
        CFLAGS="-O2" \
        LDFLAGS="-s" \
 && make MAKEINFO=true tooldir=$PREFIX -j$(nproc) \
 && make MAKEINFO=true tooldir=$PREFIX install \
 && rm $PREFIX/bin/elfedit.exe $PREFIX/bin/readelf.exe

WORKDIR /gmp
RUN /dl/gmp/configure \
        --prefix=/deps \
        --host=$ARCH \
        --disable-assembly \
        --enable-static \
        --disable-shared \
        CC=$ARCH-gcc \
        CFLAGS="-std=gnu17 -O2" \
        CXXFLAGS="-O2" \
        LDFLAGS="-s" \
 && make -j$(nproc) \
 && make install

WORKDIR /mpfr
RUN /dl/mpfr/configure \
        --prefix=/deps \
        --host=$ARCH \
        --with-gmp=/deps \
        --enable-static \
        --disable-shared \
        CC=$ARCH-gcc \
        CFLAGS="-O2" \
        LDFLAGS="-s" \
 && make -j$(nproc) \
 && make install

WORKDIR /mpc
RUN /dl/mpc/configure \
        --prefix=/deps \
        --host=$ARCH \
        --with-gmp=/deps \
        --with-mpfr=/deps \
        --enable-static \
        --disable-shared \
        CC=$ARCH-gcc \
        CFLAGS="-O2" \
        LDFLAGS="-s" \
 && make -j$(nproc) \
 && make install

WORKDIR /mingw-headers
RUN /dl/mingw/mingw-w64-headers/configure \
        --prefix=$PREFIX \
        --host=$ARCH \
        --enable-idl \
        --with-default-msvcrt=msvcrt-os \
 && make -j$(nproc) \
 && make install

WORKDIR /mingw-crt
RUN /dl/mingw/mingw-w64-crt/configure \
        --prefix=$PREFIX \
        --with-sysroot=$PREFIX \
        --host=$ARCH \
        --with-default-msvcrt=msvcrt-os \
        --disable-dependency-tracking \
        --disable-lib32 \
        --enable-lib64 \
        CFLAGS="-O2" \
        LDFLAGS="-s" \
 && make -j$(nproc) \
 && make install

WORKDIR /winpthreads
RUN /dl/mingw/mingw-w64-libraries/winpthreads/configure \
        --prefix=$PREFIX \
        --with-sysroot=$PREFIX \
        --host=$ARCH \
        --enable-static \
        --disable-shared \
        CFLAGS="-O2" \
        LDFLAGS="-s" \
 && make -j$(nproc) \
 && make install

WORKDIR /gcc
COPY src/crossgcc-*.patch $PREFIX/src/
RUN echo 'BEGIN {print "pecoff"}' \
         >/dl/gcc/libbacktrace/filetype.awk \
 && cat $PREFIX/src/crossgcc-*.patch | patch -d/dl/gcc -p1 \
 && /dl/gcc/configure \
        --prefix=$PREFIX \
        --with-sysroot=$PREFIX \
        --with-native-system-header-dir=/include \
        --target=$ARCH \
        --host=$ARCH \
        --enable-static \
        --disable-shared \
        --with-pic \
        --with-gmp=/deps \
        --with-mpc=/deps \
        --with-mpfr=/deps \
        --enable-languages=c,c++,fortran \
        --enable-libgomp \
        --enable-threads=posix \
        --enable-version-specific-runtime-libs \
        --disable-libstdcxx-verbose \
        --disable-dependency-tracking \
        --disable-lto \
        --disable-multilib \
        --disable-nls \
        --disable-win32-registry \
        --enable-mingw-wildcard \
        CFLAGS_FOR_TARGET="-O2" \
        CXXFLAGS_FOR_TARGET="-O2" \
        LDFLAGS_FOR_TARGET="-s" \
        CFLAGS="-O2" \
        CXXFLAGS="-O2" \
        LDFLAGS="-s" \
 && make -j$(nproc) \
 && make install \
 && rm -f $PREFIX/bin/ld.bfd.exe \
 && $ARCH-gcc -DEXE=g++.exe -DCMD=c++ \
        -Oz -fno-asynchronous-unwind-tables \
        -Wl,--gc-sections -s -nostdlib \
        -o $PREFIX/bin/c++.exe \
        $PREFIX/src/alias.c -lkernel32

# Create various tool aliases
RUN $ARCH-gcc -DEXE=gcc.exe -DCMD=cc \
        -Oz -fno-asynchronous-unwind-tables -Wl,--gc-sections -s -nostdlib \
        -o $PREFIX/bin/cc.exe $PREFIX/src/alias.c -lkernel32 \
 && $ARCH-gcc -DEXE=gcc.exe -DCMD="cc -std=c99" \
        -Oz -fno-asynchronous-unwind-tables -Wl,--gc-sections -s -nostdlib \
        -o $PREFIX/bin/c99.exe $PREFIX/src/alias.c -lkernel32 \
 && $ARCH-gcc -DEXE=gcc.exe -DCMD="cc -ansi" \
        -Oz -fno-asynchronous-unwind-tables -Wl,--gc-sections -s -nostdlib \
        -o $PREFIX/bin/c89.exe $PREFIX/src/alias.c -lkernel32 \
 && printf '%s\n' addr2line ar as c++filt cpp dlltool dllwrap elfedit g++ \
      gcc gcc-ar gcc-nm gcc-ranlib gcov gcov-dump gcov-tool gendef gfortran \
      ld nm objcopy objdump ranlib readelf size strings strip uuidgen widl \
      windmc windres \
    | xargs -I{} -P$(nproc) \
          $ARCH-gcc -DEXE={}.exe -DCMD=$ARCH-{} \
            -Oz -fno-asynchronous-unwind-tables \
            -Wl,--gc-sections -s -nostdlib \
            -o $PREFIX/bin/$ARCH-{}.exe $PREFIX/src/alias.c -lkernel32

# Build some extra development tools

FROM cross AS build-gendef

WORKDIR /mingw-tools/gendef
COPY src/gendef-*.patch $PREFIX/src/
RUN cat $PREFIX/src/gendef-*.patch | patch -d/dl/mingw -p1 \
 && /dl/mingw/mingw-w64-tools/gendef/configure \
        --host=$ARCH \
        CFLAGS="-O2" \
        LDFLAGS="-s" \
 && make -j$(nproc) \
 && mkdir -p /out/bin \
 && cp gendef.exe /out/bin/

WORKDIR /dl/mingw/mingw-w64-tools/widl
COPY src/uuidgen.c $PREFIX/src/
RUN ./configure \
        --host=$ARCH \
        --prefix=$PREFIX \
        --with-widl-includedir=$PREFIX/include \
        CFLAGS="-O2" \
        LDFLAGS="-s" \
 && make -j$(nproc) \
 && cp widl.exe /out/bin/ \
 && $ARCH-gcc -nostartfiles -Oz -s -o /out/bin/uuidgen.exe \
        $PREFIX/src/uuidgen.c -lmemory

# Build ncurses once and reuse it for both gdb and ccmake.
FROM cross AS build-ncurses
COPY --from=dl-ncurses /dl/ncurses /dl/ncurses

WORKDIR /ncurses
RUN /dl/ncurses/configure \
        --host=$ARCH \
        --without-ada \
        --without-cxx-binding \
        --enable-widec \
        --enable-term-driver \
        --enable-sp-funcs \
        --with-fallbacks=ms-terminal \
        --without-progs \
        --without-tests \
        --without-manpages \
        --without-debug \
        --disable-database \
        --disable-shared \
        --prefix=/deps \
        CFLAGS="-O2" \
        LDFLAGS="-s" \
 && make -j$(nproc) \
 && make install \
 && printf 'CREATE /deps/lib/libcurses.a\nADDLIB /deps/lib/libncursesw.a\nADDLIB /deps/lib/libpanelw.a\nSAVE\nEND\n' | $ARCH-ar -M \
 && cp /deps/include/ncursesw/curses.h /deps/include/curses.h

FROM cross AS build-gdb
COPY --from=dl-gdb /dl/ /dl/
COPY --from=build-ncurses /deps/lib/libcurses.a /deps/lib/
COPY --from=build-ncurses /deps/include/ /deps/include/

WORKDIR /expat
RUN /dl/expat/configure \
        --prefix=/deps \
        --host=$ARCH \
        --disable-shared \
        --without-docbook \
        --without-examples \
        --without-tests \
        CFLAGS="-O2" \
        LDFLAGS="-s" \
 && make -j$(nproc) \
 && make install

WORKDIR /libiconv
RUN /dl/libiconv/configure \
        --prefix=/deps \
        --host=$ARCH \
        --disable-nls \
        --disable-shared \
        CFLAGS="-O2" \
        LDFLAGS="-s" \
 && make -j$(nproc) \
 && make install

WORKDIR /gdb
COPY src/gdb-*.patch $PREFIX/src/
RUN cat $PREFIX/src/gdb-*.patch | patch -d/dl/gdb -p1 \
 && sed -i 's/quiet = 0/quiet = 1/' /dl/gdb/gdb/main.c \
 && /dl/gdb/configure \
        --host=$ARCH \
        --enable-tui \
        CFLAGS="-std=gnu17 -O2 -D__MINGW_USE_VC2005_COMPAT -I/deps/include" \
        CXXFLAGS="-O2 -D__MINGW_USE_VC2005_COMPAT -I/deps/include" \
        LDFLAGS="-s -L/deps/lib" \
 && make MAKEINFO=true -j$(nproc) \
 && mkdir -p /out/bin \
 && cp gdb/.libs/gdb.exe gdbserver/gdbserver.exe /out/bin/

FROM cross AS build-make
COPY --from=dl-make /dl/ /dl/

WORKDIR /make
COPY src/make-*.patch $PREFIX/src/
RUN cat $PREFIX/src/make-*.patch | patch -d/dl/make -p1 \
 && /dl/make/configure \
        --host=$ARCH \
        --disable-nls \
        CFLAGS="-std=gnu17 -O2" \
        LDFLAGS="-s" \
 && make -j$(nproc) \
 && mkdir -p /out/bin \
 && cp make.exe /out/bin/ \
 && $ARCH-gcc -DEXE=make.exe -DCMD=make \
        -Oz -fno-asynchronous-unwind-tables \
        -Wl,--gc-sections -s -nostdlib \
        -o /out/bin/mingw32-make.exe $PREFIX/src/alias.c -lkernel32

FROM cross AS build-busybox
COPY --from=dl-busybox /dl/ /dl/

WORKDIR /dl/busybox
COPY src/busybox-* $PREFIX/src/
RUN cat $PREFIX/src/busybox-*.patch | patch -p1 \
 && make mingw64u_defconfig \
 && sed -ri 's/^(CONFIG_AR)=y/\1=n/' .config \
 && sed -ri 's/^(CONFIG_ASCII)=y/\1=n/' .config \
 && sed -ri 's/^(CONFIG_DPKG\w*)=y/\1=n/' .config \
 && sed -ri 's/^(CONFIG_FTP\w*)=y/\1=n/' .config \
 && sed -ri 's/^(CONFIG_LINK)=y/\1=n/' .config \
 && sed -ri 's/^(CONFIG_MAN)=y/\1=n/' .config \
 && sed -ri 's/^(CONFIG_MAKE)=y/\1=n/' .config \
 && sed -ri 's/^(CONFIG_PDPMAKE)=y/\1=n/' .config \
 && sed -ri 's/^(CONFIG_RPM\w*)=y/\1=n/' .config \
 && sed -ri 's/^(CONFIG_STRINGS)=y/\1=n/' .config \
 && sed -ri 's/^(CONFIG_TEST2)=y/\1=n/' .config \
 && sed -ri 's/^(CONFIG_TSORT)=y/\1=n/' .config \
 && sed -ri 's/^(CONFIG_UNLINK)=y/\1=n/' .config \
 && sed -ri 's/^(CONFIG_VI)=y/\1=n/' .config \
 && sed -ri 's/^(CONFIG_XXD)=y/\1=n/' .config \
 && make -j$(nproc) CROSS_COMPILE=$ARCH- \
    CONFIG_EXTRA_CFLAGS="-D_WIN32_WINNT=0x502" \
 && mkdir -p /out/bin \
 && cp busybox.exe /out/bin/

# Create BusyBox command aliases (like "busybox --install")
RUN $ARCH-gcc -Oz -fno-asynchronous-unwind-tables -Wl,--gc-sections -s \
      -nostdlib -o alias.exe $PREFIX/src/busybox-alias.c -lkernel32 \
 && printf '%s\n' arch ash awk base32 base64 basename bash bc bunzip2 bzcat \
      bzip2 cal cat chattr chmod cksum clear cmp comm cp cpio crc32 cut date \
      dc dd df diff dirname dos2unix du echo ed egrep env expand expr factor \
      false fgrep find fold free fsync getopt grep groups gunzip gzip hd \
      head hexdump httpd iconv id inotifyd install ipcalc jn kill killall \
      lash less ln logname ls lsattr lzcat lzma lzop lzopcat md5sum mkdir \
      mktemp mv nc nl nproc od paste patch pgrep pidof pipe_progress pkill \
      printenv printf ps pwd readlink realpath reset rev rm rmdir sed seq sh \
      sha1sum sha256sum sha3sum sha512sum shred shuf sleep sort split \
      ssl_client stat su sum sync tac tail tar tee test time timeout touch \
      tr true truncate ts ttysize uname uncompress unexpand uniq unix2dos \
      unlzma unlzop unxz unzip uptime usleep uudecode uuencode watch \
      wc wget which whoami whois xargs xz xzcat yes zcat \
    | xargs -I{} cp alias.exe /out/bin/{}.exe

FROM cross AS build-vim
COPY --from=dl-vim /dl/ /dl/

WORKDIR /dl/vim
COPY src/rexxd.c src/vim-*.patch $PREFIX/src/
RUN cat $PREFIX/src/vim-*.patch | patch -p1 \
 && ARCH= make -C src -j$(nproc) -f Make_ming.mak CC="$ARCH-gcc -std=gnu17" \
        OPTIMIZE=SPEED STATIC_STDCPLUS=yes HAS_GCC_EH=no \
        UNDER_CYGWIN=yes CROSS=yes CROSS_COMPILE=$ARCH- \
        FEATURES=HUGE VIMDLL=yes NETBEANS=no WINVER=0x0501 \
 && $ARCH-strip src/vimrun.exe \
 && rm -rf runtime/tutor/tutor.* \
 && mkdir -p /out/bin /out/share \
 && cp -r runtime /out/share/vim \
 && cp src/vimrun.exe src/gvim.exe src/vim.exe src/*.dll /out/share/vim/ \
 && printf '@set SHELL=\r\n@start "" "%%~dp0/../share/vim/gvim.exe" %%*\r\n' \
        >/out/bin/gvim.bat \
 && printf '@set SHELL=\r\n@"%%~dp0/../share/vim/vim.exe" %%*\r\n' \
        >/out/bin/vim.bat \
 && printf '@set SHELL=\r\n@"%%~dp0/../share/vim/vim.exe" %%*\r\n' \
        >/out/bin/vi.bat \
 && printf '@vim -N -u NONE "+read %s" "+write" "%s"\r\n' \
        '$VIMRUNTIME/tutor/tutor' '%TMP%/tutor%RANDOM%' \
        >/out/bin/vimtutor.bat \
 && $ARCH-gcc -nostartfiles -O2 -funroll-loops -s -o /out/bin/xxd.exe \
        $PREFIX/src/rexxd.c -lmemory

FROM cross AS build-ctags
COPY --from=dl-ctags /dl/ /dl/

WORKDIR /dl/ctags
RUN sed -i /RT_MANIFEST/d win32/ctags.rc \
 && make -j$(nproc) -f mk_mingw.mak CC=gcc packcc.exe \
 && make -j$(nproc) -f mk_mingw.mak \
        CC=$ARCH-gcc WINDRES=$ARCH-windres \
        OPT= CFLAGS=-O2 LDFLAGS=-s \
 && mkdir -p /out/bin \
 && cp ctags.exe /out/bin/

FROM cross AS build-ccache
COPY --from=dl-ccache /dl/ /dl/

WORKDIR /dl/xxhash
RUN make -j$(nproc) CC=$ARCH-gcc AR=$ARCH-ar CFLAGS="-O2" libxxhash.a \
 && cp libxxhash.a /deps/lib/ \
 && cp xxhash.h /deps/include/

WORKDIR /dl/zstd/lib
RUN make -j$(nproc) CC=$ARCH-gcc AR=$ARCH-ar CFLAGS="-O2" libzstd.a \
 && cp libzstd.a /deps/lib/ \
 && cp zstd.h zstd_errors.h zdict.h /deps/include/

WORKDIR /ccache
RUN cmake -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=Windows \
        -DCMAKE_C_COMPILER=$ARCH-gcc \
        -DCMAKE_CXX_COMPILER=$ARCH-g++ \
        -DCMAKE_RC_COMPILER=$ARCH-windres \
        -DCMAKE_FIND_ROOT_PATH=/deps \
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
        -DCMAKE_EXE_LINKER_FLAGS="-s" \
        -DDEPS=LOCAL \
        -DREDIS_STORAGE_BACKEND=OFF \
        -DHTTP_STORAGE_BACKEND=OFF \
        -DENABLE_TESTING=OFF \
        -DENABLE_DOCUMENTATION=OFF \
        /dl/ccache \
 && make -j$(nproc) \
 && mkdir -p /out/bin /out/lib/ccache \
 && cp ccache.exe /out/bin/

RUN $ARCH-gcc -DEXE=ccache.exe -DCMD=gcc \
        -Oz -fno-asynchronous-unwind-tables -Wl,--gc-sections -s -nostdlib \
        -o /out/bin/ccache-gcc.exe $PREFIX/src/alias.c -lkernel32 \
 && $ARCH-gcc -DEXE=ccache.exe -DCMD=g++ \
        -Oz -fno-asynchronous-unwind-tables -Wl,--gc-sections -s -nostdlib \
        -o /out/bin/ccache-g++.exe $PREFIX/src/alias.c -lkernel32 \
 && printf '%s\n' gcc cc c89 c99 $ARCH-gcc \
    | xargs -I{} -P$(nproc) \
          $ARCH-gcc -DEXE=../../bin/ccache.exe -DCMD=gcc \
            -Oz -fno-asynchronous-unwind-tables \
            -Wl,--gc-sections -s -nostdlib \
            -o /out/lib/ccache/{}.com $PREFIX/src/alias.c -lkernel32 \
 && printf '%s\n' g++ c++ $ARCH-g++ \
    | xargs -I{} -P$(nproc) \
          $ARCH-gcc -DEXE=../../bin/ccache.exe -DCMD=g++ \
            -Oz -fno-asynchronous-unwind-tables \
            -Wl,--gc-sections -s -nostdlib \
            -o /out/lib/ccache/{}.com $PREFIX/src/alias.c -lkernel32

FROM cross AS build-ninja
COPY --from=dl-ninja /dl/ /dl/

WORKDIR /ninja
RUN cmake -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=Windows \
        -DCMAKE_CXX_COMPILER=$ARCH-g++ \
        -DCMAKE_EXE_LINKER_FLAGS="-s" \
        -DBUILD_TESTING=OFF \
        /dl/ninja \
 && make -j$(nproc) \
 && mkdir -p /out/bin \
 && cp ninja.exe /out/bin/

FROM cross AS build-cmake
COPY --from=dl-cmake /dl/ /dl/
COPY --from=build-ncurses /deps/lib/libcurses.a /deps/lib/
COPY --from=build-ncurses /deps/include/ /deps/include/

WORKDIR /cmake
COPY src/cmake-*.patch $PREFIX/src/
RUN cat $PREFIX/src/cmake-*.patch | patch -d/dl/cmake -p1 \
 && cmake -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=Windows \
        -DCMAKE_C_COMPILER=$ARCH-gcc \
        -DCMAKE_CXX_COMPILER=$ARCH-g++ \
        -DCMAKE_RC_COMPILER=$ARCH-windres \
        -DCMAKE_EXE_LINKER_FLAGS="-s" \
        -DCMAKE_INSTALL_PREFIX=$PREFIX \
        -DBUILD_CursesDialog=ON \
        -DCURSES_LIBRARY=/deps/lib/libcurses.a \
        -DCURSES_INCLUDE_PATH=/deps/include \
        -DBUILD_QtDialog=OFF \
        -DBUILD_TESTING=OFF \
        -DCMAKE_USE_OPENSSL=OFF \
        /dl/cmake \
 && make -j$(nproc) \
 && DESTDIR=/out make install \
 && rm -rf /out$PREFIX/doc/ /out$PREFIX/man/

FROM cross AS build-7z
COPY --from=dl-7z /dl/ /dl/

WORKDIR /dl/7z
COPY src/7z.mak $PREFIX/src/
RUN sed -i s/CommCtrl/commctrl/ $(grep -Rl CommCtrl CPP/) \
 && sed -i s%7z\\.ico%$PREFIX/src/w64devkit.ico% \
           CPP/7zip/Bundles/SFXWin/resource.rc \
 && make -f $PREFIX/src/7z.mak -j$(nproc) CROSS=$ARCH-

# Pack up a release

FROM cross AS final
ARG VERSION

COPY --from=build-gendef /out/ $PREFIX/
COPY --from=build-gdb /out/ $PREFIX/
COPY --from=build-make /out/ $PREFIX/
COPY --from=build-busybox /out/ $PREFIX/
COPY --from=build-vim /out/ $PREFIX/
COPY --from=build-ctags /out/ $PREFIX/
COPY --from=build-ccache /out/ $PREFIX/
COPY --from=build-ninja /out/ $PREFIX/
COPY --from=build-cmake /out$PREFIX/ $PREFIX/
COPY --from=build-7z /dl/7z/7z.sfx /7z/

COPY src $PREFIX/src

WORKDIR /
RUN rm -rf $PREFIX/share/man/ $PREFIX/share/info/ $PREFIX/share/gcc-*
COPY README.md Dockerfile w64devkit.ini $PREFIX/
RUN printf "id ICON \"$PREFIX/src/w64devkit.ico\"" >w64devkit.rc \
 && $ARCH-windres -o w64devkit.o w64devkit.rc \
 && $ARCH-gcc -DVERSION=$VERSION -Oz -nostdlib -fno-asynchronous-unwind-tables \
        -fno-builtin -Wl,--gc-sections -s -o $PREFIX/w64devkit.exe \
        $PREFIX/src/w64devkit.c w64devkit.o -lkernel32 -luser32 -lmemory \
 && $ARCH-gcc \
        -Oz -fno-asynchronous-unwind-tables \
        -Wl,--gc-sections -s -nostdlib \
        -o $PREFIX/bin/debugbreak.exe $PREFIX/src/debugbreak.c \
        -lkernel32 \
 && $ARCH-gcc \
        -Oz -fno-asynchronous-unwind-tables -fno-builtin -Wl,--gc-sections \
        -s -nostdlib -o $PREFIX/bin/pkg-config.exe $PREFIX/src/pkg-config.c \
        -lkernel32 \
 && $ARCH-gcc \
        -O2 -fno-asynchronous-unwind-tables -fno-builtin -Wl,--gc-sections \
        -s -nostdlib -o $PREFIX/bin/vc++filt.exe $PREFIX/src/vc++filt.c \
        -lkernel32 -lshell32 -ldbghelp \
 && $ARCH-gcc \
        -O2 -fno-asynchronous-unwind-tables -fno-builtin -Wl,--gc-sections \
        -s -nostdlib -o $PREFIX/bin/peports.exe $PREFIX/src/peports.c \
        -lkernel32 -lshell32 -lmemory \
 && $ARCH-gcc -DEXE=pkg-config.exe -DCMD=pkg-config \
        -Oz -fno-asynchronous-unwind-tables -Wl,--gc-sections -s -nostdlib \
        -o $PREFIX/bin/$ARCH-pkg-config.exe $PREFIX/src/alias.c -lkernel32 \
 && sed -i s/'\<ARCH\>'/$ARCH/g $PREFIX/src/profile \
 && mkdir -p $PREFIX/lib/pkgconfig \
 && cp /dl/mingw/COPYING.MinGW-w64-runtime/COPYING.MinGW-w64-runtime.txt \
        $PREFIX/ \
 && printf "\n===========\nwinpthreads\n===========\n\n" \
        >>$PREFIX/COPYING.MinGW-w64-runtime.txt . \
 && cat /dl/mingw/mingw-w64-libraries/winpthreads/COPYING \
        >>$PREFIX/COPYING.MinGW-w64-runtime.txt \
 && echo $VERSION >$PREFIX/VERSION.txt \
 && 7z a -mx=9 -mtm=- w64devkit.7z $PREFIX
CMD ["cat", "/7z/7z.sfx", "w64devkit.7z"]
