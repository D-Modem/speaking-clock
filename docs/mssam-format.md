# Microsoft Sam, Mike and Mary voice data

What synth/mssam reads, as we decoded it from the data alone. The voices
come in Microsoft's Speech API 4 TTS package `msttsl.exe` (SHA-1
d988cbb3e99bc9dd850defda939c06b81d9d4544), a cabinet holding `sam.vce`,
`male.vce` (Mike) and `female.vce` (Mary). Each `.vce` is an OLE compound
file with these streams: `Inventory`, `TreeImage`, `PhoneFile`, `Senone`,
`Cdb`, `VcHeader` (and `AltUnits` in Sam's). `tools/fetch-mssam.py`
writes `Inventory` as `<voice>.inv`, `Cdb` as `<voice>.tones`, and
`TreeImage` rewritten as `<voice>.tree`.

## Inventory (`.inv`, little-endian)

    u32 rate (22050), order (20), nlsf (6), na (11), nb (13), 2 more
    nlsf tables, then na tables, then nb tables; each:
        u32 count, u32 dim, count*dim float32
    u32 nunits, u32 size, (nunits+1) u32 offsets, then the units

The 6 line-spectrum codebooks have 256 entries of 3, 3, 3, 3, 4 and 4
values: one entry from each makes the 20 line spectral frequencies of a
frame (in cycles per sample). The excitation codebooks have 128 entries;
family A's 11 dims and family B's 13 dims each add up to 510, the points
of one pitch period scaled to a fixed length.

A unit:

    u32 n                          frames
    n frame headers                voiced: 0x7f bytes (127 each) then a
                                   byte < 0x7f, adding up to the pitch
                                   period in samples; unvoiced: 0x80
                                   bytes then a byte > 0x80, the length
                                   is 128*(number of 0x80 - 1) + that byte
    n x 6 bytes                    line-spectrum codebook indices
    n float32                      gains (the RMS of the frame's excitation)
    for each voiced frame          the first frame of a voiced run: 11
                                   family A indices, the whole period;
                                   each later one: 13 family B indices,
                                   the change from the previous period

Unvoiced frames are noise. Unit 0 is empty; unit k+1 is senone k.

## TreeImage (big-endian)

A header with the path of the question file, then the questions (u32
count, then name\0 and a 64-bit mask each), then `\\...\TREE2\0`,
`tree\0`, u32 number of sounds n, n bytes (1: one state per sound), u32 n,
n u32 (where each sound's senones end, counted from n), u32 the number of
senones, then u32 total, 0, 18, 0xffffffff, the number of composite
questions q, then q pairs of 64-bit masks, 8 more bytes, and one tree per
sound.

A mask holds sound i at bit (i + 36) mod 64, and the four word positions at
bits 32-35: begins a word, ends one, is a whole word, is inside one. A
composite question (left, right) is true when the sound before is in
`left`, this sound's word position is in `left`, and the sound after is in
`right`.

A tree is u16 nodes, u16 leaves (nodes = 2*leaves - 1), then the nodes in
order, node 0 first:

    inner node   u16 yes, u16 no, 0xffff, u16 question..., 0xffff
    leaf         0xffff, u16 k, u16 k

An inner node goes to `yes` when any of its questions is true. Leaf k of
sound s is senone (the senones of the sounds before s) + k.

PhoneFile lists the sounds in order, one per line (Sam: 52, Mike and
Mary: 58, with SILb and SILe for silence before and after).

`<voice>.tree` is the same in our own little-endian layout:

    "MSSAMTR1", u32 sounds, u32 questions, u32 senones, u32 1
    questions x (u64 left, u64 right)  sound i at bit i, positions at 60-63
    sounds x (char name[8], u32 first senone, u32 nodes, u32 first node)
    u32 nodes, u32 question refs
    nodes x (i32 yes, i32 no, u32 first ref, u32 refs)   leaf: yes = -1, no = k
    refs x u16 question index

## Senone

u32 count, then 28-byte records: the sound's name (4 bytes), u32 0, u32
the leaf number + 1, and four float32: the unit's mean length in ms, its
spread, and two energies.

## Cdb (`.tones`)

Text: pitch per tone, as `H (179) on: m=129= sd=34=; off: m=141= ...`, the
mean pitch in Hz where a vowel starts and ends. mssam uses `H` (accented),
`_` (plain), `h]` and `_]` (the last vowel of a phrase).
