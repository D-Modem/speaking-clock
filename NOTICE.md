# Notice: the voice recordings

The code in this repository (the Asterisk module, the FreePBX module and the
tools) is licensed under the GNU General Public License, version 2 or (at
your option) any later version. See [LICENSE](LICENSE).

**That licence does not cover the recordings in [`voices/`](voices/).**
Every recording belongs to its original owners: the telephone companies,
museums, voice artists, speech-synthesiser makers and hobbyists who made
them. They are not licensed under the GPL. They are collected here only so
that a speaking clock can be built from them. No claim to them is made, and
no copyright infringement is intended.

If you own a recording here and want it credited differently or taken out,
open an issue on this repository and it will be removed.

`tools/build_voices.py` converted every recording to 8 kHz 16-bit mono WAV
and renamed it (docs/voice-format.md). The original file name of each clip is
listed under `[source] files=` in its voice's `voice.conf`. Any notes that
came with a voice are kept beside it as `ORIGINAL-*`.

## Where they came from

### asterisk-tim (`voices/tim-*`)
From [paulseward/asterisk-tim](https://github.com/paulseward/asterisk-tim).

- **Pat Simmons** (`tim-pat-simmons`): the voice of the UK GPO / BT
  speaking clock (TIM), 1963 to 1985. Recordings from Andy Emmerson and the
  TIM2000 project. They are believed to come from a BT prototype digital
  announcer loaded from the original magnetic-drum machine.
- **Gordon Gow** (`tim-gordon-gow`): the voice of the Australian speaking
  clock, 1954 to 1990. Recorded by Paul Seward from the glass-disc machine
  held by the Science Museum, London.

The asterisk-tim repository has no licence file. Its notes are kept as
`ORIGINAL-README.md` in each voice.

### Steve's Talking Clock voice library (most of `voices/`)
From <http://clock.steve-audio.net/voices/>, the voices made for Steve's
Talking Clock (a Windows talking-clock program). That page says: "Each voice
is copyright to its original creator, and no copyright infringement is
intended." Each voice's `voice.conf` names the package it came from and its
download URL. The voices include:

- recordings of talking clocks, watches and calculators: Hitachi, Sharp
  EL-640, Sharp Talking Time, Radio Shack VoxClock I, II and III, Timex
  Speakeasy, Satoki, Actim, Cobolt, Braille Lite, TalkBox and others;
- speech synthesisers and TTS engines: AT&T Natural Voices (Audrey, Charles,
  Lauren, Mike, Ray, Rich, Anjali), Cepstral, ScanSoft RealSpeak, NeoSpeech,
  Eloquence, DoubleTalk, Keynote Gold, Dolphin Apollo, Echo, RC Systems,
  Toshiba, Nokia, Outspoken, rsynth;
- voices recorded by members of that community (Patrick Perdue, Kathleen
  Perdue, Rainee Perdue, James Bowden, Julie Williams, Keao Wright, Derek
  Lane, Shawn Klein, Andre Louis (Onj), Nick G, Tony and others), credited
  in each voice's notes;
- chimes and novelty clocks (Big Ben, windchimes, piano, Morse code, animal
  sounds and more).

The trademarks named belong to their owners. Synthesised voices remain
subject to the terms of the engines that produced them.

### garrysblog Speaking-Clock-MP3-Files (`voices/british-amy`, `voices/french-celine`)
From [garrysblog/Speaking-Clock-MP3-Files](https://github.com/garrysblog/Speaking-Clock-MP3-Files),
made with an online text-to-speech service for an Arduino clock. Its README
says the files are "free to download and use in your own projects".

### Speech synthesisers (`voices/sapi4-*`, `voices/dectalk-*`, `voices/sam-*`, `voices/mac-*`)
Made by `tools/synth_voices.py`, which has each synthesiser speak every
phrase and number the clock needs. The synthesisers, and the sound of their
voices, belong to their owners:

- **SAPI4 voices** (`sapi4-*`): Microsoft Sam, Mike and Mary (Microsoft
  Speech API 4.0), Lernout & Hauspie TruVoice and TTS3000, and RoboSoft.
  They were spoken by the public SAPI4 service at <https://www.tetyys.com/SAPI4/>,
  which runs Microsoft's SAPI4 runtime and voices. Microsoft, Lernout &
  Hauspie and BonziBUDDY are trademarks of their owners. The `bonzibuddy`
  voice is TruVoice Adult Male #2 at the pitch and speed BonziBUDDY used.
  The live voices (`live-*`) hold no recordings: each sentence is spoken
  when it is needed, by DECtalk or SAM on the Asterisk machine.
- **Microsoft Sam, Mike and Mary, live** (`live-mssam-*`): no recordings.
  Each sentence is spoken on the Asterisk machine by `synth/mssam`, our own
  synthesiser (GPL-2.0-or-later, like the rest of this repository), which
  reads the voice data of Microsoft's Speech API 4 voices. That data is
  Microsoft's and is not in this repository: `tools/fetch-mssam.py`
  downloads Microsoft's package (msttsl.exe) from the Internet Archive and
  keeps only the voice data, none of Microsoft's programs. Microsoft is a
  trademark of Microsoft Corporation. The word list in
  `synth/mssam/lexicon.h`, and the `cmudict.dict` the fetch script
  downloads, are from the CMU Pronouncing Dictionary, © 1993-2015 Carnegie
  Mellon University, under its BSD-style licence (in `lexicon.h`).
- **DECtalk** (`dectalk-*`): DECtalk 4.x, made by Digital Equipment
  Corporation and later Fonix. Built from the source at
  <https://github.com/dectalk/dectalk>.
- **SAM** (`sam-*`): Software Automatic Mouth, Don't Ask Software, 1982 (now
  SoftVoice, Inc.). Built from the C port at <https://github.com/s-macke/SAM>.
- **MacInTalk and PlainTalk** (`mac-*`): Apple's voices (MacinTalk 3, and
  the PlainTalk MacinTalk Pro voices Victoria, Agnes, Bruce and Vicki),
  spoken by macOS's `say`.
- **MacInTalk 3, live** (`live-macintalk-*`): no recordings. Each sentence
  is spoken on the Asterisk machine by `synth/macintalk`, our own
  synthesiser (GPL-2.0-or-later, like the rest of this repository), which
  reads the voice data of Apple's MacInTalk 3 voices. That data is Apple's
  (© Apple Computer, Inc. 1992-1995) and is not in this repository:
  `tools/fetch-macintalk.py` downloads it from a Mac OS 8.1 disk image on
  the Internet Archive. MacInTalk and Macintosh are trademarks of Apple
  Inc. The letter-to-sound rules in `synth/macintalk/rules.h` are from NRL
  Report 7948 (1976), a US government work in the public domain.
- **PlainTalk, live** (`live-plaintalk-*`): no recordings in this
  repository. Each sentence is spoken on the Asterisk machine by the same
  `synth/macintalk`, which joins the recorded diphones of Apple's PlainTalk
  (MacinTalk Pro) voices Agnes, Bruce and Victoria. Those recordings are
  Apple's (© Apple Computer, Inc. 1992-1998) and are not in this
  repository: `tools/fetch-macintalk.py` downloads them from a Mac OS 8.6
  disk image on the Internet Archive. PlainTalk is a trademark of Apple
  Inc.
