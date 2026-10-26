# YO2 Online in the browser (WASM feasibility)

Can players run the unmodified 2008 client in a browser tab instead of the Windows launcher? Short answer: **probably
yes**. The full stack works end to end with a stand-in exe. The real `yo2.exe` has not been tested yet.

## What runs where

| Piece | Today | Web version |
|---|---|---|
| `yo2.exe` (Win32, Direct3D 8, WinSock 1.1) | Windows | **Boxedwine** (x86 + Wine 11 compiled to WASM) in the tab. Its WineD3D build targets WebGL2. |
| Connect redirect (`version.dll`, `launcher/juego/dll`) | DLL proxy | The same DLL, unchanged. It builds with `i686-w64-mingw32-gcc ... -lws2_32 -lgdi32 -luser32`. In the guest, `127.0.0.1` is the gateway's own host. |
| libp2p / Tor tunnel (`launcher/sala`) | Go launcher | Boxedwine's `network-gateway.mjs`: guest TCP/UDP → WebSocket → real TCP. Run it next to the server. |
| Server (`run.py`, `yo2srv/`) | Python on host | Unchanged and native. Porting it to Pyodide isn't worth it: it uses blocking sockets, `threading` in 36 modules and listening ports, none of which Pyodide supports. |
| Duel engine (`duelcore`, ocgcore-wasm) | Node | Unchanged. It is already WASM, so it could move into the browser later, but the server stays authoritative. |
| Game files | Player's own folder | Player's own folder, never hosted. Boxedwine's shell can mount a local folder (File System Access API) or take an uploaded zip. |

## Proof (2026-10-25, Boxedwine master `2dbabfc`, Wine 11 web filesystem v22)

`yo2probe.c` is a small stand-in for `yo2.exe`. It uses the same imports (`d3d8.dll` + `WSOCK32.DLL`) and sends the
real gate handshake: `0x2005 "Ver.080912.00"` with the XOR+MD5 framing from `yo2srv/red/proto.py`. It runs inside the
browser and talks to an unmodified `run.py`.

![probe in browser](probe-in-browser.jpg)

The green background means the handshake succeeded. The server log shows the browser connection:

```
[srv:5730] <- 0x2005 #1 len=13 md5=ok ... b'Ver.080912.00'
[srv:5730] -> 0x2002 #1 len=4
[srv:5730] -> 0x2003 #2 len=1008 ... DuelCards     (the real server list)
```

| Build | Wine boot to window | Guest D3D8 frame rate | Page frame rate |
|---|---|---|---|
| `jit` (single-threaded) | ~40 s | ~800 fps | **~2 fps** (blocks the main thread, unusable) |
| `multiThreadedJit` | ~15 s | ~470 fps | 120 fps (display refresh) |

Use `multiThreadedJit`. It needs COOP/COEP headers (`server.mjs` sets them). CPU speed is roughly 250–900 emulated
MIPS. Boxedwine's own notes put MW3 (2011) at about 9 fps in the browser, so a 2008 2D/isometric client should fit.

## Real client (2026-10-25): title screen reached

