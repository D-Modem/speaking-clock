#!/usr/bin/env python3
"""SPDX-License-Identifier: GPL-2.0-or-later

Fetch the Microsoft Sam, Mike and Mary voice data for synth/mssam at build
time.

Microsoft's files are not in this repository.  This script downloads the
Microsoft Speech API 4 text-to-speech package (msttsl.exe) from the
Internet Archive, checks its SHA-256, unpacks its cabinet with cabextract
or 7z, reads each voice file (an OLE compound file) and writes the parts
synth/mssam uses as plain files:

    OUT/<voice>.inv     the voice's recorded sound units, as stored
    OUT/<voice>.tree    the trees that pick a unit for each sound in its
                        context, rewritten in our own simple layout
    OUT/<voice>.tones   the voice's pitch table (plain text)
    OUT/cmudict.dict    the CMU Pronouncing Dictionary, so that words a
                        clock does not usually say can be spoken too
                        (optional: synth/mssam knows the clock's words)

The voices are sam, mike and mary (the files sam.vce, male.vce and
female.vce).  The layouts are described in docs/mssam-format.md.

Only data is read.  Nothing from the package is run, and Microsoft's
programs in it (msttssyn.dll and the rest) are not kept.

Usage: tools/fetch-mssam.py [OUT]              (default: sources/mssam)
       tools/fetch-mssam.py --exe msttsl.exe [OUT]

Needs Python 3 and cabextract or 7z.
"""
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.request

URL = "https://archive.org/download/msttsl/msttsl.exe"
SHA1 = "d988cbb3e99bc9dd850defda939c06b81d9d4544"
CMUDICT = "https://raw.githubusercontent.com/cmusphinx/cmudict/master/cmudict.dict"
VOICES = {"sam": "sam.vce", "mike": "male.vce", "mary": "female.vce"}
POSITIONS = 4          # word positions: begins, ends, single, inside


