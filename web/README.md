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
- **Quick clicks are fine at low fps.** The game reads the button state once per frame (`GetState` 0x542270), which
  looked like it would lose a press and release inside one 150 ms frame. Measured: 6 of 6 back-to-back CDP
  down/up clicks with no wait toggled the shop cart at 6 fps, so Wine already hands the down and the up to
  different polls. No input patch needed. Clicks that seemed "random" earlier were the 1018x736 client offset (above)
  and clicks on things that only react to walking (below).
- Wine's fullscreen pointer grab sends events through its hwnd-less clip window; harmless, but
  `[HKCU\Software\Wine\X11 Driver] "GrabFullscreen"="N"` / `"GrabPointer"="N"` keep events on the game window.
- **You walk by holding the RIGHT mouse button** (not by clicking the floor). `CChibiAvatarCtrl` (0x9c5560) reads
  the right-button "held" slot of `CMyMouseEx` (vtable 0xbe9714, +8), so the avatar walks toward the cursor while
  the right button is down. Left click is for UI, NPCs and other players. In My Room, walk onto the red door; the
  gate then lists languages and rooms. The SWF door button (`go_legacy_auto_btn`) is dead in 080912.
- **Doors and the shop are map exits, not buttons.** Each map's exit table is in the exe at 0xd2d4d0 (0x1a4 bytes
  per map, `FUN_00a4fe00`; exits at +0x104, 16 bytes each, first int = target). Target 0-9 is another map, 11 is the
  shop scene (scene 14, `CShopScene`; `FUN_009fb8c0` on `CTmAvatarOnExit`). `Map_a02` (Store) exit 2 and `Map_d02`
  exit 2 are the shops: walk onto the mat in front of the CARD SHOP doors under the stairs.

### Game patches

Applied in memory by the hook DLL (`yo2net.c`) at startup. Each checks the original bytes first and skips on a
mismatch; `yo2.exe` on disk is never changed. Each is off unless its switch is set.

| Switch | Address | Original → new | Why |
|---|---|---|---|
| `YO2ONLINE_WINDOW_FORCEFS=1` | 0x608188 | `ff 52 30` (`call [edx+0x30]`, reads the setting) → `b0 01 90` (`mov al,1`) | game uses its Full-screen path: 1024x768 client, clicks land (see Input) |
| `YO2ONLINE_TEXLOADERS=8` | 0x59c8ed, 0x59f3a0 | `81 fe 80 00 00 00` / `81 fa 80 00 00 00` → limit byte `80` → `08` | 8 texture-loader threads instead of 128 (each is a Web Worker) |
| `YO2ONLINE_SKIPINTRO=1` | 0x6deebd | `6a 02` → `6a 04` | patcher asks for the title instead of the KONAMI logo |
| `YO2ONLINE_SKIPINTRO=2` | also 0x7fa659 | `6a 05` → `6a 06` | ONLINE opens login directly, skipping the rules page |
| `YO2ONLINE_MAXSKIP=n` (0-9) | 0x6090b7 | `c7 45 e8 0a 00 00 00` → immediate `0a` → `n` | the game loop (0x607fd0) skips up to 10 renders when its fixed 33 ms logic step falls behind, which is always the case here; with a small n it draws after every n+1 steps (slower game time, more frames) |
| `YO2ONLINE_FASTSORT=1` | 0x5e1b90, 0x5e1710 | `jmp` to the hook (7-byte trampoline back) | the draw-order std::sort (432 elements in the deck editor, 60 calls/s, 99.9% already sorted) gets an O(n) "already sorted?" check first, and its comparator is replaced by one without the debug z calls and pointer checks |
| `YO2ONLINE_NOASSERT=1` | 1765 flag bytes in 0x126f000-0x12be000 | `00` → `01` | sets every assert's "already reported" flag, so the checks (task-pointer validation, CTask::Call, ...) are skipped; a real failure no longer reports |
| `YO2ONLINE_DYNCASTCACHE=1` | 0xb0b20e (`__RTDynamicCast`) | `jmp` to the hook (5-byte trampoline back) | caches the cast result delta per (vftable, VfDelta, src, dst), 4096 entries |
| `YO2ONLINE_NODEBUG=1` | | | NOASSERT + DYNCASTCACHE |
| `YO2ONLINE_SORTSTATS=1` | | | with FASTSORT: one `BWSORT` line per second (calls, n, already sorted, on screen) via OutputDebugString (`WINEDEBUG=warn+debugstr`) |

Measured 2026-10-27 in Boxedwine multiThreaded, MAXSKIP=1, each variant run right after its control (fps, presented
frames; deck editor = 100 s after opening it):

