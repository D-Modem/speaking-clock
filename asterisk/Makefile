# SPDX-License-Identifier: GPL-2.0-or-later
# app_speakingclock.so, built against the headers of an installed Asterisk
# (make install-headers, or a distribution's asterisk-dev package).
#
#   make                build app_speakingclock.so
#   make install        install the module, the sample config and the voices
#   make ASTERISK_INCLUDE=/opt/asterisk/include MODULES_DIR=... SOUNDS_DIR=...

ASTERISK_INCLUDE ?= /usr/include
MULTIARCH := $(shell gcc -print-multiarch 2>/dev/null)
MODULES_DIR ?= $(firstword $(shell pkg-config --variable=modulesdir asterisk 2>/dev/null) \
	$(wildcard /usr/lib/$(MULTIARCH)/asterisk/modules) /usr/lib/asterisk/modules)
CONF_DIR ?= /etc/asterisk
SOUNDS_DIR ?= /var/lib/asterisk/sounds
CFLAGS ?= -g -O2
CFLAGS += -Wall -fPIC -pthread -D_GNU_SOURCE -I$(ASTERISK_INCLUDE) \
	-DAST_MODULE=\"app_speakingclock\" \
	-DAST_MODULE_SELF_SYM=__internal_app_speakingclock_self
# Clang: Asterisk's utils.h declares RAII_VAR's cleanup with a block type
ifneq ($(findstring clang,$(shell $(CC) --version 2>/dev/null)),)
CFLAGS += -fblocks
endif

all: app_speakingclock.so

app_speakingclock.so: app_speakingclock.c
	$(CC) $(CFLAGS) -shared -o $@ $< -lm

install: app_speakingclock.so
	install -d $(DESTDIR)$(MODULES_DIR) $(DESTDIR)$(CONF_DIR) \
		$(DESTDIR)$(SOUNDS_DIR)/speaking-clock
	install -m 755 app_speakingclock.so $(DESTDIR)$(MODULES_DIR)/
	test -e $(DESTDIR)$(CONF_DIR)/speakingclock.conf || \
		install -m 644 speakingclock.conf.sample \
		$(DESTDIR)$(CONF_DIR)/speakingclock.conf
	cp -R ../voices/. $(DESTDIR)$(SOUNDS_DIR)/speaking-clock/
	install -m 644 ../NOTICE.md $(DESTDIR)$(SOUNDS_DIR)/speaking-clock/

clean:
	rm -f app_speakingclock.so

.PHONY: all install clean
