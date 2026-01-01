# llama.cpp server and DLL build (CPU inference only)
#
# llama.cpp is an amazing project, but its build system is poor and
# growing worse. It's never properly built llama.dll under any compiler,
# and DLL builds have been unsupported by w64dk for some time. This
# makefile is a replacement build system that produces llama.dll and
# llama-server.exe using w64dk. No source file changes are needed.
#
# The DLL exports the public API and no more, and is readily usable as a
# component in another project (game engine, etc.). The server EXE is
# fully functional on Windows 7 or later. It is not linked against the
# DLL, since that's not useful, but can be made to do so with a small
# tweak to this makefile.
#
# Invoke this makefile in the llama.cpp source tree:
#
#   $ make -j$(nproc) -f path/to/w64devkit/contrib/llama.mak
#
# Incremental builds are unsupported, so clean rebuild after pulling. It
# was last tested at b7607, and an update will inevitably break it.

CROSS    =
CPPFLAGS = -w -O2 -march=x86-64-v3
LDFLAGS  = -s

.SUFFIXES: .c .cpp .o

# Parse GGML version from ggml/CMakeLists.txt
GGML_VERSION_MAJOR := $(shell sed -n 's/^set(GGML_VERSION_MAJOR \(.*\))/\1/p' ggml/CMakeLists.txt)
GGML_VERSION_MINOR := $(shell sed -n 's/^set(GGML_VERSION_MINOR \(.*\))/\1/p' ggml/CMakeLists.txt)
GGML_VERSION_PATCH := $(shell sed -n 's/^set(GGML_VERSION_PATCH \(.*\))/\1/p' ggml/CMakeLists.txt)
GGML_VERSION := $(GGML_VERSION_MAJOR).$(GGML_VERSION_MINOR).$(GGML_VERSION_PATCH)

GGML_COMMIT := $(shell git -C ggml rev-parse --short HEAD || echo unknown)

def = \
  -DGGML_COMMIT='"$(GGML_COMMIT)"' \
  -DGGML_USE_CPU \
  -DGGML_VERSION='"$(GGML_VERSION)"' \
  -DLLAMA_USE_HTTPLIB
inc = \
  -I. \
  -Icommon \
  -Iggml/include \
  -Iggml/src \
  -Iggml/src/ggml-cpu \
  -Iinclude \
  -Itools/mtmd \
  -Ivendor \
  -Ivendor/cpp-httplib
%.c.o: %.c
	$(CROSS)gcc -c -o $@ $(inc) $(def) $(CPPFLAGS) $<
%.cpp.o: %.cpp
	$(CROSS)g++ -c -o $@ $(inc) $(def) $(CPPFLAGS) $<

dll = \
  $(addsuffix .o,$(wildcard \
      ggml/src/*.c \
      ggml/src/*.cpp \
      ggml/src/ggml-cpu/*.c \
      ggml/src/ggml-cpu/*.cpp \
      ggml/src/ggml-cpu/arch/x86/*.c \
      ggml/src/ggml-cpu/arch/x86/*.cpp \
      src/*.cpp \
      src/models/*.cpp \
   ))

exe = \
  tools/mtmd/clip.cpp.o \
  tools/mtmd/mtmd-audio.cpp.o \
  tools/mtmd/mtmd-helper.cpp.o \
  tools/mtmd/mtmd.cpp.o \
  w64dk-build-info.cpp.o \
  $(addsuffix .o,$(wildcard \
      common/*.cpp \
      tools/server/*.cpp \
      tools/mtmd/models/*.cpp \
      vendor/cpp-httplib/*.cpp \
   ))

all: llama.dll llama.dll.a llama-server.exe

llama-server.exe: $(exe) $(dll)
	$(CROSS)g++ $(LDFLAGS) -o $@ $(exe) $(dll) -lws2_32

llama.dll: $(dll) llama.def
	$(CROSS)g++ -shared $(LDFLAGS) -o $@ $(dll) llama.def

llama.dll.a: llama.def
	$(CROSS)dlltool -l $@ -d $^

clean:
	rm -f $(dll) $(exe) llama.def llama.dll llama.dll.a llama-server.exe \
	   tools/server/index.html.gz.hpp tools/server/loading.html.hpp \
	   w64dk-build-info.cpp

.ONESHELL:  # needed for heredocs

# NOTE: produces valid C++ even if Git is unavailable
w64dk-build-info.cpp:
	cat >$@ <<EOF
	int         LLAMA_BUILD_NUMBER = {$$(git rev-list  --count HEAD)};
	char const *LLAMA_COMMIT       = "$$(git rev-parse --short HEAD)";
	char const *LLAMA_COMPILER     = "gcc (GCC) $$(gcc -dumpversion)";
	char const *LLAMA_BUILD_TARGET = "$$(gcc -dumpmachine)";
	EOF

w64dk-build-info.cpp.o: w64dk-build-info.cpp

tools/server/index.html.gz.hpp: tools/server/public/index.html.gz
	cd tools/server/public/ && xxd -i index.html.gz >../index.html.gz.hpp
tools/server/loading.html.hpp: tools/server/public/loading.html
	cd tools/server/public/ && xxd -i loading.html >../loading.html.hpp
tools/server/server-http.cpp.o: \
  tools/server/server.cpp \
  tools/server/index.html.gz.hpp \
  tools/server/loading.html.hpp

llama.def: $(dll)
	printf 'LIBRARY llama\nEXPORTS\n' >$@
	$(CROSS)nm -j $(dll) | grep '^llama_[a-z0-9_]\+$$' | sort >>$@
