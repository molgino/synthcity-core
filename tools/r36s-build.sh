#!/usr/bin/env bash
# Cross-Build der Geraete-Huelle fuer den R36S (aarch64) auf einem x86-Linux.
# Gebaut wird gegen einen Ubuntu-18.04-arm64-Sysroot (glibc 2.27, SDL 2.0.8): dasselbe Programm
# laeuft so auf ArkOS (SDL-Fenster) und auf dem Groovebox-OS (GB_DISPLAY=drm, Bild direkt per DRM).
# Ohne libstdc++ gelinkt - der Kern braucht nur C-Bibliotheken.
# Voraussetzung:  sudo apt install g++-aarch64-linux-gnu curl
# Ergebnis:       build/synthcity-aarch64
set -euo pipefail
cd "$(dirname "$0")/.."
SYSROOT=${SYSROOT:-$PWD/.sysroot}
MIRROR=http://ports.ubuntu.com/ubuntu-ports
PKGS=(
  pool/main/g/glibc/libc6_2.27-3ubuntu1.6_arm64.deb
  pool/main/g/glibc/libc6-dev_2.27-3ubuntu1.6_arm64.deb
  pool/main/l/linux/linux-libc-dev_4.15.0-213.224_arm64.deb
  pool/universe/libs/libsdl2/libsdl2-dev_2.0.8+dfsg1-1ubuntu1.18.04.4_arm64.deb
  pool/universe/libs/libsdl2/libsdl2-2.0-0_2.0.8+dfsg1-1ubuntu1.18.04.4_arm64.deb
  pool/main/a/alsa-lib/libasound2_1.1.3-5ubuntu0.6_arm64.deb
  pool/main/a/alsa-lib/libasound2-dev_1.1.3-5ubuntu0.6_arm64.deb
)
if [ ! -f "$SYSROOT/.done2" ]; then
  echo ">> Sysroot anlegen in $SYSROOT"
  mkdir -p "$SYSROOT/debs"
  for p in "${PKGS[@]}"; do
    f="$SYSROOT/debs/$(basename "$p")"
    [ -f "$f" ] || curl -fsSL -o "$f" "$MIRROR/$p"
    dpkg-deb -x "$f" "$SYSROOT"
  done
  ( cd "$SYSROOT/usr/lib/aarch64-linux-gnu"
    for l in $(find . -maxdepth 1 -type l); do
      t=$(readlink "$l"); case "$t" in /*) ln -sf "$SYSROOT$t" "$l";; esac
    done )
  touch "$SYSROOT/.done2"
fi
CROSS=${CROSS:-aarch64-linux-gnu-}
GCCV=$(${CROSS}gcc -dumpversion | cut -d. -f1)
CXXINC=/usr/aarch64-linux-gnu/include/c++/$GCCV
mkdir -p build
# C-Header aus dem Sysroot (alte glibc), aus dem Compiler nur die header-only-Teile (<atomic>)
${CROSS}g++ --sysroot="$SYSROOT" -std=c++17 -O2 -mcpu=cortex-a35 -fno-math-errno \
  -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables \
  -DSC_NO_LIBSTDCXX -Wall -Wno-unused-parameter -Wno-format-truncation \
  -nostdinc -isystem "$(${CROSS}gcc -print-file-name=include)" \
  -isystem "$CXXINC" -isystem "$CXXINC/aarch64-linux-gnu" \
  -isystem "$SYSROOT/usr/include/aarch64-linux-gnu" -isystem "$SYSROOT/usr/include" \
  -I"$SYSROOT/usr/include/SDL2" -D_REENTRANT \
  -c device/main.cpp -o build/r36s-main.o
for f in input drm midi usbaudio web webpage dhcpd; do
  ${CROSS}g++ --sysroot="$SYSROOT" -std=c++17 -O2 -mcpu=cortex-a35 -fno-exceptions -fno-rtti \
    -fno-threadsafe-statics -fno-asynchronous-unwind-tables -Wno-format-truncation -DWITH_MIDI -nostdinc \
    -isystem "$(${CROSS}gcc -print-file-name=include)" \
    -isystem "$SYSROOT/usr/include/aarch64-linux-gnu" -isystem "$SYSROOT/usr/include" \
    -I"$SYSROOT/usr/include/SDL2" -D_REENTRANT -c device/$f.cpp -o build/r36s-$f.o
done
${CROSS}gcc --sysroot="$SYSROOT" -B"$SYSROOT/usr/lib/aarch64-linux-gnu" \
  -L"$SYSROOT/lib/aarch64-linux-gnu" -L"$SYSROOT/usr/lib/aarch64-linux-gnu" \
  -Wl,-rpath-link,"$SYSROOT/lib/aarch64-linux-gnu" -Wl,--allow-shlib-undefined \
  -o build/synthcity-aarch64 build/r36s-*.o -lSDL2 -lasound -lm -lpthread -ldl
${CROSS}strip build/synthcity-aarch64
rm -f build/r36s-*.o
echo ">> fertig: build/synthcity-aarch64 ($(du -h build/synthcity-aarch64 | cut -f1))"
${CROSS}objdump -T build/synthcity-aarch64 | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1 | sed 's/^/   hoechste benoetigte glibc: /'
${CROSS}objdump -p build/synthcity-aarch64 | grep NEEDED | sed 's/^ */   /'
