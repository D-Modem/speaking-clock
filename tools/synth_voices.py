#!/usr/bin/env python3
"""SPDX-License-Identifier: GPL-2.0-or-later

Make clock voices from speech synthesisers: every clip a clock needs is
spoken by the synthesiser on its own (a whole phrase or number, not glued
syllables), trimmed, converted to 8 kHz 16-bit mono WAV, and written as a
voice folder with a voice.conf (docs/voice-format.md).

Usage: tools/synth_voices.py OUT VOICE...    (OUT is normally ./voices)
       tools/synth_voices.py --live OUT VOICE...
       tools/synth_voices.py --list

--live writes a live voice instead (mode=tts, live-<engine>-<voice>): no
clips, the module runs the synthesiser on the Asterisk machine for each
sentence as it is needed (dectalk, sam, mssam, macintalk and plaintalk
only; tts_program_dectalk, tts_program_sam, tts_program_mssam and
tts_program_macintalk in speakingclock.conf say where they are).

Engines (a VOICE is engine:name, see --list):
  sapi4    Microsoft Speech API 4 voices (Microsoft Sam, Mike, Mary,
           TruVoice, L&H), spoken by the public SAPI4 service at
           https://www.tetyys.com/SAPI4/ (a few hundred requests a voice,
           spaced out; downloads are cached in $SYNTH_CACHE).
  dectalk  DECtalk 4.x, from https://github.com/dectalk/dectalk (develop
           branch).  $DECTALK_SAY is its `say` program.
  sam      Software Automatic Mouth (C64, 1982), the C port at
           https://github.com/s-macke/SAM.  $SAM is its `sam` program.
  mssam    Microsoft Sam, Mike and Mary, spoken by our own synthesiser
           (synth/mssam) from the voice data that tools/fetch-mssam.py
           fetches.  $MSSAM is its `mssam` program.
  mac      MacInTalk / PlainTalk voices, with macOS's own `say` (run this
           on a Mac).
  macintalk  The MacInTalk 3 voices (Fred, Ralph, Kathy, Zarvox ...) spoken
           by our own synthesiser, synth/macintalk, from Apple's voice data
           (tools/fetch-macintalk.py gets it).  $MACINTALK is its program,
           $MACINTALK_VOICES the voice folder.  Works with --live.
  plaintalk  The PlainTalk voices (Agnes, Bruce, Victoria, and their high
           quality versions agnes-hq ...), Apple's recorded diphones
           joined by the same program, synth/macintalk.  Works with --live.

Needs ffmpeg and sox.
"""
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request

ONES = ["zero", "one", "two", "three", "four", "five", "six", "seven",
        "eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen",
        "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"]
TENS = {20: "twenty", 30: "thirty", 40: "forty", 50: "fifty"}


def words(n):
    if n < 20:
        return ONES[n]
    t, u = n // 10 * 10, n % 10
    return TENS[t] + ("-" + ONES[u] if u else "")


def clips():
    """clip name -> what to say"""
    c = {
        "phrase": "At the third stroke, it will be",
        "its": "The time is",
        "oclock": "o'clock",
        "oh": "oh",
        "precisely": "precisely.",
        "am": "A.M.",
        "pm": "P.M.",
        "noon": "twelve noon",
        "midnight": "midnight",
        "greet_morning": "Good morning.",
        "greet_afternoon": "Good afternoon.",
        "greet_evening": "Good evening.",
    }
    for n in range(0, 60):
        c["n%d" % n] = words(n)
    for m in range(1, 10):
        c["m%d" % m] = "oh " + ONES[m]
    for s in range(10, 60, 10):
        c["s%d" % s] = "and %s seconds." % words(s)
    return c


# ---- engines: each speaks text into a WAV file -----------------------------

CACHE = os.environ.get("SYNTH_CACHE", os.path.join("sources", "synth-cache"))

