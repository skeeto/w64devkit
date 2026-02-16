# llama.cpp server and DLL build (CPU or Vulkan inference)
#
# The official llama.cpp build is CMake. This makefile is a replacement
# build system that produces llama.dll (with the correct exports) and
# llama-server.exe using w64dk. No source changes required.
#
# The DLL exports the public API and no more, and is readily usable as a
# component in another project (game engine, etc.). The server EXE is
# fully functional on Windows 7 or later. It is not linked against the
# DLL because that's not useful.
#
# Invoke this makefile in the llama.cpp source tree:
#
#   $ make -j$(nproc) -f path/to/w64devkit/contrib/llama.mak
#
# To build with Vulkan GPU support, set VULKAN_SDK:
#
#   $ make ... VULKAN_SDK=C:/VulkanSDK/1.4.328.1
#
# Incremental builds are unsupported, so clean rebuild after pulling. It
# was last tested at b8124, and an update will inevitably break it.

CC       = gcc
CXX      = g++
CROSS    =
CPPFLAGS = -w -O2 -march=x86-64-v3
LDFLAGS  = -s
HOST_CXX = c++

.SUFFIXES: .c .cpp .o

# Parse GGML version from ggml/CMakeLists.txt
ggml_major := $(shell \
  sed -n 's/^set(GGML_VERSION_MAJOR \(.*\))/\1/p' ggml/CMakeLists.txt \
)
ggml_minor := $(shell \
  sed -n 's/^set(GGML_VERSION_MINOR \(.*\))/\1/p' ggml/CMakeLists.txt \
)
ggml_patch := $(shell \
  sed -n 's/^set(GGML_VERSION_PATCH \(.*\))/\1/p' ggml/CMakeLists.txt \
)
ggml_version = $(ggml_major).$(ggml_minor).$(ggml_patch)
ggml_commit := $(shell git -C ggml rev-parse --short HEAD || echo unknown)

def = \
  -DGGML_COMMIT='"$(ggml_commit)"' \
  -DGGML_USE_CPU \
  -DGGML_VERSION='"$(ggml_version)"' \
  -DLLAMA_USE_HTTPLIB
inc = \
  -I. \
  -Icommon \
  -Iggml/include \
  -Iggml/src \
  -Iggml/src/ggml-cpu \
  -Iinclude \
  -Isrc \
  -Itools/mtmd \
  -Ivendor \
  -Ivendor/cpp-httplib
