ARG VERSION=2.9.1 \
    PREFIX=/w64devkit
ARG VARIANT=x64

FROM debian:trixie-slim AS base
ARG PREFIX
ENV PREFIX=$PREFIX

RUN apt-get update && apt-get install --yes --no-install-recommends \
  build-essential cmake libgmp-dev libmpc-dev libmpfr-dev m4 p7zip-full \
  python3 quilt scons

COPY src/w64devkit.ico src/alias.c $PREFIX/src/

# Download stages: each independently downloads, verifies, and unpacks its
# own sources into /dl/. Version+hash ARGs are local to each stage so that
# changing one dependency only invalidates its own download stage cache.
# Source directories are normalized (no version in the directory name).

FROM base AS dl-cross
ARG BINUTILS_VERSION=2.47 \
    BINUTILS_SHA256=154ab23b60070e8f27013c22977f1129425d67d1e8acd6e13010e617811e4cff \
    GCC_VERSION=16.2.0 \
    GCC_SHA256=e6738e29597f733270731aa90600f37ffdc045079dfc27ec7e8192cc81085c3e \
    GMP_VERSION=6.3.0 \
    GMP_SHA256=a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898 \
    MINGW_VERSION=14.0.0 \
    MINGW_SHA256=6eaf921d9eb987d3820b364ea9775bc19b965ec81490b6fdd716526c28e1995c \
    MPC_VERSION=1.4.1 \
    MPC_SHA256=91204cd32f164bd3b7c992d4a6a8ce6519511aadab30f78b6982d0bf8d73e931 \
    MPFR_VERSION=4.2.2 \
    MPFR_SHA256=b67ba0383ef7e8a8563734e2e889ef5ec3c3b898a01d00fa0a6869ad81c6ce01
WORKDIR /dl
ADD --checksum=sha256:$BINUTILS_SHA256 \
    https://ftp.wayne.edu/gnu/binutils/binutils-$BINUTILS_VERSION.tar.xz ./
ADD --checksum=sha256:$GCC_SHA256 \
    https://ftp.wayne.edu/gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.xz ./
ADD --checksum=sha256:$GMP_SHA256 \
    https://ftp.wayne.edu/gnu/gmp/gmp-$GMP_VERSION.tar.xz ./
ADD --checksum=sha256:$MPC_SHA256 \
    https://ftp.wayne.edu/gnu/mpc/mpc-$MPC_VERSION.tar.xz ./
ADD --checksum=sha256:$MPFR_SHA256 \
    https://ftp.wayne.edu/gnu/mpfr/mpfr-$MPFR_VERSION.tar.xz ./
ADD --checksum=sha256:$MINGW_SHA256 \
    https://downloads.sourceforge.net/project/mingw-w64/mingw-w64/mingw-w64-release/mingw-w64-v$MINGW_VERSION.tar.bz2 ./
RUN mkdir binutils \
 && tar xJf binutils-$BINUTILS_VERSION.tar.xz -C binutils --strip-components=1 \
 && mkdir gcc \
 && tar xJf gcc-$GCC_VERSION.tar.xz -C gcc --strip-components=1 \
 && mkdir gmp \
 && tar xJf gmp-$GMP_VERSION.tar.xz -C gmp --strip-components=1 \
 && mkdir mpc \
 && tar xJf mpc-$MPC_VERSION.tar.xz -C mpc --strip-components=1 \
 && mkdir mpfr \
 && tar xJf mpfr-$MPFR_VERSION.tar.xz -C mpfr --strip-components=1 \
 && mkdir mingw \
 && tar xjf mingw-w64-v$MINGW_VERSION.tar.bz2 -C mingw --strip-components=1

FROM base AS dl-gdb
ARG GDB_VERSION=17.2 \
    GDB_SHA256=1c036c0d72e4b3d1fb5c94c88632add6f9d76f4d7c4d2ea793c12a9f19a3228c \
    EXPAT_VERSION=2.8.4 \
    EXPAT_TAG=R_2_8_4 \
    EXPAT_SHA256=656ae1cc8da3b4ea513bb4e254f33e6243938084c0ec6239da873376b09985a7 \
    LIBICONV_VERSION=1.19 \
    LIBICONV_SHA256=88dd96a8c0464eca144fc791ae60cd31cd8ee78321e67397e25fc095c4a19aa6
WORKDIR /dl
ADD --checksum=sha256:$GDB_SHA256 \
    https://ftp.wayne.edu/gnu/gdb/gdb-$GDB_VERSION.tar.xz ./
ADD --checksum=sha256:$EXPAT_SHA256 \
    https://github.com/libexpat/libexpat/releases/download/$EXPAT_TAG/expat-$EXPAT_VERSION.tar.xz ./
ADD --checksum=sha256:$LIBICONV_SHA256 \
    https://ftp.wayne.edu/gnu/libiconv/libiconv-$LIBICONV_VERSION.tar.gz ./
RUN mkdir gdb \
 && tar xJf gdb-$GDB_VERSION.tar.xz -C gdb --strip-components=1 \
 && mkdir expat \
 && tar xJf expat-$EXPAT_VERSION.tar.xz -C expat --strip-components=1 \
 && mkdir libiconv \
 && tar xzf libiconv-$LIBICONV_VERSION.tar.gz -C libiconv --strip-components=1

FROM base AS dl-pdcurses
ARG PDCURSES_VERSION=3.9 \
    PDCURSES_SHA256=590dbe0f5835f66992df096d3602d0271103f90cf8557a5d124f693c2b40d7ec
WORKDIR /dl
ADD --checksum=sha256:$PDCURSES_SHA256 \
    https://downloads.sourceforge.net/project/pdcurses/pdcurses/$PDCURSES_VERSION/PDCurses-$PDCURSES_VERSION.tar.gz ./
RUN mkdir pdcurses \
 && tar xzf PDCurses-$PDCURSES_VERSION.tar.gz -C pdcurses --strip-components=1

