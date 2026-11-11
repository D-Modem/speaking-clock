# SPDX-License-Identifier: GPL-2.0-or-later
# mod_speakingclock.so, built against an installed FreeSWITCH's headers
# (libfreeswitch-dev, or a source install's include/freeswitch), found with
# pkg-config.
#
#   make                build mod_speakingclock.so
#   make install        install the module, speakingclock.conf.xml and the voices
#   make FS_INCLUDE=/usr/local/freeswitch/include/freeswitch \
#        MODULES_DIR=... CONF_DIR=... SOUNDS_DIR=...

PC := $(shell pkg-config --exists freeswitch 2>/dev/null && echo freeswitch)
FS_INCLUDE ?= $(if $(PC),,$(firstword $(wildcard /usr/include/freeswitch \
	/usr/local/freeswitch/include/freeswitch) /usr/include/freeswitch))
FS_CFLAGS ?= $(if $(PC),$(shell pkg-config --cflags freeswitch),-I$(FS_INCLUDE))
FS_LIBS ?= $(if $(PC),$(shell pkg-config --libs freeswitch))
pcvar = $(if $(PC),$(shell pkg-config --variable=$(1) freeswitch))
MODULES_DIR ?= $(firstword $(call pcvar,modulesdir) $(wildcard /usr/lib/freeswitch/mod \
	/usr/local/freeswitch/mod) /usr/lib/freeswitch/mod)
CONF_DIR ?= $(firstword $(call pcvar,confdir) $(wildcard /etc/freeswitch \
	/usr/local/freeswitch/conf) /etc/freeswitch)
SOUNDS_DIR ?= $(firstword $(call pcvar,soundsdir) $(wildcard /usr/share/freeswitch/sounds \
	/usr/local/freeswitch/sounds) /usr/share/freeswitch/sounds)

CFLAGS ?= -g -O2
CFLAGS += -Wall -fPIC -pthread -D_GNU_SOURCE $(FS_CFLAGS) -I../core

SRC = mod_speakingclock.c ../core/speakingclock.c

all: mod_speakingclock.so

mod_speakingclock.so: $(SRC) ../core/speakingclock.h
	$(CC) $(CFLAGS) -shared -o $@ $(SRC) $(FS_LIBS) -lm

install: mod_speakingclock.so
	install -d $(DESTDIR)$(MODULES_DIR) $(DESTDIR)$(CONF_DIR)/autoload_configs \
		$(DESTDIR)$(SOUNDS_DIR)/speaking-clock
	install -m 755 mod_speakingclock.so $(DESTDIR)$(MODULES_DIR)/
	test -e $(DESTDIR)$(CONF_DIR)/autoload_configs/speakingclock.conf.xml || \
		install -m 644 speakingclock.conf.xml \
		$(DESTDIR)$(CONF_DIR)/autoload_configs/speakingclock.conf.xml
	cp -R ../voices/. $(DESTDIR)$(SOUNDS_DIR)/speaking-clock/
	install -m 644 ../NOTICE.md $(DESTDIR)$(SOUNDS_DIR)/speaking-clock/

clean:
	rm -f mod_speakingclock.so

.PHONY: all install clean
