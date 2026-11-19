CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter
LDLIBS ?= -lm
SAN ?= 0
ifeq ($(SAN),1)
CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1
endif

BINS = tests/unit tests/render tools/bench tools/demo
HDRS = $(wildcard src/*.h)

test: tests/unit tests/render
	./tests/unit
	./tests/render

bench: tools/bench
	./tools/bench 64

demo: tools/demo
	./tools/demo demo.wav

tests/unit:   tests/unit.cpp   $(HDRS) ; $(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)
tests/render: tests/render.cpp $(HDRS) ; $(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)
tools/bench:  tools/bench.cpp  $(HDRS) ; $(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)
tools/demo:   tools/demo.cpp   $(HDRS) ; $(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f $(BINS)

.PHONY: test bench demo clean
