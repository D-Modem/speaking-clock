# SPDX-License-Identifier: GPL-2.0-or-later
# sclock-render: what a speaking clock caller would hear, without a PBX.
#
#   make
#   ./sclock-render -v ../voices tim-pat-simmons 1762791295 30 out.sln Europe/London
#   ./sclock-render -v ../voices -l

CFLAGS ?= -g -O2
CFLAGS += -Wall -pthread

all: sclock-render

sclock-render: sclock-render.c speakingclock.c speakingclock.h
	$(CC) $(CFLAGS) -o $@ sclock-render.c speakingclock.c -lm

clean:
	rm -f sclock-render

.PHONY: all clean
