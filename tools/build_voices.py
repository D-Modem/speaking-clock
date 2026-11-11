#!/usr/bin/env python3
"""SPDX-License-Identifier: GPL-2.0-or-later

Turn the downloaded voice sources into voices/: one folder per voice,
clips renamed to the names app_speakingclock looks for, converted to 8 kHz
16-bit mono WAV, with a voice.conf and the voice's original notes.

Usage: tools/build_voices.py SOURCES OUT
  SOURCES is what tools/fetch-sources.sh made (steve/, garry/, asterisk-tim/)
  OUT is the voices folder to (re)write, normally ./voices

Needs ffmpeg.  Clip names are documented in docs/voice-format.md.
"""
import os
import re
import shutil
import subprocess
import sys
import wave
import struct
import urllib.parse

STEVE_URL = "http://clock.steve-audio.net/voices/"
GARRY_URL = "https://github.com/garrysblog/Speaking-Clock-MP3-Files"
TIM_URL = "https://github.com/paulseward/asterisk-tim"

FRAGMENTS = {"twen", "thir", "for", "fif", "six", "seven", "eight", "nine",
             "teen", "ty"} | {"ty%d" % i for i in range(1, 10)}

voices = []          # (id, info) for the index


def slug(name):
    s = name.lower().replace("&", "and").replace("'", "")
    s = re.sub(r"[^a-z0-9]+", "-", s).strip("-")
    return s


def x_name(stem):
    return "x_" + re.sub(r"[^a-z0-9]+", "_", stem.lower()).strip("_")


def convert(src, dst):
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", src, "-ac", "1",
                    "-ar", "8000", "-sample_fmt", "s16", "-map_metadata", "-1",
                    "-fflags", "+bitexact", "-flags:a", "+bitexact", dst],
                   check=True)


def wav_len(path):
    with wave.open(path) as w:
        return w.getnframes() / w.getframerate()


def pip_onsets(path):
    """Onsets (seconds) of the tone bursts in a pips clip."""
    with wave.open(path) as w:
        n = w.getnframes()
        a = struct.unpack("<%dh" % n, w.readframes(n))
    peak = max(1, max(abs(x) for x in a))
    win, on, out = 40, False, []
    for i in range(0, n - win, win):
        e = max(abs(x) for x in a[i:i + win])
        if not on and e > peak * 0.3:
            on = True
            # back up to the first sample over a tenth of the peak
            j = i
            while j > max(0, i - win) and abs(a[j]) > peak * 0.1:
                j -= 1
            out.append(j / 8000)
        elif on and e < peak * 0.1:
            on = False
    return out


def write_conf(d, voice, source):
    with open(os.path.join(d, "voice.conf"), "w") as f:
        f.write("; Voice for app_speakingclock (docs/voice-format.md)\n")
        f.write("; The recordings in this folder belong to their original "
                "owners; see NOTICE.md.\n\n[voice]\n")
        for k, v in voice.items():
            f.write("%s=%s\n" % (k, v))
        f.write("\n[source]\n")
        for k, v in source.items():
            f.write("%s=%s\n" % (k, v))


def canonical_steve(stem, chime_set):
    """Map a Steve's Talking Clock file name to our clip name."""
    s = stem.lower().strip()
    if chime_set:
        if s in ("00", "15", "30", "45"):
            return "q" + s
        if s == "chime":
            return "chime"
    if s == "00":
        return "oclock"
    if re.fullmatch(r"\d{1,2}", s):
        return "n%d" % int(s)
    m = re.fullmatch(r"(\d{1,2})a", s)
    if m:
        return "h%d" % int(m.group(1))
    if re.fullmatch(r"hour(0\d|1[0-2])", s):      # hour01..hour12: per-hour sound
        return "hr%d" % int(s[4:])
    if s in ("hour1", "hour2"):                    # top-of-hour chimes
        return "hourchime" + s[4:]
    if s in ("its",):
        return "its"
    if s == "o":
        return "oh"
    if s in ("am", "pm"):
        return s
    if s == "announcement":
        return "intro"
    if s in FRAGMENTS:
        return "f_" + s
    if s == "good morning":
        return "greet_morning"
    if s == "good afternoon":
        return "greet_afternoon"
    if s == "good evening":
        return "greet_evening"
    return x_name(s)


