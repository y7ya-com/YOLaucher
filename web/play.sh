#!/bin/bash
# play.sh [game]: one command from a fresh checkout to yo2.exe in a browser tab. Idempotent: each step skips what is
# already built or running, so rerunning it just prints the URL again. web/stop.sh stops the servers.
#
#   game   the owner's own client folder (yo2.exe, merge.dat, ...) or yo2_2008-11_Extracted.7z. Only needed the
#          first time (it goes into yo2.zip). Never hosted, never committed.
#
# Large artifacts go to $YO2WEB (default ~/.yo2web):
#   bw/            Boxedwine at 2dbabfc + web/boxedwine-yo2.patch (lazy zip, present fix, WebGL streaming fix) and
#                  web/boxedwine-sound.patch (SDL queue retry instead of a crash)
#   fs-web.zip     Boxedwine's Wine 11 web filesystem (99.5 MB download)
#   play/          the site that gets served, with yo2.zip (716 MB, store-only, read lazily with HTTP ranges)
#   play/game/     the small game files + version.dll / yo2.reg / start.bat that go into yo2.zip
# Boxedwine is built as multiThreaded (no JIT: the JIT build passes 2.5 GB in the page before the title).
# Env: PORT (static site, 8200), GW_PORT (network gateway, 8201), BW_DIST_URL (.tar.gz of a prebuilt Build/<target> to
# fetch instead of compiling). Needs node, python3.12, zip, curl, i686-w64-mingw32-gcc, emcc 6.x (to build Boxedwine;
# with Homebrew's emscripten, EMSDK_PYTHON is set for you), and 7zz only for a .7z.
set -euo pipefail
WEB=$(cd "$(dirname "$0")" && pwd); REPO=$(dirname "$WEB")
DATA=${YO2WEB:-$HOME/.yo2web}
GAME=${1:-}; PORT=${PORT:-8200}; GW_PORT=${GW_PORT:-8201}
BW=$DATA/bw; E=$BW/project/emscripten; BUILD=$E/Build/MultiThreaded
SITE=$DATA/play; G=$SITE/game; PIDS=$SITE/pids
mkdir -p "$SITE" "$G" "$PIDS"
need() { command -v "$1" >/dev/null || { echo "missing: $1 ($2)" >&2; exit 1; }; }
need node "Node 22+"; need python3.12 "brew install python@3.12"; need zip zip; need i686-w64-mingw32-gcc "brew install mingw-w64"

