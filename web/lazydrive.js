// Lazy D: drive for Boxedwine: big files are read with 1 MB HTTP range requests through a capped LRU cache, so a
// 700 MB merge.dat costs at most CACHE_CHUNKS MB of RAM instead of being downloaded and copied whole.
// ?lazy=<manifest.json> → { "base": "game/", "whole": ["yo2.exe", ...], "lazy": [["merge.dat", 691359533], ...] }
const CHUNK = 1 << 20;
const CACHE_CHUNKS = 64;                       // ponytail: one global LRU; per-file caps if several big files thrash
const cache = new Map();                        // "url#chunk" -> Uint8Array, Map order = LRU order
const dirty = new Map();                        // written chunks: kept forever (ponytail: in-memory only, lost on reload)
window.lazyDriveStats = { requests: 0, bytes: 0, cachedMB: () => cache.size, dirtyMB: () => dirty.size };

function readChunk(url, index, size) {
    const key = url + "#" + index;
    let c = dirty.get(key);
    if (c) return c;
    c = cache.get(key);
    if (c) {
        cache.delete(key);
        cache.set(key, c);
        return c;
    }
    const from = index * CHUNK;
    const to = Math.min(from + CHUNK, size) - 1;
    // FS calls are proxied to the main thread, where only text-mode sync XHR is allowed
    const x = new XMLHttpRequest();
    x.open("GET", url, false);
    x.setRequestHeader("Range", `bytes=${from}-${to}`);
    x.overrideMimeType("text/plain; charset=x-user-defined");
    x.send(null);
    if (x.status !== 206) throw new FS.ErrnoError(29);   // EIO
    const t = x.responseText;
    c = new Uint8Array(t.length);
    for (let i = 0; i < t.length; i++) c[i] = t.charCodeAt(i) & 0xff;
    cache.set(key, c);
    if (cache.size > CACHE_CHUNKS) cache.delete(cache.keys().next().value);
    lazyDriveStats.requests++;
    lazyDriveStats.bytes += c.length;
    return c;
}

function createLazyFile(dir, name, url, size) {
    const node = FS.createFile(dir, name, {}, true, true);
    node.contents = { length: size };
    Object.defineProperty(node, "usedBytes", { get: () => size });
    const ops = Object.assign({}, node.stream_ops);
    ops.read = (stream, buffer, offset, length, position) => {
        if (position >= size) return 0;
        const n = Math.min(length, size - position);
        let done = 0;
        while (done < n) {
            const p = position + done;
            const c = readChunk(url, Math.floor(p / CHUNK), size);
            const at = p % CHUNK;
            const take = Math.min(n - done, c.length - at);
            buffer.set(c.subarray(at, at + take), offset + done);
            done += take;
        }
        return n;
    };
    ops.write = (stream, buffer, offset, length, position) => {
        if (position + length > size) throw new FS.ErrnoError(27);   // EFBIG: no growing lazy files
        let done = 0;
        while (done < length) {
            const p = position + done;
            const index = Math.floor(p / CHUNK);
            const key = url + "#" + index;
            if (!dirty.has(key)) dirty.set(key, readChunk(url, index, size).slice());
            const c = dirty.get(key);
            const at = p % CHUNK;
            const take = Math.min(length - done, c.length - at);
            c.set(buffer.subarray(offset + done, offset + done + take), at);
            done += take;
        }
        console.log(`lazy drive: wrote ${length} B to ${url} @${position}`);
        return length;
    };
    node.stream_ops = ops;
}

window.setupLazyDrive = async function (manifestUrl, dir) {
    const m = await (await fetch(manifestUrl)).json();
    FS.mkdirTree(dir);
    for (const name of m.whole) {
        const data = new Uint8Array(await (await fetch(m.base + name)).arrayBuffer());
        FS.writeFile(dir + "/" + name, data);
    }
    for (const [name, size] of m.lazy) createLazyFile(dir, name, m.base + name, size);
    console.log(`lazy drive ${dir}: ${m.whole.length} whole files, ${m.lazy.length} lazy`);
};
