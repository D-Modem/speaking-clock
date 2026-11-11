# mssam

Our own synthesiser for the Microsoft Sam, Mike and Mary voices (Microsoft
Speech API 4, 1998). It reads the voices' data and speaks with it. No
Microsoft code is used or run, and none of Microsoft's files are in this
repository: `tools/fetch-mssam.py` downloads Microsoft's package at build
time and keeps only the voice data.

```sh
make                 # build mssam
make voices          # fetch the voice data into ../../sources/mssam (8 MB download, needs cabextract or 7z)
make test            # speak a sentence with each voice
sudo make install    # /usr/local/bin/mssam and /usr/local/share/mssam
```

```
mssam [-d DIR] [-v sam|mike|mary|mike-tel|mary-tel] [-r RATE] [-p PITCH] [-o OUT.wav] TEXT...
```

`mike-tel` and `mary-tel` are Microsoft's 8 kHz voices made for phone
lines. `-d` is the folder with the voice data (default `/usr/local/share/mssam`,
or `$MSSAM_DIR`). `-r` and `-p` multiply the speed and pitch. Without `-o`
the WAV (22050 Hz, or 8000 Hz for the telephone voices, 16-bit mono) goes to standard output.

## How it speaks

1. The text becomes words (numbers and times like `11:05` are spelled out,
   `a.m.` is two letters), and the words become sounds from the CMU
   Pronouncing Dictionary: the clock's own words are built in
   (`lexicon.h`), and `cmudict.dict` in the voice folder covers the rest.
   Unknown words are spelled.
2. Each sound gets one of the voice's recorded units. The voice has a
   decision tree for each sound, whose questions are about the sounds
   either side and where the sound is in its word; the leaf it reaches is
   the unit.
3. Each unit is a run of frames: a 20th-order spectrum (line spectral
   frequencies) and a gain, with a pitch pulse for voiced frames and noise
   for the others. The frames are played through the filter at a new pace
   and with a new pitch. Each phrase starts near 100 Hz, the first stressed
   word rises above it, and the phrase sinks to about 73 Hz at the end of a sentence (before a
   comma it falls only a little, a sentence's last word is not
   stressed, and its stressed last vowel is held a little longer),
   the shape measured from Sam's own recordings (Mike and Mary are scaled
   to their own pitch, from their tone tables). The spectrum glides from
   frame to frame, the loudness of a unit's first frames leans toward the
   unit before it, each pitch pulse is blended with the one before below
   2 kHz (the stored pulses are rougher than Sam's speech), the gap between loud
   vowels and quiet consonants is narrowed a little, and a treble lift
   restores the brightness of the original recordings. Sam's settings
   (tone correction, hiss level and pace, how much the pitch pulses are
   smoothed and flattened, the formant postfilter, the levelling) were
   fitted automatically to 30 of his own recordings, scoring each sound's
   tone, its loudness over time and its length, and checked on 10 more.
   When a phrase pauses, the filter rings out instead of stopping dead.

docs/mssam-format.md describes the voice data.