SAPI4 = {
    # id: (service voice, pitch, speed, name, description)
    "microsoft-sam": ("Sam", 100, 150, "Microsoft Sam (SAPI4)",
                      "Microsoft Speech API 4 voice, the voice of Windows 2000 and XP's Narrator"),
    "microsoft-mike": ("Mike", 113, 170, "Microsoft Mike (SAPI4)", "Microsoft Speech API 4 voice"),
    "microsoft-mary": ("Mary", 169, 170, "Microsoft Mary (SAPI4)", "Microsoft Speech API 4 voice"),
    "microsoft-mike-tel": ("Mike (for Telephone)", 113, 170, "Microsoft Mike for Telephone (SAPI4)",
                           "Microsoft Speech API 4 voice tuned for telephone lines"),
    "bonzibuddy": ("Adult Male #2, American English (TruVoice)", 140, 157, "BonziBUDDY (SAPI4 TruVoice)",
                   "Lernout & Hauspie TruVoice Adult Male #2 at BonziBUDDY's pitch and speed"),
    "truvoice-male1": ("Adult Male #1, American English (TruVoice)", 120, 140,
                       "TruVoice Adult Male #1 (SAPI4)", "Lernout & Hauspie TruVoice"),
    "truvoice-female1": ("Adult Female #1, American English (TruVoice)", 290, 140,
                         "TruVoice Adult Female #1 (SAPI4)", "Lernout & Hauspie TruVoice"),
    "robosoft-one": ("RoboSoft One", 100, 150, "RoboSoft One (SAPI4)", "SAPI4 novelty voice"),
    "lh-reed": ("English-American: Reed (Adult Male), 6.1", 100, 150, "L&H Reed (SAPI4)",
                "Lernout & Hauspie TTS3000 American English"),
    "lh-jane": ("English-British: Jane (Adult Female), 6.1", 100, 150, "L&H Jane (SAPI4)",
                "Lernout & Hauspie TTS3000 British English"),
}

DECTALK = {
    # id: (voice code, name)
    "paul": ("np", "Perfect Paul"), "betty": ("nb", "Beautiful Betty"),
    "harry": ("nh", "Huge Harry"), "frank": ("nf", "Frail Frank"),
    "dennis": ("nd", "Doctor Dennis"), "kit": ("nk", "Kit the Kid"),
    "ursula": ("nu", "Uppity Ursula"), "rita": ("nr", "Rough Rita"),
    "wendy": ("nw", "Whispering Wendy"),
}

SAM = {
    # id: (speed, pitch, mouth, throat, name) - the presets in SAM's manual
    "sam": (72, 64, 128, 128, "SAM"),
    "elf": (72, 64, 110, 160, "SAM Elf"),
    "little-robot": (92, 60, 190, 190, "SAM Little Robot"),
    "stuffy-guy": (82, 72, 110, 105, "SAM Stuffy Guy"),
    "little-old-lady": (82, 32, 145, 145, "SAM Little Old Lady"),
    "extra-terrestrial": (100, 64, 150, 200, "SAM Extra-Terrestrial"),
}

# macOS's own say speaks them.  The MacinTalk 3 voices (1993) and the
# PlainTalk ones (MacinTalk Pro, 1994-95, the voices of System 7.5 to Mac
# OS 9).  Which ones a Mac still has depends on its macOS: say -v '?'.
MAC_FAMILY = {
    "MacInTalk": ["Fred", "Ralph", "Junior", "Kathy", "Albert", "Bahh",
                  "Bells", "Boing", "Bubbles", "Cellos", "Trinoids", "Whisper",
                  "Zarvox", "Bad News", "Good News", "Wobble", "Jester", "Organ",
                  "Superstar", "Princess"],
    "PlainTalk": ["Victoria", "Agnes", "Bruce", "Vicki"],
}
MAC = {v: f for f, vs in MAC_FAMILY.items() for v in vs}

