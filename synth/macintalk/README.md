# macintalk

A speech synthesiser for Linux that speaks with the MacInTalk 3 voices of
classic Mac OS: Fred, Ralph, Kathy, Junior, Princess, Zarvox, Trinoids,
Whisper, the singing voices (Cellos, Pipe Organ, Good News, Bad News) and
the novelty ones (Bells, Boing, Bubbles, Albert, Bahh, Deranged,
Hysterical). It also speaks with the PlainTalk voices Agnes, Bruce and
Victoria, by joining their recorded diphones.

It is a clean-room program. It has no Apple code and doesn't run, emulate or
load any. It reads only the voices' data, which `tools/fetch-macintalk.py`
takes from Mac OS 8.1 and 8.6 disk images on the Internet Archive when you
build it (the 8.6 one is a RAR archive: install `unar` or `unrar`, or the
PlainTalk voices are skipped). Apple's files are not in this repository.
See [docs/macintalk-format.md](../../docs/macintalk-format.md) and
[docs/plaintalk-format.md](../../docs/plaintalk-format.md) for what the
voice data holds and what is ours.

```
make                     # build ./macintalk (C99, no dependencies but libm)
make voices              # download the voice data to ../../sources/macintalk
sudo make install        # /usr/local/bin/macintalk, /usr/local/share/macintalk

macintalk -l
macintalk -v zarvox -o zarvox.wav "That looks like a peaceful planet."
macintalk -v victoria -o victoria.wav "Hello, my name is Victoria."
macintalk -v fred "At the third stroke, it will be ten oh five." | aplay
```

Options: `-v voice` (default fred), `-d dir` (the voice folder; default
`$MACINTALK_VOICES`, then `/usr/local/share/macintalk`,
`/usr/share/macintalk`, `./sources/macintalk`), `-o file` (default: the
WAV goes to standard output), `-r wpm` and `-p hz` (override the voice's
rate and pitch), `-x` (print the phonemes instead). The WAV is 22050 Hz,
16-bit mono.

The text can carry Speech Manager commands: `[[inpt PHON]]` switches to
Apple's phoneme symbols (`1EHkspIHrIHmEHnt`), `[[inpt TEXT]]` back,
`[[rate 200]]`, `[[pbas 50]]` (base pitch, as a MIDI note), `[[slnc 500]]`
(silence, ms).

## For the speaking clock

The `live-macintalk-*` and `live-plaintalk-*` voices in `voices/` run this
program for each sentence (docs/voice-format.md, mode=tts). Install it,
then, if it isn't on Asterisk's PATH, tell speakingclock.conf where it is:

```
tts_program_macintalk=/usr/local/bin/macintalk
```

The voice data must be in one of the default folders (or set
`MACINTALK_VOICES` in Asterisk's environment).

## How it works

1. Text to phonemes: numbers and times ("10:05") to words, a short word
   list for the clock's words and common ones, and the NRL letter-to-sound
   rules (Elovitz et al. 1976, public domain) for everything else.
2. Timing: each phoneme's length from Klatt's rules (shorter when
   unstressed, longer at the end of a phrase), scaled to the voice's rate.
   Stops get a closure, a burst and, before a vowel, aspiration.
3. Pitch: a falling line per phrase with a rise on each stressed syllable
   and a fall (or a question's rise) at the end, scaled by the voice's pitch
   and modulation. The singing voices put each vowel on the next note of
   their tune.
4. Sound: the voice source is built harmonic by harmonic from the voice's
   own glottal spectra (or from its sampled sound), mixed with breath noise,
   and shaped by a cascade of five formant resonators and a nasal pole and
   zero, with the voice's formant shifts and bandwidths; fricatives and
   bursts are noise through their own band.

Pronunciation and rhythm are ours, so the voices sound like MacInTalk 3's
voices rather than exactly like them.

The PlainTalk voices (plaintalk.c) share steps 1 to 3. Then each phoneme is
the second half of a recorded diphone into it and the first half of one out
of it, decoded from the voice's codebook, stretched to its length by
repeating or dropping periods, and each voiced period is resampled to the
pitch. Their high quality versions aren't supported yet.