FROM base AS dl-make
ARG MAKE_VERSION=4.4.1 \
    MAKE_SHA256=dd16fb1d67bfab79a72f5e8390735c49e3e8e70b4945a15ab1f81ddb78658fb3
WORKDIR /dl
ADD --checksum=sha256:$MAKE_SHA256 \
    https://ftp.wayne.edu/gnu/make/make-$MAKE_VERSION.tar.gz ./
RUN mkdir make \
 && tar xzf make-$MAKE_VERSION.tar.gz -C make --strip-components=1

FROM base AS dl-busybox
ARG BUSYBOX_VERSION=FRP-6075-g169694ebd \
    BUSYBOX_SHA256=aa953010f16989cec8e165c5ffddaa9f6633f65670e20d5d7678c987d776f1d7
WORKDIR /dl
ADD --checksum=sha256:$BUSYBOX_SHA256 \
    https://github.com/rmyorston/busybox-w32/archive/refs/tags/$BUSYBOX_VERSION.tar.gz \
    busybox-w32-$BUSYBOX_VERSION.tar.gz
RUN mkdir busybox \
 && tar xzf busybox-w32-$BUSYBOX_VERSION.tar.gz -C busybox --strip-components=1 \
 && echo $BUSYBOX_VERSION >busybox/.frp_describe

FROM base AS dl-vim
ARG VIM_VERSION=9.0 \
    VIM_SHA256=a6456bc154999d83d0c20d968ac7ba6e7df0d02f3cb6427fb248660bacfb336e
WORKDIR /dl
ADD --checksum=sha256:$VIM_SHA256 \
    https://mirror.math.princeton.edu/pub/vim/unix/vim-$VIM_VERSION.tar.bz2 ./
RUN mkdir vim \
 && tar xjf vim-$VIM_VERSION.tar.bz2 -C vim --strip-components=1

FROM base AS dl-ctags
ARG CTAGS_VERSION=6.2.1 \
    CTAGS_SHA256=f56829e9a576025e98955597ee967099a871987b3476fbd8dbbc2b9dc921f824
WORKDIR /dl
ADD --checksum=sha256:$CTAGS_SHA256 \
    https://github.com/universal-ctags/ctags/archive/refs/tags/v$CTAGS_VERSION.tar.gz \
    ctags-$CTAGS_VERSION.tar.gz
RUN mkdir ctags \
 && tar xzf ctags-$CTAGS_VERSION.tar.gz -C ctags --strip-components=1

FROM base AS dl-zstd
ARG ZSTD_VERSION=1.5.7 \
    ZSTD_SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
WORKDIR /dl
ADD --checksum=sha256:$ZSTD_SHA256 \
    https://github.com/facebook/zstd/releases/download/v$ZSTD_VERSION/zstd-$ZSTD_VERSION.tar.gz ./
RUN mkdir zstd \
 && tar xzf zstd-$ZSTD_VERSION.tar.gz -C zstd --strip-components=1

FROM base AS dl-ccache
ARG CCACHE_VERSION=4.14 \
    CCACHE_SHA256=b093ac5d38204cb4d9f29b0bbd570675aa5a592a78e6675b2c506dbe045234e7 \
    XXHASH_VERSION=0.8.3 \
    XXHASH_SHA256=aae608dfe8213dfd05d909a57718ef82f30722c392344583d3f39050c7f29a80
WORKDIR /dl
ADD --checksum=sha256:$CCACHE_SHA256 \
    https://github.com/ccache/ccache/releases/download/v$CCACHE_VERSION/ccache-$CCACHE_VERSION.tar.xz ./
ADD --checksum=sha256:$XXHASH_SHA256 \
    https://github.com/Cyan4973/xxhash/archive/refs/tags/v$XXHASH_VERSION.tar.gz \
    xxHash-$XXHASH_VERSION.tar.gz
RUN mkdir ccache \
 && tar xJf ccache-$CCACHE_VERSION.tar.xz -C ccache --strip-components=1 \
 && mkdir xxhash \
 && tar xzf xxHash-$XXHASH_VERSION.tar.gz -C xxhash --strip-components=1

FROM base AS dl-ninja
ARG NINJA_VERSION=1.13.2 \
    NINJA_SHA256=974d6b2f4eeefa25625d34da3cb36bdcebe7fbce40f4c16ac0835fd1c0cbae17
WORKDIR /dl
ADD --checksum=sha256:$NINJA_SHA256 \
    https://github.com/ninja-build/ninja/archive/refs/tags/v$NINJA_VERSION.tar.gz \
    ninja-$NINJA_VERSION.tar.gz
RUN mkdir ninja \
 && tar xzf ninja-$NINJA_VERSION.tar.gz -C ninja --strip-components=1

FROM base AS dl-cmake
ARG CMAKE_VERSION=4.4.3 \
    CMAKE_SHA256=c46400618b4f1f2b43507f24fb22f3ae830c3416cf23b776e16e1d413aa892f0
WORKDIR /dl
ADD --checksum=sha256:$CMAKE_SHA256 \
    https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION.tar.gz ./
RUN mkdir cmake \
 && tar xzf cmake-$CMAKE_VERSION.tar.gz -C cmake --strip-components=1

FROM base AS dl-dcmake
ARG DCMAKE_VERSION=1.7.1 \
    DCMAKE_SHA256=9d7388088cd03fa7d47e287809f86eee28f02fa6f57bbffa2b556f70dcd8adf9
WORKDIR /dl
ADD --checksum=sha256:$DCMAKE_SHA256 \
    https://github.com/skeeto/dcmake/releases/download/v$DCMAKE_VERSION/dcmake-$DCMAKE_VERSION.tar.gz ./
RUN mkdir dcmake \
 && tar xzf dcmake-$DCMAKE_VERSION.tar.gz -C dcmake --strip-components=1

