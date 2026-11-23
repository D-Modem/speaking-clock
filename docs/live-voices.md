# Live voices

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
  [synth/macintalk](../synth/macintalk). This is our own synthesiser, and it
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
Mary for telephone) run [synth/mssam](../synth/mssam). This is our own
synthesiser, and it reads the voice data of Microsoft.
`cd synth/mssam && make && make voices && sudo make install` builds the
synthesiser and downloads the data. Its setting is `tts_program_mssam`.

Refer to [docs/voice-format.md](voice-format.md#live-voices-modetts).