The real `yo2.exe` Ver.080912 (its `merge.dat` matches `extraer_juego.py`'s SHA-256) ran in the same browser setup:

- The updater fetched `/yo2/root.txt` and `/yo2/merge.txt` from `run.py`'s HTTP port 8730 through the gateway.
- It showed the KONAMI logo, then the full **title menu rendered through D3D8 → WebGL**. Mouse hover worked.
- Clicks: solved 2026-10-26, see "Input" below. The client reads the mouse from window messages and
  `GetAsyncKeyState`; DirectInput is only used for the keyboard.
- The menu came up in German. Solved (2026-10-26): the language is `language = N` in `system.bin` (0 japanese,
  1 english, 2 german, 3 french, 4 italian, 5 spanish, 6 korean), and the stock 2008 file ships `2`. YOLaucher's
  launcher never touches it; players switched it in the client's Options dialog, which rewrites the file.
  `python3.12 web/systembin.py game/system.bin language=1` sets English before zipping (format in the script: Blowfish,
  key `nameko!`; a plaintext `system.txt` next to the exe is read instead if present). Verified: English title menu.

Things that had to change:

- **The hook DLL needs the registry override.** `WINEDLLOVERRIDES=version=n,b` from the environment was ignored, and
  Wine kept loading builtin `version.dll`. `regedit /S` with `[HKEY_CURRENT_USER\Software\Wine\DllOverrides]
  "version"="native,builtin"` worked. Build the DLL with `build_dll.ps1`'s flags (`-nostdlib -ffreestanding
  -Wl,--entry,_DllMain@12 ... -lkernel32 -luser32 -lgdi32`).
- **Launch** through a `.bat`: `regedit /S yo2.reg`, `set YO2ONLINE_NET=1`, `yo2.exe`.
- **Sound on crashed the emulator** right after the client opened audio: `kpanic("Failed to queue SDL callback")`.
  2026-10-26: not reproduced with current builds. The game only opens audio when `Sound.dat` (416 MB, left out of the
  lean zip) is in the game folder; with it, the non-JIT build (Build/MultiThreaded) opened audio through the
  AudioWorklet (`openAudio: freq=11025 ... channels=2`) and sat at the title with menu hover for a minute, no crash.
  Audio never calls `sdlDispatch`, and every caller waits for its callback, so the queue can't fill from callbacks;
  the likely cause was `SDL_PushEvent` failing on `SDL_malloc` near the JIT build's 3 GB heap (it reached 3.6 GB).
  `web/boxedwine-sound.patch` (on 2dbabfc) waits and retries instead of the kpanic, logging `BWSDLQ` with SDL's error
  and the queue contents once a second, so the next occurrence names its cause instead of killing the tab.

> ⚠️ **Memory.** Never pass the game as a single `app` zip with `storage=memory`. Boxedwine downloads the whole
> 1.1 GB zip into the page and copies it into its in-memory filesystem, so it takes several GB of RAM per load, and
> repeated reloads exhausted the test Mac's memory.

### Boot overlay (2026-10-26)

The web filesystem's Wine prefix is already initialised (`.update-timestamp`), so Wine never rebuilds it and a
"pre-booted" snapshot saves little. Boot time goes to the services wineboot starts and to yo2.exe's own startup.
`python3 web/bootoverlay.py site/boxedwine.zip site/overlay.zip` writes a 2-file overlay (system.reg + user.reg):
the yo2net `version` DllOverride baked in, winemenubuilder disabled, Eventlog and PlugPlay (plugplay.exe, winebus,
winehid, the failing winebth) set to Start=4. Add `&overlay=overlay.zip` to the URL. MountMgr and NDIS/nsiproxy stay.

Measured on the non-JIT build, seconds after Wine launch (`Launching` in the console):

| run | yo2.exe starts | game window (KONAMI) |
|---|---|---|
| no overlay (2 runs) | 14, 14 | 32, 32 |
| overlay keeping PlugPlay | 14 | 33 |
| overlay | 12 | (logging cut; KONAMI on screen) |
| overlay, machine at load 56 | 19 | 45 (noise) |

So about 2 s from the services, still to confirm with A/B pairs at load < 6. The rest is yo2.exe itself: ~13 s
between its GL init and its game window (the updater's HTTP fetches, then a quiet gap). With the overlay, start.bat's
`regedit /S yo2.reg` line is redundant (about 1 s). `wine-override=nomono` changes nothing here: the prefix is current,
so wineboot doesn't install Mono or Gecko.

### Lazy loading (solved)

[`lazydrive.js`](lazydrive.js) + [`boxedwine-lazy.patch`](boxedwine-lazy.patch) (also add
`<script src=lazydrive.js></script>` after `fsfs.js` in `boxedwine.html`):

- `?app=yo2&lazyapp=1` makes `yo2.zip` (store-only, no `Sound.dat`, 716 MB) a **range-read file**. Boxedwine reads only
  the zip's index at mount, then seeks per file. It reads 1 MB HTTP ranges through a 64-chunk (64 MB) LRU cache, and
  writes go to an in-memory overlay. Mounting took 4 MB of downloads; reaching the title menu took 38 MB.
- `server.mjs` needs single-range `Range:` support (in the patch).
- Don't put the game on `D:` (`?lazy=` manifest mode). `CreateDirectoryA("profiles")` fails there, and the updater
  stops after `root.txt`. The zip on `C:\files` gets Boxedwine's writable overlay.

### GPU memory leak at the title screen (fixed)

While the title menu was on screen the browser's GPU process grew about 70 MB/s. Counting WebGL calls per frame
showed no leaking WebGL object (9 textures, 1 buffer, flat). The leak was the present path: every frame Boxedwine's GL
pthread called `OffscreenCanvas.transferToImageBitmap()` and posted the bitmap to the page. Chrome frees a transferred
WebGL bitmap's GPU buffer with a task on the *producing* thread, and that pthread never returns to its event loop, so
every frame (3 MB at 1024x768) stayed in the GPU process, even when the page closed the bitmap at once.
[`boxedwine-yo2.patch`](boxedwine-yo2.patch) presents with `readPixels` into a transferable buffer instead
(`sdlgl.cpp` + a `boxedwinePresentPixels` page handler). The GPU process now stays at 300–400 MB.

### Speed fixes

- **Dialog screens ran at 3 fps.** WineD3D streams vertices into one 16 MB WebGL buffer with a tiny `bufferSubData`
  per draw (~200 per frame on the login screen). On ANGLE's Metal backend each such update to a buffer the GPU is
  still using costs about 1 ms of GPU time (in a stand-alone benchmark: 16 MB buffer × 216 updates = 252 ms/frame,
  256 KB buffer = 6 ms). The patch's `boxedwine-webgl-streaming.js` (`--pre-js`) routes small updates of buffers
  ≥ 1 MB through a 64 KB staging buffer and `copyBufferSubData`: 17 ms in the benchmark, login dialog 3 → 17 fps.
- **128 texture-loader threads.** `CD3DTextureManager` (0x59c6d0) starts 0x80 threads that poll their slot with
  `Sleep(1)`. Every guest thread is a Web Worker here (~175 live threads). The hook DLL's `YO2ONLINE_TEXLOADERS=8`
  lowers the limit in the two loops that use it (0x59c8ed creates mutex + thread per slot, 0x59f3a0 picks a free slot):
  55 threads, page 1.9 → 1.4 GB.
- **Use the non-JIT build** (`make multiThreaded`). `multiThreadedJit` reached 3.6 GB in the page within ~40 s of
  boot (heap 1.3 GB, the rest compiled JIT modules and worker isolates), too much next to a normal desktop session.
  Non-JIT: title ~36 fps, room ~30 fps, dialogs ~17 fps; it's CPU-bound in the interpreter (x87 softfloat is hot).

### Input

- In the app's embedded pane clicks didn't arrive at all; in a normal Chrome window (or Chrome driven over CDP)
  mouse and keyboard events reach the game.
- **Small buttons missed.** Boxedwine's screen is 1024x768, so the game's framed 1024x768 window doesn't fit and its
  client area shrinks to 1018x736. The game lays out for 1024x768 and its `CMouseLayer` doesn't scale, so clicks
  drift up to ~30 px toward the bottom right. `YO2ONLINE_WINDOW_FORCEFS=1` (hook DLL; now honoured on its own) makes
  the game use its Full-screen path: client exactly 1024x768, every button lands. `fullscreen = 1` in `system.bin`
  hangs the client before it draws; `resolution=1030x800` breaks D3D device creation (no 1024x768 mode); YOLaucher's
  `YO2ONLINE_WINDOW=fit` is off by 1.5x (Wine reports a DPI-scaled monitor).
- **The game samples the button once per frame.** A press and release that land in the same frame are lost, so slow
  frames (debug tracing, a second emulator on the machine) make clicks look random. Hold a click for two frames.
- Wine's fullscreen pointer grab sends events through its hwnd-less clip window; harmless, but
  `[HKCU\Software\Wine\X11 Driver] "GrabFullscreen"="N"` / `"GrabPointer"="N"` keep events on the game window.

### Status (2026-10-26)

Title → rules → login (account seeded with `make_state.py`) → character select → **My Room** (lobby server
connected) → Main Menu → **Edit Deck** with the trunk all work in the browser. Not yet: leaving My Room. Every map
object (doors, TV, shelf, floor) ignores clicks although the game receives correct `WM_LBUTTON*` messages; shop and
NPC duel are behind that. The deck editor runs at ~1 fps (the game thread waits on main-thread file I/O proxies).

## How to reproduce

```sh
git clone https://github.com/danoon2/Boxedwine && cd Boxedwine/project/emscripten
make multiThreadedJit                                   # emsdk on PATH
curl -LO https://boxedwine.org/v2/22/TinyCore15Wine11.0-web.zip
mkdir site && cp -R Build/MultiThreadedJit/* site/ && cp TinyCore15Wine11.0-web.zip site/boxedwine.zip
i686-w64-mingw32-gcc -O2 -std=c99 -mwindows yo2probe.c -o yo2probe.exe -ld3d8 -lwsock32 -ladvapi32
zip site/yo2probe.zip yo2probe.exe
node server.mjs --root site --port 8000 &
node network-gateway.mjs --port 8001 &                  # allows 127.0.0.1:* by default
(cd tools/yo2emu && YO2_NO_CLIENT=1 python3.12 run.py 0) &
open 'http://127.0.0.1:8000/boxedwine.html?app=yo2probe&p=yo2probe.exe&network=true&storage=memory&sound=false'
```

## Before trying the real client

1. **Delete the client folder's own `d3d8.dll`, `d3d9.dll` and `dxgi.dll`** (DXVK / d3d8to9, see `yo2net.c` line 16).
   Boxedwine sets `d3d8,d3d9,dxgi=n,b`, so a DLL next to the exe would win over the WebGL WineD3D, and DXVK needs
   Vulkan, which the browser doesn't have.
2. Load `version.dll` as native: `&env="WINEDLLOVERRIDES:version=n,b"`.
3. Allow the gateway every server port: gate 5730, accounts 5731-5732, rooms 5740-5750 and 5800-5876, duel 5760,
   HTTP 8730. The default `127.0.0.1:*` rule already covers these when the gateway runs on the server host.
4. Persistence: with emsdk 6.0.13 `IDBFS` isn't a global, so `storage=indexeddb` throws. Use `storage=memory` for
   now, or export `IDBFS` in the makefile so the 100 MB filesystem and saved data are cached between visits.
5. Known risk: an earlier attempt on a GPU-less Windows box couldn't create a D3D8 device under software dgVoodoo.
   This path is different (WineD3D → WebGL on the browser's GPU), and the probe's `CreateDevice` succeeds. Still, the
   client's exact present parameters and texture formats are untested.

## Hosting for real players

Put the static site (≈100 MB filesystem + 4 MB wasm) on any CDN that can send COOP/COEP headers. Run
`network-gateway.mjs` over `wss://` on the server box, with `--allow-origin https://<site>` and
`BOXEDWINE_GATEWAY_AUTH` set. The gateway replaces the launcher's libp2p/Tor reachability, so the server needs one
public WebSocket port. Each player brings their own `yo2.exe` folder; nothing from the game is ever served.
