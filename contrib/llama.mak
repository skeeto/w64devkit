# llama.cpp server and DLL build (CPU or Vulkan inference)
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
# To build with Vulkan GPU support, set USE_VULKAN=1:
# Requires Vulkan SDK installed (set VULKAN_SDK if not at default location).
#
#   $ make -j$(nproc) -f path/to/w64devkit/contrib/llama.mak USE_VULKAN=1
#
# Incremental builds are unsupported, so clean rebuild after pulling. It
# was last tested at b7955, and an update will inevitably break it.

CROSS    =
CPPFLAGS = -w -O2 -march=x86-64-v3
LDFLAGS  = -s
USE_VULKAN = 0

.SUFFIXES: .c .cpp .o

# Parse GGML version from ggml/CMakeLists.txt
GGML_VERSION_MAJOR := $(shell sed -n 's/^set(GGML_VERSION_MAJOR \(.*\))/\1/p' ggml/CMakeLists.txt)
GGML_VERSION_MINOR := $(shell sed -n 's/^set(GGML_VERSION_MINOR \(.*\))/\1/p' ggml/CMakeLists.txt)
GGML_VERSION_PATCH := $(shell sed -n 's/^set(GGML_VERSION_PATCH \(.*\))/\1/p' ggml/CMakeLists.txt)
GGML_VERSION := $(GGML_VERSION_MAJOR).$(GGML_VERSION_MINOR).$(GGML_VERSION_PATCH)

GGML_COMMIT := $(shell git -C ggml rev-parse --short HEAD || echo unknown)

ifeq ($(USE_VULKAN),1)
    VULKAN_SDK ?= C:/VulkanSDK/1.4.328.1
    GLSLC := "$(VULKAN_SDK)/Bin/glslc.exe"
    SHADER_GEN := vulkan-shaders-gen.exe
    SHADER_BUILD_DIR := build_shaders
    SHADER_HEADER := $(SHADER_BUILD_DIR)/ggml-vulkan-shaders.hpp

    def_vk = -DGGML_USE_VULKAN
    inc_vk = -I$(VULKAN_SDK)/Include -I$(SHADER_BUILD_DIR)
    ldflags_vk = -L$(VULKAN_SDK)/Lib -lvulkan-1
else
    def_vk =
    inc_vk =
    ldflags_vk =
endif

def = \
  -DGGML_COMMIT='"$(GGML_COMMIT)"' \
  -DGGML_USE_CPU \
  -DGGML_VERSION='"$(GGML_VERSION)"' \
  -DLLAMA_USE_HTTPLIB \
  $(def_vk)
inc = \
  -I. \
  -Icommon \
  -Iggml/include \
  -Iggml/src \
  -Iggml/src/ggml-cpu \
  -Iinclude \
  -Itools/mtmd \
  -Ivendor \
  -Ivendor/cpp-httplib \
  $(inc_vk)
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

ifeq ($(USE_VULKAN),1)
    dll += ggml/src/ggml-vulkan/ggml-vulkan.cpp.o
    vk_shaders = $(wildcard ggml/src/ggml-vulkan/vulkan-shaders/*.comp)
    vk_shader_gen_src = $(wildcard ggml/src/ggml-vulkan/vulkan-shaders/*.cpp ggml/src/ggml-vulkan/vulkan-shaders/*.h)
    vk_shader_objs = $(patsubst %.comp,$(SHADER_BUILD_DIR)/%.comp.cpp.o,$(notdir $(vk_shaders)))
    dll += $(vk_shader_objs)
endif

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

all: llama.dll llama.dll.a llama-server.exe

llama-server.exe: $(exe) $(dll)
	$(CROSS)g++ $(LDFLAGS) -o $@ $(exe) $(dll) -lws2_32 $(ldflags_vk)

llama.dll: $(dll) llama.def
	$(CROSS)g++ -shared $(LDFLAGS) -o $@ $(dll) llama.def $(ldflags_vk)

llama.dll.a: llama.def
	$(CROSS)dlltool -l $@ -d $^

clean:
	rm -f $(dll) $(exe) llama.def llama.dll llama.dll.a llama-server.exe \
	   tools/server/index.html.gz.hpp tools/server/loading.html.hpp \
	   w64dk-build-info.cpp w64dk-license.c $(SHADER_GEN)
	rm -rf $(SHADER_BUILD_DIR)

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
ifeq ($(USE_VULKAN),1)

# vulkan-shaders-gen spawns multiple glslc processes per shader, for each
# variant, so parallelizing it spawns hundreds of glslc processes and fails
.NOTPARALLEL: $(vk_shader_objs)

$(SHADER_GEN): $(vk_shader_gen_src)
	@echo "Building shader generator..."
	@mkdir -p $(SHADER_BUILD_DIR)
	$(CROSS)g++ -std=c++17 -O2 -o $@ \
		ggml/src/ggml-vulkan/vulkan-shaders/vulkan-shaders-gen.cpp \
		-Iggml/src/ggml-vulkan/vulkan-shaders

$(SHADER_HEADER): $(SHADER_GEN) $(vk_shaders)
	@echo "Generating shader header..."
	@mkdir -p $(SHADER_BUILD_DIR)/vulkan-shaders.spv
	./$(SHADER_GEN) --output-dir $(SHADER_BUILD_DIR)/vulkan-shaders.spv \
		--target-hpp $(SHADER_HEADER)

# Pattern rule: each .comp file generates a .cpp file
$(SHADER_BUILD_DIR)/%.comp.cpp: ggml/src/ggml-vulkan/vulkan-shaders/%.comp $(SHADER_GEN) $(SHADER_HEADER)
	@echo "Compiling shader $<..."
	./$(SHADER_GEN) --glslc $(GLSLC) \
		--source $< \
		--output-dir $(SHADER_BUILD_DIR)/vulkan-shaders.spv \
		--target-hpp $(SHADER_HEADER) \
		--target-cpp $@

# Pattern rule: compile each shader .cpp to .o
$(SHADER_BUILD_DIR)/%.comp.cpp.o: $(SHADER_BUILD_DIR)/%.comp.cpp
	$(CROSS)g++ -c -o $@ $(inc) $(def) $(CPPFLAGS) $<

ggml/src/ggml-vulkan/ggml-vulkan.cpp.o: $(SHADER_HEADER)

endif
