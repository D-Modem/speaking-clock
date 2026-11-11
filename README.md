# speaking-clock

A telephone speaking clock for Asterisk, FreePBX and FreeSWITCH. Its voices include the
UK's TIM (Pat Simmons), Australia's Gordon Gow, the whole Steve's Talking
Clock library of talking clocks and watches, and classic speech synthesisers
(Microsoft Sam and the other SAPI4 voices, DECtalk, SAM, MacInTalk, PlainTalk), each
able to give the time to the second.

- **`asterisk/`**: `app_speakingclock`, the `SpeakingClock()` dialplan
  application, written in C.
- **`freepbx/speakingclock/`**: a FreePBX module. Clocks are destinations,
  with optional numbers to dial.
- **`freeswitch/`**: `mod_speakingclock`, the `speakingclock` dialplan
  application for FreeSWITCH, the same clock as `SpeakingClock()`.
- **`core/`**: the clock itself, with no PBX in it (mod_speakingclock is
  built on it), and `sclock-render`, which writes what a caller would hear.
- **`voices/`**: the voices ([INDEX](voices/INDEX.md), [format](docs/voice-format.md)).
- **`tools/`**: fetch the original recordings and rebuild `voices/`.

The recordings in `voices/` belong to their original owners, and **are not
covered by the code's licence**. See [NOTICE.md](NOTICE.md).

## How it keeps time

The old machines were one recording that every caller listened in on. The
time mark (the third pip, or the tone) fell exactly on the second it
announced, and callers joined part way through a sentence.

`SpeakingClock()` works the same way. Each announcement is placed by sample
against the system clock (so keep it on NTP), and the module checks the
clock every second, jumping back to it if it drifts by more than 20 ms.
Announcements are not queued with `Playback()`, so the clock doesn't drift
however long a call is up. Measured on a test call, Pat Simmons' third pip
landed within 10 ms of the second. mod_speakingclock does the same on
FreeSWITCH.

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

`SpeakingClock([voice[,options]])` answers and plays until the caller hangs
up or the duration runs out. Its options:

| Option | Effect |
|---|---|
| `t(zone)` | time zone (default: `timezone` in speakingclock.conf, else the system's) |
| `d(seconds)` | hang up after this long; 0 means never (default `maxduration`, 180) |
| `h` | 24-hour clock |
| `p(seconds)` | seconds between announcements |
| `s(a+b+c)` | what to say, instead of the voice's sequence (docs/voice-format.md) |
| `w` | wait for the next announcement to start, instead of joining part way |
| `x` | return when the caller presses a key |

CLI:

- `speakingclock show voices` lists the voices.
- `speakingclock render <voice> <unix time> <seconds> <file.sln> [zone]`
  writes what a caller would hear, as 8 kHz signed linear, without a call.
- `module reload app_speakingclock` rereads the config and the voices.

Asterisk logs "Couldn't find application SpeakingClock in XML
documentation" because a module built outside Asterisk's tree has no XML
docs installed. The warning does no harm.

## Live voices

A voice with `mode=tts` has no clips. The module runs a synthesiser on the
Asterisk machine for each announcement's whole sentence ("At the third
stroke, it will be four fifteen and twenty seconds."), a little ahead of
when it is needed, so the call never waits on it. The `live-dectalk-*` and
`live-sam-*` voices run DECtalk's `say` and `sam`. The `live-macintalk-*`
voices (Fred, Ralph, Kathy, Zarvox and the other 15 MacInTalk 3 voices) and
the `live-plaintalk-*` voices (Agnes, Bruce and Victoria, standard and
high quality) run
[synth/macintalk](synth/macintalk), our own synthesiser, which reads
Apple's voice data (`cd synth/macintalk && make && make voices && sudo make
install` builds it and fetches the data; the PlainTalk voices also need
`unar` or `unrar` to unpack their disk image). Set where they are in
speakingclock.conf:

```
tts_program_dectalk=/opt/dectalk/say
tts_program_sam=/usr/local/bin/sam
tts_program_macintalk=/usr/local/bin/macintalk
```

The `live-mssam-*` voices (Microsoft Sam, Mike and Mary, and Mike and Mary
for telephone) run
[synth/mssam](synth/mssam), our own synthesiser, which reads Microsoft's
voice data (`cd synth/mssam && make && make voices && sudo make install`
builds it and fetches the data). Its setting is `tts_program_mssam`.

See [docs/voice-format.md](docs/voice-format.md#live-voices-modetts).

## FreeSWITCH

```sh
cd freeswitch
make                 # needs FreeSWITCH's headers (libfreeswitch-dev, or a source install)
sudo make install    # module, autoload_configs/speakingclock.conf.xml, voices into sounds/speaking-clock
fs_cli -x 'load mod_speakingclock'
```

Add `<load module="mod_speakingclock"/>` to `autoload_configs/modules.conf.xml`
to load it at start. In the dialplan
([dialplan-example.xml](freeswitch/dialplan-example.xml)):

```xml
<extension name="speakingclock_tim">
  <condition field="destination_number" expression="^123$">
    <action application="speakingclock" data="tim-pat-simmons t(Europe/London)"/>
  </condition>
</extension>
```

`speakingclock [voice] [options]` takes the same voices and options as
`SpeakingClock()` above (`t(zone)`, `d(seconds)`, `h`, `p(seconds)`,
`s(a+b+c)`, `w`, `x`), written after the voice with a space. With `x`, the
key the caller pressed is left in the channel variable `speakingclock_key`.
Time zones are FreeSWITCH's own, from `timezones.conf.xml`. The live voices
work the same way: set `tts_program_*` in
[speakingclock.conf.xml](freeswitch/speakingclock.conf.xml).

The module sends the clock in 8 kHz signed linear frames the length of the
call's, and FreeSWITCH converts them to the call's codec and rate. Each
frame comes from the clock's position against the system clock, so the
time mark lands on the second whatever the codec.

API (fs_cli):

- `speakingclock voices` lists the voices.
- `speakingclock render <voice> <unix time> <seconds> <file.sln> [zone]`
  writes what a caller would hear, as 8 kHz signed linear.
- `speakingclock reload` (or `reloadxml`) rereads the config and the voices.

`PBX=freeswitch test/render_test.py` runs the timing test against it.

Without a PBX, `cd core && make` builds `sclock-render`, which does the same
as `speakingclock render`:
`./sclock-render -v ../voices tim-pat-simmons 1762791295 30 out.sln Europe/London`.

## FreePBX

Pack `freepbx/speakingclock` into a tarball and install it with Module Admin
(upload), or copy it to `/var/www/html/admin/modules/` and run
`fwconsole ma install speakingclock`. It needs app_speakingclock installed as
above. **Applications > Speaking Clock** then lists your clocks. Each clock
is a destination for inbound routes, IVRs and time conditions, and can have
a number to dial from extensions.

## Rebuilding the voices

```sh
tools/fetch-sources.sh sources        # curl, git, unzip, 7z or innoextract
tools/build_voices.py sources voices  # ffmpeg
```

The synthesiser voices come from `tools/synth_voices.py`. It has the
synthesiser speak every clip on its own (a whole number, "and twenty
seconds", "At the third stroke, it will be"), so they are `tim` voices that
give the time to the second, with the module making the pips:

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
version, so the module can be loaded into Asterisk (GPL-2.0). See
[LICENSE](LICENSE). The recordings are not covered by it: see
[NOTICE.md](NOTICE.md).