lib =

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
  w64dk-license.c.o \
  $(addsuffix .o,$(wildcard \
      common/*.cpp \
      common/jinja/*.cpp \
      tools/server/*.cpp \
      tools/mtmd/models/*.cpp \
      vendor/cpp-httplib/*.cpp \
   ))

ifdef VULKAN_SDK
  GLSLC = $(VULKAN_SDK)/Bin/glslc.exe
  vk_shaders_gen = vulkan-shaders-gen.exe
  vk_build_dir = vk-shaders
  vk_shader_header = $(vk_build_dir)/ggml-vulkan-shaders.hpp
  vk_shaders = $(wildcard \
    ggml/src/ggml-vulkan/vulkan-shaders/*.comp \
  )
  vk_shader_gen_obj = $(addsuffix .gen.o,$(wildcard \
    ggml/src/ggml-vulkan/vulkan-shaders/*.cpp \
  ))
  vk_shader_objs = \
    $(patsubst %.comp,$(vk_build_dir)/%.comp.cpp.o,$(notdir $(vk_shaders)))

  def += -DGGML_USE_VULKAN
  inc += -I$(VULKAN_SDK)/Include -I$(vk_build_dir)
  lib += -L$(VULKAN_SDK)/Lib -lvulkan-1
  dll += ggml/src/ggml-vulkan/ggml-vulkan.cpp.o $(vk_shader_objs)
endif

%.c.o: %.c
	$(CROSS)$(CC) -c -o $@ $(inc) $(def) $(CPPFLAGS) $<
%.cpp.o: %.cpp
	$(CROSS)$(CXX) -c -o $@ $(inc) $(def) $(CPPFLAGS) $<

all: llama.dll llama.dll.a llama-server.exe

llama-server.exe: $(exe) $(dll)
	$(CROSS)$(CXX) $(LDFLAGS) -o $@ $(exe) $(dll) -lws2_32 $(lib)

llama.dll: $(dll) llama.def
	$(CROSS)$(CXX) -shared $(LDFLAGS) -o $@ $(dll) llama.def $(lib)

llama.dll.a: llama.def
	$(CROSS)dlltool -l $@ -d $^

clean:
	rm -f $(dll) $(exe) llama.def llama.dll llama.dll.a llama-server.exe \
	   tools/server/index.html.gz.hpp tools/server/loading.html.hpp \
	   w64dk-build-info.cpp w64dk-license.c $(vk_shaders_gen)
	rm -rf $(vk_build_dir)

.ONESHELL:  # needed for heredocs

# NOTE: produces valid C++ even if Git is unavailable
w64dk-build-info.cpp:
	cat >$@ <<EOF
	int         LLAMA_BUILD_NUMBER = {$$(git rev-list  --count HEAD)};
	char const *LLAMA_COMMIT       = "$$(git rev-parse --short HEAD)";
	char const *LLAMA_COMPILER     = "$$($(CC) --version | head -n1)";
	char const *LLAMA_BUILD_TARGET = "$$($(CC) -dumpmachine)";
	EOF

w64dk-build-info.cpp.o: w64dk-build-info.cpp

licenses = LICENSE $(wildcard licenses/LICENSE-*)
w64dk-license.c: $(licenses)
	cat >$@ <<-EOF
	const char *LICENSES[] = {
	$$(for f in $(licenses); do
	    printf '    "%s", (char[]){\n' "$${f##*-}"
	    printf '        #embed "%s"\n' "$$f"
	    printf '        , 0\n'
	    printf '    },\n'
	done)
	    0
	};
	EOF

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

# Vulkan shader generation rules
ifdef VULKAN_SDK

# vulkan-shaders-gen spawns multiple glslc processes per shader, for each
# variant, so parallelizing it spawns hundreds of glslc processes and fails
.NOTPARALLEL: $(vk_shader_objs)

%.cpp.gen.o: %.cpp
	$(HOST_CXX) -c -o $@ -std=c++17 -O2 \
	  -Iggml/src/ggml-vulkan/vulkan-shaders $<
$(vk_shaders_gen): $(vk_shader_gen_obj)
	mkdir -p $(vk_build_dir)
	$(HOST_CXX) -o $@ $(vk_shader_gen_obj)

$(vk_shader_header): $(vk_shaders_gen) $(vk_shaders)
	mkdir -p $(vk_build_dir)/vulkan-shaders.spv
	./$(vk_shaders_gen) \
	  --output-dir $(vk_build_dir)/vulkan-shaders.spv \
	  --target-hpp $(vk_shader_header)

$(vk_build_dir)/%.comp.cpp: \
  ggml/src/ggml-vulkan/vulkan-shaders/%.comp \
  $(vk_shaders_gen) \
  $(vk_shader_header)
	./$(vk_shaders_gen) \
	  --glslc $(GLSLC) \
	  --source $< \
	  --output-dir $(vk_build_dir)/vulkan-shaders.spv \
	  --target-hpp $(vk_shader_header) \
	  --target-cpp $@

# Pattern rule: compile each shader .cpp to .o
$(vk_build_dir)/%.comp.cpp.o: $(vk_build_dir)/%.comp.cpp
	$(CROSS)$(CXX) -c -o $@ $(inc) $(def) $(CPPFLAGS) $<

ggml/src/ggml-vulkan/ggml-vulkan.cpp.o: $(vk_shader_header)

endif  # VULKAN_SDK
