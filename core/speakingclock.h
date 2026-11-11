/*
 * speakingclock - the clock itself, for the FreeSWITCH module (and anything
 * else that wants it): voices, announcements placed by sample against the
 * system clock, and live speech from a synthesiser program.
 *
 * Copyright (C) 2026 D-Modem Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * It works the way app_speakingclock does (asterisk/), and reads the same
 * voice folders (docs/voice-format.md).  It has no dependencies but libc,
 * libm and pthreads: the program it runs in gives it a log and a clock in
 * a time zone (struct sc_host).
 */

#ifndef SPEAKINGCLOCK_H
#define SPEAKINGCLOCK_H

#include <stddef.h>
#include <stdint.h>

#define SC_RATE 8000            /* everything is 8 kHz, 16-bit, mono */

enum sc_log_level { SC_LOG_DEBUG, SC_LOG_NOTICE, SC_LOG_WARNING, SC_LOG_ERROR };

struct sc_tm {
	int hour, min, sec;
};

struct sc_host {
	/* one line, without a newline */
	void (*log)(enum sc_log_level level, const char *msg);
	/* the time t (seconds since the epoch) in zone ("" or NULL: the
	 * system's); 0 on success */
	int (*localtime)(long long t, const char *zone, struct sc_tm *tm);
	/* microseconds since the epoch; NULL: gettimeofday() */
	long long (*now_us)(void);
};

/* Start (the speech worker) and stop.  sc_init() keeps the pointer. */
int sc_init(const struct sc_host *host);
void sc_shutdown(void);

/* Configuration: begin resets every setting to its default, set takes one
 * (voicedir, default_voice, timezone, maxduration, tts_timeout,
 * tts_program_<name>), and end drops the loaded voices, so they load
 * again on next use.  Unknown keys are ignored. */
void sc_config_begin(const char *default_voicedir);
void sc_config_set(const char *key, const char *value);
void sc_config_end(void);
int sc_maxduration(void);

/* The options SpeakingClock() takes, in the same letters:
 * d(seconds) h p(seconds) s(seq) t(zone) w x */
struct sc_options {
	int has_duration;
	int duration;
	int int24;
	int period;             /* 0: the voice's */
	char sequence[256];     /* "": the voice's */
	char zone[64];          /* "": timezone= in the config, else the system's */
	int wait;
	int exit_on_key;
};
/* 0, or -1 for options it doesn't know */
int sc_options_parse(const char *s, struct sc_options *o);

typedef struct sc_clock sc_clock;

/* A clock playing voice (NULL or "": default_voice), or NULL if there is
 * no such voice. */
sc_clock *sc_clock_new(const char *voice, const struct sc_options *o);
void sc_clock_free(sc_clock *c);
/* Put the clock on the system's time now (call it when the call is
 * answered and audio is about to flow).  With the 'w' option, nothing
 * that started before now is played. */
void sc_clock_start(sc_clock *c);
/* The next n samples (n <= SC_RATE) for a call: it stays on the system
 * clock, jumping back to it if it drifts by more than 20 ms. */
void sc_clock_play(sc_clock *c, int16_t *out, int n);
/* Rendering without a call: from t (seconds since the epoch), with live
 * speech waited for, so nothing is missing.  Then sc_clock_fill(). */
void sc_clock_render_from(sc_clock *c, long long t, const char *zone);
void sc_clock_fill(sc_clock *c, int16_t *out, int n);

/* Every voice in voicedir, sorted: cb(arg, id, mode, name).  The number
 * of voices, or -1 if voicedir can't be read. */
int sc_voices_list(void (*cb)(void *arg, const char *id, const char *mode, const char *name),
	void *arg);
/* The voicedir setting, copied to out. */
void sc_voicedir(char *out, size_t len);

#endif
