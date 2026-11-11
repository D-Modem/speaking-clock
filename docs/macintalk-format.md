# MacInTalk 3 voice files

What `synth/macintalk` knows about Apple's MacInTalk 3 voices. It was
worked out by comparing the data of the 19 voices that come with Mac OS 8.1
(MacInTalk 3 version 1.4, 1992-1995) with each other and with how the
voices are known to sound. No Apple code was run, emulated, disassembled or
read to find it.

## Where the files are

`tools/fetch-macintalk.py` downloads a Mac OS 8.1 hard disk image (for the
Basilisk II emulator) from the Internet Archive:
<https://archive.org/details/macos-8.1-basilisk-ii>, file
`macos-8.1-basilisk-ii-2026.10.1.zip` (SHA-256
`ce6ee4ba5294182535d20bf6639f64415376e4420b3473167803315e615c97fa`). The
zip holds one 1 GB HFS volume. The voices are in `System Folder:Extensions:
Voices`. Each is a file of type `ttvf`, creator `mtk3`, with an empty data
fork and these resources:

| Resource | What it is |
|---|---|
| `ttvd` | the voice description: the Speech Manager's `VoiceDescription`, then MacInTalk 3's parameter block |
| `ttvw` | a sampled sound, in 9 of the 19 voices |
| `vers` 1, 2 | "1.4, © Apple Computer, Inc 1992-1995", "Mac OS 8.0" |

The script writes each voice's `ttvd` and `ttvw` to `<voice>.ttvd` and
`<voice>.ttvw` (the voice's name in lower case, spaces as `-`), which is
what `synth/macintalk` reads. It doesn't extract the MacInTalk 3 extension
(the synthesiser itself, 68k and PowerPC code); nothing here needs it.

Other files on the disk that are not used: `Speech Manager` (the system
extension), `MacinTalk 3` (its resources are code, `ttvi` 8 to 10 and the
PowerPC fragment in `ttvi` 11, and data: going by the text in them,
`ttvi` 3 to 7 hold the phoneme names, suffix lists, letter-to-sound rules
and probably the pronunciation dictionary; `ttss` 0 is the Speech Manager
phoneme table).

## `ttvd`

All big-endian. The first 362 bytes are the Speech Manager's
`VoiceDescription` (Inside Macintosh: Sound):

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | length (362) |
| 4 | 4 | creator, `mtk3` |
| 8 | 4 | voice id (Fred 1, Kathy 2, Princess 3, Junior 4, Ralph 5, Whisper 6, Zarvox 8, Trinoids 9 ...) |
| 12 | 4 | version (0x104) |
| 16 | 64 | name (Pascal string) |
| 80 | 256 | comment: the sample sentence the Speech control panel speaks |
| 336 | 2 × 5 | gender, age, script, language, region (all 0) |
| 346 | 16 | reserved |

Then the parameter block, signed 16-bit words, numbered from 0 at offset
362. Most voices have 176 words. The singing voices add a tune at the end.
"Used" says whether `synth/macintalk` uses the word, and how.

| Word | Values (Fred, Ralph, Kathy, Zarvox, Whisper) | What it is | Used |
|---|---|---|---|
| 0 | 97, 65, 200, 90, 110 | base pitch, Hz | yes |
| 1 | 100, 100, 80, 0, 100 | pitch modulation, % (0 for the monotone and singing voices) | yes |
| 2 | 60, 50, 40, 70, 70 | 0 for the singing voices; 100 for Albert and Bahh. Perhaps pitch wobble | no |
| 3 | 160, 150, 150, 160, 140 | speaking rate, words per minute | yes |
| 4 | 0, 0, 1, 0, 0 | a flag: set for Kathy, Princess, Boing, Trinoids | no |
| 5 | 100, 125, 55, 40, 0 | unknown (0 for the sampled voices) | no |
| 6 | 0, 60, 30, 0, 400 | breathiness (aspiration mixed with the voice) | yes |
| 7 | 192, 192, 200, 192, 16 | voicing amplitude (Whisper 16, Bubbles 32) | yes |
| 8, 9 | 3000/200, 2800/350, 3000/300, 3500/50, 3500/50 | fourth formant: frequency, bandwidth (Hz) | yes |
| 10, 11 | 3600/150, 3500/150, 3600/150, 4500/150, 4500/150 | fifth formant: frequency, bandwidth | yes |
| 12 to 15 | 3750, 100, 4500, 150 in every voice | probably two fixed higher resonances | no |
| 16, 17 | 330, 400 in every voice | nasal pole and zero, Hz | yes |
| 18 | 60 (Kathy 250, Princess 260) | unknown | no |
| 19 | 100 (0 for Kathy, Junior, Princess, Good News) | unknown | no |
| 20 to 22 | 150/100/100, 100/100/50, 200/100/100, 100/50/50, 100/100/50 | bandwidths of formants 1 to 3, % | yes |
| 23 to 25 | 0/0/0, −80/−40/−70, −40/−10/−10, 0/0/0, 0/0/0 | shifts of formants 1 to 3, Hz (Junior +50/+70/+70) | yes |
| 26 | 0 (Zarvox 3, Trinoids −32) | a second voice, at this % of the pitch above or below | yes (as a guess) |
| 27 | 100, 100, 140, 250, 200 | output gain, % | yes |
| 28, 29 | 0, 0 (sampled voices: 47 to 54, 70 to 250) | settings for the sampled sound | no |
| 30 | 2 for Fred and Whisper, else 0 | unknown | no |
| 31 to 45 | the same in every voice (260, 29, −29, 29, −29, 1, 0, 51, 7200 ...) | engine constants | no |
| 46 | 0, 1 or 2 | how the sampled sound is used: 0 none, 1 looped as the voice source, 2 one segment per syllable (Bells, Hysterical) | yes |
| 47 | 0 | | |
| 48 to 94 | 47 values | glottal source spectrum A: the amplitude of harmonics 1 to 47 | yes |
| 95 | 0 | | |
| 96 to 142 | 47 values | glottal source spectrum B | yes |
| 143 | 1, or the `ttvw` resource id | which sampled sound the voice uses | no (there is one) |
| 144 to 173 | mostly constant | unknown | no |
| 174 | 1, or the number of notes | the tune's length | yes |
| 175 ... | one word per note | high byte: length (in about eighths), low byte: semitones from the base pitch, signed | yes |

The two source spectra are the clearest part of the format. Fred's falls
smoothly from 14636 to about 100 by the 36th harmonic. Kathy's falls much
faster and stops at the 16th (a softer voice). Zarvox has only the odd
harmonics, which is what gives it its hollow, buzzing sound. `synth/
macintalk` builds the voice source from them, harmonic by harmonic, using
spectrum B on stressed vowels and A elsewhere. Which of the two Apple used
for what isn't known.

The tunes are recognisable from their intervals. Cellos is In the Hall of
the Mountain King (0, 2, 3, 5, 7, 3, 7, 6, 2, 6, 5, 1, 5 semitones). Bad
News is Chopin's funeral march (four notes on one pitch, then +3, +2, +2,
0, 0, −1, 0). Good News is Pomp and Circumstance. Pipe Organ has a tune
too.

