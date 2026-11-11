# Voice format

A voice is a folder under `voicedir` (default
`<astdatadir>/sounds/speaking-clock` on Asterisk, `<sounds_dir>/speaking-clock`
on FreeSWITCH). The folder's name is the voice's id, the name you pass to
`SpeakingClock()` (or FreeSWITCH's `speakingclock`). app_speakingclock and
mod_speakingclock read the same voices. It holds a `voice.conf` and its clips.

## Clips

Each clip is an 8 kHz, 16-bit, mono PCM `.wav` file. app_speakingclock skips
anything else, so `tools/build_voices.py` converts every clip to that format.
A clip's file name says what it holds:

| Clip | Says | Used by |
|---|---|---|
| `intro` | a chime or "attention" before the time | `intro` |
| `its` | "It's" / "The time is" | `its` |
| `phrase` | "At the third stroke (it will be)" | `phrase` |
| `greet_morning`, `greet_afternoon`, `greet_evening` | "Good morning" ... | `greeting` |
| `n0` .. `n59` | a number said whole | `hour`, `minute` |
| `h1` .. `h12` | the hour, when it is said differently from the number (TIM: "it will be three") | `hour` |
| `hoc1` .. `hoc12` | "*n* o'clock" in one clip | `hour` on the hour |
| `noon`, `midnight` | | `hour` at 12:00 and 0:00 |
| `m1` .. `m59` | the minute, when it is said differently from the number (TIM) | `minute` |
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
| `x_*` | anything else the voice came with (alarms, dings, messages) | by name |

Numbers are built the way Steve's Talking Clock voices build them. A
number's `n` clip is used if there is one. Otherwise 13 to 19 are built from
a fragment plus `f_teen` ("thir" + "teen"). Twenty and above are built from
`f_twen` plus `f_ty3` ("twen" + "ty-three") when the voice has `f_ty`, and
from `n20` plus `n3` when it doesn't.

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
  - `talking`: `intro+its+hour+minute+ampm`, every 10 s
  - `tim`: `phrase+hour+minute+seconds+pips`, every 10 s
  - `chime`: `quarter+strikes`, every 30 s
  - `hourly`: `hourly`, every 10 s
  - `tts`: `speech+pips`, every 10 s (see Live voices, below)
- **sequence** lists what to say, joined by `+`. It can use the words in the
  table above (`intro`, `its`, `greeting`, `phrase`, `hour`, `minute`,
  `seconds`, `ampm`, `pips`, `quarter`, `strikes`, `hourly`, `hourchime`) or
  any clip by name (`x_exactly`). A missing clip is skipped. `pips` with no
  `pips.wav` plays made-up pips: three 0.1 s, 1 kHz tones a second apart.
- **period**: announcement *j* gives the time *j* × period.
- **marker**: where the time mark falls in `pips.wav`. With a marker, each
  announcement is placed so that the mark lands on the exact second it
  announces, as on the real machines. Without one, each announcement starts
  on its second. For made-up pips the mark is the third pip.
  `tools/build_voices.py` measures this from the clip (the onset of the last
  pip).

## Live voices (mode=tts)

A live voice has no number clips. Each announcement's sentence is spoken
whole, when it is needed, by a synthesiser program on the Asterisk or FreeSWITCH machine
(DECtalk, SAM).

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

- **command**: the program to run, split into words like a shell would
  (with `"..."` and `'...'`), but run without one, so the text is always a
  single argument. `{program:name}` becomes `tts_program_name` from
  speakingclock.conf or speakingclock.conf.xml (else `name`, found on the PATH), `{text}` the text,
  and an argument `{out}` a file for it to write the WAV to. Without `{out}`
  the WAV is read from its standard output.
- **text**: what each announcement says. In words: `{hour}`, `{minute}`
  ("oh five", "o'clock" on the hour, "hundred" on a 24-hour clock),
  `{seconds}` ("and twenty seconds", "precisely" on the minute), `{ampm}`
  ("A.M.", "P.M."), `{greeting}` ("Good morning"). In digits: `{H}`,
  `{HH}`, `{M}`, `{MM}`, `{S}`, `{SS}`.
- **sequence**: `speech` is the sentence. Clips in the voice's folder can be
  used as well (a `pips.wav`, an `intro.wav`).

The program's WAV can be any PCM: 8 or 16 bits, mono or stereo, any rate.
The module converts it to 8 kHz, trims the silence around it and evens its
level, and keeps the last 128 sentences. A worker thread runs the program,
about 20 seconds before each sentence is due, so a call never waits on it.
An announcement whose sentence isn't ready in time plays the pips alone,
and a program that fails is tried again after 30 seconds. `tts_timeout`
(default 15 s) stops a program that hangs.

`tools/synth_voices.py --live voices dectalk:<voice>` (or `sam:`) writes
these voices (`live-dectalk-*`, `live-sam-*`).
