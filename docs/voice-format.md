# Voice format

A voice is a folder under `voicedir`. The default is
`<astdatadir>/sounds/speaking-clock` on Asterisk, and
`<sounds_dir>/speaking-clock` on FreeSWITCH. The name of the folder is
the id of the voice. You give this name to `SpeakingClock()` (or to
`speakingclock` on FreeSWITCH). app_speakingclock and mod_speakingclock
read the same voices. The folder contains a `voice.conf` and the clips
of the voice.

## Clips

Each clip is an 8 kHz, 16-bit, mono PCM `.wav` file. app_speakingclock
skips all other files. Thus `tools/build_voices.py` converts all clips to
that format. The file name of a clip tells what the clip contains:

| Clip | Says | Used by |
|---|---|---|
| `intro` | a chime or "attention" before the time | `intro` |
| `its` | "It's" / "The time is" | `its` |
| `phrase` | "At the third stroke (it will be)" | `phrase` |
| `greet_morning`, `greet_afternoon`, `greet_evening` | "Good morning" ... | `greeting` |
| `n0` .. `n59` | a full number in one clip | `hour`, `minute` |
| `h1` .. `h12` | the hour, if the voice says it differently from the number (TIM: "it will be three") | `hour` |
| `hoc1` .. `hoc12` | "*n* o'clock" in one clip | `hour` on the hour |
| `noon`, `midnight` | | `hour` at 12:00 and 0:00 |
| `m1` .. `m59` | the minute, if the voice says it differently from the number (TIM) | `minute` |
| `oh` | "oh", for minutes 1 to 9 ("four oh five") | `minute` |
| `oclock` | "o'clock", for minute 0 | `minute` |
| `f_twen`, `f_thir`, `f_for`, `f_fif`, `f_six`, `f_seven`, `f_eight`, `f_nine` | the first half of 13 to 19 and the tens | numbers that have no `n` clip |
| `f_teen`, `f_ty`, `f_ty1` .. `f_ty9` | "-teen", "-ty", "-ty one" .. "-ty nine" | numbers that have no `n` clip |
| `s10` .. `s50` | "and 10 seconds" .. | `seconds` |
| `precisely` | "precisely" (second 0) | `seconds` |
| `am`, `pm` | | `ampm` |
| `pips` | the time signal | `pips` |
| `q00`, `q15`, `q30`, `q45` | a quarter-hour chime | `quarter` |
| `chime` | one strike of the hour | `strikes` |
| `hr1` .. `hr12` | a sound for each hour | `hourly` |
| `hourchime1`, `hourchime2` | top-of-hour chimes | `hourchime` |
| `x_*` | all other sounds that came with the voice (alarms, dings, messages) | by name |

The module builds numbers in the same way as the voices of Steve's
Talking Clock:

1. If the number has an `n` clip, the module uses it.
2. If not, the module builds 13 to 19 from a fragment and `f_teen`
   ("thir" + "teen").
3. The module builds twenty and above from `f_twen` and `f_ty3`
   ("twen" + "ty-three") if the voice has `f_ty`. If the voice does not
   have `f_ty`, it uses `n20` and `n3`.

## voice.conf

```
[voice]
name=Pat Simmons
description=UK GPO speaking clock (TIM), 1963-1985
mode=tim                ; tim | talking | chime | hourly | tts
sequence=phrase+hour+minute+seconds+pips
period=10               ; seconds between announcements
marker=2.2949           ; the time mark: this many seconds into pips.wav
hours=12                ; or 24

[source]
...                     ; where the clips came from (for people, not the module)
```

- **mode** sets the defaults for `sequence` and `period`:
  - `talking`: `intro+its+hour+minute+ampm`, each 10 s
  - `tim`: `phrase+hour+minute+seconds+pips`, each 10 s
  - `chime`: `quarter+strikes`, each 30 s
  - `hourly`: `hourly`, each 10 s
  - `tts`: `speech+pips`, each 10 s (refer to Live voices, below)
- **sequence** lists the items to say, joined by `+`. It can use the
  words in the table above (`intro`, `its`, `greeting`, `phrase`, `hour`,
  `minute`, `seconds`, `ampm`, `pips`, `quarter`, `strikes`, `hourly`,
  `hourchime`) or a clip by name (`x_exactly`). The module skips a
  missing clip. If there is no `pips.wav`, `pips` plays synthetic pips:
  three 0.1 s, 1 kHz tones, one second apart.
- **period**: announcement *j* gives the time *j* × period.
- **marker**: the position of the time mark in `pips.wav`. With a marker,
  the module aligns each announcement so that the mark comes on the exact
  second that it announces, as on the real machines. Without a marker,
  each announcement starts on its second. For synthetic pips, the mark is
  the third pip. `tools/build_voices.py` measures this value from the clip
  (the start of the last pip).

## Live voices (mode=tts)

A live voice has no number clips. A synthesiser program on the Asterisk
or FreeSWITCH machine (DECtalk, SAM) speaks the full sentence of each
announcement when the module must have it.

```
[voice]
name=DECtalk Perfect Paul, live
mode=tts
sequence=speech+pips
period=10

[tts]
command={program:dectalk} -fo {out} -a "[:np] {text}"
text=At the third stroke, it will be {hour} {minute} {seconds}.
```

- **command**: the program to run. The module divides it into words as
  a shell does (with `"..."` and `'...'`), but it does not use a shell.
  Thus the text is always one argument. `{program:name}` becomes
  `tts_program_name` from speakingclock.conf or speakingclock.conf.xml.
  If that setting is not there, it becomes `name`, found on the PATH.
  `{text}` becomes the text. An argument `{out}` becomes a file, and the
  program writes the WAV to that file. Without `{out}`, the module reads
  the WAV from the standard output of the program.
- **text**: the words of each announcement. In words: `{hour}`,
  `{minute}` ("oh five", "o'clock" on the hour, "hundred" on a 24-hour
  clock), `{seconds}` ("and twenty seconds", "precisely" on the minute),
  `{ampm}` ("A.M.", "P.M."), `{greeting}` ("Good morning"). In digits:
  `{H}`, `{HH}`, `{M}`, `{MM}`, `{S}`, `{SS}`.
- **sequence**: `speech` is the sentence. You can also use clips in the
  folder of the voice (a `pips.wav`, an `intro.wav`).

The WAV of the program can be any PCM: 8 or 16 bits, mono or stereo, at
any rate. The module converts it to 8 kHz, removes the silence before
and after it, and makes its level even. The module keeps the last 128
sentences. A worker thread runs the program about 20 seconds before the
module must have each sentence. Thus a call never waits for the program.
If a sentence is not ready in time, the announcement plays only the
pips. If a program fails, the module tries again after 30 seconds.
`tts_timeout` (default 15 s) stops a program that hangs.

`tools/synth_voices.py --live voices dectalk:<voice>` (or `sam:`) writes
these voices (`live-dectalk-*`, `live-sam-*`).