FROM base AS dl-7z
ARG Z7_VERSION=2301 \
    Z7_SHA256=356071007360e5a1824d9904993e8b2480b51b570e8c9faf7c0f58ebe4bf9f74
WORKDIR /dl
ADD --checksum=sha256:$Z7_SHA256 \
    https://downloads.sourceforge.net/project/sevenzip/7-Zip/23.01/7z$Z7_VERSION-src.tar.xz ./
RUN mkdir 7z \
 && tar xJf 7z$Z7_VERSION-src.tar.xz -C 7z

FROM base AS dl-aas-sign
ARG AAS_SIGN_VERSION=1.1.0 \
    AAS_SIGN_SHA256=4ba127b0434f6e0f8af639e51a0c961e95b5adeb06391dd6ef02445e0b027c3f
WORKDIR /dl
ADD --checksum=sha256:$AAS_SIGN_SHA256 \
    https://github.com/skeeto/aas-sign/releases/download/v$AAS_SIGN_VERSION/aas-sign-$AAS_SIGN_VERSION.tar.gz ./
RUN mkdir aas-sign \
 && tar xzf aas-sign-$AAS_SIGN_VERSION.tar.gz -C aas-sign --strip-components=1

FROM base AS dl-nsis
ARG NSIS_VERSION=3.12 \
    NSIS_SHA256=f3ed7a8e4aa2cf4e8cf47d3b563a02559e0cb4934db2662b2f9661b824e2b186
WORKDIR /dl
ADD --checksum=sha256:$NSIS_SHA256 \
    https://downloads.sourceforge.net/project/nsis/NSIS%203/$NSIS_VERSION/nsis-$NSIS_VERSION-src.tar.bz2 ./
RUN mkdir nsis \
 && tar xjf nsis-$NSIS_VERSION-src.tar.bz2 -C nsis --strip-components=1

# Build cross-compiler

FROM dl-cross AS variant-x64
ENV ARCH=x86_64-w64-mingw32 \
    GCC_ARCH_FLAG=--with-arch-32=pentium4 \
    GCC_MULTILIB=enable \
    CRT_LIB32=enable \
    CRT_LIB64=enable \
    GCC_MANIFEST_FLAG="" \
    BUSYBOX_CONFIG=mingw64u_defconfig \
    ZSTD_THREAD_FLAG="" \
    CMAKE_WINNT_C_FLAGS="" \
    CMAKE_WINNT_CXX_FLAGS="" \
    NSIS_ARCH=amd64

FROM dl-cross AS variant-x86
ENV ARCH=i686-w64-mingw32 \
    GCC_ARCH_FLAG=--with-arch=pentium4 \
    GCC_MULTILIB=disable \
    CRT_LIB32=enable \
    CRT_LIB64=disable \
    GCC_MANIFEST_FLAG=--disable-win32-utf8-manifest \
    BUSYBOX_CONFIG=mingw32w_defconfig \
    ZSTD_THREAD_FLAG=HAVE_THREAD=0 \
    CMAKE_WINNT_C_FLAGS="-O2 -D_WIN32_WINNT=0x0601" \
    CMAKE_WINNT_CXX_FLAGS="-O2 -D_WIN32_WINNT=0x0601" \
    NSIS_ARCH=x86

FROM variant-${VARIANT} AS cross

WORKDIR /dl/binutils
COPY src/binutils/ $PREFIX/src/binutils/
# Patch queues are applied with quilt, ordered by each queue's series
# file. The .pc/ state is removed after each push: source trees are
# never popped, and a stale .pc would block pushing a second queue
# onto the same tree later (gcc then gcc-final, mingw then gendef).
RUN sed -ri 's/(static bool insert_timestamp = )/\1!/' ld/emultempl/pe*.em \
 && sed -ri 's/(int pe_enable_stdcall_fixup = )/\1!!/' ld/emultempl/pe*.em \
 && QUILT_PATCHES=$PREFIX/src/binutils quilt push -a \
 && rm -rf .pc
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

COPY src/mingw/ $PREFIX/src/mingw/
RUN cd /dl/mingw \
 && QUILT_PATCHES=$PREFIX/src/mingw quilt push -a \
 && rm -rf .pc

WORKDIR /x-mingw-headers
RUN printf '#include <crtdefs.h>\n#if __has_include_next(<stddef.h>)\n#include_next <stddef.h>\n#endif\n' \
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
COPY src/gcc/ $PREFIX/src/gcc/
RUN (cd /dl/gcc \
     && QUILT_PATCHES=$PREFIX/src/gcc quilt push -a \
     && rm -rf .pc) \
 && /dl/gcc/configure \
        --prefix=/bootstrap \
        --with-sysroot=/bootstrap \
        $GCC_ARCH_FLAG \
        --${GCC_MULTILIB}-multilib \
        --target=$ARCH \
        --enable-static \
        --disable-shared \
        --with-pic \
        --enable-languages=c,c++,fortran \
        --enable-libgomp \
        --enable-threads=posix \
        --enable-tls \
        --enable-version-specific-runtime-libs \
        --disable-libstdcxx-verbose \
        --disable-dependency-tracking \
        --disable-nls \
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
 && ln $PREFIX/lib/libchkstk.a /bootstrap/lib/ \
 && if [ "$GCC_MULTILIB" = enable ]; then \
        mkdir -p $PREFIX/lib32 /bootstrap/lib32 \
     && CC="$ARCH-gcc -m32" AR=$ARCH-ar DESTDIR=$PREFIX/lib32/ \
            sh $PREFIX/src/libmemory.c \
     && ln $PREFIX/lib32/libmemory.a /bootstrap/lib32/ \
     && CC="$ARCH-gcc -m32" AR=$ARCH-ar DESTDIR=$PREFIX/lib32/ \
            sh $PREFIX/src/libchkstk.S \
     && ln $PREFIX/lib32/libchkstk.a /bootstrap/lib32/ ; \
    fi

