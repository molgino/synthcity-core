CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter
CLAPINC = -Ithird_party/clap/include
LDLIBS ?= -lm
SAN ?= 0
ifeq ($(SAN),1)
CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1
endif

BINS = tests/unit tests/render tests/plugin tools/bench tools/demo build/synthcity.clap
HDRS = $(wildcard src/*.h)

test: tests/unit tests/render tests/plugin build/synthcity.clap
	./tests/unit
	./tests/render
	./tests/plugin build/synthcity.clap

bench: tools/bench
	./tools/bench 64

demo: tools/demo
	./tools/demo demo.wav

plugin: build/synthcity.clap

tests/unit:   tests/unit.cpp   $(HDRS) ; $(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)
tests/render: tests/render.cpp $(HDRS) ; $(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)
tools/bench:  tools/bench.cpp  $(HDRS) ; $(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)
tools/demo:   tools/demo.cpp   $(HDRS) ; $(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)

build/synthcity.clap: plugin/clap_plugin.cpp $(HDRS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(CLAPINC) -fPIC -shared -o $@ $< $(LDLIBS)

tests/plugin: tests/plugin.cpp
	$(CXX) $(CXXFLAGS) $(CLAPINC) -o $@ $< -ldl $(LDLIBS)

clean:
	rm -f $(BINS)

.PHONY: test bench demo plugin clean
