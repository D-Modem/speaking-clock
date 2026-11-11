# SPDX-License-Identifier: GPL-2.0-or-later
#   make                    build macintalk
#   make voices             fetch Apple's MacInTalk 3 voice data (about 50 MB download)
#   make install            install the program and the voices
#   make test               speak a sentence with every voice
PREFIX ?= /usr/local
VOICEDIR ?= $(PREFIX)/share/macintalk
CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wno-missing-field-initializers
SOURCES ?= ../../sources/macintalk

macintalk: macintalk.c rules.h
	$(CC) $(CFLAGS) -o $@ macintalk.c -lm

voices: $(SOURCES)/fred.ttvd

$(SOURCES)/fred.ttvd:
	python3 ../../tools/fetch-macintalk.py $(SOURCES)

install: macintalk voices
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(VOICEDIR)
	install -m 755 macintalk $(DESTDIR)$(PREFIX)/bin/macintalk
	install -m 644 $(SOURCES)/*.ttvd $(SOURCES)/*.ttvw $(DESTDIR)$(VOICEDIR)/

test: macintalk voices
	@for v in $$(./macintalk -d $(SOURCES) -l | cut -d' ' -f1); do \
		./macintalk -d $(SOURCES) -v $$v -o /tmp/macintalk-$$v.wav \
			"At the third stroke, it will be ten oh five and twenty seconds." && \
		echo "/tmp/macintalk-$$v.wav"; \
	done

clean:
	rm -f macintalk

.PHONY: voices install test clean
