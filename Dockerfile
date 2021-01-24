FROM debian:buster-slim

ARG VERSION=1.5.0
ARG PREFIX=/w64devkit

ARG BINUTILS_VERSION=2.35.1
ARG BUSYBOX_VERSION=FRP-3812-g12e14ebba
ARG CTAGS_VERSION=20200824
ARG GCC_VERSION=10.2.0
ARG GDB_VERSION=10.1
ARG GMP_VERSION=6.2.0
ARG MAKE_VERSION=4.2
ARG MINGW_VERSION=7.0.0
ARG MPC_VERSION=1.2.1
ARG MPFR_VERSION=4.1.0
ARG NASM_VERSION=2.14.02
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
    https://www.nasm.us/pub/nasm/releasebuilds/2.14.02/nasm-$NASM_VERSION.tar.xz \
    http://deb.debian.org/debian/pool/main/u/universal-ctags/universal-ctags_0+git$CTAGS_VERSION.orig.tar.gz \
    https://downloads.sourceforge.net/project/mingw-w64/mingw-w64/mingw-w64-release/mingw-w64-v$MINGW_VERSION.tar.bz2
COPY SHA256SUMS .
RUN sha256sum -c SHA256SUMS \
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
RUN echo '@"%~dp0/g++.exe" %*' >$PREFIX/bin/c++.bat

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

RUN echo '@"%~dp0/gcc.exe" %*' >$PREFIX/bin/cc.bat

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
RUN sed -i 's/"sh\.exe"/"sh.bat"/' /make-$MAKE_VERSION/job.c
RUN /make-$MAKE_VERSION/configure \
        --host=x86_64-w64-mingw32 \
        --disable-nls \
        CFLAGS="-I/make-$MAKE_VERSION/glob -Os" \
        LDFLAGS="-s"
RUN make -j$(nproc)
RUN cp make.exe $PREFIX/bin/
RUN echo '@"%~dp0/make.exe" %*' >$PREFIX/bin/mingw32-make.bat

WORKDIR /busybox-w32
RUN make mingw64_defconfig
RUN sed -ri 's/^(CONFIG_(XXD|AR|STRINGS|DPKG|DPKG_DEB|TEST2|RPM|VI))=y/\1=n/' \
        .config
RUN sed -i '/\\007/d' libbb/lineedit.c
RUN make -j$(nproc)
RUN cp busybox.exe $PREFIX/bin/

