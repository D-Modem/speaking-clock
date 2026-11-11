#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Checks app_speakingclock in a running Asterisk, through its CLI.

  - For the tim voices, the third pip starts exactly on the second it
    announces (to the sample).
  - Every voice renders a minute of sound at a few times of day without
    crashing Asterisk, and isn't silent.

  - With TTS_VOICE=<a live voice>, that voice speaks (needs its
    synthesiser, tts_program_* in speakingclock.conf) and its made-up third pip
    lands on the second.  Live voices are left out otherwise.

Run as a user that may use `asterisk -rx`, with the module loaded and the
voices installed:  test/render_test.py
"""
import os
import struct
import subprocess
import sys
import tempfile

RATE = 8000
failed = 0


def ok(cond, what):
    global failed
    if not cond:
        failed += 1
        print("FAIL:", what)


def cli(cmd):
    return subprocess.run(["asterisk", "-rx", cmd], capture_output=True,
                          text=True, check=True).stdout


def render(voice, start, seconds, zone="UTC"):
    with tempfile.NamedTemporaryFile(suffix=".sln", delete=False) as f:
        path = f.name
    try:
        out = cli("speakingclock render %s %d %d %s %s" % (voice, start, seconds, path, zone))
        if "Wrote" not in out:
            raise RuntimeError(out.strip())
        with open(path, "rb") as f:
            d = f.read()
    finally:
        os.unlink(path)
    return struct.unpack("<%dh" % (len(d) // 2), d)


rows = [l.split() for l in cli("speakingclock show voices").splitlines()[1:] if l.strip()]
voices = [r[0] for r in rows if r[1] != "tts"]
ok(len(voices) >= 80, "voices listed: %d" % len(voices))

# the time marks: start 5 s before 16:15:00 UTC, look at three cycles.
# The pips clip is copied into the stream sample for sample, so find it,
# and add the voice's marker (where in the clip the third pip starts).
T = 1762791295
VOICEDIR = os.environ.get("VOICEDIR", os.path.join(os.path.dirname(__file__), "..", "voices"))


def clip(voice, name):
    import wave
    with wave.open(os.path.join(VOICEDIR, voice, name + ".wav")) as w:
        return w.readframes(w.getnframes())


def marker(voice):
    for line in open(os.path.join(VOICEDIR, voice, "voice.conf")):
        if line.startswith("marker="):
            return float(line.split("=", 1)[1])
    return None


for v in ("tim-pat-simmons", "tim-gordon-gow"):
    a = render(v, T, 32)
    raw = struct.pack("<%dh" % len(a), *a)
    pips = clip(v, "pips")
    m = round(marker(v) * RATE)
    # a stretch from the middle of the clip, clear of the other clips
    probe = pips[len(pips) // 2 - 400:len(pips) // 2 + 400]
    found, at = [], raw.find(probe)
    while at >= 0:
        if at % 2 == 0:
            found.append(at // 2 - (len(pips) // 2 - 400) // 2 + m)
        at = raw.find(probe, at + 2)
    ok(len(found) >= 3, "%s: pips found %d times" % (v, len(found)))
    for p in found:
        off = (T * RATE + p) % (10 * RATE)
        ok(off == 0, "%s: third pip %+.2f ms from the second" % (v, off * 1000 / RATE))


# every voice, three times of day (morning, noon, evening), not silent
for v in voices:
    for t in (1762758000, 1762776000, 1762804500):   # 07:00, 12:00, 19:55 UTC
        try:
            a = render(v, t, 60)
        except Exception as e:  # noqa: BLE001
            ok(False, "%s at %d: %s" % (v, t, e))
            continue
        ok(max(abs(x) for x in a) > 1000, "%s at %d: silent" % (v, t))

# a live voice: speech before the pips, and the third made-up pip (1 kHz)
# exactly on the second
tts = os.environ.get("TTS_VOICE")
if tts:
    a = render(tts, T, 32)
    tone = [int(8000 * __import__("math").sin(2 * 3.141592653589793 * 1000 * i / RATE)) for i in range(80)]
    raw = struct.pack("<%dh" % len(a), *a)
    marks = []
    for k in range(len(a) // (10 * RATE) + 1):
        second = (k + 1) * 10 * RATE - (T * RATE) % (10 * RATE)   # samples to each :x0
        if second + 80 <= len(a):
            marks.append(list(a[second:second + 80]) == tone)
    ok(marks and all(marks), "%s: third pip on the second %s" % (tts, marks))
    ok(max(abs(x) for x in a[:3 * RATE]) > 1000, "%s: no speech" % tts)
ok("Asterisk" in cli("core show version"), "Asterisk still up")

print("%s" % ("all passed" if not failed else "%d failed" % failed))
sys.exit(1 if failed else 0)