| Variant | My Room ctl → var | Deck editor ctl → var |
|---|---|---|
| FASTSORT | 2.9 → 3.7 | 3.8 → 4.9 (+29%) |
| FASTSORT + SORTSTATS | 2.7 → 3.6 | 3.6 → 4.8 |
| FASTSORT + NOASSERT | 2.5 → 3.4 | 3.3 → 4.9 |
| FASTSORT + NODEBUG | 2.5 → 4.0 | 3.7 → 5.2 (+41%) |

FASTSORT is the proven win. NOASSERT/NODEBUG on top of it adds about 0.3 fps, which is inside run-to-run noise, and
DYNCASTCACHE alone was never measured. Culling by screen position wouldn't help: 428 of the 432 sorted elements are
on screen.

### Status (2026-10-26)

Title → login (account seeded with `make_state.py`) → character select → **My Room** → Edit Deck → walk out →
gate (English, Beginner's Lobby A) → **lobby rooms** (MOVE between Duel Academy / Store / Duel Space / Slifer Red)
→ **NPC dialogs** → **NPC duel** → **card shop** (buy and open a pack) all work in the browser. The duel against
A-lien (Store) goes through rock-paper-scissors, the field, summoning from the hand, phases and the NPC's turns
(server `0x6xxx` on port 5760, OCGcore on the emulator).

- Entering a lobby room failed with `err:d3d:resource_init Out of adapter memory` (WineD3D's default VRAM estimate
  for WebGL is small). `[HKCU\Software\Wine\Direct3D] "VideoMemorySize"="1024"` fixes it.
- `YO2ONLINE_SKIPINTRO=2` (hook) skips the logo and the rules page; ONLINE then opens the login dialog.
- Speed: lobby rooms ~4 fps, duel field similar, deck editor ~1 fps (the game thread waits on main-thread file I/O
  proxies). Playable but slow; the speed patches in this folder are the next lever.
- **Shop**: walking onto the CARD SHOP doors opens Sadie's store (REQSHOPITEM 0x5208 → catalogue); buying a
  200 MP booster sends 0x5210 → 0x5212/0x5211/0x4140 and the pack-opening shows the 5 cards.
- Testing headless: Chrome for Testing with `--headless=new` keeps ANGLE Metal on Apple GPUs and runs at the same
  frame rate as a visible window (a hidden headed window drops to ~1 fps). Drive it over CDP with trusted
  `Input.dispatchMouseEvent`.

### Measured: JIT cache, fast x87, where the time goes (2026-10-26)

Same path every run (title → login → My Room → deck editor), off-screen Chrome, non-JIT build unless stated;
fps = presented frames.

| screen | multiThreaded | + `boxedwine-x87-fast.patch` |
|---|---|---|
| title | 30.1 | 29.0 |
| character list | 13.1 | 13.6 |
| My Room (15 s, noisy) | 3.8 | 2.9 |
| deck editor (140 s) | 1.2 | 1.2 |

- **JIT cache: not worth it.** A recorded and offline-optimised cache (`boxedwine-wasm-jit-cache-pipeline.mjs`;
  15 s of boot = 6.4k blocks → 42 groups, 65.6 → 21.8 MB of wasm, 1.75 MB zip) makes the JIT build reach 2.5 GB in
  the page *sooner* (31 s vs 37 s without it). The memory is the JIT's own C++ heap (sbrk 1.28 GB against 0.3 GB of
  guest RAM at 38 s), not the generated code. Recording longer than ~20 s also hangs "Save JIT Cache" on a lock the
  page's main thread waits for (`boxedwine-jit-save.patch` has an untested fix, not committed).