# Install all BusyBox commands as batch scripts (like busybox --install)
RUN echo '@"%~dp0/busybox" arch %*' >$PREFIX/bin/arch.bat \
 && echo '@"%~dp0/busybox" ash %*' >$PREFIX/bin/ash.bat \
 && echo '@"%~dp0/busybox" awk %*' >$PREFIX/bin/awk.bat \
 && echo '@"%~dp0/busybox" base32 %*' >$PREFIX/bin/base32.bat \
 && echo '@"%~dp0/busybox" base64 %*' >$PREFIX/bin/base64.bat \
 && echo '@"%~dp0/busybox" basename %*' >$PREFIX/bin/basename.bat \
 && echo '@"%~dp0/busybox" bash %*' >$PREFIX/bin/bash.bat \
 && echo '@"%~dp0/busybox" bunzip2 %*' >$PREFIX/bin/bunzip2.bat \
 && echo '@"%~dp0/busybox" bzcat %*' >$PREFIX/bin/bzcat.bat \
 && echo '@"%~dp0/busybox" bzip2 %*' >$PREFIX/bin/bzip2.bat \
 && echo '@"%~dp0/busybox" cal %*' >$PREFIX/bin/cal.bat \
 && echo '@"%~dp0/busybox" cat %*' >$PREFIX/bin/cat.bat \
 && echo '@"%~dp0/busybox" chattr %*' >$PREFIX/bin/chattr.bat \
 && echo '@"%~dp0/busybox" chmod %*' >$PREFIX/bin/chmod.bat \
 && echo '@"%~dp0/busybox" cksum %*' >$PREFIX/bin/cksum.bat \
 && echo '@"%~dp0/busybox" clear %*' >$PREFIX/bin/clear.bat \
 && echo '@"%~dp0/busybox" cmp %*' >$PREFIX/bin/cmp.bat \
 && echo '@"%~dp0/busybox" comm %*' >$PREFIX/bin/comm.bat \
 && echo '@"%~dp0/busybox" cp %*' >$PREFIX/bin/cp.bat \
 && echo '@"%~dp0/busybox" cpio %*' >$PREFIX/bin/cpio.bat \
 && echo '@"%~dp0/busybox" cut %*' >$PREFIX/bin/cut.bat \
 && echo '@"%~dp0/busybox" date %*' >$PREFIX/bin/date.bat \
 && echo '@"%~dp0/busybox" dc %*' >$PREFIX/bin/dc.bat \
 && echo '@"%~dp0/busybox" dd %*' >$PREFIX/bin/dd.bat \
 && echo '@"%~dp0/busybox" df %*' >$PREFIX/bin/df.bat \
 && echo '@"%~dp0/busybox" diff %*' >$PREFIX/bin/diff.bat \
 && echo '@"%~dp0/busybox" dirname %*' >$PREFIX/bin/dirname.bat \
 && echo '@"%~dp0/busybox" dos2unix %*' >$PREFIX/bin/dos2unix.bat \
 && echo '@"%~dp0/busybox" du %*' >$PREFIX/bin/du.bat \
 && echo '@"%~dp0/busybox" echo %*' >$PREFIX/bin/echo.bat \
 && echo '@"%~dp0/busybox" ed %*' >$PREFIX/bin/ed.bat \
 && echo '@"%~dp0/busybox" egrep %*' >$PREFIX/bin/egrep.bat \
 && echo '@"%~dp0/busybox" env %*' >$PREFIX/bin/env.bat \
 && echo '@"%~dp0/busybox" expand %*' >$PREFIX/bin/expand.bat \
 && echo '@"%~dp0/busybox" expr %*' >$PREFIX/bin/expr.bat \
 && echo '@"%~dp0/busybox" factor %*' >$PREFIX/bin/factor.bat \
 && echo '@"%~dp0/busybox" false %*' >$PREFIX/bin/false.bat \
 && echo '@"%~dp0/busybox" fgrep %*' >$PREFIX/bin/fgrep.bat \
 && echo '@"%~dp0/busybox" find %*' >$PREFIX/bin/find.bat \
 && echo '@"%~dp0/busybox" fold %*' >$PREFIX/bin/fold.bat \
 && echo '@"%~dp0/busybox" fsync %*' >$PREFIX/bin/fsync.bat \
 && echo '@"%~dp0/busybox" ftpget %*' >$PREFIX/bin/ftpget.bat \
 && echo '@"%~dp0/busybox" ftpput %*' >$PREFIX/bin/ftpput.bat \
 && echo '@"%~dp0/busybox" getopt %*' >$PREFIX/bin/getopt.bat \
 && echo '@"%~dp0/busybox" grep %*' >$PREFIX/bin/grep.bat \
 && echo '@"%~dp0/busybox" groups %*' >$PREFIX/bin/groups.bat \
 && echo '@"%~dp0/busybox" gunzip %*' >$PREFIX/bin/gunzip.bat \
 && echo '@"%~dp0/busybox" gzip %*' >$PREFIX/bin/gzip.bat \
 && echo '@"%~dp0/busybox" hd %*' >$PREFIX/bin/hd.bat \
 && echo '@"%~dp0/busybox" head %*' >$PREFIX/bin/head.bat \
 && echo '@"%~dp0/busybox" hexdump %*' >$PREFIX/bin/hexdump.bat \
 && echo '@"%~dp0/busybox" httpd %*' >$PREFIX/bin/httpd.bat \
 && echo '@"%~dp0/busybox" iconv %*' >$PREFIX/bin/iconv.bat \
 && echo '@"%~dp0/busybox" id %*' >$PREFIX/bin/id.bat \
 && echo '@"%~dp0/busybox" inotifyd %*' >$PREFIX/bin/inotifyd.bat \
 && echo '@"%~dp0/busybox" install %*' >$PREFIX/bin/install.bat \
 && echo '@"%~dp0/busybox" ipcalc %*' >$PREFIX/bin/ipcalc.bat \
 && echo '@"%~dp0/busybox" kill %*' >$PREFIX/bin/kill.bat \
 && echo '@"%~dp0/busybox" killall %*' >$PREFIX/bin/killall.bat \
 && echo '@"%~dp0/busybox" less %*' >$PREFIX/bin/less.bat \
 && echo '@"%~dp0/busybox" link %*' >$PREFIX/bin/link.bat \
 && echo '@"%~dp0/busybox" ln %*' >$PREFIX/bin/ln.bat \
 && echo '@"%~dp0/busybox" logname %*' >$PREFIX/bin/logname.bat \
 && echo '@"%~dp0/busybox" ls %*' >$PREFIX/bin/ls.bat \
 && echo '@"%~dp0/busybox" lsattr %*' >$PREFIX/bin/lsattr.bat \
 && echo '@"%~dp0/busybox" lzcat %*' >$PREFIX/bin/lzcat.bat \
 && echo '@"%~dp0/busybox" lzma %*' >$PREFIX/bin/lzma.bat \
 && echo '@"%~dp0/busybox" lzop %*' >$PREFIX/bin/lzop.bat \
 && echo '@"%~dp0/busybox" lzopcat %*' >$PREFIX/bin/lzopcat.bat \
 && echo '@"%~dp0/busybox" man %*' >$PREFIX/bin/man.bat \
 && echo '@"%~dp0/busybox" md5sum %*' >$PREFIX/bin/md5sum.bat \
 && echo '@"%~dp0/busybox" mkdir %*' >$PREFIX/bin/mkdir.bat \
 && echo '@"%~dp0/busybox" mktemp %*' >$PREFIX/bin/mktemp.bat \
 && echo '@"%~dp0/busybox" mv %*' >$PREFIX/bin/mv.bat \
 && echo '@"%~dp0/busybox" nc %*' >$PREFIX/bin/nc.bat \
 && echo '@"%~dp0/busybox" nl %*' >$PREFIX/bin/nl.bat \
 && echo '@"%~dp0/busybox" od %*' >$PREFIX/bin/od.bat \
 && echo '@"%~dp0/busybox" paste %*' >$PREFIX/bin/paste.bat \
 && echo '@"%~dp0/busybox" patch %*' >$PREFIX/bin/patch.bat \
 && echo '@"%~dp0/busybox" pgrep %*' >$PREFIX/bin/pgrep.bat \
 && echo '@"%~dp0/busybox" pidof %*' >$PREFIX/bin/pidof.bat \
 && echo '@"%~dp0/busybox" pipe_progress %*' >$PREFIX/bin/pipe_progress.bat \
 && echo '@"%~dp0/busybox" pkill %*' >$PREFIX/bin/pkill.bat \
 && echo '@"%~dp0/busybox" printenv %*' >$PREFIX/bin/printenv.bat \
 && echo '@"%~dp0/busybox" printf %*' >$PREFIX/bin/printf.bat \
 && echo '@"%~dp0/busybox" ps %*' >$PREFIX/bin/ps.bat \
 && echo '@"%~dp0/busybox" pwd %*' >$PREFIX/bin/pwd.bat \
 && echo '@"%~dp0/busybox" readlink %*' >$PREFIX/bin/readlink.bat \
 && echo '@"%~dp0/busybox" realpath %*' >$PREFIX/bin/realpath.bat \
 && echo '@"%~dp0/busybox" reset %*' >$PREFIX/bin/reset.bat \
 && echo '@"%~dp0/busybox" rev %*' >$PREFIX/bin/rev.bat \
 && echo '@"%~dp0/busybox" rm %*' >$PREFIX/bin/rm.bat \
 && echo '@"%~dp0/busybox" rmdir %*' >$PREFIX/bin/rmdir.bat \
 && echo '@"%~dp0/busybox" rpm2cpio %*' >$PREFIX/bin/rpm2cpio.bat \
 && echo '@"%~dp0/busybox" sed %*' >$PREFIX/bin/sed.bat \
 && echo '@"%~dp0/busybox" seq %*' >$PREFIX/bin/seq.bat \
 && echo '@"%~dp0/busybox" sh %*' >$PREFIX/bin/sh.bat \
 && echo '@"%~dp0/busybox" sha1sum %*' >$PREFIX/bin/sha1sum.bat \
 && echo '@"%~dp0/busybox" sha256sum %*' >$PREFIX/bin/sha256sum.bat \
 && echo '@"%~dp0/busybox" sha3sum %*' >$PREFIX/bin/sha3sum.bat \
 && echo '@"%~dp0/busybox" sha512sum %*' >$PREFIX/bin/sha512sum.bat \
 && echo '@"%~dp0/busybox" shred %*' >$PREFIX/bin/shred.bat \
 && echo '@"%~dp0/busybox" shuf %*' >$PREFIX/bin/shuf.bat \
 && echo '@"%~dp0/busybox" sleep %*' >$PREFIX/bin/sleep.bat \
 && echo '@"%~dp0/busybox" sort %*' >$PREFIX/bin/sort.bat \
 && echo '@"%~dp0/busybox" split %*' >$PREFIX/bin/split.bat \
 && echo '@"%~dp0/busybox" ssl_client %*' >$PREFIX/bin/ssl_client.bat \
 && echo '@"%~dp0/busybox" stat %*' >$PREFIX/bin/stat.bat \
 && echo '@"%~dp0/busybox" su %*' >$PREFIX/bin/su.bat \
 && echo '@"%~dp0/busybox" sum %*' >$PREFIX/bin/sum.bat \
 && echo '@"%~dp0/busybox" tac %*' >$PREFIX/bin/tac.bat \
 && echo '@"%~dp0/busybox" tail %*' >$PREFIX/bin/tail.bat \
 && echo '@"%~dp0/busybox" tar %*' >$PREFIX/bin/tar.bat \
 && echo '@"%~dp0/busybox" tee %*' >$PREFIX/bin/tee.bat \
 && echo '@"%~dp0/busybox" test %*' >$PREFIX/bin/test.bat \
 && echo '@"%~dp0/busybox" time %*' >$PREFIX/bin/time.bat \
 && echo '@"%~dp0/busybox" timeout %*' >$PREFIX/bin/timeout.bat \
 && echo '@"%~dp0/busybox" touch %*' >$PREFIX/bin/touch.bat \
 && echo '@"%~dp0/busybox" tr %*' >$PREFIX/bin/tr.bat \
 && echo '@"%~dp0/busybox" true %*' >$PREFIX/bin/true.bat \
 && echo '@"%~dp0/busybox" truncate %*' >$PREFIX/bin/truncate.bat \
 && echo '@"%~dp0/busybox" ts %*' >$PREFIX/bin/ts.bat \
 && echo '@"%~dp0/busybox" ttysize %*' >$PREFIX/bin/ttysize.bat \
 && echo '@"%~dp0/busybox" uname %*' >$PREFIX/bin/uname.bat \
 && echo '@"%~dp0/busybox" uncompress %*' >$PREFIX/bin/uncompress.bat \
 && echo '@"%~dp0/busybox" unexpand %*' >$PREFIX/bin/unexpand.bat \
 && echo '@"%~dp0/busybox" uniq %*' >$PREFIX/bin/uniq.bat \
 && echo '@"%~dp0/busybox" unix2dos %*' >$PREFIX/bin/unix2dos.bat \
 && echo '@"%~dp0/busybox" unlink %*' >$PREFIX/bin/unlink.bat \
 && echo '@"%~dp0/busybox" unlzma %*' >$PREFIX/bin/unlzma.bat \
 && echo '@"%~dp0/busybox" unlzop %*' >$PREFIX/bin/unlzop.bat \
 && echo '@"%~dp0/busybox" unxz %*' >$PREFIX/bin/unxz.bat \
 && echo '@"%~dp0/busybox" unzip %*' >$PREFIX/bin/unzip.bat \
 && echo '@"%~dp0/busybox" usleep %*' >$PREFIX/bin/usleep.bat \
 && echo '@"%~dp0/busybox" uudecode %*' >$PREFIX/bin/uudecode.bat \
 && echo '@"%~dp0/busybox" uuencode %*' >$PREFIX/bin/uuencode.bat \
 && echo '@"%~dp0/busybox" watch %*' >$PREFIX/bin/watch.bat \
 && echo '@"%~dp0/busybox" wc %*' >$PREFIX/bin/wc.bat \
 && echo '@"%~dp0/busybox" wget %*' >$PREFIX/bin/wget.bat \
 && echo '@"%~dp0/busybox" which %*' >$PREFIX/bin/which.bat \
 && echo '@"%~dp0/busybox" whoami %*' >$PREFIX/bin/whoami.bat \
 && echo '@"%~dp0/busybox" whois %*' >$PREFIX/bin/whois.bat \
 && echo '@"%~dp0/busybox" xargs %*' >$PREFIX/bin/xargs.bat \
 && echo '@"%~dp0/busybox" xz %*' >$PREFIX/bin/xz.bat \
 && echo '@"%~dp0/busybox" xzcat %*' >$PREFIX/bin/xzcat.bat \
 && echo '@"%~dp0/busybox" yes %*' >$PREFIX/bin/yes.bat \
 && echo '@"%~dp0/busybox" zcat %*' >$PREFIX/bin/zcat.bat