# The MacInTalk 3 voices of Mac OS 8.1, spoken by synth/macintalk.
# id: (name, what the voice is like)
MACINTALK = {
    "fred": ("Fred", "the standard male voice"),
    "ralph": ("Ralph", "a deep male voice"),
    "kathy": ("Kathy", "the standard female voice"),
    "junior": ("Junior", "a boy's voice"),
    "princess": ("Princess", "a girl's voice"),
    "zarvox": ("Zarvox", "a robot"),
    "trinoids": ("Trinoids", "aliens speaking together"),
    "whisper": ("Whisper", "a whisper"),
    "albert": ("Albert", "a frog in the throat"),
    "bahh": ("Bahh", "a bleating lamb"),
    "bells": ("Bells", "spoken by bells"),
    "boing": ("Boing", "spoken by a spring"),
    "bubbles": ("Bubbles", "speaking under water"),
    "cellos": ("Cellos", "sung by cellos, to In the Hall of the Mountain King"),
    "pipe-organ": ("Pipe Organ", "sung by a pipe organ"),
    "good-news": ("Good News", "sung to Pomp and Circumstance"),
    "bad-news": ("Bad News", "sung to Chopin's funeral march"),
    "deranged": ("Deranged", "an unhinged voice"),
    "hysterical": ("Hysterical", "speaking through laughter"),
}

# The PlainTalk (MacinTalk Pro) voices of Mac OS 8.6, spoken by synth/macintalk.
PLAINTALK = {
    "agnes": ("Agnes", "female"),
    "bruce": ("Bruce", "male"),
    "victoria": ("Victoria", "female"),
    "agnes-hq": ("Agnes (high quality)", "female"),
    "bruce-hq": ("Bruce (high quality)", "male"),
    "victoria-hq": ("Victoria (high quality)", "female"),
}

last_request = [0.0]


def sapi4(vid, text, out):
    voice, pitch, speed = SAPI4[vid][:3]
    url = "https://www.tetyys.com/SAPI4/SAPI4?" + urllib.parse.urlencode(
        {"text": text, "voice": voice, "pitch": pitch, "speed": speed})
    key = hashlib.sha1(url.encode()).hexdigest()
    cached = os.path.join(CACHE, "sapi4", key + ".wav")
    if not os.path.exists(cached):
        os.makedirs(os.path.dirname(cached), exist_ok=True)
        for attempt in range(4):
            wait = 0.5 - (time.time() - last_request[0])
            if wait > 0:
                time.sleep(wait)
            last_request[0] = time.time()
            try:
                req = urllib.request.Request(url, headers={"User-Agent": "speaking-clock voice builder"})
                with urllib.request.urlopen(req, timeout=60) as r:
                    data = r.read()
                if data[:4] != b"RIFF":
                    raise RuntimeError("not a WAV")
                with open(cached + ".part", "wb") as f:
                    f.write(data)
                os.rename(cached + ".part", cached)
                break
            except Exception:  # noqa: BLE001
                if attempt == 3:
                    raise
                time.sleep(2 ** (attempt + 1))
    shutil.copy(cached, out)


def dectalk(vid, text, out):
    say = os.environ.get("DECTALK_SAY", "say")
    code = DECTALK[vid][0]
    subprocess.run([say, "-fo", out, "-a", "[:%s] %s" % (code, text)], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def sam(vid, text, out):
    speed, pitch, mouth, throat = SAM[vid][:4]
    prog = os.environ.get("SAM", "sam")
    subprocess.run([prog, "-wav", out, "-speed", str(speed), "-pitch", str(pitch),
                    "-mouth", str(mouth), "-throat", str(throat), text.replace("-", " ")],
                   check=True, stdout=subprocess.DEVNULL)


MSSAM = {
    "sam": ("Microsoft Sam", "Microsoft Speech API 4 voice Sam"),
    "mike": ("Microsoft Mike", "Microsoft Speech API 4 voice Mike"),
    "mary": ("Microsoft Mary", "Microsoft Speech API 4 voice Mary"),
    "mike-tel": ("Microsoft Mike for Telephone", "Microsoft Speech API 4 voice Mike, made for phone lines"),
    "mary-tel": ("Microsoft Mary for Telephone", "Microsoft Speech API 4 voice Mary, made for phone lines"),
}


def mssam(vid, text, out):
    prog = os.environ.get("MSSAM", "mssam")
    subprocess.run([prog, "-v", vid, "-o", out, text], check=True)


def mac_check(vid):
    have = subprocess.run(["say", "-v", "?"], capture_output=True, text=True).stdout
    if not any(line.split("  ")[0].strip() == vid for line in have.splitlines()):
        sys.exit("this Mac has no voice %r (say -v '?' lists the ones it has)" % vid)


def mac(vid, text, out):
    aiff = out + ".aiff"
    subprocess.run(["say", "-v", vid, "-o", aiff, text], check=True)
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", aiff, out], check=True)
    os.unlink(aiff)


def macintalk(vid, text, out):
    prog = os.environ.get("MACINTALK", os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "synth", "macintalk", "macintalk"))
    cmd = [prog, "-v", vid, "-o", out, text]
    if os.environ.get("MACINTALK_VOICES"):
        cmd[1:1] = ["-d", os.environ["MACINTALK_VOICES"]]
    subprocess.run(cmd, check=True)


