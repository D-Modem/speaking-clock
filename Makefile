# SPDX-License-Identifier: GPL-2.0-or-later
# Builds and installs everything: the PBX modules, our synthesisers and the
# voices.
#
#   make                  build the modules for the PBXs found here, the
#                         synthesisers, sclock-render, and fetch the
#                         Microsoft and Apple voice data
#   sudo make install     install the modules, their configs, voices/, and
#                         the synthesisers with their voice data
#   make test             render 30 seconds of a voice with sclock-render
#   make clean            remove what make built (keeps sources/)
#   make distclean        also remove the downloaded voice data
#
# Settings:
#   PBX="asterisk freeswitch"   which modules to build (default: the PBXs
#                               whose headers are installed)
#   FETCH=no                    don't download the voice data; the live
#                               mssam, macintalk and plaintalk voices then
#                               won't speak until it is fetched
#   SYNTH=no                    skip synth/mssam and synth/macintalk
#   PREFIX=/usr/local           where the synthesisers go
#   DESTDIR=...                 staging root, for packaging
# and the sub-makes' own: ASTERISK_INCLUDE, FS_INCLUDE, MODULES_DIR,
# CONF_DIR, SOUNDS_DIR (see asterisk/Makefile and freeswitch/Makefile).
#
# Microsoft's and Apple's voice files are not in this repository: they are
# downloaded from the Internet Archive into sources/ (tools/fetch-mssam.py,
# tools/fetch-macintalk.py). The PlainTalk voices need unar or unrar, and
# the Microsoft voices cabextract or 7z.

PREFIX ?= /usr/local
FETCH ?= yes
SYNTH ?= yes

# PBXs whose headers are installed
HAVE_ASTERISK := $(if $(ASTERISK_INCLUDE),$(wildcard $(ASTERISK_INCLUDE)/asterisk.h),$(wildcard /usr/include/asterisk.h))
HAVE_FREESWITCH := $(or $(shell pkg-config --exists freeswitch 2>/dev/null && echo yes), \
	$(if $(FS_INCLUDE),$(wildcard $(FS_INCLUDE)/switch.h)), \
	$(wildcard /usr/include/freeswitch/switch.h /usr/local/freeswitch/include/freeswitch/switch.h))
PBX ?= $(if $(HAVE_ASTERISK),asterisk) $(if $(HAVE_FREESWITCH),freeswitch)
PBX := $(strip $(PBX))

SYNTHS := $(if $(filter yes,$(SYNTH)),mssam macintalk)
VOICEDATA := $(if $(filter yes,$(FETCH)),$(addprefix voices-,$(SYNTHS)))
SUBMAKE = $(MAKE) PREFIX=$(PREFIX)

all: check-pbx $(PBX) $(SYNTHS) core $(VOICEDATA)

check-pbx:
	@if [ -z "$(PBX)" ]; then \
		echo "No Asterisk or FreeSWITCH headers found: building only the synthesisers"; \
		echo "and sclock-render. Install asterisk-dev or libfreeswitch-dev, or set"; \
		echo "PBX=asterisk / PBX=freeswitch with ASTERISK_INCLUDE= / FS_INCLUDE=."; \
	else echo "Building for: $(PBX)"; fi

asterisk freeswitch core:
	$(MAKE) -C $@

mssam macintalk:
	$(SUBMAKE) -C synth/$@

voices-mssam voices-macintalk:
	$(SUBMAKE) -C synth/$(@:voices-%=%) voices

# voices/ goes into each PBX's sounds directory with its module
install: all $(addprefix install-,$(PBX)) $(if $(VOICEDATA),$(addprefix install-,$(SYNTHS)),$(addprefix install-bin-,$(SYNTHS)))

install-asterisk install-freeswitch:
	$(MAKE) -C $(@:install-%=%) install

install-mssam install-macintalk:
	$(SUBMAKE) -C synth/$(@:install-%=%) install

# FETCH=no: the program only; its voice data can be installed later
install-bin-mssam install-bin-macintalk:
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 synth/$(@:install-bin-%=%)/$(@:install-bin-%=%) $(DESTDIR)$(PREFIX)/bin/

test: core
	cd core && ./sclock-render -v ../voices tim-pat-simmons 1762791295 30 /tmp/sclock-test.sln Europe/London
	@echo "/tmp/sclock-test.sln (8 kHz signed linear)"

clean:
	$(MAKE) -C asterisk clean
	$(MAKE) -C freeswitch clean
	$(MAKE) -C core clean
	$(MAKE) -C synth/mssam clean
	$(MAKE) -C synth/macintalk clean

distclean: clean
	rm -rf sources/mssam sources/macintalk

.PHONY: all check-pbx asterisk freeswitch core mssam macintalk \
	voices-mssam voices-macintalk install install-asterisk install-freeswitch \
	install-mssam install-macintalk install-bin-mssam install-bin-macintalk \
	test clean distclean