def build_steve(src, out):
    pkgs = sorted(os.listdir(os.path.join(src, "steve", "x")))
    for pkg in pkgs:
        root = os.path.join(src, "steve", "x", pkg)
        dl = [f for f in os.listdir(os.path.join(src, "steve", "dl"))
              if os.path.splitext(f)[0] == pkg][0]
        url = STEVE_URL + urllib.parse.quote(dl)
        desc = ""
        notes = []
        for dp, _, fs in os.walk(root):
            for f in fs:
                p = os.path.join(dp, f)
                if f.lower() == "new.ini":
                    for line in open(p, encoding="latin-1"):
                        k, _, v = line.strip().partition("=")
                        if v and k.lower() != "concat":
                            desc = v
                elif f.lower().endswith((".txt", ".rtf")):
                    notes.append(p)
        dirs = sorted({dp for dp, _, fs in os.walk(root)
                       if any(f.lower().endswith(".wav") for f in fs)})
        for d in dirs:
            name = pkg if len(dirs) == 1 else os.path.basename(d)
            vid = slug(name)
            wavs = [f for f in os.listdir(d) if f.lower().endswith(".wav")]
            stems = {os.path.splitext(f)[0].lower() for f in wavs}
            chime_set = {"15", "45", "chime"} <= stems and "1" not in stems
            vd = os.path.join(out, vid)
            os.makedirs(vd)
            got = {}
            for f in sorted(wavs):
                c = canonical_steve(os.path.splitext(f)[0], chime_set)
                if c in got:          # e.g. 1.wav and 01.wav
                    c = x_name(os.path.splitext(f)[0])
                got[c] = f
                convert(os.path.join(d, f), os.path.join(vd, c + ".wav"))
            if chime_set:
                mode = "chime"
            elif any(k.startswith("hr") for k in got) and "n1" not in got:
                mode = "hourly"
            else:
                mode = "talking"
            for n in notes:
                shutil.copy(n, os.path.join(vd, "ORIGINAL-" + os.path.basename(n)))
            voice = {"name": name, "description": desc or name, "mode": mode}
            if mode == "talking":
                voice["sequence"] = "intro+its+hour+minute+ampm"
            write_conf(vd, voice, {
                "collection": "Steve's Talking Clock voice library",
                "package": dl,
                "url": url,
                "files": ", ".join("%s=%s" % (k, v) for k, v in sorted(got.items())),
            })
            voices.append((vid, name, mode, "Steve's Talking Clock"))


GARRY_MAP = {
    "the time is": "its", "it is": "x_it_is", "am": "am", "pm": "pm",
    "good morning": "greet_morning", "good afternoon": "greet_afternoon",
    "good evening": "greet_evening", "at the third stroke, it will be": "phrase",
    "at the third stoke": "x_at_the_third_stroke", "beep beep beep": "pips",
    "precisely": "precisely", "12 noon": "noon", "twelve noon": "noon",
    "midnight": "midnight",
}


