# speaking-clock

A telephone speaking clock for Asterisk and FreePBX, with 81 voices: the UK's
TIM (Pat Simmons), Australia's Gordon Gow, and the whole Steve's Talking
Clock library of talking clocks, watches and speech synthesisers.

- **`asterisk/`**: `app_speakingclock`, the `SpeakingClock()` dialplan
  application, written in C.
- **`freepbx/speakingclock/`**: a FreePBX module. Clocks are destinations,
  with optional numbers to dial.
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
landed within 10 ms of the second.

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

## Licence

The code is licensed under the GPL, version 2 or (at your option) any later
version, so the module can be loaded into Asterisk (GPL-2.0). See
[LICENSE](LICENSE). The recordings are not covered by it: see
[NOTICE.md](NOTICE.md).
