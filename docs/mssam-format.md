# Microsoft Sam, Mike and Mary voice data

This page describes the data that synth/mssam reads. We decoded the
format from the data only. The voices come in the Speech API 4 TTS
package of Microsoft, `msttsl.exe` (SHA-1
d988cbb3e99bc9dd850defda939c06b81d9d4544). This package is a cabinet
that contains these files:

- `sam.vce`, `male.vce` (Mike) and `female.vce` (Mary).
- `male8.vce` and `female8.vce`, Mike and Mary for telephone
  (`mike-tel`, `mary-tel`).

Each `.vce` is an OLE compound file with these streams: `Inventory`,
`TreeImage`, `PhoneFile`, `Senone`, `Cdb`, `VcHeader` (and `AltUnits` in
the file of Sam). `tools/fetch-mssam.py` writes `Inventory` as
`<voice>.inv` and `Cdb` as `<voice>.tones`. It converts `TreeImage` and
writes it as `<voice>.tree`.

## Inventory (`.inv`, little-endian)

    u32 rate (22050), order (20), nlsf (6), na (11), nb (13), 2 more
    nlsf tables, then na tables, then nb tables; each:
        u32 count, u32 dim, count*dim float32
    u32 nunits, u32 size, (nunits+1) u32 offsets, then the units

The 6 line-spectrum codebooks have 256 entries of 3, 3, 3, 3, 4 and 4
values. One entry from each codebook gives the 20 line spectral
frequencies of a frame (in cycles per sample). The excitation codebooks
have 128 entries. The 11 dims of family A add up to 510. The 13 dims of
family B also add up to 510. 510 is the number of points in one pitch
period, scaled to a fixed length.

The telephone voices are the same, at 8000 Hz with order 12. They have 4
line-spectrum codebooks of 3 values, and 10 excitation codebooks in each
family. The dims are 12, 12, 16, 16, 16, 18, 24, 38, 46 and 56, and they
add up to 254. Their unvoiced frame bytes (147-238) are not correct as
lengths at 8 kHz. Thus mssam plays those frames as 5 ms. This value
agrees best with the recorded lengths of the units (Senone).

This is the layout of a unit:

    u32 n                          frames
    n frame headers                voiced: 0x7f bytes (127 each) then a
                                   byte < 0x7f, adding up to the pitch
                                   period in samples; unvoiced: 0x80
                                   bytes then a byte > 0x80, the length
                                   is 128*(number of 0x80 - 1) + that byte
    n x 6 bytes (4 for telephone)  line-spectrum codebook indices
    n float32                      gains (the RMS of the frame's excitation)
    for each voiced frame          the first frame of a voiced run: 11
                                   family A indices, the whole period;
                                   each later one: 13 family B indices,
                                   the change from the previous period

Unvoiced frames are noise. Unit 0 is empty. Unit k+1 is senone k.

## TreeImage (big-endian)

The stream has these parts, in this sequence:

1. A header with the path of the question file.
2. The questions: a u32 count, then a name\0 and a 64-bit mask for each
   question.
3. `\\...\TREE2\0` and `tree\0`.
4. A u32 number of sounds n, then n bytes (1: one state for each sound).
5. A u32 n, then n u32 values (the end of the senones of each sound,
   counted from n).
6. A u32 number of senones.
7. A u32 total, 0, 18, 0xffffffff, and the number of composite
   questions q.
8. q pairs of 64-bit masks, then 8 more bytes.
9. One tree for each sound.

A mask holds sound i at bit (i + 36) mod 64. It holds the four word
positions at bits 32-35: starts a word, ends a word, is a full word, is
inside a word. A composite question (left, right) is true when these
three conditions are true:

- The sound before is in `left`.
- The word position of this sound is in `left`.
- The sound after is in `right`.

A tree is u16 nodes and u16 leaves (nodes = 2*leaves - 1). Then the
nodes follow in sequence, node 0 first:

    inner node   u16 yes, u16 no, 0xffff, u16 question..., 0xffff
    leaf         0xffff, u16 k, u16 k

An inner node goes to `yes` when one or more of its questions is true.
Leaf k of sound s is senone (the senones of the sounds before s) + k.

PhoneFile lists the sounds in sequence, one on each line (Sam: 52, Mike
and Mary: 58). SILb and SILe are for the silence before and after.

`<voice>.tree` has the same data in our own little-endian layout:

    "MSSAMTR1", u32 sounds, u32 questions, u32 senones, u32 1
    questions x (u64 left, u64 right)  sound i at bit i, positions at 60-63
    sounds x (char name[8], u32 first senone, u32 nodes, u32 first node)
    u32 nodes, u32 question refs
    nodes x (i32 yes, i32 no, u32 first ref, u32 refs)   leaf: yes = -1, no = k
    refs x u16 question index

## Senone

A u32 count, then 28-byte records. Each record has the name of the
sound (4 bytes), u32 0, u32 the leaf number + 1, and four float32
values. The four values are the mean length of the unit in ms, its
spread, and two energies.

## Cdb (`.tones`)

Text: the pitch for each tone, as
`H (179) on: m=129= sd=34=; off: m=141= ...`. The values are the mean
pitch in Hz at the start and at the end of a vowel. mssam uses `H`
(accented), `_` (plain), `h]` and `_]` (the last vowel of a phrase).