# TODO: Either somehow use $VIM_VERSION or normalize the workdir
WORKDIR /vim82/src
RUN make -j$(nproc) -f Make_ming.mak \
        ARCH=x86-64 OPTIMIZE=SIZE STATIC_STDCPLUS=yes HAS_GCC_EH=no \
        UNDER_CYGWIN=yes CROSS=yes CROSS_COMPILE=x86_64-w64-mingw32- \
        FEATURES=HUGE OLE=no IME=no NETBEANS=no
RUN make -j$(nproc) -f Make_ming.mak \
        ARCH=x86-64 OPTIMIZE=SIZE STATIC_STDCPLUS=yes HAS_GCC_EH=no \
        UNDER_CYGWIN=yes CROSS=yes CROSS_COMPILE=x86_64-w64-mingw32- \
        FEATURES=HUGE OLE=no IME=no NETBEANS=no \
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
COPY README.md Dockerfile SHA256SUMS $PREFIX/
RUN cp /mingw-w64-v$MINGW_VERSION/COPYING.MinGW-w64-runtime/COPYING.MinGW-w64-runtime.txt \
        $PREFIX/
RUN printf "\n===========\nwinpthreads\n===========\n\n" \
        >>$PREFIX/COPYING.MinGW-w64-runtime.txt .
RUN cat /mingw-w64-v$MINGW_VERSION/mingw-w64-libraries/winpthreads/COPYING \
        >>$PREFIX/COPYING.MinGW-w64-runtime.txt
RUN printf '@set PATH=%%~dp0\\bin;%%PATH%%\r\n@busybox sh -l\r\n' \
        >$PREFIX/activate.bat
RUN echo $VERSION >$PREFIX/VERSION.txt
ENV PREFIX=${PREFIX}
CMD zip -qXr - $PREFIX