# 1. Boxedwine 2dbabfc + patches, built (or fetched)
[ -d "$BW/.git" ] || { git clone https://github.com/danoon2/Boxedwine "$BW"; git -C "$BW" checkout -q 2dbabfc; for p in boxedwine-yo2 boxedwine-sound; do git -C "$BW" apply "$WEB/$p.patch"; done; }
if [ ! -f "$BUILD/boxedwine.wasm" ]; then
  if [ -n "${BW_DIST_URL:-}" ]; then
    mkdir -p "$BUILD"; curl -fsSL "$BW_DIST_URL" | tar -xz -C "$BUILD"
  else
    need emcc "emsdk 6.x, or set BW_DIST_URL"
    (cd "$E" && EMSDK_PYTHON=${EMSDK_PYTHON:-$(command -v python3.12)} make multiThreaded)
  fi
fi
for f in boxedwine.js boxedwine.wasm boxedwine-shell.js boxedwine.css fsfs.js; do cp -p "$BUILD/$f" "$SITE/"; done
cp -p "$WEB/lazydrive.js" "$SITE/"
sed 's#<script src=fsfs.js></script>#<script src=fsfs.js></script><script src=lazydrive.js></script>#' "$BUILD/boxedwine.html" > "$SITE/boxedwine.html"

# 2. Wine filesystem
[ -s "$DATA/fs-web.zip" ] || { echo "downloading the Wine filesystem (95 MB)"; curl -fsSL -o "$DATA/fs-web.zip" https://boxedwine.org/v2/22/TinyCore15Wine11.0-web.zip; }
ln -sfn "$DATA/fs-web.zip" "$SITE/boxedwine.zip"
# Boot overlay: the prefix registry with unused services off and the hook's DllOverride baked in (~2 s faster boot)
[ "$SITE/overlay.zip" -nt "$WEB/bootoverlay.py" ] || python3 "$WEB/bootoverlay.py" "$DATA/fs-web.zip" "$SITE/overlay.zip" >/dev/null

# 3. yo2.zip = the owner's files (no Sound.dat; system.bin switched to English) + hook DLL + registry + launcher batch,
# store-only for range reads.
# Wine only loads a native version.dll through the registry; FORCEFS keeps the client at 1024x768 full screen (a framed
# window doesn't fit Boxedwine's screen and small buttons then miss); 8 texture loader threads instead of 128 (each
# guest thread is a Web Worker); 1 GB of video memory (entering a lobby room failed with "Out of adapter memory");
# SKIPINTRO=2 skips the KONAMI logo and the rules page.
DLL=$REPO/tools/yo2emu/launcher/juego/dll
[ -f "$G/version.dll" ] && [ "$G/version.dll" -nt "$DLL/yo2net.c" ] ||
  i686-w64-mingw32-gcc -O2 -s -shared -nostdlib -ffreestanding -fno-builtin -fno-stack-protector \
    -fno-asynchronous-unwind-tables -Wall -Wno-unused-function -Wl,--entry,_DllMain@12 -Wl,--image-base,0x6a5d0000 \
    -Wl,--dynamicbase -Wl,--nxcompat "$DLL/yo2net.c" "$DLL/version.def" -lkernel32 -luser32 -lgdi32 -o "$G/version.dll"
printf 'REGEDIT4\r\n\r\n[HKEY_CURRENT_USER\\Software\\Wine\\DllOverrides]\r\n"version"="native,builtin"\r\n\r\n[HKEY_CURRENT_USER\\Software\\Wine\\Direct3D]\r\n"renderer"="gl"\r\n"VideoMemorySize"="1024"\r\n' > "$G/yo2.reg.new"
printf 'regedit /S yo2.reg\r\nset YO2ONLINE_NET=1\r\nset YO2ONLINE_TEXLOADERS=8\r\nset YO2ONLINE_WINDOW_FORCEFS=1\r\nset YO2ONLINE_SKIPINTRO=2\r\nyo2.exe\r\n' > "$G/start.bat.new"
for f in yo2.reg start.bat; do cmp -s "$G/$f.new" "$G/$f" && rm "$G/$f.new" || mv "$G/$f.new" "$G/$f"; done
FILES="yo2.exe merge.dat merge.log system.bin TestData.dat MagicKey.dll"
if [ ! -f "$SITE/yo2.zip" ]; then
  [ -n "$GAME" ] && [ -e "$GAME" ] || { echo "first run: pass your client folder or yo2_2008-11_Extracted.7z" >&2; exit 1; }
  if [ -d "$GAME" ]; then for f in $FILES; do ln -sf "$(cd "$GAME" && pwd)/$f" "$G/$f"; done; rm "$G/system.bin"; cp "$GAME/system.bin" "$G/"
  else need 7zz "brew install sevenzip"; 7zz e -y -o"$G" "$GAME" $FILES >/dev/null; fi
  python3.12 "$WEB/systembin.py" "$G/system.bin" language=1 >/dev/null   # the stock file says 2 = German
  (cd "$G" && zip -0 -q "$SITE/yo2.zip" $FILES version.dll yo2.reg start.bat)
  [ -L "$G/merge.dat" ] || rm -f "$G/merge.dat"   # 690 MB extracted copy, only needed inside the zip
fi
python3.12 "$WEB/systembin.py" "$G/system.bin" | grep -q "^language = 1" || python3.12 "$WEB/systembin.py" "$G/system.bin" language=1 >/dev/null
CHANGED=$(cd "$G" && for f in version.dll yo2.reg start.bat system.bin; do [ "$f" -nt "$SITE/yo2.zip" ] && echo "$f"; done; true)
[ -z "$CHANGED" ] || (cd "$G" && zip -0 -q "$SITE/yo2.zip" $CHANGED)

# 4. Servers (background; pids in play/pids): static site with COOP/COEP + Range, network gateway, run.py
up() { [ -f "$PIDS/$1" ] && kill -0 "$(cat "$PIDS/$1")" 2>/dev/null; }
up web || { nohup node "$E/server.mjs" --root "$SITE" --port "$PORT" > "$SITE/web.log" 2>&1 & echo $! > "$PIDS/web"; }
up gateway || { nohup node "$E/network-gateway.mjs" --port "$GW_PORT" --allow-origin "http://127.0.0.1:$PORT" > "$SITE/gateway.log" 2>&1 & echo $! > "$PIDS/gateway"; }
if ! up server; then
  if lsof -nP -iTCP:5730 -sTCP:LISTEN >/dev/null; then echo "run.py already running (port 5730), reusing it"
  else (cd "$REPO/tools/yo2emu" && YO2_NO_CLIENT=1 nohup python3.12 -u run.py 0 > "$SITE/server.log" 2>&1 & echo $! > "$PIDS/server"); fi
fi
sleep 1
URL="http://127.0.0.1:$PORT/boxedwine.html?app=yo2&lazyapp=1&p=start.bat&network=true&networkGateway=ws%3A%2F%2F127.0.0.1%3A$GW_PORT%2Fboxedwine-network&overlay=overlay.zip&resolution=1024x768&storage=memory&sound=false"
echo "Open in Chrome (~1.5 GB for the tab; the title appears after about a minute):"
echo "$URL"