- **Fast x87 is neutral.** `KSystem::useF64` is already on in wasm builds. The patch only replaces softfloat's
  f64 → extF80 → f32 rounding (D3D's single-precision control word) with the native cast for round-to-nearest. It's
  bit-identical on 20M inputs (`x87-f32-check.c`, which also catches a wrong rounding mode), but no faster.
  Optional.
- **The slow screens are guest-CPU-bound.** At the deck editor the page's main thread is under 20% busy and
  proxied calls are 0.1%. One guest thread (the game's) is 100% busy in the interpreter. The game runs logic at a
  fixed 30 Hz and skips up to 10 renders when it falls behind (0x607fd0), so 1.2 fps means each logic step takes
  ~10× its 33 ms budget. The next lever is game-side: find the hot yo2.exe code there.

### JIT memory: `boxedwine-jit-mem.patch` (2026-10-27, track stopped)

`multiThreadedJit` hit our 2.5 GB page cap before the title. This patch (on top of `boxedwine-yo2.patch`) gets it
through title → login → My Room → deck editor, and it is faster on the character list. It does not reach the
~1.5 GB goal: the peak is 1.9 GB. The JIT track stopped here (the effort moved to static recompilation), so this is
a record, not the shipped build.

Where the JIT's C++ heap went (stock build at 31 s, page 2.5 GB, from the patch's `[JITMEM]` console line, logged
every 5 s while blocks compile). malloc in use was 1143 MB:

| item | MB |
|---|---|
| guest RAM | 222 |
| merged runtime-group wasm, stored **twice** (`group.bytes` + identical `patchedBytes`) | 270 × 2 |
| standalone / pending block bytes | ~10 |
| everything else: decoded ops, Wine, 8 MB inline-TLB arrays per guest process (12 processes) | ~370 |

30k blocks had been compiled by then, about 10 KB of wasm each. A block compiled after 200 runs, so Wine's and the
DLLs' one-off startup code got compiled too.

The patch:
- **One copy** of each runtime group's bytes.
- **Warm-up sampling:** `firstDynamicOp` counts a random 1-in-N run (xorshift, so loop periods can't alias it), so a
  block compiles after ~200·N runs. Pending blocks sample their "flush the batch now" hits the same way, so batches
  hold ~10 blocks per wasm module instead of ~2. Every module has a fixed V8 cost: 777 modules instead of 3191 at the
  title. `&jithot=N` on the page sets N (default 32; `1` = stock behaviour).
- **Inline-TLB arrays** (2 × 4 MB per process) come from untouched `sbrk` memory, with a dirty bit per host page, so
  only pages the process maps get committed. A sequence counter keeps the fill race-free against `onPageChanged`
  callers that don't hold the memory lock.
- **No duplicate group compiles:** the module broker hands a compiled module to another worker by `postMessage`, and
  guest threads rarely return to their event loop. So a worker touching a group compiled elsewhere compiled the whole
  group again: ~2.3 compiles per module, each one costing V8 its code and its own copy of the wire bytes. Now the
  worker runs that block interpreted until the block is as hot on that worker as the first compile required.
  The last two changes (TLB dirty bits, no duplicate compiles) are built but **not measured**: the track stopped
  before their run.

Measured with `&jithot=32` (before those two), same route as the table above. fps / peak renderer MB:

| screen | non-JIT (`multiThreaded`) | `multiThreadedJit` + patch | stock `multiThreadedJit` |
|---|---|---|---|
| title | 30.4 / 1198 | 30.2 / 1689 | killed at 2.5 GB at 31–37 s, before the title |
| character list | 11.5 / 1299 | **23.2** / 1785 | |
| My Room | 1.0 / 1286 | 1.5 / 1834 | |
| deck editor | 1.2 / 1285 | 1.7 / 1910 | |

`&jithot=8` reaches the title at ~31 fps with the page flat at ~2.0 GB (13.7k blocks, 139 MB of group wasm).
`&jithot=32` gives 6.6k blocks / 70 MB at the title, ~10k blocks / 130 MB at the deck editor.

Where the remaining memory is (renderer at the deck editor, `footprint`):

| | non-JIT | JIT + patch |
|---|---|---|
| V8 tag: wasm memory + V8 heaps + compiled code | 981 MB | 1350 MB (+369) |
| PartitionAlloc: Chrome's malloc, holding V8's per-module wasm metadata and wire-byte copies | 240 MB | 424 MB (+184) |

Where the deck editor's time goes under the JIT:
- The game thread spends ~42% in `wasmStartJITOp` itself (block-to-block dispatch through C++; runtime groups have no
  direct calls between blocks) and only ~15–20% in generated code.
- ~35% of JIT time is `emulateSingleOp`. It is mostly LOCK-prefixed atomics, which the JIT doesn't compile
  (`CmpXchg`/`Xchg`/`Xadd`/`Add`/`And`/`Sub` `_Lock`: Wine's interlocked ops and critical sections), plus `int 0x80`
  syscalls.
- In My Room the game thread is ~40% waiting.

So the next JIT levers would be: compiling LOCK ops, chaining blocks inside a group directly, and evicting cold
groups.

### Native Direct3D 8 (2026-10-27)

Every sprite the game draws is one `DrawPrimitiveUP` of 4 vertices, and with Wine each one went through `d3d8.dll`
→ `wined3d` → `opengl32`, all emulated. [`boxedwine-d3d8native.patch`](boxedwine-d3d8native.patch) (on Boxedwine
2dbabfc, independent of the other patches) and a 4.6 KB guest DLL
([`d3d8nativo.c`](../tools/yo2emu/launcher/juego/dll/d3d8nativo.c)) replace that chain:

- The guest `d3d8.dll` implements nothing. `Direct3DCreate8` reserves an arena (up to 1 GB; Boxedwine only backs pages
  that get touched) and calls into Boxedwine, which writes the COM vtables there. Each vtable slot is a 16-byte thunk
  `int 0x99; .long 0x0D3D0000|interface<<8|slot; ret N`: the same trap Boxedwine's own `opengl32` uses, so a D3D call
  costs one emulated instruction. Arguments are read off the guest stack and the result goes back in `eax`.
- Only two slots are guest code: `CreateDevice` makes a WGL context on the game window first (Boxedwine turns it into
  a WebGL2 context on the game's thread), and `Present` calls `SwapBuffers` after the native side has blitted the back
  buffer, so presenting uses the existing path.
- Native side (`source/opengl/d3d8native.cpp`): the fixed-function subset a 2D game needs (XYZ|DIFFUSE|TEX1
  triangles, texture stage 0, lighting without lights, `A8R8G8B8`/`X8R8G8B8`, render-target textures, D3DX's helper
  calls). The world matrix and lighting are applied on the CPU when a sprite is queued, so consecutive sprites with
  the same texture and state go out as one `glDrawArrays`. Texture pixels live in the arena (so `LockRect` gives the
  game a pointer of its own) and are uploaded lazily from the thread that owns the context; the 8 texture-loader
  threads never touch GL. BGRA is uploaded as is and swizzled in the shader. Unsupported methods log
  `d3dnative: unsupported X::Y` once and fail; a stats line every 300 frames gives calls per frame for each method.
- Use it: build Boxedwine with the patch, then
  `python3 web/bootoverlay.py site/boxedwine.zip site/gfx-overlay.zip --d3d8 d3d8.dll` and add
  `&overlay=gfx-overlay.zip`. Boxedwine's web build links `system32\d3d8.dll` to `C:\webgl\d3d8.dll` and already sets
  `d3d8=n,b`, so the overlay only replaces that file. Without the overlay the same build runs Wine's D3D as before.
  Without the patch, the guest DLL's first `int 0x99` stops the emulator, so never ship the DLL alone.

Measured on the same wasm (non-JIT `multiThreaded`, headless Chrome for Testing), with and without the overlay;
presented frames per second:

| screen | Wine d3d8 | native d3d8 | sprites per frame (GL draws) |
|---|---|---|---|
| title | 28–30 | 28–30 | 83 (15) |
| My Room | 25–28 | 26–30 | 42 (28) |
| lobby: Beginner's Lobby A, Store[2] | 3.4–4.6 | 29.1–29.6 | 325 (149) |
| NPC duel (A-lien): RPS, field | ~4 (see below) | 29.9–30.0 | 700 (91) |
| deck editor | 1.2 | 1.4 | 593 (227) |

- 30 fps is the game's own cap: its loop runs logic at a fixed 30 Hz. So the lobby and the duel go from ~4 fps to
  the cap (about 7x), and the title and My Room were already there.
- The Wine duel figure is the earlier "duel field similar" (to the lobby) from this README: the scripted Wine run
  walks by holding the button for a fixed time, so at 4 fps the avatar stopped short of A-lien and the duel didn't
  start.
- **Correct:** full-size PNGs of the same screens agree with Wine's output. The deck editor has 99.0% identical
  pixels and 100% within 8 levels. The title has 94.8% / 99.0%, My Room 96.4% / 97.5%. The character list differs only
  in its rotating logo ring. The NPC dialog, RPS, the duel field and the hand all render.
- **The deck editor is not graphics-bound.** It draws 593 sprites per frame either way, and it stays at ~1.4 fps:
  the time is in the game's logic steps (up to 10 per rendered frame), not in Direct3D.

### Coverage trace (debug only)

`boxedwine-trace.patch` (on top of `boxedwine-yo2.patch`) records which of the game's code runs. With `&trace=1` in
the page URL, the first decode of every basic block that starts in yo2.exe's `.text` stores the current phase number
for that address. A block decodes once, so it costs nothing per run, and without `&trace=1` it is one byte check. In
the page console, `bwTraceMark(n)` starts phase n (phase 1 is on from boot), and `bwTraceDump()` returns `addr:phase`
hex pairs. Add `boxedwine-eip-sampler.patch` and `&debug=1` for hotness (`bwEip.dump()`). Mapping the addresses to
functions needs a disassembly of the exe, which isn't in this repository.

## Play it locally

`web/play.sh <client folder or yo2_2008-11_Extracted.7z>` builds everything under `$YO2WEB` (default `~/.yo2web`),
starts the static server, the gateway and `run.py`, and prints the URL. It's safe to rerun. `web/stop.sh` stops it.
Tested from a fresh clone. `BW_DIST_URL=<tarball of Build/MultiThreaded>` skips the emscripten build. Hosting for
other players: [HOSTING.md](HOSTING.md).

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
