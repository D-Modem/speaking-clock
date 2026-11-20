# macintalk

This is a speech synthesiser for Linux. It speaks with the MacInTalk 3
voices of classic Mac OS:

- Fred, Ralph, Kathy, Junior, Princess, Zarvox, Trinoids and Whisper.
- The singing voices (Cellos, Pipe Organ, Good News, Bad News).
- The novelty voices (Bells, Boing, Bubbles, Albert, Bahh, Deranged,
  Hysterical).

It also speaks with the PlainTalk voices Agnes, Bruce and Victoria (and
their high quality versions). For these voices, it joins their recorded
diphones.

It is a clean-room program. It has no Apple code, and it does not run,
emulate or load Apple code. It reads only the data of the voices. When
you build it, `tools/fetch-macintalk.py` gets the data from Mac OS 8.1
and 8.6 disk images on the Internet Archive. The 8.6 image is a RAR
archive. Install `unar` or `unrar`, or the build skips the PlainTalk
voices. This repository does not contain files from Apple. Refer to
[docs/macintalk-format.md](../../docs/macintalk-format.md) and
[docs/plaintalk-format.md](../../docs/plaintalk-format.md) for the
contents of the voice data and for the parts that are ours.

```
make                     # build ./macintalk (C99, no dependencies but libm)
make voices              # download the voice data to ../../sources/macintalk
sudo make install        # /usr/local/bin/macintalk, /usr/local/share/macintalk

macintalk -l
macintalk -v zarvox -o zarvox.wav "That looks like a peaceful planet."
macintalk -v victoria -o victoria.wav "Hello, my name is Victoria."
macintalk -v fred "At the third stroke, it will be ten oh five." | aplay
```

These are the options:

| Option | Effect |
|---|---|
| `-v voice` | the voice (default fred) |
| `-d dir` | the voice folder. The default is `$MACINTALK_VOICES`, then `/usr/local/share/macintalk`, `/usr/share/macintalk`, `./sources/macintalk` |
| `-o file` | the output file. By default, the WAV goes to standard output |
| `-r wpm`, `-p hz` | override the rate and the pitch of the voice |
| `-x` | print the phonemes, not the WAV |

The WAV is 22050 Hz, 16-bit mono.

The text can contain Speech Manager commands:

- `[[inpt PHON]]` changes to the phoneme symbols of Apple
  (`1EHkspIHrIHmEHnt`).
- `[[inpt TEXT]]` changes back to text.
- `[[rate 200]]` sets the rate.
- `[[pbas 50]]` sets the base pitch, as a MIDI note.
- `[[slnc 500]]` gives silence, in ms.

## For the speaking clock

The `live-macintalk-*` and `live-plaintalk-*` voices in `voices/` run
this program for each sentence (docs/voice-format.md, mode=tts). Install
the program. If it is not on the PATH of Asterisk, give its location in
speakingclock.conf:

```
tts_program_macintalk=/usr/local/bin/macintalk
```

The voice data must be in one of the default folders. Alternatively,
set `MACINTALK_VOICES` in the environment of Asterisk.

## How it works

1. Text to phonemes: numbers and times ("10:05") become words. A short
   word list gives the words of the clock and common words. The NRL
   letter-to-sound rules (Elovitz et al. 1976, public domain) give all
   other words.
2. Timing: Klatt's rules give the length of each phoneme. A phoneme is
   shorter when it is unstressed, and longer at the end of a phrase. The
   length is scaled to the rate of the voice. Stops get a closure, a
   burst and, before a vowel, aspiration.
3. Pitch: each phrase has a line that decreases. The line goes up on each
   stressed syllable and goes down at the end (or goes up, for a
   question). The pitch and modulation of the voice scale the line. The
   singing voices put each vowel on the next note of their tune.
4. Sound: the synthesiser builds the voice source harmonic by harmonic,
   from the glottal spectra of the voice (or from its sampled sound). It
   mixes the source with breath noise. A cascade of five formant
   resonators and a nasal pole and zero shapes the source, with the
   formant shifts and bandwidths of the voice. Fricatives and bursts are
   noise through their own band.

The pronunciation and the rhythm are ours. Thus the voices sound similar
to the voices of MacInTalk 3, but not exactly the same.

The PlainTalk voices (plaintalk.c) use steps 1 to 3. Then each phoneme
is made from two parts: the second half of a recorded diphone into the
phoneme, and the first half of a recorded diphone out of it. The
synthesiser decodes these parts from the codebook of the voice. Then it
sets the length and pitch with TD-PSOLA (the recorded periods, windowed,
put at the new spacing). Their high quality versions are agnes-hq,
bruce-hq and victoria-hq.
