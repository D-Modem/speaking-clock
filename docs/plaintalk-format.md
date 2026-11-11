# PlainTalk voice files

What `synth/macintalk` knows about Apple's PlainTalk voices (MacinTalk Pro
1.5: Agnes, Bruce and Victoria). It was worked out from the voices' data
alone: by reading the tables, decoding the sound and checking the result
by ear and with a speech recogniser. No Apple code was run, emulated,
disassembled or read to find it.

## Where the files are

`tools/fetch-macintalk.py` downloads a Mac OS 8.6 hard disk image for the
SheepShaver emulator from the Internet Archive:
<https://archive.org/details/MacOS8.6Image-SheepShaverEmulator>, file
`Mac OS 8.6 on disk image + SheepShaver.exe` (SHA-256
`003910efa3fc95a9c0da3f064e0e9aafafd9c2b20b11fde2e01d2f8fd9472c92`). It is
a self-extracting RAR archive (RAR 2.9 compression), which `unar` or
`unrar` unpacks; the script takes only `SheepShaver/Mac_OS_HD.dsk` out of
it, a 500 MB HFS volume. The voices are in `System Folder:Extensions:
Voices`, files of type `ttvf` and creator `gala`, with an empty data fork
and these resources:

| Resource | What it is |
|---|---|
| `gtss` 3 "Engl…Data" | the recorded sound and its tables (below) |
| `gtss` 4 "Engl…Code" | 68k code: not extracted, not used |
| `ttvd` | the Speech Manager's `VoiceDescription` (name, gender, age) |
| `gtsv`, `gtsg`, `gtsm`, `gtsi` | small tables that tie the parts together |
| `vers`, `STR `, `ftag` | version 1.4.2, "English Text-To-Speech 1.5.3", © 1992-98 |

The script writes each voice's `gtss` 3 to `<voice>.gtss` (`agnes.gtss`,
`bruce.gtss`, `victoria.gtss`). The three "high quality" voices code their
sound differently (see the end) and are left out for now.

The MacinTalk Pro extension itself holds the English text rules and
dictionary (`gtst`), the phoneme and allophone lists (`gtsp`, `gtsa`) and
the code (`gtse`). Only the allophone names in `gtsa` 2 were read, to name
the sounds below.

## `gtss` 3

All big-endian. A 16-byte header, then twelve 32-bit offsets of sections
0 to 11 (from the start of the resource).

| Header word | Agnes, Bruce, Victoria | What it is |
|---|---|---|
| 0 | 50 | the number of wave-phonemes (the tables are 50 × 50) |
| 1, 2 | 100, 185, 125 | the speaker's typical pitch period, samples at 22050 Hz |
| 4 | 5739, 3922, 7804 | unknown |
| 7 | 0x0702 (high quality: 0x0403) | the coding |

| Section | Size (Agnes) | What it is |
|---|---|---|
| 0 | 20000 | first halves of the diphones: 2500 entries of 8 bytes, entry 50 × left + right |
| 1 | 20000 | second halves, the same way |
| 2 | 256668 | the voiced first halves (left sound 0 to 30) |
| 3 | 280676 | the voiced second halves (right sound 0 to 30) |
| 4 | 120688 | the consonant first halves (left sound 31 to 49) |
| 5 | 114716 | the consonant second halves |
| 6 | 19764 | period lengths of the units in section 2: 16-bit, each unit's list ends with 0xffff |
| 7 | 21668 | period lengths of the units in section 3 |
| 8 | 52 | a class for each wave-phoneme: 0, 1 (vowels and sonorants) or 2 (f, th, s, sh) |
| 9 | 172 | for each of the 86 allophones, its wave-phoneme and a second one (for ch, j and syllabic consonants) |
| 10 | 2048 | the codebook: 256 vectors of 4 signed 16-bit samples |
| 11 | 352 | unknown (the same in every voice) |

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
plus m or n, ch is t plus sh and j is d plus zh.

### The diphone tables

Entry *left* × 50 + *right* of section 0 says where the first half of the
diphone *left*–*right* is (from the middle of *left* to the boundary), and
the same entry of section 1 where its second half is (from the boundary to
the middle of *right*). An entry is two 32-bit words:

- `0xffffffff, …`: no such diphone.
- `0x0000ffff, (l << 16) | r`: use diphone *l*–*r* instead.
- otherwise the unit's offset in its section divided by 4, then a second
  word: for a voiced unit a running count of the frames before it in its
  section, for a consonant unit the number of whole frames (high half)
  and a value up to about 120 (low half, unknown).

A half whose sound (the left one in section 0, the right one in section 1)
is a consonant, wave-phoneme 31 or more, is in section 4 or 5; the others
are in 2 or 3. Silence has no halves of its own.

### A unit

Each unit starts on a 4-byte boundary with one byte (unknown, mostly 100 to
170), then frames. A frame codes one pitch period, resampled to *N*
samples, where *N* is 96 for Agnes and Victoria and 160 for Bruce (it is not
in the header; it is the frame size that makes all the units parse):

| Bytes | |
|---|---|
| 1 | the gain *A*: a 4-bit exponent and 4-bit mantissa, gain = (16 + (*A* & 15)) × 2^(*A* >> 4) / 256. Its top bit is clear. |
| 1 | the lag *B* |
| *N* / 4 | codebook indices |

The period is the unit's signal one period plus *B* − 1 samples back
(predicted from what came before in the same unit, period by period) plus
the gain times the codebook vectors, one vector for each 4 samples. The
first frame of a unit has nothing before it, low nibble 0 in *A* and *B* = 1.
Lags of 50 and more turn up in noisy and changing frames; what they mean
exactly is not certain, and taking them as plain lags works.

The last frame is shorter: its first byte has the top bit set and the low
7 bits are its number of indices, then the lag and the indices, then one
more byte (it looks like a gain), then padding to the next 4-byte
boundary. It is probably the part of a period up to the diphone's
boundary; `synth/macintalk` leaves it out.

A voiced unit's whole frames are its periods, and section 6 or 7 gives
each one's length as recorded. Consonant units have no such list; their
frames are taken as one typical period (header word 1) long.

## How `synth/macintalk` uses it

The text front end, timing and pitch are the same as for the MacInTalk 3
voices. Each phoneme becomes a wave-phoneme (with the r-coloured vowels,
and p, t, k after s), and is made of the second half of the diphone from
the phoneme before it and the first half of the diphone into the phoneme
after it. Frames are repeated or dropped to give the phoneme its length,
and each voiced period is resampled from *N* samples to the period the
pitch wants (the coding makes this the natural way to change the pitch;
it also moves the formants a little). The rest of the voice, Apple's
rules for the timing and the pitch, is not used.

## Not done

- The "high quality" voices code 2 samples per codebook vector (a 1024-byte
  codebook) and their last frames are not marked the same way.
- The unit's first byte, the end byte, header word 4 and section 11.
