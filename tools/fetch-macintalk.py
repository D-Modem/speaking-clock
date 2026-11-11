#!/usr/bin/env python3
"""SPDX-License-Identifier: GPL-2.0-or-later

Fetch the MacInTalk voice files for synth/macintalk at build time.

Apple's files are not in this repository.  This script downloads two hard
disk images from the Internet Archive, checks their SHA-256, finds the voice
files in System Folder:Extensions:Voices on each disk's HFS volume, and
writes the voices' data resources as plain files:

  from Mac OS 8.1, the MacInTalk 3 voices (Fred, Ralph, Kathy ...):
    OUT/<voice>.ttvd   the voice description (parameters, see
                       docs/macintalk-format.md)
    OUT/<voice>.ttvw   the voice's sampled sound, for the voices that
                       have one (Bells, Boing, Cellos ...)
  from Mac OS 8.6, the PlainTalk voices (Agnes, Bruce, Victoria):
    OUT/<voice>.gtss   the recorded diphones (see docs/plaintalk-format.md)

Only data is read.  Nothing from the disks is run, and Apple's code (the
MacinTalk 3 and MacinTalk Pro extensions, and the code resources in the
PlainTalk voice files) is not extracted.

Usage: tools/fetch-macintalk.py [OUT]     (default: sources/macintalk)
       tools/fetch-macintalk.py --image FILE.hda|FILE.zip|FILE.exe [OUT]
       tools/fetch-macintalk.py --no-plaintalk [OUT]

Needs Python 3 and about 1.2 GB of free disk while it runs.  The Mac OS 8.6
image comes as a self-extracting RAR archive, so the PlainTalk voices also
need unar (Debian/Ubuntu package unar) or unrar; without either, they are
skipped.
"""
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

URL = ("https://archive.org/download/macos-8.1-basilisk-ii/"
       "macos-8.1-basilisk-ii-2026.10.1.zip")
SHA256 = "ce6ee4ba5294182535d20bf6639f64415376e4420b3473167803315e615c97fa"
URL_86 = ("https://archive.org/download/MacOS8.6Image-SheepShaverEmulator/"
          "Mac%20OS%208.6%20on%20disk%20image%20%2B%20SheepShaver.exe")
SHA256_86 = "003910efa3fc95a9c0da3f064e0e9aafafd9c2b20b11fde2e01d2f8fd9472c92"
DSK_86 = "SheepShaver/Mac_OS_HD.dsk"
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
    """Write the voices found on the HFS disk image hda; returns how many."""
    with open(hda, "rb") as f:
        vol = HFS(f)
        voices = vol.find(VOICE_DIR)
    os.makedirs(out, exist_ok=True)
    n = 0
    for name, (ftype, creator, _, rsrc) in sorted(voices.items()):
        if ftype != b"ttvf":
            continue
        res = resources(rsrc)
        if creator == b"mtk3":      # MacinTalk 3
            want = {"ttvd": "ttvd", "ttvw": "ttvw"}
        elif creator == b"gala":    # MacinTalk Pro (PlainTalk)
            if "high quality" in name:
                continue  # coded differently; synth/macintalk can't read them yet
            # 'gtss' 3 is the voice's recorded sound; 'gtss' 4 is code: left out
            res = {k: v for k, v in res.items() if k == ("gtss", 3)}
            want = {"gtss": "gtss"}
        else:
            continue
        for (rtype, _), (_, data) in res.items():
            if rtype in want:
                with open(os.path.join(out, voice_id(name) + "." + want[rtype]), "wb") as o:
                    o.write(data)
        n += 1
        print("voice", name)
    return n


def fetch(url, sha, path):
    if not os.path.exists(path):
        print("fetch", url)
        urllib.request.urlretrieve(url, path + ".part")
        os.rename(path + ".part", path)
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    if h.hexdigest() != sha:
        sys.exit("%s: SHA-256 mismatch" % path)


def unrar(archive, member, tmp):
    """Unpack one file of a (self-extracting) RAR archive with unar or
    unrar; None if neither is installed."""
    for cmd in (["unar", "-q", "-f", "-o", tmp, archive, member],
                ["unrar", "x", "-y", "-idq", archive, member, tmp + os.sep]):
        if shutil.which(cmd[0]):
            subprocess.run(cmd, check=True)
            path = os.path.join(tmp, *member.split("/"))
            if os.path.exists(path):
                return path
    return None


def unpack(image, tmp):
    """The disk image itself, from a zip or RAR archive if it's in one."""
    if zipfile.is_zipfile(image):
        with zipfile.ZipFile(image) as z:
            member = [i for i in z.namelist() if i.endswith((".hda", ".dsk", ".img"))][0]
            print("unpack", member)
            return z.extract(member, tmp)
    with open(image, "rb") as f:
        head = f.read(1 << 20)
    if head[:2] == b"MZ" and b"Rar!\x1a\x07" in head:
        print("unpack", DSK_86)
        return unrar(image, DSK_86, tmp)
    return image


def main(argv):
    images, plaintalk = [], True
    while argv[:1] and argv[0].startswith("--"):
        if argv[0] == "--image" and len(argv) > 1:
            images.append(argv[1])
            argv = argv[2:]
        elif argv[0] == "--no-plaintalk":
            plaintalk = False
            argv = argv[1:]
        else:
            sys.exit(__doc__)
    out = argv[0] if argv else os.path.join("sources", "macintalk")
    os.makedirs(out, exist_ok=True)
    if not images:
        images.append(os.path.join(out, "macos-8.1.zip"))
        fetch(URL, SHA256, images[0])
        if plaintalk:
            if shutil.which("unar") or shutil.which("unrar"):
                images.append(os.path.join(out, "macos-8.6.exe"))
                fetch(URL_86, SHA256_86, images[1])
            else:
                print("unar or unrar not found: skipping the PlainTalk voices")
    found = 0
    for image in images:
        with tempfile.TemporaryDirectory(dir=out) as tmp:
            disk = unpack(image, tmp)
            if disk is None:
                print("%s: can't unpack it (install unar or unrar)" % image)
                continue
            found += extract(disk, out)
    if not found:
        sys.exit("no voices found")
    print("voices in", out)


if __name__ == "__main__":
    main(sys.argv[1:])
