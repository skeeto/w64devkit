ext      := $(shell find externals -mindepth 1 -type d)
src      := $(shell find cli lib externals -name '*.cpp')
obj      := $(src:.cpp=.o)
CXXFLAGS := -w -Os -Ilib $(addprefix -I,$(ext))
cppcheck.exe: $(obj)
	$(CXX) -s -o $@ $(obj) -lshlwapi
cppcheck: $(obj)
	$(CXX) -pthread -s -o $@ $(obj)
