# Building and installing

From the top of the repository:

```sh
make                 # modules for the PBXs found here, synth/mssam, synth/macintalk,
                     # sclock-render, and the Microsoft and Apple voice data
sudo make install    # modules, configs, voices/ into each PBX's sounds/speaking-clock,
                     # and the synthesisers with their voice data under /usr/local
make test            # render 30 seconds of Pat Simmons with sclock-render
```

It builds `app_speakingclock` if Asterisk's headers are installed and
`mod_speakingclock` if FreeSWITCH's are. `make PBX="asterisk freeswitch"`
picks them yourself (with `ASTERISK_INCLUDE=` or `FS_INCLUDE=` for headers
somewhere else). Microsoft's and Apple's voice files are not in this
repository: `make` downloads them from the Internet Archive into
`sources/` (cabextract or 7z for Microsoft's, unar or unrar for the
PlainTalk voices). `FETCH=no` skips the download, `SYNTH=no` skips the
synthesisers, and `PREFIX=` and `DESTDIR=` work as usual. The pages for [Asterisk](asterisk.md),
[FreeSWITCH](freeswitch.md) and [live voices](live-voices.md) build one part at a
time.