def build_garry(src, out):
    root = os.path.join(src, "garry")
    for sub in sorted(os.listdir(root)):
        d = os.path.join(root, sub)
        if not os.path.isdir(d) or sub.startswith("."):
            continue
        vid = slug(sub)
        vd = os.path.join(out, vid)
        os.makedirs(vd)
        got = {}
        for f in sorted(os.listdir(d)):
            m = re.fullmatch(r"(\d{3}) (.*)\.mp3", f)
            if not m:
                continue
            num, text = int(m.group(1)), m.group(2).strip()
            if num <= 60:
                c = "n%d" % num
            elif 61 <= num <= 72:
                c = "hoc%d" % (num - 60)
            else:
                c = GARRY_MAP.get(text.lower(), x_name(text))
            got[c] = f
            convert(os.path.join(d, f), os.path.join(vd, c + ".wav"))
        write_conf(vd, {
            "name": sub.replace("-", " "),
            "description": "Text-to-speech voice made for an Arduino clock "
                           "by Garry (garrysblog.com)",
            "mode": "talking",
            "sequence": "greeting+its+hour+minute+ampm",
        }, {
            "collection": "garrysblog/Speaking-Clock-MP3-Files",
            "url": GARRY_URL,
            "terms": "\"free to download and use in your own projects\" "
                     "(the repository's README)",
            "files": ", ".join("%s=%s" % (k, v) for k, v in sorted(got.items())),
        })
        voices.append((vid, sub.replace("-", " "), "talking", "garrysblog"))


TIM_INFO = {
    "pat_simmons": ("Pat Simmons", "UK GPO speaking clock (TIM), 1963-1985",
                    "Andy Emmerson and the TIM2000 project, via asterisk-tim"),
    "gordon_gow": ("Gordon Gow", "Australian speaking clock, 1954-1990",
                   "recorded by Paul Seward from the glass-disc machine at the "
                   "Science Museum, London, via asterisk-tim"),
}


def build_tim(src, out):
    root = os.path.join(src, "asterisk-tim", "Audio")
    for sub, (name, desc, credit) in TIM_INFO.items():
        d = os.path.join(root, sub)
        vid = "tim-" + slug(sub)
        vd = os.path.join(out, vid)
        os.makedirs(vd)
        for f in sorted(os.listdir(d)):
            p = os.path.join(d, f)
            if not f.endswith(".wav") or f == "00.wav":
                continue
            s = f[:-4]
            if s == "the_phrase":
                c = "phrase"
            elif s.startswith("hour_"):
                c = "h%d" % int(s[5:])
            elif re.fullmatch(r"\d\d", s):
                c = "m%d" % int(s)
            elif s.startswith("and_"):
                c = "s%d" % int(s.split("_")[1])
            elif s in ("oclock", "precisely", "pips"):
                c = s
            else:
                c = x_name(s)
            convert(p, os.path.join(vd, c + ".wav"))
        shutil.copy(os.path.join(d, "README.md"),
                    os.path.join(vd, "ORIGINAL-README.md"))
        onsets = pip_onsets(os.path.join(vd, "pips.wav"))
        seq = ["phrase", "h1", "m1", "s10", "pips"]
        length = sum(wav_len(os.path.join(vd, c + ".wav")) for c in seq)
        before = sum(wav_len(os.path.join(vd, c + ".wav")) for c in seq[:-1])
        write_conf(vd, {
            "name": name, "description": desc, "mode": "tim",
            "sequence": "phrase+hour+minute+seconds+pips",
            "period": "10",
            "marker": "%.4f" % onsets[-1],
        }, {
            "collection": "asterisk-tim", "url": TIM_URL, "credit": credit,
            "note": "cycle %.2f s, third pip %.3f s into it" % (length, before + onsets[-1]),
        })
        voices.append((vid, name, "tim", "asterisk-tim"))


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    # start again from nothing, but keep the synthesised voices
    # (tools/synth_voices.py), whose voice.conf names an engine
    for d in os.listdir(out):
        conf = os.path.join(out, d, "voice.conf")
        if os.path.isdir(os.path.join(out, d)) and \
                "\nengine=" not in (open(conf).read() if os.path.exists(conf) else ""):
            shutil.rmtree(os.path.join(out, d))
    build_tim(src, out)
    build_garry(src, out)
    build_steve(src, out)
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from voice_index import write_index
    write_index(out)
    print("%d voices built" % len(voices))


if __name__ == "__main__":
    main()
