# speaking-clock

This is a telephone speaking clock for Asterisk, FreePBX and FreeSWITCH.
It has these voices, and each voice can give the time to the second:

- TIM of the UK (Pat Simmons).
- Gordon Gow of Australia.
- All of the Steve's Talking Clock library of talking clocks and watches.
- Classic speech synthesisers: Microsoft Sam and the other SAPI4 voices,
  DECtalk, SAM, MacInTalk and PlainTalk.

- **`asterisk/`**: `app_speakingclock`, the `SpeakingClock()` dialplan
  application, written in C.
- **`freepbx/speakingclock/`**: a FreePBX module. Clocks are destinations,
  with optional numbers to dial.
- **`freeswitch/`**: `mod_speakingclock`, the `speakingclock` dialplan
  application for FreeSWITCH, the same clock as `SpeakingClock()`.
- **`core/`**: the clock itself, without a PBX (mod_speakingclock is
  built on it), and `sclock-render`, which writes the audio that a caller
  hears.
- **`voices/`**: the voices ([INDEX](voices/INDEX.md), [format](docs/voice-format.md)).
- **`tools/`**: tools that download the original recordings and build
  `voices/` again.

The recordings in `voices/` belong to their original owners, and **are not
covered by the code's licence**. Refer to [NOTICE.md](NOTICE.md).

## How it keeps time

The old machines played one recording, and all callers listened to it.
The time mark (the third pip, or the tone) came exactly on the second
that the recording announced. Callers started to listen in the middle of
a sentence.

`SpeakingClock()` operates in the same way. The module aligns each
announcement to the system clock, sample by sample. Thus, keep the
system clock on NTP. The module checks the clock each second. If the
clock drifts by more than 20 ms, the module moves back to the clock. The
module does not put announcements in a queue with `Playback()`. Thus the
clock does not drift, for calls of all lengths. On a test call, the third
pip of Pat Simmons came less than 10 ms from the second.
mod_speakingclock does the same on FreeSWITCH.

## Asterisk

```sh
cd asterisk
make                 # needs Asterisk's headers (asterisk-dev, or make install-headers)
sudo make install    # module, speakingclock.conf, voices into sounds/speaking-clock
asterisk -rx 'module load app_speakingclock.so'
```

```
exten => 123,1,SpeakingClock(tim-pat-simmons,t(Europe/London))
exten => 1194,1,SpeakingClock(tim-gordon-gow,t(Australia/Sydney))
exten => 767,1,SpeakingClock(atandt-mike,t(America/Los_Angeles)d(300))
```

`SpeakingClock([voice[,options]])` answers the call. Then it plays until
the caller hangs up or until the duration ends. These are its options:

| Option | Effect |
|---|---|
| `t(zone)` | time zone (default: `timezone` in speakingclock.conf, or if it is not set, the time zone of the system) |
| `d(seconds)` | hang up after this number of seconds. 0 means never (default `maxduration`, 180) |
| `h` | 24-hour clock |
| `p(seconds)` | seconds between announcements |
| `s(a+b+c)` | the clips to say, in place of the sequence of the voice (docs/voice-format.md) |
| `w` | wait for the start of the next announcement. Do not start in the middle of an announcement |
| `x` | return when the caller pushes a key |

CLI:

- `speakingclock show voices` lists the voices.
- `speakingclock render <voice> <unix time> <seconds> <file.sln> [zone]`
  writes the audio that a caller hears, as 8 kHz signed linear, without a
  call.
- `module reload app_speakingclock` reads the configuration and the
  voices again.

Asterisk writes "Couldn't find application SpeakingClock in XML
documentation" in the log. The cause is that a module built outside the
source tree of Asterisk has no installed XML documentation. This warning
does not cause a problem.

## Live voices