WORKDIR /x-mingw-crt
RUN /dl/mingw/mingw-w64-crt/configure \
        --prefix=/bootstrap \
        --with-sysroot=/bootstrap \
        --host=$ARCH \
        --with-default-msvcrt=msvcrt-os \
        --disable-dependency-tracking \
        --${CRT_LIB32}-lib32 \
        --${CRT_LIB64}-lib64 \
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

WORKDIR /x-winpthreads32
RUN if [ "$GCC_MULTILIB" = enable ]; then \
        /dl/mingw/mingw-w64-libraries/winpthreads/configure \
            --prefix=/bootstrap \
            --libdir=/bootstrap/lib32 \
            --with-sysroot=/bootstrap \
            --host=$ARCH \
            --enable-static \
            --disable-shared \
            CC="$ARCH-gcc -m32" \
            CFLAGS="-O2" \
            LDFLAGS="-s" \
     && make -j$(nproc) \
     && make install ; \
    fi

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
 && rm $PREFIX/bin/dllwrap.exe $PREFIX/bin/elfedit.exe $PREFIX/bin/readelf.exe

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

WORKDIR /zlib
RUN /dl/binutils/zlib/configure \
        --host=$ARCH \
        CFLAGS="-O2" \
 && make -j$(nproc) libz.a \
 && cp libz.a /deps/lib/ \
 && cp /dl/binutils/zlib/zlib.h /dl/binutils/zlib/zconf.h /deps/include/

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
        --${CRT_LIB32}-lib32 \
        --${CRT_LIB64}-lib64 \
        CFLAGS="-O2" \
        LDFLAGS="-s" \
 && make -j$(nproc) \
 && make install

COPY src/threads.c $PREFIX/src/
COPY src/threads.h $PREFIX/include/
RUN $ARCH-gcc -c -Oz -I$PREFIX/include/ \
        -ffunction-sections -Wa,--no-pad-sections $PREFIX/src/threads.c \
 && $ARCH-ar r $PREFIX/lib/libmingwex.a threads.o \
 && if [ "$GCC_MULTILIB" = enable ]; then \
        $ARCH-gcc -m32 -c -Oz -I$PREFIX/include/ \
            -ffunction-sections -Wa,--no-pad-sections \
            -o threads32.o $PREFIX/src/threads.c \
     && $ARCH-ar r $PREFIX/lib32/libmingwex.a threads32.o ; \
    fi

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

WORKDIR /winpthreads32
RUN if [ "$GCC_MULTILIB" = enable ]; then \
        /dl/mingw/mingw-w64-libraries/winpthreads/configure \
            --prefix=$PREFIX \
            --libdir=$PREFIX/lib32 \
            --with-sysroot=$PREFIX \
            --host=$ARCH \
            --enable-static \
            --disable-shared \
            CC="$ARCH-gcc -m32" \
            CFLAGS="-O2" \
            LDFLAGS="-s" \
     && make -j$(nproc) \
     && make install ; \
    fi