ENGINES = {
    "sapi4": (sapi4, SAPI4, lambda v: (SAPI4[v][3], SAPI4[v][4]),
              "Microsoft Speech API 4 runtime and voices, spoken by https://www.tetyys.com/SAPI4/"),
    "dectalk": (dectalk, DECTALK, lambda v: ("DECtalk " + DECTALK[v][1], "DECtalk 4.x"),
                "DECtalk (Digital Equipment Corp.), https://github.com/dectalk/dectalk"),
    "sam": (sam, SAM, lambda v: (SAM[v][4], "Software Automatic Mouth (Commodore 64, 1982)"),
            "SAM, Don't Ask Software (now SoftVoice, Inc.), C port https://github.com/s-macke/SAM"),
    "mssam": (mssam, MSSAM, lambda v: (MSSAM[v][0], "%s, by our own synthesiser" % MSSAM[v][1]),
              "synth/mssam (this repository, clean-room) reading Microsoft's Speech API 4 voice data "
              "(msttsl.exe), fetched by tools/fetch-mssam.py"),
    "mac": (mac, MAC, lambda v: ("%s %s" % (MAC[v], v),
                                 "Apple PlainTalk (MacinTalk Pro) voice" if MAC[v] == "PlainTalk"
                                 else "Apple MacInTalk 3 voice"),
            "Apple MacInTalk / PlainTalk, spoken by macOS's say"),
    "macintalk": (macintalk, MACINTALK,
                  lambda v: ("MacInTalk %s" % MACINTALK[v][0],
                             "MacInTalk 3 voice (%s)" % MACINTALK[v][1]),
                  "synth/macintalk (this repository, clean-room) reading Apple's "
                  "MacInTalk 3 voice data (Mac OS 8.1), fetched by tools/fetch-macintalk.py"),
    "plaintalk": (macintalk, PLAINTALK,
                  lambda v: ("PlainTalk %s" % PLAINTALK[v][0],
                             "PlainTalk (MacinTalk Pro) voice, %s" % PLAINTALK[v][1]),
                  "synth/macintalk (this repository, clean-room) joining Apple's recorded "
                  "PlainTalk diphones (Mac OS 8.6), fetched by tools/fetch-macintalk.py"),
}


def finish(raw, out):
    """trim the silence around a clip, leave 40 ms each side, 8 kHz mono"""
    subprocess.run(["sox", raw, "-r", "8000", "-c", "1", "-b", "16", "-e", "signed-integer", out,
                    "silence", "1", "0.01", "0.3%", "reverse", "silence", "1", "0.01", "0.3%", "reverse",
                    "pad", "0.04", "0.04", "norm", "-3"], check=True)


def build(out_dir, engine, vid):
    fn, table, info, source = ENGINES[engine]
    name, desc = info(vid)
    if engine == "mac":
        mac_check(vid)
    d = os.path.join(out_dir, "%s-%s" % (engine, vid.lower().replace(" ", "-")))
    if os.path.exists(d):
        shutil.rmtree(d)
    os.makedirs(d)
    with tempfile.TemporaryDirectory() as tmp:
        for clip, text in clips().items():
            raw = os.path.join(tmp, clip + ".raw.wav")
            fn(vid, text, raw)
            finish(raw, os.path.join(d, clip + ".wav"))
    with open(os.path.join(d, "voice.conf"), "w") as f:
        f.write("; Voice for app_speakingclock (docs/voice-format.md), made by\n"
                "; tools/synth_voices.py %s:%s.  The synthesiser belongs to its owners;\n"
                "; see NOTICE.md.\n\n[voice]\nname=%s\ndescription=%s\nmode=tim\n"
                "sequence=phrase+hour+minute+seconds+pips\nperiod=10\n"
                "; no pips.wav: the module makes the pips (1 kHz, the third one marks the time)\n"
                "\n[source]\nengine=%s\nvoice=%s\nsynthesiser=%s\n" % (engine, vid, name, desc, engine, vid, source))
    print(d)


