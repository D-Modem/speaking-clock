# Rebuilding the voices

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