WORKDIR /gcc
COPY src/gcc-final/ $PREFIX/src/gcc-final/
# libstdc++, libgfortran, and libquadmath are built as fat LTO objects:
# they link normally without -flto, but an -flto link can inline into them
# and drop their unused functions, which --gc-sections cannot do on PE.
# libstdc++ is the only C++ target library, so CXXFLAGS_FOR_TARGET reaches
# it alone.
# libgfortran and libquadmath share CFLAGS_FOR_TARGET with libgcc, libgomp,
# and libatomic, which must stay plain, so they are rebuilt afterward with
# their own flags. The LTO plugin is also installed where ar, nm, and
# ranlib find it, so they can handle slim LTO objects without gcc-ar.
RUN (cd /dl/gcc \
     && QUILT_PATCHES=$PREFIX/src/gcc-final quilt push -a \
     && rm -rf .pc) \
 && /dl/gcc/configure \
        --prefix=$PREFIX \
        --with-sysroot=$PREFIX \
        --with-native-system-header-dir=/include \
        $GCC_ARCH_FLAG \
        --${GCC_MULTILIB}-multilib \
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
        --enable-tls \
        --enable-version-specific-runtime-libs \
        --disable-libstdcxx-verbose \
        --disable-dependency-tracking \
        --disable-nls \
        --disable-win32-registry \
        $GCC_MANIFEST_FLAG \
        --enable-mingw-wildcard \
        CFLAGS_FOR_TARGET="-O2" \
        CXXFLAGS_FOR_TARGET="-O2 -flto -ffat-lto-objects" \
        LDFLAGS_FOR_TARGET="-s" \
        CFLAGS="-O2" \
        CXXFLAGS="-O2" \
        LDFLAGS="-s" \
 && make -j$(nproc) \
 && make clean-target-libquadmath clean-target-libgfortran \
 && make -j$(nproc) all-target-libquadmath all-target-libgfortran \
        CFLAGS_FOR_TARGET="-O2 -flto -ffat-lto-objects" \
 && make install \
 && cp $PREFIX/libexec/gcc/$ARCH/*/liblto_plugin.dll $PREFIX/lib/bfd-plugins/ \
 && rm -f $PREFIX/bin/ld.bfd.exe $PREFIX/bin/lto-dump.exe \
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
 && printf '%s\n' addr2line ar as c++filt cpp dlltool g++ \
      gcc gcc-ar gcc-nm gcc-ranlib gcov gcov-dump gcov-tool gendef gfortran \
      ld nm objcopy objdump ranlib size strings strip uuidgen widl \
      windmc windres \
    | xargs -I{} -P$(nproc) \
          $ARCH-gcc -DEXE={}.exe -DCMD=$ARCH-{} \
            -Oz -fno-asynchronous-unwind-tables \
            -Wl,--gc-sections -s -nostdlib \
            -o $PREFIX/bin/$ARCH-{}.exe $PREFIX/src/alias.c -lkernel32

# Create i686 tool aliases
RUN if [ "$GCC_MULTILIB" = enable ]; then \
    printf '%s\n' addr2line ar c++filt gcc-ar gcc-nm gcc-ranlib gcov \
        gcov-dump gcov-tool gendef nm objcopy objdump ranlib size strings \
        strip uuidgen windmc \
    | xargs -I{} -P$(nproc) \
          $ARCH-gcc -DEXE={}.exe -DCMD=i686-w64-mingw32-{} \
            -Oz -fno-asynchronous-unwind-tables \
            -Wl,--gc-sections -s -nostdlib \
            -o $PREFIX/bin/i686-w64-mingw32-{}.exe \
            $PREFIX/src/alias.c -lkernel32 \
 && printf '%s\n' cpp gcc g++ gfortran \
    | xargs -I{} -P$(nproc) \
          $ARCH-gcc -DEXE={}.exe -DCMD="i686-w64-mingw32-{} -m32" \
            -Oz -fno-asynchronous-unwind-tables \
            -Wl,--gc-sections -s -nostdlib \
            -o $PREFIX/bin/i686-w64-mingw32-{}.exe \
            $PREFIX/src/alias.c -lkernel32 \
 && $ARCH-gcc -DEXE=as.exe -DCMD="i686-w64-mingw32-as --32" \
        -Oz -fno-asynchronous-unwind-tables -Wl,--gc-sections -s -nostdlib \
        -o $PREFIX/bin/i686-w64-mingw32-as.exe \
        $PREFIX/src/alias.c -lkernel32 \
 && $ARCH-gcc -DEXE=ld.exe -DCMD="i686-w64-mingw32-ld -m i386pe" \
        -Oz -fno-asynchronous-unwind-tables -Wl,--gc-sections -s -nostdlib \
        -o $PREFIX/bin/i686-w64-mingw32-ld.exe \
        $PREFIX/src/alias.c -lkernel32 \
 && $ARCH-gcc -DEXE=dlltool.exe \
        -DCMD="i686-w64-mingw32-dlltool -m i386 --as-flags=--32" \
        -Oz -fno-asynchronous-unwind-tables -Wl,--gc-sections -s -nostdlib \
        -o $PREFIX/bin/i686-w64-mingw32-dlltool.exe \
        $PREFIX/src/alias.c -lkernel32 \
 && $ARCH-gcc -DEXE=widl.exe \
        -DCMD="i686-w64-mingw32-widl --win32" \
        -Oz -fno-asynchronous-unwind-tables -Wl,--gc-sections -s -nostdlib \
        -o $PREFIX/bin/i686-w64-mingw32-widl.exe \
        $PREFIX/src/alias.c -lkernel32 \
 && $ARCH-gcc -DEXE=windres.exe \
        -DCMD="i686-w64-mingw32-windres --target=pe-i386" \
        -Oz -fno-asynchronous-unwind-tables -Wl,--gc-sections -s -nostdlib \
        -o $PREFIX/bin/i686-w64-mingw32-windres.exe \
        $PREFIX/src/alias.c -lkernel32 ; \
    fi

# Build some extra development tools

FROM cross AS build-gendef

WORKDIR /mingw-tools/gendef
COPY src/gendef/ $PREFIX/src/gendef/
RUN (cd /dl/mingw \
     && QUILT_PATCHES=$PREFIX/src/gendef quilt push -a \
     && rm -rf .pc) \
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

FROM cross AS build-quilt
COPY src/quilt.cpp $PREFIX/src/
RUN mkdir -p /out/bin \
 && $ARCH-g++ -std=c++20 -O2 -fno-exceptions -s \
        -o /out/bin/quilt.exe $PREFIX/src/quilt.cpp

# Build PDCurses once and reuse it for both gdb and ccmake.
FROM cross AS build-pdcurses
COPY --from=dl-pdcurses /dl/pdcurses /dl/pdcurses

WORKDIR /dl/pdcurses
RUN make -j$(nproc) -C wincon \
       CC=$ARCH-gcc AR=$ARCH-ar CFLAGS="-I.. -O2 -DPDC_WIDE" pdcurses.a \
 && mkdir -p /deps/lib /deps/include \
 && cp wincon/pdcurses.a /deps/lib/libcurses.a \
 && cp curses.h /deps/include/curses.h

FROM cross AS build-gdb
COPY --from=dl-gdb /dl/ /dl/
COPY --from=build-pdcurses /deps/lib/libcurses.a /deps/lib/
COPY --from=build-pdcurses /deps/include/curses.h /deps/include/

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
COPY src/gdb/ $PREFIX/src/gdb/
RUN (cd /dl/gdb \
     && QUILT_PATCHES=$PREFIX/src/gdb quilt push -a \
     && rm -rf .pc) \
 && sed -i 's/quiet = 0/quiet = 1/' /dl/gdb/gdb/main.c \
 && /dl/gdb/configure \
        --host=$ARCH \
        --enable-tui \
        CFLAGS="-std=gnu17 -O2 -D__MINGW_USE_VC2005_COMPAT -DPDC_WIDE -I/deps/include" \
        CXXFLAGS="-O2 -D__MINGW_USE_VC2005_COMPAT -DPDC_WIDE -I/deps/include" \
        LDFLAGS="-s -L/deps/lib" \
 && make MAKEINFO=true -j$(nproc) \
 && mkdir -p /out/bin \
 && cp gdb/.libs/gdb.exe gdbserver/gdbserver.exe /out/bin/

FROM cross AS build-make
COPY --from=dl-make /dl/ /dl/

WORKDIR /make
COPY src/make/ $PREFIX/src/make/
RUN (cd /dl/make \
     && QUILT_PATCHES=$PREFIX/src/make quilt push -a \
     && rm -rf .pc) \
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
COPY src/busybox/ $PREFIX/src/busybox/
COPY src/busybox-alias.c $PREFIX/src/
RUN QUILT_PATCHES=$PREFIX/src/busybox quilt push -a \
 && rm -rf .pc \
 && make $BUSYBOX_CONFIG \
 && sed -ri 's/^(CONFIG_AR)=y/\1=n/' .config \
 && sed -ri 's/^(CONFIG_ASCII)=y/\1=n/' .config \
 && sed -ri -e 's/^(CONFIG_BASH_IS_ASH)=y/\1=n/' \
            -e 's/^# (CONFIG_BASH_IS_NONE) is not set/\1=y/' .config \
 && sed -ri 's/^(CONFIG_CRON\w*)=y/\1=n/' .config \
 && sed -ri 's/^(CONFIG_DPKG\w*)=y/\1=n/' .config \
 && sed -ri 's/^(CONFIG_FEATURE_FAIL_IF_UTF8_MANIFEST_UNSUPPORTED)=y/\1=n/' .config \
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
 && sed -ri 's/^(CONFIG_UUIDGEN)=y/\1=n/' .config \
 && sed -ri 's/^(CONFIG_VI)=y/\1=n/' .config \
 && sed -ri 's/^(CONFIG_XXD)=y/\1=n/' .config \
 && make -j$(nproc) CROSS_COMPILE=$ARCH- \
    CONFIG_EXTRA_CFLAGS="-D_WIN32_WINNT=0x502" \
 && mkdir -p /out/bin \
 && cp busybox.exe /out/bin/

# Create BusyBox command aliases (like "busybox --install")
RUN $ARCH-gcc -Oz -fno-asynchronous-unwind-tables -Wl,--gc-sections -s \
      -nostdlib -o alias.exe $PREFIX/src/busybox-alias.c -lkernel32 \
 && printf '%s\n' arch ash awk base32 base64 basename bc bunzip2 bzcat \
      bzip2 cal cat chattr chmod cksum clear cmp comm cp cpio crc32 cut date \
      dc dd df diff dirname dos2unix du echo ed egrep env expand expr factor \
      false fgrep find fold free fsync getopt grep groups gunzip gzip hd \
      head hexdump httpd iconv id inotifyd install ipcalc jn join kill killall \
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
COPY src/rexxd.c $PREFIX/src/
COPY src/vim/ $PREFIX/src/vim/
RUN QUILT_PATCHES=$PREFIX/src/vim quilt push -a \
 && rm -rf .pc \
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
COPY src/ctags/ $PREFIX/src/ctags/
RUN QUILT_PATCHES=$PREFIX/src/ctags quilt push -a \
 && rm -rf .pc \
 && sed -i /RT_MANIFEST/d win32/ctags.rc \
 && make -j$(nproc) -f mk_mingw.mak CC=gcc packcc.exe \
 && make -j$(nproc) -f mk_mingw.mak \
        CC=$ARCH-gcc WINDRES=$ARCH-windres \
        OPT= CFLAGS=-O2 LDFLAGS=-s \
 && mkdir -p /out/bin \
 && cp ctags.exe /out/bin/

FROM cross AS build-zstd
COPY --from=dl-zstd /dl/ /dl/

WORKDIR /dl/zstd/lib
RUN make -j$(nproc) CC=$ARCH-gcc AR=$ARCH-ar CFLAGS="-O2" libzstd.a \
 && cp libzstd.a /deps/lib/ \
 && cp zstd.h zstd_errors.h zdict.h /deps/include/

WORKDIR /dl/zstd
RUN make -j$(nproc) -C programs zstd \
        CC=$ARCH-gcc CFLAGS="-O2" LDFLAGS="-s" EXT=.exe $ZSTD_THREAD_FLAG \
 && mkdir -p /out/bin \
 && cp programs/zstd.exe /out/bin/ \
 && $ARCH-gcc -DEXE=zstd.exe -DCMD=unzstd \
        -Oz -fno-asynchronous-unwind-tables -Wl,--gc-sections -s -nostdlib \
        -o /out/bin/unzstd.exe $PREFIX/src/alias.c -lkernel32

FROM cross AS build-ccache
COPY --from=dl-ccache /dl/ /dl/
COPY --from=build-zstd /deps/ /deps/
COPY src/ccache/ $PREFIX/src/ccache/
RUN cd /dl/ccache \
 && QUILT_PATCHES=$PREFIX/src/ccache quilt push -a \
 && rm -rf .pc

WORKDIR /dl/xxhash
RUN make -j$(nproc) CC=$ARCH-gcc AR=$ARCH-ar CFLAGS="-O2" libxxhash.a \
 && cp libxxhash.a /deps/lib/ \
 && cp xxhash.h /deps/include/

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
COPY src/ninja/ $PREFIX/src/ninja/

WORKDIR /ninja
RUN (cd /dl/ninja \
     && QUILT_PATCHES=$PREFIX/src/ninja quilt push -a \
     && rm -rf .pc) \
 && cmake -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=Windows \
        -DCMAKE_CXX_COMPILER=$ARCH-g++ \
        -DCMAKE_EXE_LINKER_FLAGS="-s" \
        -DBUILD_TESTING=OFF \
        /dl/ninja \
 && make -j$(nproc) \
 && mkdir -p /out/bin \
 && cp ninja.exe /out/bin/

FROM cross AS build-dcmake
COPY --from=dl-dcmake /dl/ /dl/

WORKDIR /dcmake
RUN cmake -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=Windows \
        -DCMAKE_CXX_COMPILER=$ARCH-g++ \
        -DCMAKE_RC_COMPILER=$ARCH-windres \
        -DCMAKE_EXE_LINKER_FLAGS="-s" \
        /dl/dcmake \
 && make -j$(nproc) \
 && mkdir -p /out/bin \
 && cp dcmake.exe /out/bin/

FROM cross AS build-cmake
COPY --from=dl-cmake /dl/ /dl/
COPY --from=build-pdcurses /deps/lib/libcurses.a /deps/lib/
COPY --from=build-pdcurses /deps/include/curses.h /deps/include/

WORKDIR /cmake
COPY src/cmake/ $PREFIX/src/cmake/
RUN (cd /dl/cmake \
     && QUILT_PATCHES=$PREFIX/src/cmake quilt push -a \
     && rm -rf .pc) \
 && cmake -DCMAKE_C_FLAGS="$CMAKE_WINNT_C_FLAGS" \
        -DCMAKE_CXX_FLAGS="$CMAKE_WINNT_CXX_FLAGS" \
        -DCMAKE_BUILD_TYPE=Release \
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
 && rm -rf /out$PREFIX/doc/ /out$PREFIX/man/ \
       /out$PREFIX/share/bash-completion/ \
       /out$PREFIX/share/emacs/ \
       /out$PREFIX/share/aclocal/ \
       /out$PREFIX/share/cmake-*/Help/

