# mssam

This is our own synthesiser for the Microsoft Sam, Mike and Mary voices
(Microsoft Speech API 4, 1998). It reads the data of the voices and
speaks with it. It does not use or run Microsoft code. This repository
has no files from Microsoft. At build time, `tools/fetch-mssam.py`
downloads the package of Microsoft and keeps only the voice data.

```sh
make                 # build mssam
make voices          # fetch the voice data into ../../sources/mssam (8 MB download, needs cabextract or 7z)
make test            # speak a sentence with each voice
sudo make install    # /usr/local/bin/mssam and /usr/local/share/mssam
```

```
mssam [-d DIR] [-v sam|mike|mary|mike-tel|mary-tel] [-r RATE] [-p PITCH] [-o OUT.wav] TEXT...
```

`mike-tel` and `mary-tel` are 8 kHz voices of Microsoft for telephone
lines. `-d` is the folder with the voice data (default
`/usr/local/share/mssam`, or `$MSSAM_DIR`). `-r` and `-p` multiply the
speed and the pitch. Without `-o`, the WAV goes to standard output. The
WAV is 22050 Hz (8000 Hz for the telephone voices), 16-bit mono.

## How it speaks

1. The text becomes words. Numbers and times (for example, `11:05`)
   become words, and `a.m.` becomes two letters. Then the words become
   sounds from the CMU Pronouncing Dictionary. The words of the clock are
   part of the program (`lexicon.h`). `cmudict.dict` in the voice folder
   has the other words. The synthesiser spells unknown words.
2. Each sound gets one of the recorded units of the voice. The voice has
   a decision tree for each sound. The questions of the tree are about
   the sounds on each side, and about the position of the sound in its
   word. The leaf at the end of the path is the unit.
3. Each unit is a sequence of frames. Each frame has a 20th-order
   spectrum (line spectral frequencies) and a gain. A voiced frame has a
   pitch pulse, and the other frames have noise. The synthesiser plays
   the frames through the filter at a new speed and with a new pitch.
4. The pitch has this shape:
   - Each phrase starts near 100 Hz.
   - The first stressed word goes higher than 100 Hz.
   - At the end of a sentence, the phrase decreases to about 73 Hz.
     Before a comma, it decreases only a little.
   - The last word of a sentence is not stressed. Its stressed last vowel
     is a little longer.

   We measured this shape from the recordings of Sam. The pitch of Mike
   and Mary is scaled to their own pitch, from their tone tables.
5. The synthesiser also does these steps:
   - The spectrum changes smoothly from frame to frame.
   - The loudness of the first frames of a unit moves toward the unit
     before it.
   - Below 2 kHz, the synthesiser blends each pitch pulse with the pulse
     before it. The stored pulses are rougher than the speech of Sam.
   - The difference between loud vowels and quiet consonants becomes a
     little smaller.
   - A treble lift gives back the brightness of the original recordings.
6. Sam also gets these corrections:
   - A tone correction, because his vowels have less of the 2.5-4 kHz
     range than his own voice.
   - Louder and shorter hiss sounds (s, f, th).
   - The stored loudness of each frame becomes the loudness of the sound
     itself. Thus an "m" is not louder than its vowel.
   - A formant postfilter (as speech codecs use) that removes the haze
     between his formants.
7. When a phrase pauses, the filter rings until it decays. It does not
   stop suddenly.

docs/mssam-format.md describes the voice data.
