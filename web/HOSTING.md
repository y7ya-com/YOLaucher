# Hosting the browser client for real players

How to run `yo2.exe` in Boxedwine for players who aren't on the dev Mac. For local play, use `web/play.sh`, which
starts the same three pieces on one machine.

## The pieces

| Piece | Where it runs | Notes |
|---|---|---|
| Static site: `boxedwine.html/.js/.wasm`, `boxedwine-shell.js`, `fsfs.js`, `lazydrive.js`, `boxedwine.zip` (Wine 11 web filesystem, 99.5 MB), `overlay.zip` (boot registry) | Any static host or CDN that can set response headers | Contains no game files |
| `network-gateway.mjs` (from the Boxedwine checkout) | The game-server box, behind TLS (`wss://`) | Turns the guest's TCP/UDP into WebSocket frames, then real sockets on the server box |
| `tools/yo2emu/run.py` | The same box as the gateway | Ports 5730 (gate), 5731-5732, 5740-5750, 5760, 5800-5876 and HTTP 8730. They only need to be reachable from the gateway, not from the internet |
| Game files (`yo2.exe`, `merge.dat`, `system.bin`, `TestData.dat`, `MagicKey.dll`, `merge.log`) | The player's own disk | **Never hosted by us.** Each player supplies their own copy |

The hook `version.dll` (built from `tools/yo2emu/launcher/juego/dll/yo2net.c`), `yo2.reg` and `start.bat` are ours,
so they can be served with the site.

## 1. Static site

1. Build it once with `web/play.sh` (it ends up in `$YO2WEB/play/`, default `~/.yo2web/play/`). Upload everything except `yo2.zip`,
   `game/`, `pids/` and `*.log`. Resolve the `boxedwine.zip` symlink when you upload.
2. Every response needs these headers. Boxedwine's threads need `SharedArrayBuffer`, which only exists when the page
   is cross-origin isolated:
   ```
   Cross-Origin-Opener-Policy: same-origin
   Cross-Origin-Embedder-Policy: require-corp
   Cross-Origin-Resource-Policy: same-origin
   ```
   On Cloudflare Pages, that's a `_headers` file with `/*` and those three lines. GitHub Pages can't set headers.
3. Serve `.wasm` as `application/wasm`. Gzip/Brotli is fine for the `.js`/`.wasm` files. The zips are already
   compressed.
4. If a zip is ever range-read from the CDN, the host must answer single `Range:` requests with `206`. Most CDNs do.
   With player-supplied files (below), nothing big is range-read from the CDN.

## 2. Network gateway (`wss://`)

```sh
node network-gateway.mjs --host 127.0.0.1 --port 8001 --allow-origin https://<site-host>
```

Put a TLS reverse proxy in front of it (Caddy: `reverse_proxy /boxedwine-network 127.0.0.1:8001`), then point the
page at it with `&networkGateway=wss%3A%2F%2F<gw-host>%2Fboxedwine-network`.

The hook DLL sends every connection to `127.0.0.1` on the original port (`*.yugioh-online.net` resolves to
`127.0.0.1`, and HTTP goes to `127.0.0.1:8730`). The gateway resolves that on **its own** host, so `run.py` must run
on the gateway's machine, or in the same network namespace.

⚠️ **The gateway is an open TCP/UDP relay to its own localhost.**

- Its built-in allow rules are `127.0.0.1:*` and `localhost:*`, and no flag removes them (`--allow` only adds rules).
  Any client that reaches the WebSocket can open a connection to **any** port on the gateway box's loopback.
- `--allow-origin` only checks the `Origin` header. A browser can't forge it, but a script can.
- `BOXEDWINE_GATEWAY_AUTH` / `--auth` protects only the dashboard (`/`, `/api/*`), not the WebSocket.

So, until it gets a real auth step:

- Run the gateway and `run.py` together in a container (or VM) that has nothing else listening on its loopback.
- Or patch the default `allowRules` in `network-gateway.mjs` down to the game ports listed above.
- Rate-limit or IP-limit `/boxedwine-network` at the proxy.
- `BOXEDWINE_GATEWAY_AUTH` should still be set so the dashboard isn't public. Generate it with
  `node network-gateway-password-hash.mjs`.

## 3. `run.py`

```sh
cd tools/yo2emu && YO2_NO_CLIENT=1 python3.12 -u run.py 0
```

Run it under a supervisor (systemd or a container restart policy). Keep `state.json` on a persistent volume and back
it up. Don't expose its ports publicly. Only the gateway talks to it.

## 4. Players' own game files

The site must not serve `yo2.zip`. The way to load a player's own files is already proven:

- `lazydrive.js` `createLazyFile(dir, name, url, size)` range-reads a file through a **synchronous** XHR. A `blob:`
  URL of a file the player picked answers that XHR with `206` and the right bytes. This was checked in Chromium on
  2026-10-26. So `createLazyFile("/", "yo2.zip", URL.createObjectURL(file), file.size)` works unchanged for a
  player-supplied store-only `yo2.zip`. The 690 MB `merge.dat` then costs at most 64 MB of RAM, like the HTTP path.
- What's left: a picker on the page (an `<input type=file>` for the zip, or `showDirectoryPicker()` for the game
  folder), and a small change to the `lazyapp` branch in `boxedwine-shell.js` so it takes that blob URL instead of
  `Config.locateAppBaseUrl + Config.appZipFile`.
- With a folder picker, each game file can be its own lazy file under `C:\files`. The site then serves only
  `version.dll`, `yo2.reg` and `start.bat`, and players don't have to build a zip. (Don't use `D:`: `CreateDirectoryA`
  fails there.)

## Requirements and limits for players

- Desktop Chromium (Chrome or Edge) is what was tested. Safari and Firefox are untested.
- A tab uses about 1.5–3 GB of RAM, so 8 GB machines are borderline.
- `sound=false` is required for now: `sound=true` crashes with "Failed to queue SDL callback".
- `storage=memory`: nothing persists across reloads (settings, saved login). With emsdk 6.0.13, `IDBFS` isn't
  exported, so `storage=indexeddb` throws.

## Unresolved

- **Gateway auth**: there's no per-player token on the WebSocket, and the gateway is an open loopback relay (see
  above). It needs a token check in the `upgrade` handler, or a proxy that does one.
- **Player file picker**: the approach is proven, but the code isn't written yet (section 4).
- **Many players behind one gateway**: they all reach `run.py` from `127.0.0.1`. Whether `run.py` keys anything on
  the client IP, and whether anything in a duel goes peer-to-peer, hasn't been checked with two browsers yet.
- **Input and screens past login**: owned by the main session; see `README.md`.
- **Prebuilt Boxedwine**: `play.sh` compiles Boxedwine 2dbabfc with emsdk unless `BW_DIST_URL` points at a tarball
  of `Build/MultiThreaded`. No such tarball is published yet.
