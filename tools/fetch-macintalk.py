#!/usr/bin/env python3
"""SPDX-License-Identifier: GPL-2.0-or-later

Fetch the MacInTalk 3 voice files for synth/macintalk at build time.

Apple's files are not in this repository.  This script downloads a Mac OS
8.1 hard disk image from the Internet Archive, checks its SHA-256, finds
the voice files in System Folder:Extensions:Voices on the disk's HFS
volume, and writes each voice's two resources as plain files:

    OUT/<voice>.ttvd   the voice description (parameters, see
                       docs/macintalk-format.md)
    OUT/<voice>.ttvw   the voice's sampled sound, for the voices that
                       have one (Bells, Boing, Cellos ...)

Only data is read.  Nothing from the disk is run, and the MacinTalk 3
extension itself (Apple's code) is not extracted.

Usage: tools/fetch-macintalk.py [OUT]     (default: sources/macintalk)
       tools/fetch-macintalk.py --image FILE.hda|FILE.zip [OUT]

Needs only Python 3 (and about 1.1 GB of free disk while it runs).
"""
import hashlib
import os
import struct
import sys
import tempfile
import urllib.request
import zipfile

URL = ("https://archive.org/download/macos-8.1-basilisk-ii/"
       "macos-8.1-basilisk-ii-2026.10.1.zip")
SHA256 = "ce6ee4ba5294182535d20bf6639f64415376e4420b3473167803315e615c97fa"
VOICE_DIR = ("System Folder", "Extensions", "Voices")


class HFS:
    """Just enough of an HFS (Mac OS Standard) reader to get files out."""

    def __init__(self, f, base=0):
        self.f, self.base = f, base
        mdb = self.read(base + 1024, 162)
        if mdb[:2] != b"BD":
            raise ValueError("not an HFS volume")
        self.blk = struct.unpack(">I", mdb[20:24])[0]
        self.start = struct.unpack(">H", mdb[28:30])[0] * 512
        size = struct.unpack(">I", mdb[146:150])[0]
        self.catalog = self.fork(struct.unpack(">6H", mdb[150:162]), size)

    def read(self, off, n):
        self.f.seek(off)
        return self.f.read(n)

    def fork(self, ext, size):
        out = b""
        for i in range(0, 6, 2):
            first, count = ext[i], ext[i + 1]
            if count:
                out += self.read(self.base + self.start + first * self.blk, count * self.blk)
        if len(out) < size:
            raise ValueError("fragmented fork (extents overflow) is not supported")
        return out[:size]

    def records(self):
        """Every leaf record of the catalog B-tree: (parent, name, data)."""
        cat = self.catalog
        node_size = struct.unpack(">H", cat[32:34])[0]
        node = struct.unpack(">I", cat[24:28])[0]  # first leaf
        while node:
            n = cat[node * node_size:(node + 1) * node_size]
            flink = struct.unpack(">I", n[0:4])[0]
            nrec = struct.unpack(">H", n[10:12])[0]
            offs = [struct.unpack(">H", n[node_size - 2 * (i + 1):node_size - 2 * i])[0]
                    for i in range(nrec)]
            for o in offs:
                klen = n[o]
                if klen == 0:
                    continue
                parent = struct.unpack(">I", n[o + 2:o + 6])[0]
                name = n[o + 7:o + 7 + n[o + 6]].decode("mac_roman")
                d = o + 1 + klen
                d += d & 1
                yield parent, name, n[d:]
            node = flink

    def find(self, path):
        """{name: (type, creator, data fork, resource fork)} of a folder."""
        dirs, files = {}, {}
        for parent, name, d in self.records():
            if d[0] == 1:
                dirs[(parent, name)] = struct.unpack(">I", d[6:10])[0]
            elif d[0] == 2:
                files.setdefault(parent, {})[name] = d
        cur = 2  # the root folder
        for comp in path:
            match = [k for k in dirs if k[0] == cur and k[1] == comp]
            if not match:
                raise KeyError(":".join(path))
            cur = dirs[match[0]]
        out = {}
        for name, d in files.get(cur, {}).items():
            ftype, creator = d[4:8], d[8:12]
            dsize = struct.unpack(">I", d[26:30])[0]
            rsize = struct.unpack(">I", d[36:40])[0]
            dext = struct.unpack(">6H", d[74:86])
            rext = struct.unpack(">6H", d[86:98])
            out[name] = (ftype, creator, self.fork(dext, dsize), self.fork(rext, rsize))
        return out


def resources(fork):
    """{(type, id): (name, data)} from a resource fork."""
    if len(fork) < 16:
        return {}
    doff, moff = struct.unpack(">II", fork[:8])
    m = fork[moff:]
    tlo, nlo = struct.unpack(">HH", m[24:28])
    tl = m[tlo:]
    out = {}
    for i in range(struct.unpack(">H", tl[:2])[0] + 1):
        rtype, cnt, rlo = struct.unpack(">4sHH", tl[2 + 8 * i:10 + 8 * i])
        for j in range(cnt + 1):
            r = tl[rlo + 12 * j:rlo + 12 * j + 12]
            rid, noff = struct.unpack(">hH", r[:4])
            off = struct.unpack(">I", b"\0" + r[5:8])[0]
            size = struct.unpack(">I", fork[doff + off:doff + off + 4])[0]
            name = None
            if noff != 0xFFFF:
                p = m[nlo + noff:]
                name = p[1:1 + p[0]].decode("mac_roman")
            out[(rtype.decode("mac_roman"), rid)] = (name, fork[doff + off + 4:doff + off + 4 + size])
    return out


def voice_id(name):
    return name.lower().replace(" ", "-")


def extract(hda, out):
    with open(hda, "rb") as f:
        vol = HFS(f)
        voices = vol.find(VOICE_DIR)
    os.makedirs(out, exist_ok=True)
    n = 0
    for name, (ftype, creator, _, rsrc) in sorted(voices.items()):
        if ftype != b"ttvf" or creator != b"mtk3":
            continue  # only MacinTalk 3 voices
        res = resources(rsrc)
        for (rtype, _), (_, data) in res.items():
            if rtype in ("ttvd", "ttvw"):
                with open(os.path.join(out, voice_id(name) + "." + rtype), "wb") as o:
                    o.write(data)
        n += 1
        print("voice", name)
    return n


def main(argv):
    image = None
    if argv[:1] == ["--image"]:
        image, argv = argv[1], argv[2:]
    out = argv[0] if argv else os.path.join("sources", "macintalk")
    with tempfile.TemporaryDirectory() as tmp:
        if image is None:
            image = os.path.join(out, "macos-8.1.zip")
            os.makedirs(out, exist_ok=True)
            if not os.path.exists(image):
                print("fetch", URL)
                urllib.request.urlretrieve(URL, image + ".part")
                os.rename(image + ".part", image)
            h = hashlib.sha256()
            with open(image, "rb") as f:
                for b in iter(lambda: f.read(1 << 20), b""):
                    h.update(b)
            if h.hexdigest() != SHA256:
                sys.exit("%s: SHA-256 mismatch" % image)
        if zipfile.is_zipfile(image):
            with zipfile.ZipFile(image) as z:
                member = [i for i in z.namelist() if i.endswith((".hda", ".dsk", ".img"))][0]
                print("unpack", member)
                image = z.extract(member, tmp)
        if not extract(image, out):
            sys.exit("no MacinTalk 3 voices found")
    print("voices in", out)


if __name__ == "__main__":
    main(sys.argv[1:])