FROM cross AS build-7z
COPY --from=dl-7z /dl/ /dl/

WORKDIR /dl/7z
COPY src/7z.mak $PREFIX/src/
RUN sed -i s/CommCtrl/commctrl/ $(grep -Rl CommCtrl CPP/) \
 && sed -i s%7z\\.ico%$PREFIX/src/w64devkit.ico% \
           CPP/7zip/Bundles/SFXWin/resource.rc \
 && make -f $PREFIX/src/7z.mak -j$(nproc) CROSS=$ARCH-

# aas-sign: native Linux x86_64 binary used at run time inside the
# `signed` container. Built with the base image's Debian gcc (NOT the
# cross compiler).
FROM base AS build-aas-sign
COPY --from=dl-aas-sign /dl/aas-sign /dl/aas-sign
RUN cmake -B /aas-sign-build -S /dl/aas-sign -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /aas-sign-build -j$(nproc) \
 && mkdir -p /out/usr/local/bin \
 && cp /aas-sign-build/aas-sign /out/usr/local/bin/

FROM cross AS build-aas-sign-w32
COPY --from=dl-aas-sign /dl/aas-sign /dl/aas-sign
RUN cmake -B /aas-sign-build-w32 -S /dl/aas-sign \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=Windows \
        -DCMAKE_C_COMPILER=$ARCH-gcc \
        -DCMAKE_CXX_COMPILER=$ARCH-g++ \
        -DCMAKE_RC_COMPILER=$ARCH-windres \
        -DCMAKE_EXE_LINKER_FLAGS="-s" \
 && cmake --build /aas-sign-build-w32 -j$(nproc) \
 && mkdir -p /out$PREFIX/bin \
 && cp /aas-sign-build-w32/aas-sign.exe /out$PREFIX/bin/

