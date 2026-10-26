"""Read or edit yo2.exe's system.bin (its "key = value" settings file).

    python3.12 web/systembin.py game/system.bin                 # print the settings
    python3.12 web/systembin.py game/system.bin language=1      # set keys in place (1 = English)

Format (from the exe: FUN_00624a10 loads it, FUN_0060c780 is the cipher reader, key at 0xc89f00):
u32 LE plaintext length, then the CRLF text padded to 8 bytes and Blowfish-ECB encrypted with the key "nameko!",
each 32-bit half of a block taken little-endian (x86 Blowfish). If a plaintext system.txt sits next to it, the exe
reads that instead. language: 0 japanese, 1 english, 2 german, 3 french, 4 italian, 5 spanish, 6 korean
(table at 0xc8cf6c; the stock 2008 file ships 2 = German). Needs `cryptography` (python3.12 here has it).
"""
import struct
import sys

try:
    from cryptography.hazmat.decrepit.ciphers.algorithms import Blowfish
except ImportError:
    from cryptography.hazmat.primitives.ciphers.algorithms import Blowfish
from cryptography.hazmat.primitives.ciphers import Cipher, modes

KEY = b"nameko!"


def _swap(b):  # flip each 32-bit word: the exe's Blowfish reads block halves little-endian
    return b"".join(struct.pack(">I", *struct.unpack("<I", b[i:i + 4])) for i in range(0, len(b), 4))


def _bf(data, enc):
    c = Cipher(Blowfish(KEY), modes.ECB())
    op = c.encryptor() if enc else c.decryptor()
    return _swap(op.update(_swap(data)) + op.finalize())


def decode(raw):
    n = struct.unpack_from("<I", raw)[0]
    return _bf(raw[4:], False)[:n]


def encode(text):
    pad = text + b"\0" * (-len(text) % 8)
    return struct.pack("<I", len(text)) + _bf(pad, True)


def set_keys(text, kv):
    lines = text.split(b"\r\n")
    for k, v in kv.items():
        hit = [i for i, l in enumerate(lines) if l.split(b" = ")[0] == k]
        if hit:
            lines[hit[0]] = k + b" = " + v
        else:
            lines.insert(-1 if lines[-1] == b"" else len(lines), k + b" = " + v)
    return b"\r\n".join(lines)


if __name__ == "__main__":
    assert decode(encode(b"a = 1\r\nlanguage = 2\r\n")) == b"a = 1\r\nlanguage = 2\r\n"
    assert set_keys(b"a = 1\r\nlanguage = 2\r\n", {b"language": b"1"}) == b"a = 1\r\nlanguage = 1\r\n"
    path, edits = sys.argv[1], sys.argv[2:]
    text = decode(open(path, "rb").read())
    if edits:
        text = set_keys(text, dict(e.encode().split(b"=", 1) for e in edits))
        open(path, "wb").write(encode(text))
    sys.stdout.write(text.decode("latin1"))
