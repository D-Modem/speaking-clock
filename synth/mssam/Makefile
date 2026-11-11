# SPDX-License-Identifier: GPL-2.0-or-later
#   make                    build mssam
#   make voices             fetch the Microsoft Sam, Mike and Mary voice data (an 8 MB download)
#   make install            install the program and the voices
#   make test               speak a sentence with every voice
PREFIX ?= /usr/local
VOICEDIR ?= $(PREFIX)/share/mssam
CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
SOURCES ?= ../../sources/mssam

mssam: mssam.c lexicon.h
	$(CC) $(CFLAGS) -DVOICEDIR='"$(VOICEDIR)"' -o $@ mssam.c -lm

voices: $(SOURCES)/sam.inv

$(SOURCES)/sam.inv:
	python3 ../../tools/fetch-mssam.py $(SOURCES)

install: mssam voices
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(VOICEDIR)
	install -m 755 mssam $(DESTDIR)$(PREFIX)/bin/mssam
	install -m 644 $(SOURCES)/*.inv $(SOURCES)/*.tree $(SOURCES)/*.tones $(DESTDIR)$(VOICEDIR)/
	if [ -f $(SOURCES)/cmudict.dict ]; then install -m 644 $(SOURCES)/cmudict.dict $(DESTDIR)$(VOICEDIR)/; fi

test: mssam voices
	@for v in sam mike mary; do \
		./mssam -d $(SOURCES) -v $$v -o /tmp/mssam-$$v.wav \
			"At the third stroke, it will be ten oh five and twenty seconds." && \
		echo "/tmp/mssam-$$v.wav"; \
	done

clean:
	rm -f mssam

.PHONY: voices install test clean