class Compound:
    """Just enough of an OLE compound file reader to get streams out."""

    def __init__(self, data):
        if data[:8] != bytes.fromhex("d0cf11e0a1b11ae1"):
            raise ValueError("not an OLE compound file")
        self.d = data
        self.ss = 1 << struct.unpack("<H", data[30:32])[0]
        self.mss = 1 << struct.unpack("<H", data[32:34])[0]
        nfat, dir0 = struct.unpack("<II", data[44:52])
        self.cutoff, mfat0, nmfat, difat0, ndifat = struct.unpack("<5I", data[56:76])
        difat = list(struct.unpack("<109I", data[76:512]))
        s = difat0
        for _ in range(ndifat):
            v = struct.unpack("<%dI" % (self.ss // 4), self.sector(s))
            difat += v[:-1]
            s = v[-1]
        self.fat = []
        for s in difat[:nfat]:
            self.fat += struct.unpack("<%dI" % (self.ss // 4), self.sector(s))
        self.minifat = []
        if nmfat:
            m = self.chain(mfat0)
            self.minifat = list(struct.unpack("<%dI" % (len(m) // 4), m))
        dirs = self.chain(dir0)
        self.entries = {}
        for i in range(0, len(dirs), 128):
            e = dirs[i:i + 128]
            nlen = struct.unpack("<H", e[64:66])[0]
            if nlen < 2:
                continue
            name = e[:nlen - 2].decode("utf-16-le")
            start, size = struct.unpack("<II", e[116:124])
            self.entries[name] = (e[66], start, size)
            if e[66] == 5:
                self.ministream = self.chain(start)[:size]

    def sector(self, s):
        o = (s + 1) * self.ss
        return self.d[o:o + self.ss]

    def chain(self, s):
        out = []
        while s < 0xFFFFFFFA:
            out.append(self.sector(s))
            s = self.fat[s]
        return b"".join(out)

    def stream(self, name):
        typ, start, size = self.entries[name]
        if size >= self.cutoff:
            return self.chain(start)[:size]
        out = []
        s = start
        while s < 0xFFFFFFFA:
            out.append(self.ministream[s * self.mss:(s + 1) * self.mss])
            s = self.minifat[s]
        return b"".join(out)[:size]


def rotate(m):
    """The voice stores a set of sounds as a 64-bit mask with sound i at bit
    (i + 36) mod 64 and the four word positions at bits 32-35.  Ours has
    sound i at bit i and the word positions at bits 60-63."""
    return ((m >> 36) | (m << 28)) & 0xFFFFFFFFFFFFFFFF


def u32(d, o):
    return struct.unpack(">I", d[o:o + 4])[0]


def u16(d, o):
    return struct.unpack(">H", d[o:o + 2])[0]


def parse_trees(d, o, count):
    """count trees of nodes; returns [(leaves, nodes)] and the end offset.
    Node: yes, no, 0xffff, then the indices of its questions, 0xffff.
    Leaf: 0xffff, n, n."""
    out = []
    for _ in range(count):
        nn, nl = u16(d, o), u16(d, o + 2)
        o += 4
        if nn != max(2 * nl - 1, 0):
            raise ValueError("tree header")
        nodes = []
        for _ in range(nn):
            x = u16(d, o)
            if x == 0xFFFF:
                a, b = u16(d, o + 2), u16(d, o + 4)
                o += 6
                if a != b or a >= nl:
                    raise ValueError("leaf")
                nodes.append((-1, a, []))
            else:
                y = u16(d, o + 2)
                if u16(d, o + 4) != 0xFFFF or x >= nn or y >= nn:
                    raise ValueError("node")
                o += 6
                q = []
                while u16(d, o) != 0xFFFF:
                    q.append(u16(d, o))
                    o += 2
                o += 2
                nodes.append((x, y, q))
        out.append((nl, nodes))
    return out, o


def convert_tree(tree, phonefile):
    phones = [l.split()[0] for l in phonefile.decode("latin-1").splitlines() if l.strip()]
    n = len(phones)
    o = tree.index(b"tree\0") + 5
    if u32(tree, o) != n:
        raise ValueError("phone count")
    o += 4 + n          # one byte per sound: its number of states (always 1)
    o += 4 + 4 * n      # sound count again, then where each sound's senones end
    total = u32(tree, o)
    # then: total, 0, 18, 0xffffffff, the number of questions
    o += 4
    while u32(tree, o) != 0xFFFFFFFF:
        o += 4
    nq = u32(tree, o + 4)
    o += 8
    questions = [struct.unpack(">QQ", tree[o + 16 * i:o + 16 * i + 16]) for i in range(nq)]
    o += 16 * nq + 8
    trees, end = parse_trees(tree, o, n)
    if end != len(tree) or sum(t[0] for t in trees) != total:
        raise ValueError("trees do not add up")

    out = [b"MSSAMTR1", struct.pack("<4I", n, nq, total, 1)]
    for left, right in questions:
        out.append(struct.pack("<QQ", rotate(left), rotate(right)))
    first = 0
    nodes = []
    qlist = []
    for name, (nl, tn) in zip(phones, trees):
        out.append(struct.pack("<8sIII", name.encode()[:8], first, len(tn), len(nodes)))
        for yes, no, q in tn:
            nodes.append(struct.pack("<iiII", yes, no, len(qlist), len(q)))
            qlist += q
        first += nl
    out.append(struct.pack("<II", len(nodes), len(qlist)))
    out += nodes
    out.append(struct.pack("<%dH" % len(qlist), *qlist))
    return b"".join(out)


def unpack(exe, tmp):
    for cmd in (["cabextract", "-q", "-d", tmp, exe], ["7z", "x", "-y", "-o" + tmp, exe]):
        if shutil.which(cmd[0]):
            subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
            return
    sys.exit("fetch-mssam: needs cabextract or 7z to unpack msttsl.exe")


def main():
    args = sys.argv[1:]
    exe = None
    if args[:1] == ["--exe"]:
        exe, args = args[1], args[2:]
    out = args[0] if args else "sources/mssam"
    os.makedirs(out, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        if not exe:
            exe = os.path.join(tmp, "msttsl.exe")
            print("fetch-mssam: downloading", URL)
            urllib.request.urlretrieve(URL, exe)
        if hashlib.sha1(open(exe, "rb").read()).hexdigest() != SHA1:
            sys.exit("fetch-mssam: msttsl.exe does not match the expected checksum")
        unpack(exe, tmp)
        files = {f.lower(): os.path.join(tmp, f) for f in os.listdir(tmp)}
        for voice, vce in VOICES.items():
            c = Compound(open(files[vce], "rb").read())
            with open(os.path.join(out, voice + ".inv"), "wb") as f:
                f.write(c.stream("Inventory"))
            with open(os.path.join(out, voice + ".tree"), "wb") as f:
                f.write(convert_tree(c.stream("TreeImage"), c.stream("PhoneFile")))
            with open(os.path.join(out, voice + ".tones"), "wb") as f:
                f.write(c.stream("Cdb"))
            print("fetch-mssam: wrote", voice)
    dic = os.path.join(out, "cmudict.dict")
    if not os.path.exists(dic):
        try:
            urllib.request.urlretrieve(CMUDICT, dic + ".tmp")
            os.replace(dic + ".tmp", dic)
            print("fetch-mssam: wrote cmudict.dict")
        except OSError as e:
            print("fetch-mssam: no cmudict.dict (%s); only the clock's own words will be known" % e)


if __name__ == "__main__":
    main()