FROM cross AS build-nsis
COPY --from=dl-nsis /dl/nsis /dl/nsis
COPY src/nsis/ $PREFIX/src/nsis/
WORKDIR /dl/nsis
# Source/build.cpp ships with CRLF line endings; everything else in
# the tree is LF. Strip the carriage returns so our LF patch applies.
RUN sed -i 's/\r$//' Source/build.cpp \
 && QUILT_PATCHES=$PREFIX/src/nsis quilt push -a \
 && rm -rf .pc \
 && scons -j$(nproc) \
        XGCC_W32_PREFIX=$ARCH- \
        TARGET_ARCH=$NSIS_ARCH \
        PREFIX=$PREFIX \
        PREFIX_BIN=$PREFIX/share/nsis/bin \
        PREFIX_DATA=$PREFIX/share/nsis \
        NSIS_CONFIG_CONST_DATA_PATH=no \
        PREFIX_DEST=/out \
        SKIPDOC=all \
        SKIPUTILS="NSIS Menu,Makensisw,VPatch/Source/GenPat,MakeLangId,zip2exe" \
        ZLIB_W32=/deps \
        install-compiler install-stubs install-includes install-plugins \
        install-contrib install-utils

# Also provide x86 stubs and plugins in the multilib kit so makensis
# can produce 32-bit installers ("Target x86-unicode"). NSIS never
# passes -m32 itself and derives every tool name from XGCC_W32_PREFIX,
# so present the multilib compiler through an i686 interface, wrapper
# scripts mirroring the kit's own i686 aliases. The wrappers embed
# absolute tool paths because scons scrubs the PATH of its
# subprocesses down to the directory holding the prefixed tools, so
# they cannot find /bootstrap/bin themselves. NSIS's zlib configure
# probe runs even for stub-only targets and link-tests with -m32, so
# it needs a 32-bit libz.a, built from Binutils' bundled zlib exactly
# like the 64-bit copy in /deps.
RUN if [ "$GCC_MULTILIB" = enable ]; then \
        printf '#!/bin/sh\nexec %s -m32 "$@"\n' "$(command -v $ARCH-gcc)" \
            >/usr/local/bin/i686-w64-mingw32-gcc \
     && printf '#!/bin/sh\nexec %s -m32 "$@"\n' "$(command -v $ARCH-g++)" \
            >/usr/local/bin/i686-w64-mingw32-g++ \
     && printf '#!/bin/sh\nexec %s --32 "$@"\n' "$(command -v $ARCH-as)" \
            >/usr/local/bin/i686-w64-mingw32-as \
     && printf '#!/bin/sh\nexec %s --target=pe-i386 "$@"\n' \
            "$(command -v $ARCH-windres)" \
            >/usr/local/bin/i686-w64-mingw32-windres \
     && printf '#!/bin/sh\nexec %s "$@"\n' "$(command -v $ARCH-ar)" \
            >/usr/local/bin/i686-w64-mingw32-ar \
     && printf '#!/bin/sh\nexec %s "$@"\n' "$(command -v $ARCH-ranlib)" \
            >/usr/local/bin/i686-w64-mingw32-ranlib \
     && chmod +x /usr/local/bin/i686-w64-mingw32-* \
     && mkdir -p /zlib32 /deps32/lib /deps32/include \
     && (cd /zlib32 \
         && /dl/binutils/zlib/configure --host=i686-w64-mingw32 \
                CFLAGS="-O2" \
         && make -j$(nproc) libz.a \
         && cp libz.a /deps32/lib/ \
         && cp /dl/binutils/zlib/zlib.h /dl/binutils/zlib/zconf.h \
               /deps32/include/) \
     && scons -j$(nproc) \
            XGCC_W32_PREFIX=i686-w64-mingw32- \
            TARGET_ARCH=x86 \
            PREFIX=$PREFIX \
            PREFIX_BIN=$PREFIX/share/nsis/bin \
            PREFIX_DATA=$PREFIX/share/nsis \
            NSIS_CONFIG_CONST_DATA_PATH=no \
            PREFIX_DEST=/out \
            SKIPDOC=all \
            SKIPUTILS="NSIS Menu,Makensisw,VPatch/Source/GenPat,MakeLangId,zip2exe" \
            ZLIB_W32=/deps32 \
            install-stubs install-plugins ; \
    fi

