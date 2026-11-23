# Asterisk

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