def build_live(out_dir, engine, vid):
    table = ENGINES[engine][1]
    name, desc = ENGINES[engine][2](vid)
    if engine == "dectalk":
        how = 'command={program:dectalk} -fo {out} -a "[:%s] {text}"' % DECTALK[vid][0]
        by = "DECtalk's say, run here (tts_program_dectalk)"
    elif engine == "mssam":
        how = 'command={program:mssam} -v %s -o {out} "{text}"' % vid
        by = "synth/mssam, run here (tts_program_mssam)"
    elif engine in ("macintalk", "plaintalk"):
        how = 'command={program:macintalk} -v %s -o {out} "{text}"' % vid
        by = "synth/macintalk, run here (tts_program_macintalk)"
    else:
        speed, pitch, mouth, throat = SAM[vid][:4]
        how = ("command={program:sam} -wav {out} -speed %d -pitch %d -mouth %d -throat %d \"{text}\""
               % (speed, pitch, mouth, throat))
        by = "SAM, run here (tts_program_sam)"
    owners = "; The synthesiser belongs to its owners; see NOTICE.md."
    if engine == "mssam":
        owners = ("; The synthesiser is ours; the voice data it reads is Microsoft's and is\n"
                  "; not in this repository (tools/fetch-mssam.py downloads it); see NOTICE.md.")
    elif engine in ("macintalk", "plaintalk"):
        owners = ("; The synthesiser is ours; the voice data it reads is Apple's and is\n"
                  "; not in this repository (tools/fetch-macintalk.py downloads it); see NOTICE.md.")
    text = "At the third stroke, it will be {hour} {minute} {seconds}."
    d = os.path.join(out_dir, "live-%s-%s" % (engine, vid.lower().replace(" ", "-")))
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "voice.conf"), "w") as f:
        f.write("; Live voice for app_speakingclock (docs/voice-format.md), made by\n"
                "; tools/synth_voices.py --live %s:%s.  Each sentence is spoken when\n"
                "; it is needed, by %s.\n"
                "%s\n\n"
                "[voice]\nname=%s, live\ndescription=%s, spoken live\n"
                "mode=tts\nsequence=speech+pips\nperiod=10\n\n[tts]\n%s\ntext=%s\n"
                "\n[source]\nengine=%s\nvoice=%s\nsynthesiser=%s\n"
                % (engine, vid, by, owners, name, desc, how, text, engine, vid, ENGINES[engine][3]))
    assert vid in table
    print(d)


def main():
    if len(sys.argv) > 2 and sys.argv[1] == "--live":
        for spec in sys.argv[3:]:
            engine, _, vid = spec.partition(":")
            if (engine not in ("dectalk", "sam", "mssam", "macintalk", "plaintalk")
                    or vid not in ENGINES[engine][1]):
                sys.exit("--live takes dectalk, sam, mssam, macintalk and plaintalk voices "
                         "(see --list)")
            build_live(sys.argv[2], engine, vid)
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from voice_index import write_index
        write_index(sys.argv[2])
        return
    if len(sys.argv) > 1 and sys.argv[1] == "--list":
        for e, (_, table, info, _) in ENGINES.items():
            for v in table:
                print("%s:%s\t%s" % (e, v, info(v)[0]))
        return
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out = sys.argv[1]
    for spec in sys.argv[2:]:
        engine, _, vid = spec.partition(":")
        if engine not in ENGINES or vid not in ENGINES[engine][1]:
            sys.exit("unknown voice %s (see --list)" % spec)
        build(out, engine, vid)
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from voice_index import write_index
    write_index(out)


if __name__ == "__main__":
    main()
