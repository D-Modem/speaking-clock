# PlainTalk voice files

This page gives the data about the PlainTalk voices of Apple that
`synth/macintalk` uses (MacinTalk Pro 1.5: Agnes, Bruce and Victoria,
each in a standard and a high quality version). We found this data from
the data of the voices only. We read the tables, decoded the sound, and
checked the result by ear and with a speech recogniser. We did not run,
emulate, disassemble or read Apple code to find it.

## Where the files are

`tools/fetch-macintalk.py` downloads a Mac OS 8.6 hard disk image for the
SheepShaver emulator from the Internet Archive:
<https://archive.org/details/MacOS8.6Image-SheepShaverEmulator>, file
`Mac OS 8.6 on disk image + SheepShaver.exe` (SHA-256
`003910efa3fc95a9c0da3f064e0e9aafafd9c2b20b11fde2e01d2f8fd9472c92`). It is
a self-extracting RAR archive (RAR 2.9 compression). `unar` or `unrar`
can unpack it. The script extracts only `SheepShaver/Mac_OS_HD.dsk`, a
500 MB HFS volume. The voices are in `System Folder:Extensions:
Voices`. They are files of type `ttvf` and creator `gala`, with an empty
data fork and these resources:

| Resource | What it is |
|---|---|
| `gtss` 3 "Engl…Data" | the recorded sound and its tables (below) |
| `gtss` 4 "Engl…Code" | 68k code. The script does not extract it, and we do not use it |
| `ttvd` | the `VoiceDescription` of the Speech Manager (name, gender, age) |
| `gtsv`, `gtsg`, `gtsm`, `gtsi` | small tables that connect the parts |
| `vers`, `STR `, `ftag` | version 1.4.2, "English Text-To-Speech 1.5.3", © 1992-98 |

The script writes the `gtss` 3 of each voice to `<voice>.gtss`
(`agnes.gtss`, `bruce.gtss`, `victoria.gtss`). For the files with the
name "Agnes, high quality" and similar names, it writes `agnes-hq.gtss`
and similar names.

The MacinTalk Pro extension itself holds the English text rules and
dictionary (`gtst`), the phoneme and allophone lists (`gtsp`, `gtsa`) and
the code (`gtse`). We read only the allophone names in `gtsa` 2, to give
names to the sounds below.

## `gtss` 3

All values are big-endian. The resource has a 16-byte header, then
twelve 32-bit offsets of sections 0 to 11 (from the start of the
resource).

| Header word | Agnes, Bruce, Victoria | What it is |
|---|---|---|
| 0 | 50 | the number of wave-phonemes (the tables are 50 × 50) |
| 1, 2 | 100, 185, 125 (Victoria high quality: 70) | approximately the pitch period of the speaker, in samples at 22050 Hz. The median of the recorded periods (101, 191, 108) is better |
| 4 | 5739, 3922, 7804 | unknown |
| 7 | 0x0702, high quality 0x0403 | the coding |

| Section | Size (Agnes) | What it is |
|---|---|---|
| 0 | 20000 | first halves of the diphones: 2500 entries of 8 bytes, entry 50 × left + right |
| 1 | 20000 | second halves, in the same layout |
| 2 | 256668 | the voiced first halves (left sound 0 to 30) |
| 3 | 280676 | the voiced second halves (right sound 0 to 30) |
| 4 | 120688 | the consonant first halves (left sound 31 to 49) |
| 5 | 114716 | the consonant second halves |
| 6 | 19764 | period lengths of the units in section 2: 16-bit. The list of each unit ends with 0xffff |
| 7 | 21668 | period lengths of the units in section 3 |
| 8 | 52 | a class for each wave-phoneme: 0, 1 (vowels and sonorants) or 2 (f, th, s, sh) |
| 9 | 172 | for each of the 86 allophones, its wave-phoneme and a second one (for ch, j and syllabic consonants) |
| 10 | 2048 | the codebook: 256 vectors of 4 signed 16-bit samples (high quality: 1024 bytes, 256 vectors of 2) |
| 11 | 352 | unknown (the same in all voices) |

### The wave-phonemes

Section 9 maps the allophone names of `gtsa` 2 to these:

| | |
|---|---|
| 0, 1 | silence, breath |
| 2 to 22 | IY IH IX EH AE UX AX AA AO UH UW OW ER IR AR OR UR AY EY OY AW |
| 23 to 30 | syllabic l, l, m, n, N (ng), y, r, w |
| 31 to 37 | b d g v D z Z |
| 38 to 42 | f T s S h |
| 43 to 49 | p, p after s, t, t after s, glottal stop, k, k after s |

IR, AR, OR and UR are the vowels before r. Syllabic m and n are UX or UH
plus m or n. ch is t plus sh, and j is d plus zh.

### The diphone tables

Entry *left* × 50 + *right* of section 0 gives the location of the
first half of the diphone *left*–*right* (from the middle of *left* to
the boundary). The same entry of section 1 gives the location of its
second half (from the boundary to the middle of *right*). An entry is
two 32-bit words:

- `0xffffffff, …`: there is no such diphone.
- `0x0000ffff, (l << 16) | r`: use diphone *l*–*r* in its place.
- All other values: the offset of the unit in its section, divided by 4
  (by 8 in the high quality voices). This value can be more than 0xffff.
  Then a second word follows. For a voiced unit, it is a cumulative count
  of the frames before the unit in its section. For a consonant unit, it
  is the number of whole frames (high half) and a value up to about 120
  (low half, unknown).

If the sound of a half is a consonant (wave-phoneme 31 or more), the half
is in section 4 or 5. This is the left sound in section 0, and the right
sound in section 1. The other halves are in section 2 or 3. Silence has
no halves of its own.

### A unit

Each unit starts on a 4-byte boundary (8 in high quality). It starts with
one byte (unknown, usually 100 to 170), then frames follow. A unit is one
continuous sound at 22050 Hz, divided into frames of *N* samples. *N* is
96 for Agnes and Victoria and 160 for Bruce. *N* is not in the header. It
is the frame size with which all the units parse correctly. The frames
are not pitch periods. They only follow each other.

| Bytes | |
|---|---|
| 1 | the gain *A*: a 4-bit exponent and 4-bit mantissa, gain = (16 + (*A* & 15)) × 2^(*A* >> 4) / 256. Its top bit is clear. |
| 1 | the lag *B* |
| *N* / 4 (high quality *N* / 2) | codebook indices |

Each sample is the sum of two values:

- The sample one pitch period before it, *N* + *B* − 1 samples earlier in
  the same unit.
- The gain, multiplied by its sample of the codebook vector (one vector
  for each 4 samples, or 2).

The first frame has nothing before it (low nibble 0 in *A*, and *B* = 1).
In voiced sound, *N* + *B* − 1 is the pitch period. It agrees with the
recorded period lengths of sections 6 and 7 to one or two samples. Lags
of 50 and more occur in noisy frames and in frames that change. If you
use them as plain lags, the result is correct.

The last frame is shorter. The top bit of its first byte is set, and its
low 7 bits are its number of indices. Then the lag, the indices, the gain
byte of the frame, and padding to the next unit follow.

The number of samples of a voiced unit is the sum of its period lengths
in section 6 or 7 (plus or minus 3). Thus those lengths are its pitch
marks. Consonant units have no list. The lags give the periods of the
voiced parts.

## How `synth/macintalk` uses it

The text front end, the timing and the pitch are the same as for the
MacInTalk 3 voices. Each phoneme becomes a wave-phoneme. This includes
the r-coloured vowels, and p, t, k after s. ch and j become two
wave-phonemes each: t and sh, and d and zh. Each phoneme is made of two
parts:

- The second half of the diphone from the phoneme before it.
- The first half of the diphone into the phoneme after it.

TD-PSOLA changes that sound to the necessary length and pitch. Each
recorded period, in a window that is two periods wide, goes at the
spacing that the pitch sets. To get the length, the synthesiser repeats
or removes periods.

The synthesiser does not stretch b, d and g. In some voices, their
release is only a quick start of voicing, and a stretch changes it into
a glide. Thus the synthesiser plays them as recorded, and adds silence
to the closure or removes silence from it.

A low-pass filter at 6 kHz removes the hiss of the coding (2 bits for
each sample, 4 in high quality). For these voices, the ends of phrases
become slower and decrease less than for the MacInTalk 3 voices. We do
not use the other parts of the voice (the rules of Apple for the timing
and the pitch).

## Not done

- The first byte of the unit, header word 4 and section 11.
