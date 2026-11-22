#!/usr/bin/env bash
# Pakete fuer den R36S aus build/synthcity-aarch64 (vorher tools/r36s-build.sh):
#   dist/synthcity-os-update.tgz  App-Update fuers Groovebox-OS: ersetzt das Programm unter
#                                 /opt/groovebox (die alte Version bleibt als groovebox.bak)
#   dist/synthcity-arkos.zip      Ports-Ordner fuer ArkOS: Inhalt nach /roms/ports/ kopieren
set -euo pipefail
cd "$(dirname "$0")/.."
[ -f build/synthcity-aarch64 ] || ./tools/r36s-build.sh
VER="synthcity-core $(git rev-parse --short HEAD 2>/dev/null || echo lokal) $(date +%Y-%m-%d)"
mkdir -p dist
# Groovebox-OS: groovebox-run installiert /data/update/*.tgz beim naechsten Start und stellt die
# alte Version wieder her, wenn die neue dreimal direkt nach dem Start abstuerzt.
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
install -m 755 build/synthcity-aarch64 "$T/groovebox"
echo "$VER" > "$T/VERSION"
tar -czf dist/synthcity-os-update.tgz -C "$T" groovebox VERSION
# ArkOS
A=$(mktemp -d)
mkdir -p "$A/synthcity/projects" "$A/synthcity/samples"
install -m 755 build/synthcity-aarch64 "$A/synthcity/synthcity"
install -m 755 "device/arkos/SYNTH CITY CORE.sh" "$A/"
echo "$VER" > "$A/synthcity/VERSION"
rm -f dist/synthcity-arkos.zip
(cd "$A" && zip -qr - .) > dist/synthcity-arkos.zip
rm -rf "$A"
echo ">> dist/synthcity-os-update.tgz, dist/synthcity-arkos.zip ($VER)"
