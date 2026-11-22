#!/bin/bash
# SYNTH CITY CORE - Starter fuer ArkOS (Ports-Menue). Beenden: SELECT + START 2 s halten
# oder PROJECT > QUIT. Projekte und Samples liegen im Ordner synthcity/ daneben.
DIR="/roms/ports/synthcity"
[ -d "/roms2/ports/synthcity" ] && DIR="/roms2/ports/synthcity"
cd "$DIR" || exit 1
chmod +x ./synthcity 2>/dev/null
export SDL_AUDIODRIVER=alsa
./synthcity "$@" > "$DIR/synthcity.log" 2>&1
printf "\033c" > /dev/tty1 2>/dev/null
