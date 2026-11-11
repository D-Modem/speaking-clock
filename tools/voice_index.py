#!/usr/bin/env python3
"""SPDX-License-Identifier: GPL-2.0-or-later

Write voices/INDEX.md from every voice folder's voice.conf.
Usage: tools/voice_index.py [VOICES]   (default ./voices)
"""
import os
import sys


def read_conf(path):
    conf, section = {}, None
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.strip()
        if not line or line.startswith(";"):
            continue
        if line.startswith("["):
            section = line.strip("[]").lower()
            continue
        k, _, v = line.partition("=")
        conf.setdefault(section, {})[k.strip().lower()] = v.strip()
    return conf


def write_index(out):
    rows = []
    for vid in sorted(os.listdir(out)):
        p = os.path.join(out, vid, "voice.conf")
        if not os.path.isfile(p):
            continue
        c = read_conf(p)
        v, s = c.get("voice", {}), c.get("source", {})
        origin = s.get("collection") or ("%s synthesiser" % s["engine"] if "engine" in s else "")
        rows.append((vid, v.get("name", vid).replace("|", "/"), v.get("mode", "talking"), origin))
    with open(os.path.join(out, "INDEX.md"), "w") as f:
        f.write("# Voices\n\nWritten by tools/voice_index.py. Every recording "
                "belongs to its original owner (../NOTICE.md).\n\n")
        f.write("| id | name | mode | from |\n|---|---|---|---|\n")
        for r in rows:
            f.write("| `%s` | %s | %s | %s |\n" % r)
    return len(rows)


if __name__ == "__main__":
    print("%d voices" % write_index(sys.argv[1] if len(sys.argv) > 1 else "voices"))
