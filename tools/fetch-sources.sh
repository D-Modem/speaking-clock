#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Download the voice sources that tools/build_voices.py turns into voices/.
# Usage: tools/fetch-sources.sh [DIR]   (default: ./sources)
# Needs curl, git, unzip and 7z (p7zip) or innoextract for the installers.
# Nothing downloaded is run: the Windows installers are only unpacked.
set -eu
DIR=${1:-sources}
mkdir -p "$DIR/steve/dl" "$DIR/steve/x"

# 1. Steve's Talking Clock voices (http://clock.steve-audio.net/voices/)
STEVE=http://clock.steve-audio.net/voices/
curl -fsSL "$STEVE" | grep -o 'href="[^"]*"' | sed 's/^href="//; s/"$//' |
	grep -E '\.(exe|zip)$' | while read -r url; do
	name=$(printf '%s' "${url##*/}" | python3 -c 'import sys,urllib.parse;print(urllib.parse.unquote(sys.stdin.read()))')
	[ -s "$DIR/steve/dl/$name" ] && continue
	echo "fetch $name"
	curl -fsSL -o "$DIR/steve/dl/$name" "$url"
done
for f in "$DIR"/steve/dl/*; do
	base=$(basename "$f"); out="$DIR/steve/x/${base%.*}"
	[ -d "$out" ] && continue
	mkdir -p "$out"
	case "$f" in
	*.zip) unzip -oq "$f" -d "$out" ;;
	*.exe) 7z x -y -o"$out" "$f" >/dev/null 2>&1 ||
		innoextract -s -d "$out" "$f" >/dev/null ;;
	esac
done

# 2. garrysblog/Speaking-Clock-MP3-Files
[ -d "$DIR/garry" ] ||
	git clone -q --depth 1 https://github.com/garrysblog/Speaking-Clock-MP3-Files.git "$DIR/garry"

# 3. paulseward/asterisk-tim
[ -d "$DIR/asterisk-tim" ] ||
	git clone -q --depth 1 https://github.com/paulseward/asterisk-tim.git "$DIR/asterisk-tim"

echo "sources in $DIR"
