# speaking-clock

This is a telephone speaking clock for Asterisk, FreePBX and FreeSWITCH.
It has these voices, and each voice can give the time to the second:

- TIM of the UK (Pat Simmons).
- Gordon Gow of Australia.
- All of the Steve's Talking Clock library of talking clocks and watches.
- Classic speech synthesisers: Microsoft Sam and the other SAPI4 voices,
  DECtalk, SAM, MacInTalk and PlainTalk.

The repository has these parts:

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

## Quick start

```sh
make                 # the modules for the PBXs on this machine, the synthesisers and sclock-render
sudo make install    # the modules, the configuration and the voices
make test            # render 30 seconds of Pat Simmons with sclock-render
```

Then dial the clock from the dialplan:

```
exten => 123,1,SpeakingClock(tim-pat-simmons,t(Europe/London))
```

The clock keeps to the system clock to the sample. Thus, keep the system
clock on NTP ([How it keeps time](docs/timing.md)).

## Documentation

- [Building and installing](docs/building.md): the options of `make`, and the voice data that it downloads
- [Asterisk](docs/asterisk.md): `SpeakingClock()`, its options and the CLI commands
- [FreeSWITCH](docs/freeswitch.md): `mod_speakingclock` and its API
- [FreePBX](docs/freepbx.md): the module, and clocks as destinations
- [Live voices](docs/live-voices.md): DECtalk, SAM, MacInTalk, PlainTalk and Microsoft Sam, spoken at each announcement
- [Rebuilding the voices](docs/rebuilding-voices.md): the source recordings and the synthesiser voices
- [Voice format](docs/voice-format.md), [MacInTalk format](docs/macintalk-format.md), [PlainTalk format](docs/plaintalk-format.md), [Microsoft Sam format](docs/mssam-format.md)
- [Documentation style](docs/STYLE.md): the rules for ASD-STE100 Simplified Technical English

## Licence

The code is under the GPL, version 2 or (at your option) any later
version, so that Asterisk (GPL-2.0) can load the module. Refer to
[LICENSE](LICENSE). The recordings are not covered by it. Refer to
[NOTICE.md](NOTICE.md).