A voice with `mode=tts` has no clips. For each announcement, the module
runs a synthesiser on the Asterisk machine for the full sentence ("At the
third stroke, it will be four fifteen and twenty seconds."). It does this
a short time before the module must have the sentence. Thus the call
never waits for the synthesiser.

- The `live-dectalk-*` and `live-sam-*` voices run `say` of DECtalk and
  `sam`.
- The `live-macintalk-*` voices (Fred, Ralph, Kathy, Zarvox and the other
  15 MacInTalk 3 voices) and the `live-plaintalk-*` voices (Agnes, Bruce
  and Victoria, standard and high quality) run
  [synth/macintalk](synth/macintalk). This is our own synthesiser, and it
  reads the voice data of Apple. `cd synth/macintalk && make && make voices && sudo make install`
  builds the synthesiser and downloads the data. The PlainTalk voices
  also must have `unar` or `unrar` to unpack their disk image.

Set the locations of the programs in speakingclock.conf:

```
tts_program_dectalk=/opt/dectalk/say
tts_program_sam=/usr/local/bin/sam
tts_program_macintalk=/usr/local/bin/macintalk
```

The `live-mssam-*` voices (Microsoft Sam, Mike and Mary, and Mike and
Mary for telephone) run [synth/mssam](synth/mssam). This is our own
synthesiser, and it reads the voice data of Microsoft.
`cd synth/mssam && make && make voices && sudo make install` builds the
synthesiser and downloads the data. Its setting is `tts_program_mssam`.

Refer to [docs/voice-format.md](docs/voice-format.md#live-voices-modetts).

## FreeSWITCH

```sh
cd freeswitch
make                 # needs FreeSWITCH's headers (libfreeswitch-dev, or a source install)
sudo make install    # module, autoload_configs/speakingclock.conf.xml, voices into sounds/speaking-clock
fs_cli -x 'load mod_speakingclock'
```

To load the module at start, add `<load module="mod_speakingclock"/>` to
`autoload_configs/modules.conf.xml`. This is an example for the dialplan
([dialplan-example.xml](freeswitch/dialplan-example.xml)):

```xml
<extension name="speakingclock_tim">
  <condition field="destination_number" expression="^123$">
    <action application="speakingclock" data="tim-pat-simmons t(Europe/London)"/>
  </condition>
</extension>
```

`speakingclock [voice] [options]` accepts the same voices and options as
`SpeakingClock()` above (`t(zone)`, `d(seconds)`, `h`, `p(seconds)`,
`s(a+b+c)`, `w`, `x`). Write the options after the voice, with a space.
With `x`, the channel variable `speakingclock_key` contains the key that
the caller pushed. The time zones are the time zones of FreeSWITCH, from
`timezones.conf.xml`. The live voices operate in the same way. Set
`tts_program_*` in
[speakingclock.conf.xml](freeswitch/speakingclock.conf.xml).

The module sends the clock in 8 kHz signed linear frames. The frames
have the same length as the frames of the call. FreeSWITCH converts them
to the codec and rate of the call. The module calculates each frame from
the position of the clock against the system clock. Thus the time mark
comes on the second, for all codecs.

API (fs_cli):

- `speakingclock voices` lists the voices.
- `speakingclock render <voice> <unix time> <seconds> <file.sln> [zone]`
  writes the audio that a caller hears, as 8 kHz signed linear.
- `speakingclock reload` (or `reloadxml`) reads the configuration and the
  voices again.

`PBX=freeswitch test/render_test.py` runs the timing test on the module.

Without a PBX, `cd core && make` builds `sclock-render`. It does the same
as `speakingclock render`:
`./sclock-render -v ../voices tim-pat-simmons 1762791295 30 out.sln Europe/London`.

## FreePBX

Do one of these procedures:

- Put `freepbx/speakingclock` into a tarball and install it with Module
  Admin (upload).
- Copy `freepbx/speakingclock` to `/var/www/html/admin/modules/` and run
  `fwconsole ma install speakingclock`.

The module must have app_speakingclock, installed as described above.
Then **Applications > Speaking Clock** lists your clocks. Each clock is a
destination for inbound routes, IVRs and time conditions. Each clock can
also have a number that extensions can dial.

## Rebuilding the voices

```sh
tools/fetch-sources.sh sources        # curl, git, unzip, 7z or innoextract
tools/build_voices.py sources voices  # ffmpeg
```

The synthesiser voices come from `tools/synth_voices.py`. This tool
tells the synthesiser to speak each clip separately (a whole number,
"and twenty seconds", "At the third stroke, it will be"). Thus they are
`tim` voices that give the time to the second, and the module makes the
pips:

```sh
tools/synth_voices.py --list                        # the voices it knows
tools/synth_voices.py voices sapi4:microsoft-sam    # SAPI4, via tetyys.com
DECTALK_SAY=~/dectalk/dist/say tools/synth_voices.py voices dectalk:paul
SAM=~/SAM/sam tools/synth_voices.py voices sam:sam
MSSAM=synth/mssam/mssam tools/synth_voices.py --live voices mssam:sam  # live-mssam-sam
tools/synth_voices.py voices mac:Fred                # MacInTalk, on a Mac
tools/synth_voices.py voices mac:Victoria            # PlainTalk, on a Mac
tools/synth_voices.py voices macintalk:fred          # MacInTalk 3, synth/macintalk
tools/synth_voices.py --live voices macintalk:zarvox # live-macintalk-zarvox
tools/synth_voices.py --live voices plaintalk:agnes  # live-plaintalk-agnes
```

## Licence

The code is licensed under the GPL, version 2 or (at your option) any later
version. Thus Asterisk (GPL-2.0) can load the module. Refer to
[LICENSE](LICENSE). The recordings are not covered by it. Refer to
[NOTICE.md](NOTICE.md).
