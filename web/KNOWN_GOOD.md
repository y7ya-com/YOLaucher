# Known-good recipe: real yo2.exe to the title screen in a browser (2026-10-25)

This exact setup reached the title menu in Claude's in-app browser (Electron Chromium). Reproduce it exactly before
changing anything.

## 1. Boxedwine: pin the commit

```sh
git clone https://github.com/danoon2/Boxedwine && cd Boxedwine && git checkout 2dbabfc   # master as of 2026-10-25
cd project/emscripten
make -j6 multiThreadedJit       # emsdk "latest" (emcc 6.0.13); with brew emscripten: EMSDK_PYTHON=/opt/homebrew/bin/python3.12
```

- Newer master may behave differently. Rule out a Boxedwine regression first by building 2dbabfc.
- With emcc 6.0.13, `IDBFS` isn't a global, so always use `storage=memory`.

## 2. Site folder

```sh
mkdir site && cp -R Build/MultiThreadedJit/* site/
curl -L -o site/boxedwine.zip https://boxedwine.org/v2/22/TinyCore15Wine11.0-web.zip   # web filesystem v22, 99.5 MB
cp <fork>/web/lazydrive.js site/
patch -p0 < <fork>/web/boxedwine-lazy.patch     # site/boxedwine-shell.js (lazy app zip) + server.mjs (Range support)
# boxedwine.html: add <script src=lazydrive.js></script> right after <script src=fsfs.js></script>
```

## 3. Game zip (store-only, no Sound.dat, ~716 MB, keep local)

Extract from `duel-evolution-restore/extraction/recovery/yo2_2008-11_Extracted.7z`, leaving out Sound.dat:

```sh
7zz x yo2_2008-11_Extracted.7z -x'!Sound.dat' -ogame
```

Add three files to `game/`:

- `version.dll`, built from YOLaucher's hook with build_dll.ps1's flags:
  ```sh
  i686-w64-mingw32-gcc -O2 -s -shared -nostdlib -ffreestanding -fno-builtin -fno-stack-protector \
    -fno-asynchronous-unwind-tables -Wall -Wno-unused-function -Wl,--entry,_DllMain@12 -Wl,--image-base,0x6a5d0000 \
    -Wl,--dynamicbase -Wl,--nxcompat tools/yo2emu/launcher/juego/dll/yo2net.c tools/yo2emu/launcher/juego/dll/version.def \
    -lkernel32 -luser32 -lgdi32 -o game/version.dll
  ```
- `yo2.reg`, CRLF line endings:
  ```
  REGEDIT4

  [HKEY_CURRENT_USER\Software\Wine\DllOverrides]
  "version"="native,builtin"
  ```
- `start.bat`, CRLF line endings:
  ```
  regedit /S yo2.reg
  set YO2ONLINE_NET=1
  yo2.exe
  ```

Set English (the stock `system.bin` says `language = 2`, German):

```sh
python3.12 <fork>/web/systembin.py game/system.bin language=1
```

Then zip them, store-only:

```sh
cd game && zip -0 ../site/yo2.zip yo2.exe merge.dat merge.log system.bin TestData.dat MagicKey.dll version.dll start.bat yo2.reg
```

## 4. Servers

```sh
node server.mjs --root site --port 8002                                            # the Range-patched one
node network-gateway.mjs --port 8003 --allow-origin http://127.0.0.1:8002
cd YOLaucher-web/tools/yo2emu && YO2_NO_CLIENT=1 python3.12 run.py 0               # gate 5730, HTTP 8730, ...
```

## 5. URL

```
http://127.0.0.1:8002/boxedwine.html?app=yo2&lazyapp=1&p=start.bat&network=true&networkGateway=ws%3A%2F%2F127.0.0.1%3A8003%2Fboxedwine-network&resolution=1024x768&storage=memory&sound=false
```

## What it did

- About 4 MB fetched to mount the zip. The updater fetched `/yo2/root.txt` and `/yo2/merge.txt` from run.py through
  the gateway. Then the KONAMI logo, then the title menu (in German before the `system.bin` step), after roughly 40–60 s. Only 38 MB of the zip
  was fetched in total.
- Memory: the page process was about 1.5–3 GB.
- The Claude app's GPU helper then grew about 70 MB/s at the title screen (the leak), so cap runs to about 60 s past
  the title.
- Mouse hover worked; clicks did not register (DirectInput8).
- `sound=true` crashed with "Failed to queue SDL callback".
- Don't run from `D:` (the `?lazy=` manifest mode): `CreateDirectoryA` fails there and the updater stops after
  root.txt. Use the zip on `C:\files` as above.
