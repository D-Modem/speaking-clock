# FreeSWITCH

```sh
cd freeswitch
make                 # needs FreeSWITCH's headers (libfreeswitch-dev, or a source install)
sudo make install    # module, autoload_configs/speakingclock.conf.xml, voices into sounds/speaking-clock
fs_cli -x 'load mod_speakingclock'
```

To load the module at start, add `<load module="mod_speakingclock"/>` to
`autoload_configs/modules.conf.xml`. This is an example for the dialplan
([dialplan-example.xml](../freeswitch/dialplan-example.xml)):

```xml
<extension name="speakingclock_tim">
  <condition field="destination_number" expression="^123$">
    <action application="speakingclock" data="tim-pat-simmons t(Europe/London)"/>
  </condition>
</extension>
```

`speakingclock [voice] [options]` accepts the same voices and options as
`SpeakingClock()` ([Asterisk](asterisk.md)) (`t(zone)`, `d(seconds)`, `h`, `p(seconds)`,
`s(a+b+c)`, `w`, `x`). Write the options after the voice, with a space.
With `x`, the channel variable `speakingclock_key` contains the key that
the caller pushed. The time zones are the time zones of FreeSWITCH, from
`timezones.conf.xml`. The live voices operate in the same way. Set
`tts_program_*` in
[speakingclock.conf.xml](../freeswitch/speakingclock.conf.xml).

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
