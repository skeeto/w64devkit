# Build and install Cppcheck inside w64devkit
#
# Invoke in a Cppcheck source tree:
#   $ make -j$(nproc) -f path/to/cppcheck.mak install
#
# Alternatively, use the default target and run it in place in the
# source tree without installing.

ext      := $(shell find externals -mindepth 1 -type d)
src      := $(shell find cli lib externals -name '*.cpp')
obj      := $(src:.cpp=.o)
CXXFLAGS := -w -O2 -Ilib $(addprefix -I,$(ext))

cppcheck.exe: $(obj)
	$(CXX) -s -o $@ $(obj) -lshlwapi

install: cppcheck.exe
	mkdir -p "$$W64DEVKIT_HOME"/share/cppcheck
	cp -r cppcheck.exe cfg/ "$$W64DEVKIT_HOME"/share/cppcheck/
	$(CC) -DEXE=../share/cppcheck/cppcheck.exe -DCMD=cppcheck \
	      -Oz -s -nostartfiles -o "$$W64DEVKIT_HOME"/bin/cppcheck.exe \
	      "$$W64DEVKIT_HOME"/src/alias.c

uninstall:
	rm -rf "$$W64DEVKIT_HOME"/share/cppcheck/ \
	       "$$W64DEVKIT_HOME"/bin/cppcheck.exe