# Collect source tarballs
FROM base AS source
COPY --from=dl-cross /dl/*.* /source/
COPY --from=dl-gdb /dl/*.* /source/
COPY --from=dl-pdcurses /dl/*.* /source/
COPY --from=dl-make /dl/*.* /source/
COPY --from=dl-busybox /dl/*.* /source/
COPY --from=dl-vim /dl/*.* /source/
COPY --from=dl-ctags /dl/*.* /source/
COPY --from=dl-zstd /dl/*.* /source/
COPY --from=dl-ccache /dl/*.* /source/
COPY --from=dl-ninja /dl/*.* /source/
COPY --from=dl-cmake /dl/*.* /source/
COPY --from=dl-dcmake /dl/*.* /source/
COPY --from=dl-7z /dl/*.* /source/
COPY --from=dl-aas-sign /dl/*.* /source/
COPY --from=dl-nsis /dl/*.* /source/

# Pack up a release

FROM cross AS final
ARG VERSION

COPY --from=build-gendef /out/ $PREFIX/
COPY --from=build-quilt /out/ $PREFIX/
COPY --from=build-gdb /out/ $PREFIX/
COPY --from=build-make /out/ $PREFIX/
COPY --from=build-busybox /out/ $PREFIX/
COPY --from=build-vim /out/ $PREFIX/
COPY --from=build-ctags /out/ $PREFIX/
COPY --from=build-zstd /out/ $PREFIX/
COPY --from=build-ccache /out/ $PREFIX/
COPY --from=build-ninja /out/ $PREFIX/
COPY --from=build-dcmake /out/ $PREFIX/
COPY --from=build-cmake /out$PREFIX/ $PREFIX/
COPY --from=build-7z /dl/7z/7z.sfx /7z/
COPY --from=build-nsis /out$PREFIX/ $PREFIX/
COPY --from=build-aas-sign-w32 /out$PREFIX/ $PREFIX/

COPY src $PREFIX/src
COPY etc $PREFIX/etc

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
 && $ARCH-gcc \
        -Oz -fno-asynchronous-unwind-tables -fno-builtin -Wl,--gc-sections \
        -s -nostdlib -o $PREFIX/bin/recycle.exe $PREFIX/src/recycle.c \
        -lkernel32 -lshell32 -lmemory \
 && $ARCH-gcc -DEXE=pkg-config.exe -DCMD=pkg-config \
        -Oz -fno-asynchronous-unwind-tables -Wl,--gc-sections -s -nostdlib \
        -o $PREFIX/bin/$ARCH-pkg-config.exe $PREFIX/src/alias.c -lkernel32 \
 && $ARCH-gcc -DEXE=../share/nsis/bin/makensis.exe -DCMD=makensis \
        -Oz -fno-asynchronous-unwind-tables -Wl,--gc-sections -s -nostdlib \
        -o $PREFIX/bin/makensis.exe $PREFIX/src/alias.c -lkernel32 \
 && sed -i s/'\<ARCH\>'/$ARCH/g $PREFIX/etc/profile \
 && mkdir -p $PREFIX/lib/pkgconfig \
 && cp /dl/mingw/COPYING.MinGW-w64-runtime/COPYING.MinGW-w64-runtime.txt \
        $PREFIX/ \
 && printf "\n===========\nwinpthreads\n===========\n\n" \
        >>$PREFIX/COPYING.MinGW-w64-runtime.txt . \
 && cat /dl/mingw/mingw-w64-libraries/winpthreads/COPYING \
        >>$PREFIX/COPYING.MinGW-w64-runtime.txt \
 && echo $VERSION >$PREFIX/VERSION.txt

# Release target: signs every PE in the toolchain (~250 files), packs
# with 7z, then signs the resulting concatenated SFX. All signing runs
# at `docker run` time so secrets never enter the build cache.
FROM final AS signed
# aas-sign reaches GitHub OIDC and Azure over HTTPS at run time and
# needs the system trust store.
RUN apt-get update \
 && apt-get install --yes --no-install-recommends ca-certificates \
 && rm -rf /var/lib/apt/lists/*
COPY --from=build-aas-sign /out/usr/local/bin/aas-sign /usr/local/bin/aas-sign
COPY src/sign-and-pack.sh /
CMD ["sh", "/sign-and-pack.sh"]

# Default target: unsigned package, behaviorally identical to the old
# `final` (which packed and cat'd the SFX).
FROM final AS pack
RUN 7z a -mx=9 -mtm=- w64devkit.7z $PREFIX
CMD ["cat", "/7z/7z.sfx", "w64devkit.7z"]
