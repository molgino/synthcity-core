CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter
CLAPINC = -Ithird_party/clap/include
LDLIBS ?= -lm
SAN ?= 0
ifeq ($(SAN),1)
CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1
endif

WASMFLAGS = --target=wasm32 -O2 -std=c++17 -nostdlib -ffreestanding -fno-exceptions -fno-rtti \
            -fno-threadsafe-statics -DSC_NO_FILES -Iweb/stub -include web/freestanding.h \
            -Wl,--no-entry -Wl,--export-all -Wl,--initial-memory=67108864
BINS = tests/unit tests/render tests/plugin tests/sampler tools/bench tools/bench_device tools/demo build/synthcity.clap web/synthcity.wasm
HDRS = $(wildcard src/*.h)

test: tests/unit tests/render tests/plugin tests/sampler build/synthcity.clap web/synthcity.wasm
	./tests/unit
	./tests/render
	./tests/sampler
	./tests/plugin build/synthcity.clap
	node tests/web.js web/synthcity.wasm

# Oberflaeche im echten Browser. Braucht node, playwright und einen kurzen Webserver.
uitest: web/synthcity.wasm
	@python3 -m http.server 8799 --directory web >/dev/null 2>&1 & srv=$$!; sleep 1; \
	node tests/browser.js http://localhost:8799/index.html && node tests/konto.js http://localhost:8799/index.html; st=$$?; \
	kill $$srv >/dev/null 2>&1; wait $$srv 2>/dev/null; exit $$st

bench: tools/bench
	./tools/bench 64

demo: tools/demo
	./tools/demo demo.wav

plugin: build/synthcity.clap

# Browser-Bau: clang kann wasm32 von Haus aus; die fehlende libc steht in web/freestanding.*
web: web/synthcity.wasm
web/synthcity.wasm: web/bridge.cpp web/freestanding.cpp web/freestanding.h $(HDRS)
	clang++ $(WASMFLAGS) -o $@ web/bridge.cpp web/freestanding.cpp

tests/unit:   tests/unit.cpp   $(HDRS) ; $(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)
tests/render: tests/render.cpp $(HDRS) ; $(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)
tests/sampler: tests/sampler.cpp web/bridge.cpp $(HDRS) ; $(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)
tools/bench:  tools/bench.cpp  $(HDRS) ; $(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)
tools/demo:   tools/demo.cpp   $(HDRS) ; $(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)
tools/bench_device: tools/bench_device.cpp $(HDRS) ; $(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)

build/synthcity.clap: plugin/clap_plugin.cpp $(HDRS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(CLAPINC) -fPIC -shared -o $@ $< $(LDLIBS)

tests/plugin: tests/plugin.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) $(CLAPINC) -o $@ $< -ldl $(LDLIBS)

clean:
	rm -f $(BINS)

.PHONY: test uitest bench demo plugin web clean

# --- Geraet (R36S-Huelle): SDL2, Bild per Fenster oder DRM, Ton per SDL/ALSA ---------------
DEVSRC = device/main.cpp device/input.cpp device/drm.cpp device/midi.cpp device/usbaudio.cpp device/web.cpp device/webpage.cpp device/dhcpd.cpp
SDL_CFLAGS ?= $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   ?= $(shell sdl2-config --libs 2>/dev/null)
device: build/synthcity-device
build/synthcity-device: $(DEVSRC) device/*.h web/bridge.cpp $(HDRS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -fno-exceptions -fno-rtti -Wno-missing-field-initializers -Wno-format-truncation -DWITH_MIDI $(SDL_CFLAGS) -o $@ $(DEVSRC) $(SDL_LIBS) -lasound -lpthread $(LDLIBS)
# Dasselbe Programm mit Sanitizern: Speicherfehler/undefiniertes Verhalten bzw. Datenrennen
# (zweiter Rechenkern, Webserver, MIDI). Beide laufen auch in der CI.
DEVFLAGS = -std=c++17 -fno-exceptions -fno-rtti -Wno-format-truncation -DWITH_MIDI $(SDL_CFLAGS)
build/synthcity-device-asan: $(DEVSRC) device/*.h web/bridge.cpp $(HDRS)
	@mkdir -p build
	$(CXX) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $(DEVFLAGS) -o $@ $(DEVSRC) $(SDL_LIBS) -lasound -lpthread $(LDLIBS)
build/synthcity-device-tsan: $(DEVSRC) device/*.h web/bridge.cpp $(HDRS)
	@mkdir -p build
	$(CXX) -O1 -g -fsanitize=thread $(DEVFLAGS) -o $@ $(DEVSRC) $(SDL_LIBS) -lasound -lpthread $(LDLIBS)
devtest-asan: build/synthcity-device-asan
	@rm -rf build/devhome-asan && mkdir -p build/devhome-asan
	SC_HOME=build/devhome-asan ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./build/synthcity-device-asan --selftest
devtest-tsan: build/synthcity-device-tsan
	@rm -rf build/devhome-tsan && mkdir -p build/devhome-tsan
	SC_HOME=build/devhome-tsan TSAN_OPTIONS=halt_on_error=1 ./build/synthcity-device-tsan --selftest
devtest: build/synthcity-device
	@rm -rf build/devhome && mkdir -p build/devhome build/shots
	SC_HOME=build/devhome ./build/synthcity-device --shots build/shots
# Cross-Build fuer den R36S (aarch64, ohne libstdc++): siehe tools/r36s-build.sh
r36s:
	./tools/r36s-build.sh
.PHONY: device devtest devtest-asan devtest-tsan r36s