## `ttvw`

A sampled sound, 8-bit unsigned (128 is silence), apparently at 11127 Hz.
Either a 4-byte length and then the samples, or a 4-byte length, a 4-byte
count *n*, *n* 4-byte offsets that cut the samples into *n* − 1 segments,
and the samples.

| Voice | Name | Length | Segments |
|---|---|---|---|
| Albert | Crow | 4407 | 1 |
| Bahh | Lamb | 4900 | 1 |
| Bells | Clock | 33820 | 4 |
| Boing | Boing | 6528 | 1 |
| Bubbles | Bubbles | 7222 | 1 |
| Cellos | BowedStr | 11854 | 1 |
| Deranged | Insane | 17244 | 1 |
| Hysterical | Male Laugh | 40194 | 5 |
| Pipe Organ | PipeOrgan | 9908 | 1 |

## The voices

| Voice | Sample sentence |
|---|---|
| Albert | I have a frog in my throat. No, I mean a real frog! |
| Bad News | The light you see at the end of the tunnel is the headlamp of a fast approaching train. |
| Bahh | Do not pull the wool over my eyes. |
| Bells | Time flies when you are having fun. |
| Boing | Spring has sprung, fall has fell, winter's here and it's colder than usual. |
| Bubbles | Pull the plug! I'm drowning! |
| Cellos | Doo da doo da dum dee dee doodly doo dum dum dum doo da doo da doo da doo da doo da doo da doo |
| Deranged | I need to go on a really long vacation. |
| Fred | I sure like being inside this fancy computer |
| Good News | Congratulations you just won the sweepstakes and you don't have to pay income tax again. |
| Hysterical | Please stop tickling me! |
| Junior | My favorite food is pizza. |
| Kathy | Isn't it nice to have a computer that will talk to you? |
| Pipe Organ | We must rejoice in this morbid voice. |
| Princess | When I grow up I'm going to be a scientist. |
| Ralph | The sum of the squares of the legs of a right triangle is equal to the square of the hypotenuse. |
| Trinoids | We cannot communicate with these carbon units. |
| Whisper | Pssssst, hey you, Yeah you, Who do ya think I'm talking to, the mouse? |
| Zarvox | That looks like a peaceful planet. |

## What isn't Apple's

The voice data sets how a voice sounds. How text becomes speech is ours:
the pronunciation comes from the NRL letter-to-sound rules (a 1976 US
Navy report, public domain) and a small word list, the phoneme targets and
durations are textbook values (Klatt 1979 and 1980), and the synthesiser is
a cascade formant synthesiser written for this repository. So the voices
are in the style of MacInTalk 3 rather than copies of it: the pitch, speed,
timbre, tunes and samples are Apple's, the pronunciation and rhythm are
not. Apple's own rules and dictionary (`ttvi` 3 to 7) could be decoded the
same way later.

The PlainTalk voices (MacInTalk Pro: Victoria, Agnes, Bruce) are recorded
diphones, a different kind of synthesiser, and are not on this disk.
