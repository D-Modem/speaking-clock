/*
 * speakingclock - the clock itself (see speakingclock.h)
 *
 * Copyright (C) 2026 D-Modem Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See LICENSE.
 *
 * A port of asterisk/app_speakingclock.c with Asterisk's APIs taken out.
 * One announcement every period, placed by sample against the system
 * clock, so the time mark (the third pip, the tone) falls exactly on the
 * second it announces, however long the call has been up.  Callers join
 * mid-announcement, as they did on the real thing, unless they ask to wait
 * for the next one.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "speakingclock.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

#define RATE SC_RATE
#define MAX_CLIPS 512
#define MAX_PIECES 32
#define NREND 4

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const struct sc_host *host;

static void sc_log(enum sc_log_level level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sc_log(enum sc_log_level level, const char *fmt, ...)
{
	char msg[1024];
	va_list ap;
	if (!host || !host->log) {
		return;
	}
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	host->log(level, msg);
}

static void copy(char *dst, const char *src, size_t len)
{
	if (len) {
		snprintf(dst, len, "%s", src ? src : "");
	}
}

static long long now_us(void)
{
	struct timeval tv;
	if (host && host->now_us) {
		return host->now_us();
	}
	gettimeofday(&tv, NULL);
	return (long long) tv.tv_sec * 1000000 + tv.tv_usec;
}

static void tm_get(long long t, const char *zone, struct sc_tm *tm)
{
	memset(tm, 0, sizeof(*tm));
	if (host && host->localtime && !host->localtime(t, zone, tm)) {
		return;
	} else {
		time_t tt = (time_t) t;
		struct tm l;
		localtime_r(&tt, &l);
		tm->hour = l.tm_hour;
		tm->min = l.tm_min;
		tm->sec = l.tm_sec;
	}
}

enum mode { MODE_TALKING, MODE_TIM, MODE_CHIME, MODE_HOURLY, MODE_TTS };

struct clip {
	char name[48];
	int16_t *s;
	size_t n;
};

struct voice {
	struct voice *next;     /* in the loaded list */
	int refs;               /* under voices_lock */
	char id[96];
	char name[128];
	enum mode mode;
	char sequence[256];
	int period;             /* seconds */
	double marker;          /* seconds into the pips of the time mark, <0: none */
	int int24;              /* 24-hour clock */
	char tts_command[512];  /* mode=tts: the program that speaks it */
	char tts_text[512];     /* mode=tts: what to say */
	size_t nclips;
	struct clip clips[];
};

/* configuration, under conf_lock */
static pthread_mutex_t conf_lock = PTHREAD_MUTEX_INITIALIZER;
static char voicedir[PATH_MAX];
static char default_voice[96] = "tim-pat-simmons";
static char default_zone[64];
static int maxduration = 180;
static int tts_timeout = 15;
#define TTS_PROGRAMS 32
/* tts_program_<name> */
static struct { char name[48]; char path[256]; } tts_programs[TTS_PROGRAMS];
static int ntts_programs;

/* loaded voices */
static pthread_mutex_t voices_lock = PTHREAD_MUTEX_INITIALIZER;
static struct voice *voices;

/* ---- configuration ---------------------------------------------------- */

void sc_config_begin(const char *default_voicedir)
{
	pthread_mutex_lock(&conf_lock);
	copy(voicedir, default_voicedir, sizeof(voicedir));
	copy(default_voice, "tim-pat-simmons", sizeof(default_voice));
	default_zone[0] = '\0';
	maxduration = 180;
	tts_timeout = 15;
	ntts_programs = 0;
	pthread_mutex_unlock(&conf_lock);
}

void sc_config_set(const char *key, const char *value)
{
	if (!key || !value) {
		return;
	}
	pthread_mutex_lock(&conf_lock);
	if (!strcasecmp(key, "voicedir")) {
		copy(voicedir, value, sizeof(voicedir));
	} else if (!strcasecmp(key, "default_voice")) {
		copy(default_voice, value, sizeof(default_voice));
	} else if (!strcasecmp(key, "timezone")) {
		copy(default_zone, value, sizeof(default_zone));
	} else if (!strcasecmp(key, "maxduration")) {
		maxduration = atoi(value);
	} else if (!strcasecmp(key, "tts_timeout")) {
		if (atoi(value) > 0) {
			tts_timeout = atoi(value);
		}
	} else if (!strncasecmp(key, "tts_program_", 12) && ntts_programs < TTS_PROGRAMS) {
		copy(tts_programs[ntts_programs].name, key + 4, sizeof(tts_programs[0].name));
		copy(tts_programs[ntts_programs].path, value, sizeof(tts_programs[0].path));
		ntts_programs++;
	}
	pthread_mutex_unlock(&conf_lock);
}

static void voice_unref(struct voice *v);

void sc_config_end(void)
{
	struct voice *v, *next;
	/* voices load again on next use */
	pthread_mutex_lock(&voices_lock);
	v = voices;
	voices = NULL;
	pthread_mutex_unlock(&voices_lock);
	for (; v; v = next) {
		next = v->next;
		voice_unref(v);
	}
}

int sc_maxduration(void)
{
	int d;
	pthread_mutex_lock(&conf_lock);
	d = maxduration;
	pthread_mutex_unlock(&conf_lock);
	return d;
}

void sc_voicedir(char *out, size_t len)
{
	pthread_mutex_lock(&conf_lock);
	copy(out, voicedir, len);
	pthread_mutex_unlock(&conf_lock);
}

/* ---- voices ----------------------------------------------------------- */

static void voice_free(struct voice *v)
{
	size_t i;
	for (i = 0; i < v->nclips; i++) {
		free(v->clips[i].s);
	}
	free(v);
}

static void voice_unref(struct voice *v)
{
	int last;
	if (!v) {
		return;
	}
	pthread_mutex_lock(&voices_lock);
	last = --v->refs == 0;
	pthread_mutex_unlock(&voices_lock);
	if (last) {
		voice_free(v);
	}
}

static const struct clip *clip_find(const struct voice *v, const char *name)
{
	size_t i;
	for (i = 0; i < v->nclips; i++) {
		if (!strcasecmp(v->clips[i].name, name)) {
			return &v->clips[i];
		}
	}
	return NULL;
}

static uint32_t le32(const unsigned char *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24;
}

/* An 8 kHz 16-bit mono PCM WAV file (what tools/build_voices.py makes). */
static int wav_load(const char *path, struct clip *c)
{
	FILE *f = fopen(path, "rb");
	unsigned char h[12], ch[8], fmt[16];
	int have_fmt = 0;

	if (!f) {
		return -1;
	}
	if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
		goto bad;
	}
	while (fread(ch, 1, 8, f) == 8) {
		uint32_t len = le32(ch + 4);
		if (!memcmp(ch, "fmt ", 4) && len >= 16) {
			if (fread(fmt, 1, 16, f) != 16) {
				goto bad;
			}
			if (fseek(f, len - 16 + (len & 1), SEEK_CUR)) {
				goto bad;
			}
			/* PCM, mono, 8000 Hz, 16 bits */
			if (fmt[0] != 1 || fmt[1] || fmt[2] != 1 || fmt[3] ||
			    le32(fmt + 4) != RATE || fmt[14] != 16) {
				sc_log(SC_LOG_WARNING, "%s: not 8 kHz 16-bit mono PCM, skipped", path);
				goto bad;
			}
			have_fmt = 1;
		} else if (!memcmp(ch, "data", 4) && have_fmt) {
			size_t i;
			unsigned char *raw = malloc(len ? len : 1);
			if (!raw) {
				goto bad;
			}
			len = fread(raw, 1, len, f);
			c->n = len / 2;
			c->s = malloc((c->n ? c->n : 1) * sizeof(int16_t));
			if (!c->s) {
				free(raw);
				goto bad;
			}
			for (i = 0; i < c->n; i++) {
				c->s[i] = (int16_t) (raw[2 * i] | raw[2 * i + 1] << 8);
			}
			free(raw);
			fclose(f);
			return 0;
		} else if (fseek(f, len + (len & 1), SEEK_CUR)) {
			goto bad;
		}
	}
bad:
	fclose(f);
	return -1;
}

static enum mode mode_parse(const char *s)
{
	if (!strcasecmp(s, "tim")) {
		return MODE_TIM;
	} else if (!strcasecmp(s, "chime")) {
		return MODE_CHIME;
	} else if (!strcasecmp(s, "hourly")) {
		return MODE_HOURLY;
	} else if (!strcasecmp(s, "tts")) {
		return MODE_TTS;
	}
	return MODE_TALKING;
}

static const char *mode_name(enum mode m)
{
	static const char *names[] = { "talking", "tim", "chime", "hourly", "tts" };
	return names[m];
}

/* A voice folder's id is its name: letters, digits, '-', '_' and '.' only,
 * so it can't climb out of voicedir. */
static int id_ok(const char *id)
{
	if (!id || !*id || id[0] == '.') {
		return 0;
	}
	for (; *id; id++) {
		if (!isalnum((unsigned char) *id) && !strchr("-_.", *id)) {
			return 0;
		}
	}
	return 1;
}

static char *strip(char *s)
{
	char *e;
	while (isspace((unsigned char) *s)) {
		s++;
	}
	e = s + strlen(s);
	while (e > s && isspace((unsigned char) e[-1])) {
		*--e = '\0';
	}
	return s;
}

/* voice.conf is in Asterisk's config format: [section], key=value (or
 * key => value), and ';' to the end of the line a comment ('\;' is a
 * semicolon).  cb(v, section, key, value) for each setting. */
static void conf_read(const char *path, struct voice *v,
	void (*cb)(struct voice *v, const char *section, const char *key, const char *value))
{
	char line[1024], section[64] = "";
	FILE *f = fopen(path, "r");

	if (!f) {
		return;
	}
	while (fgets(line, sizeof(line), f)) {
		char *r, *w, *s, *eq;
		/* comments out, \; to ; */
		for (r = w = line; *r && *r != '\n'; r++) {
			if (*r == '\\' && r[1] == ';') {
				*w++ = ';';
				r++;
			} else if (*r == ';') {
				break;
			} else {
				*w++ = *r;
			}
		}
		*w = '\0';
		s = strip(line);
		if (*s == '[') {
			char *close = strchr(s, ']');
			if (close) {
				*close = '\0';
				copy(section, strip(s + 1), sizeof(section));
			}
			continue;
		}
		if (!(eq = strchr(s, '='))) {
			continue;
		}
		*eq = '\0';
		if (eq[1] == '>') {
			eq++;
		}
		cb(v, section, strip(s), strip(eq + 1));
	}
	fclose(f);
}

static void voice_setting(struct voice *v, const char *section, const char *key, const char *value)
{
	if (!strcasecmp(section, "voice")) {
		if (!strcasecmp(key, "name")) {
			copy(v->name, value, sizeof(v->name));
		} else if (!strcasecmp(key, "mode")) {
			v->mode = mode_parse(value);
		} else if (!strcasecmp(key, "sequence")) {
			copy(v->sequence, value, sizeof(v->sequence));
		} else if (!strcasecmp(key, "period")) {
			v->period = atoi(value);
		} else if (!strcasecmp(key, "marker")) {
			v->marker = atof(value);
		} else if (!strcasecmp(key, "hours")) {
			v->int24 = atoi(value) == 24;
		}
	} else if (!strcasecmp(section, "tts")) {
		if (!strcasecmp(key, "command")) {
			copy(v->tts_command, value, sizeof(v->tts_command));
		} else if (!strcasecmp(key, "text")) {
			copy(v->tts_text, value, sizeof(v->tts_text));
		}
	}
}

static struct voice *voice_load(const char *dir, const char *id)
{
	char path[PATH_MAX + 512];
	struct voice *v;
	struct dirent *de;
	DIR *d;

	snprintf(path, sizeof(path), "%s/%s", dir, id);
	if (!(d = opendir(path))) {
		sc_log(SC_LOG_WARNING, "SpeakingClock: no voice '%s' in %s", id, dir);
		return NULL;
	}
	v = calloc(1, sizeof(*v) + MAX_CLIPS * sizeof(struct clip));
	if (!v) {
		closedir(d);
		return NULL;
	}
	copy(v->id, id, sizeof(v->id));
	copy(v->name, id, sizeof(v->name));
	v->marker = -1;
	v->period = 0;

	snprintf(path, sizeof(path), "%s/%s/voice.conf", dir, id);
	conf_read(path, v, voice_setting);

	while ((de = readdir(d)) && v->nclips < MAX_CLIPS) {
		size_t len = strlen(de->d_name);
		struct clip *c = &v->clips[v->nclips];
		if (len < 5 || len - 4 >= sizeof(c->name) || strcasecmp(de->d_name + len - 4, ".wav")) {
			continue;
		}
		snprintf(path, sizeof(path), "%s/%s/%s", dir, id, de->d_name);
		copy(c->name, de->d_name, len - 3);
		if (!wav_load(path, c)) {
			v->nclips++;
		}
	}
	closedir(d);

	if (!*v->sequence) {
		static const char *seq[] = {
			[MODE_TALKING] = "intro+its+hour+minute+ampm",
			[MODE_TIM] = "phrase+hour+minute+seconds+pips",
			[MODE_CHIME] = "quarter+strikes",
			[MODE_HOURLY] = "hourly",
			[MODE_TTS] = "speech+pips",
		};
		copy(v->sequence, seq[v->mode], sizeof(v->sequence));
	}
	if (v->period <= 0) {
		v->period = v->mode == MODE_CHIME ? 30 : 10;
	}
	if (v->mode == MODE_TTS) {
		if (!*v->tts_text) {
			copy(v->tts_text, "At the third stroke, it will be {hour} {minute} {seconds}.",
				sizeof(v->tts_text));
		}
		if (!*v->tts_command) {
			sc_log(SC_LOG_WARNING, "SpeakingClock: tts voice '%s' has no [tts] command=", id);
			voice_free(v);
			return NULL;
		}
	} else if (!v->nclips) {
		sc_log(SC_LOG_WARNING, "SpeakingClock: voice '%s' has no usable clips", id);
		voice_free(v);
		return NULL;
	}
	return v;
}

/* The voice, loaded on first use and kept until the config is reloaded. */
static struct voice *voice_get(const char *id)
{
	struct voice *v;
	char dir[PATH_MAX];

	if (!id_ok(id)) {
		sc_log(SC_LOG_WARNING, "SpeakingClock: bad voice name '%s'", id ? id : "");
		return NULL;
	}
	pthread_mutex_lock(&voices_lock);
	for (v = voices; v && strcmp(v->id, id); v = v->next) {
	}
	if (!v) {
		sc_voicedir(dir, sizeof(dir));
		if ((v = voice_load(dir, id))) {
			v->refs = 1;    /* the list's */
			v->next = voices;
			voices = v;
		}
	}
	if (v) {
		v->refs++;
	}
	pthread_mutex_unlock(&voices_lock);
	return v;
}

/* ---- live speech (mode=tts) -------------------------------------------- *
 * A tts voice has no number clips.  Each announcement's sentence is spoken
 * whole by a synthesiser program on this machine (DECtalk, SAM), converted
 * to 8 kHz, and cached.  A worker thread runs the program, so a call never
 * waits on it: the clock asks for an announcement two periods before it is
 * due, and one whose speech isn't ready in time plays without it (pips
 * only). */

#define SPEECH_SLOTS 128
#define SPEECH_RETRY 30          /* seconds before a failed fetch is tried again */
#define SPEECH_MAX (8 << 20)     /* largest WAV accepted */

enum speech_state { SP_EMPTY, SP_WANTED, SP_FETCHING, SP_READY, SP_FAILED };

struct speech {
	char *key;
	enum speech_state state;
	int16_t *s;
	size_t n;
	time_t used, failed;
	unsigned long seq;           /* order asked for */
};

static pthread_mutex_t speech_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t speech_cond;   /* work for the fetcher, or a fetch done */
static struct speech speech[SPEECH_SLOTS];
static unsigned long speech_seq;
static pthread_t speech_thread;
static int speech_running;
static int speech_quit;

/* The tts_<kind>_<name> setting, else def. */
static void tts_setting(const char *kind, const char *name, const char *def, char *out, size_t len)
{
	char key[48];
	int i;
	snprintf(key, sizeof(key), "%s_%.30s", kind, name);
	copy(out, def, len);
	pthread_mutex_lock(&conf_lock);
	for (i = 0; i < ntts_programs; i++) {
		if (!strcasecmp(tts_programs[i].name, key)) {
			copy(out, tts_programs[i].path, len);
		}
	}
	pthread_mutex_unlock(&conf_lock);
}

/* Windowed-sinc resampling to 8 kHz, with the cut-off below both Nyquists. */
static int16_t *resample(const float *x, size_t n, unsigned rate, size_t *out_n)
{
	double ratio = (double) rate / RATE, fc = 0.45 * MIN(1.0, 1.0 / ratio);
	int w = (int) ceil(12 / fc);
	size_t k, m = (size_t) (n / ratio);
	int16_t *y = malloc((m ? m : 1) * sizeof(int16_t));

	if (!y) {
		return NULL;
	}
	for (k = 0; k < m; k++) {
		double t = k * ratio, acc = 0;
		long i, c = (long) floor(t);
		for (i = c - w + 1; i <= c + w; i++) {
			double d = t - i, h;
			if (i < 0 || i >= (long) n) {
				continue;
			}
			h = d == 0 ? 2 * fc : sin(2 * M_PI * fc * d) / (M_PI * d);
			acc += x[i] * h * (0.5 + 0.5 * cos(M_PI * d / w));
		}
		acc = acc > 32767 ? 32767 : acc < -32768 ? -32768 : acc;
		y[k] = (int16_t) lrint(acc);
	}
	*out_n = m;
	return y;
}

/* A PCM WAV of any rate, 8 or 16 bits, mono or stereo: to 8 kHz mono,
 * with the silence around it trimmed (40 ms left each side) and its peak
 * brought to -3 dBFS, the way tools/synth_voices.py makes clips. */
static int16_t *speech_decode(const unsigned char *d, size_t len, size_t *out_n)
{
	const unsigned char *fmt = NULL, *data = NULL;
	size_t dlen = 0, pos = 12, i, frames, a, b, n;
	unsigned ch, rate, bits;
	float *x;
	int16_t *y;
	int peak = 0;

	if (len < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) {
		return NULL;
	}
	while (pos + 8 <= len) {
		size_t clen = le32(d + pos + 4);
		if (!memcmp(d + pos, "fmt ", 4) && clen >= 16 && pos + 8 + 16 <= len) {
			fmt = d + pos + 8;
		} else if (!memcmp(d + pos, "data", 4)) {
			data = d + pos + 8;
			dlen = MIN(clen, len - pos - 8);   /* streamed WAVs give no length */
			break;
		}
		pos += 8 + clen + (clen & 1);
	}
	if (!fmt || !data) {
		return NULL;
	}
	ch = fmt[2] | fmt[3] << 8;
	rate = le32(fmt + 4);
	bits = fmt[14] | fmt[15] << 8;
	if (!((fmt[0] == 1 && !fmt[1]) || (fmt[0] == 0xfe && fmt[1] == 0xff)) ||
	    ch < 1 || ch > 2 || (bits != 8 && bits != 16) || rate < 4000 || rate > 96000) {
		return NULL;
	}
	frames = dlen / (ch * bits / 8);
	if (!(x = malloc((frames ? frames : 1) * sizeof(float)))) {
		return NULL;
	}
	for (i = 0; i < frames; i++) {
		float v = 0;
		unsigned c;
		for (c = 0; c < ch; c++) {
			const unsigned char *p = data + (i * ch + c) * (bits / 8);
			v += bits == 8 ? (p[0] - 128) * 256 : (int16_t) (p[0] | p[1] << 8);
		}
		x[i] = v / ch;
	}
	if (rate == RATE) {
		n = frames;
		y = malloc((n ? n : 1) * sizeof(int16_t));
		for (i = 0; y && i < n; i++) {
			y[i] = (int16_t) x[i];
		}
	} else {
		y = resample(x, frames, rate, &n);
	}
	free(x);
	if (!y) {
		return NULL;
	}
	for (i = 0; i < n; i++) {
		peak = MAX(peak, abs(y[i]));
	}
	if (peak < 100) {
		free(y);
		return NULL;                /* silence */
	}
	for (a = 0; a < n && abs(y[a]) < peak / 30; a++) {
	}
	for (b = n; b > a && abs(y[b - 1]) < peak / 30; b--) {
	}
	a = a > RATE / 25 ? a - RATE / 25 : 0;
	b = MIN(n, b + RATE / 25);
	for (i = a; i < b; i++) {
		y[i - a] = (int16_t) (y[i] * 23197.0 / peak);
	}
	*out_n = b - a;
	return y;
}

/* Run a speech program: key is "exec:" and its arguments, split by \x1f.
 * It writes the WAV to the file named by the argument "{out}", or else to
 * its standard output.  No shell: the text is one argument. */
static int16_t *speech_exec(const char *key, size_t *n)
{
	char tmpl[] = "/tmp/speakingclock-XXXXXX.wav", *args, *argv[64], *a, *dup;
	int fd, argc = 0, status = -1, to_file = 0, timeout;
	posix_spawn_file_actions_t fa;
	unsigned char *d = NULL;
	int16_t *s = NULL;
	struct stat sb;
	time_t until;
	pid_t pid;

	if ((fd = mkstemps(tmpl, 4)) < 0) {
		return NULL;
	}
	if (!(dup = args = strdup(key + 5))) {
		close(fd);
		unlink(tmpl);
		return NULL;
	}
	while ((a = strsep(&args, "\x1f")) && argc < 63) {
		if (!strcmp(a, "{out}")) {
			argv[argc++] = tmpl;
			to_file = 1;
		} else {
			argv[argc++] = a;
		}
	}
	argv[argc] = NULL;
	pthread_mutex_lock(&conf_lock);
	timeout = tts_timeout;
	pthread_mutex_unlock(&conf_lock);
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
	if (to_file) {
		posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
	} else {
		posix_spawn_file_actions_adddup2(&fa, fd, 1);
	}
	posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
	if (argc && !posix_spawnp(&pid, argv[0], &fa, NULL, argv, environ)) {
		pid_t w;
		until = time(NULL) + timeout;
		while ((w = waitpid(pid, &status, WNOHANG)) == 0) {
			if (time(NULL) > until) {
				kill(pid, SIGKILL);
				waitpid(pid, &status, 0);
				sc_log(SC_LOG_WARNING, "SpeakingClock: %s took too long", argv[0]);
				status = -1;
				break;
			}
			usleep(20000);
		}
		if (w < 0) {
			sc_log(SC_LOG_WARNING, "SpeakingClock: can't wait for %s: %s (is SIGCHLD ignored?)",
				argv[0], strerror(errno));
			status = -1;
		}
	} else {
		sc_log(SC_LOG_WARNING, "SpeakingClock: can't run %s", argc ? argv[0] : "(nothing)");
	}
	posix_spawn_file_actions_destroy(&fa);
	if (status == 0 && !stat(tmpl, &sb) && sb.st_size > 0 && sb.st_size < SPEECH_MAX &&
	    (d = malloc(sb.st_size))) {
		FILE *f = fopen(tmpl, "rb");
		if (f && fread(d, 1, sb.st_size, f) == (size_t) sb.st_size &&
		    !(s = speech_decode(d, sb.st_size, n))) {
			sc_log(SC_LOG_WARNING, "SpeakingClock: %s: not a WAV file it can read", argv[0]);
		}
		if (f) {
			fclose(f);
		}
	} else if (status > 0) {
		sc_log(SC_LOG_WARNING, "SpeakingClock: %s failed (%d)", argv[0], status);
	}
	free(d);
	free(dup);
	close(fd);
	unlink(tmpl);
	return s;
}

static void *speech_worker(void *unused)
{
	(void) unused;
	pthread_mutex_lock(&speech_lock);
	while (!speech_quit) {
		struct speech *e = NULL;
		char *key;
		int16_t *s;
		size_t i, n = 0;

		for (i = 0; i < SPEECH_SLOTS; i++) {
			if (speech[i].state == SP_WANTED && (!e || speech[i].seq < e->seq)) {
				e = &speech[i];
			}
		}
		if (!e) {
			pthread_cond_wait(&speech_cond, &speech_lock);
			continue;
		}
		e->state = SP_FETCHING;
		key = strdup(e->key);
		pthread_mutex_unlock(&speech_lock);
		s = key ? speech_exec(key, &n) : NULL;
		pthread_mutex_lock(&speech_lock);
		/* the slot is never reused while it is fetching */
		e->s = s;
		e->n = n;
		e->state = s ? SP_READY : SP_FAILED;
		e->failed = time(NULL);
		free(key);
		pthread_cond_broadcast(&speech_cond);
	}
	pthread_mutex_unlock(&speech_lock);
	return NULL;
}

enum { SPEECH_PEEK, SPEECH_ASK, SPEECH_WAIT };

/* The speech for key, copied into *s (the caller frees it): 0 when it's
 * here, -1 when it isn't (yet).  SPEECH_ASK asks for it if it hasn't been
 * asked for, SPEECH_WAIT also waits up to the fetch timeout for it, and
 * SPEECH_PEEK only looks. */
static int speech_get(const char *key, int16_t **s, size_t *n, int how)
{
	struct speech *e = NULL, *victim = NULL;
	time_t now = time(NULL);
	struct timespec until;
	size_t i;
	int res = -1, timeout;

	pthread_mutex_lock(&conf_lock);
	timeout = tts_timeout;
	pthread_mutex_unlock(&conf_lock);
	clock_gettime(CLOCK_REALTIME, &until);
	until.tv_sec += timeout + 5;

	pthread_mutex_lock(&speech_lock);
	for (i = 0; i < SPEECH_SLOTS && !e; i++) {
		if (speech[i].state != SP_EMPTY && !strcmp(speech[i].key, key)) {
			e = &speech[i];
		}
	}
	if (!e && how != SPEECH_PEEK) {
		/* an empty slot, else the one least recently used */
		for (i = 0; i < SPEECH_SLOTS; i++) {
			struct speech *c = &speech[i];
			if (c->state == SP_FETCHING) {
				continue;
			}
			if (c->state == SP_EMPTY) {
				victim = c;
				break;
			}
			if (!victim || c->used < victim->used) {
				victim = c;
			}
		}
		if (victim) {
			free(victim->key);
			free(victim->s);
			memset(victim, 0, sizeof(*victim));
			if ((victim->key = strdup(key))) {
				e = victim;
				e->state = SP_WANTED;
				e->seq = ++speech_seq;
				pthread_cond_broadcast(&speech_cond);
			}
		}
	} else if (e && how != SPEECH_PEEK && e->state == SP_FAILED && now - e->failed >= SPEECH_RETRY) {
		e->state = SP_WANTED;
		e->seq = ++speech_seq;
		pthread_cond_broadcast(&speech_cond);
	}
	while (e && how == SPEECH_WAIT && speech_running &&
	       (e->state == SP_WANTED || e->state == SP_FETCHING)) {
		if (pthread_cond_timedwait(&speech_cond, &speech_lock, &until) == ETIMEDOUT) {
			break;
		}
	}
	if (e && e->key && strcmp(e->key, key)) {
		e = NULL;                   /* reused for another while we waited */
	}
	if (e) {
		e->used = now;
		if (e->state == SP_READY && (*s = malloc((e->n ? e->n : 1) * sizeof(int16_t)))) {
			memcpy(*s, e->s, e->n * sizeof(int16_t));
			*n = e->n;
			res = 0;
		}
	}
	pthread_mutex_unlock(&speech_lock);
	return res;
}

int sc_init(const struct sc_host *h)
{
	host = h;
	pthread_cond_init(&speech_cond, NULL);
	speech_quit = 0;
	if (pthread_create(&speech_thread, NULL, speech_worker, NULL)) {
		sc_log(SC_LOG_ERROR, "SpeakingClock: can't start the speech fetcher");
		speech_running = 0;
		return -1;
	}
	speech_running = 1;
	return 0;
}

void sc_shutdown(void)
{
	size_t i;
	if (speech_running) {
		pthread_mutex_lock(&speech_lock);
		speech_quit = 1;
		pthread_cond_broadcast(&speech_cond);
		pthread_mutex_unlock(&speech_lock);
		pthread_join(speech_thread, NULL);
		speech_running = 0;
	}
	for (i = 0; i < SPEECH_SLOTS; i++) {
		free(speech[i].key);
		free(speech[i].s);
		memset(&speech[i], 0, sizeof(speech[i]));
	}
	pthread_cond_destroy(&speech_cond);
	sc_config_end();
}

/* ---- what a tts voice says ---- */

/* a growing string */
struct str {
	char *s;
	size_t len, cap;
};

static void str_add(struct str *b, const char *s, size_t n)
{
	if (b->len + n + 1 > b->cap) {
		size_t cap = MAX(b->cap * 2, b->len + n + 64);
		char *p = realloc(b->s, cap);
		if (!p) {
			return;
		}
		b->s = p;
		b->cap = cap;
	}
	memcpy(b->s + b->len, s, n);
	b->len += n;
	b->s[b->len] = '\0';
}

static void str_puts(struct str *b, const char *s)
{
	str_add(b, s, strlen(s));
}

static const char *ones[] = { "zero", "one", "two", "three", "four", "five", "six",
	"seven", "eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen",
	"fifteen", "sixteen", "seventeen", "eighteen", "nineteen" };
static const char *tens_w[] = { "", "", "twenty", "thirty", "forty", "fifty" };

static void words(int n, char *out, size_t len)
{
	if (n < 20) {
		copy(out, ones[n], len);
	} else if (n % 10) {
		snprintf(out, len, "%s-%s", tens_w[n / 10], ones[n % 10]);
	} else {
		copy(out, tens_w[n / 10], len);
	}
}

/* The template with {hour} {minute} {seconds} {ampm} {greeting} in words,
 * and {H} {HH} {M} {MM} {S} {SS} in digits, filled in. */
static void tts_text(const char *tpl, const struct sc_tm *tm, int int24, char *out, size_t len)
{
	struct str s = { 0 };
	const char *p;

	for (p = tpl; *p; p++) {
		char key[16], w[48];
		const char *close = *p == '{' ? strchr(p, '}') : NULL;
		if (!close || close - p - 1 >= (ptrdiff_t) sizeof(key)) {
			str_add(&s, p, 1);
			continue;
		}
		copy(key, p + 1, close - p);
		p = close;
		w[0] = '\0';
		if (!strcmp(key, "hour")) {
			words(int24 ? tm->hour : tm->hour % 12 ? tm->hour % 12 : 12, w, sizeof(w));
		} else if (!strcmp(key, "minute")) {
			if (!tm->min) {
				copy(w, int24 ? "hundred" : "o'clock", sizeof(w));
			} else if (tm->min < 10) {
				snprintf(w, sizeof(w), "oh %s", ones[tm->min]);
			} else {
				words(tm->min, w, sizeof(w));
			}
		} else if (!strcmp(key, "seconds")) {
			if (!tm->sec) {
				copy(w, "precisely", sizeof(w));
			} else {
				char n[24];
				words(tm->sec, n, sizeof(n));
				snprintf(w, sizeof(w), "and %s second%s", n, tm->sec == 1 ? "" : "s");
			}
		} else if (!strcmp(key, "ampm")) {
			copy(w, int24 ? "" : tm->hour < 12 ? "A.M." : "P.M.", sizeof(w));
		} else if (!strcmp(key, "greeting")) {
			copy(w, tm->hour < 12 ? "Good morning" :
				tm->hour < 18 ? "Good afternoon" : "Good evening", sizeof(w));
		} else if (!strcmp(key, "H") || !strcmp(key, "HH")) {
			int h = int24 ? tm->hour : tm->hour % 12 ? tm->hour % 12 : 12;
			snprintf(w, sizeof(w), key[1] ? "%02d" : "%d", h);
		} else if (!strcmp(key, "M") || !strcmp(key, "MM")) {
			snprintf(w, sizeof(w), key[1] ? "%02d" : "%d", tm->min);
		} else if (!strcmp(key, "S") || !strcmp(key, "SS")) {
			snprintf(w, sizeof(w), key[1] ? "%02d" : "%d", tm->sec);
		} else {
			snprintf(w, sizeof(w), "{%s}", key);
		}
		str_puts(&s, w);
	}
	copy(out, s.s, len);
	free(s.s);
}

/* The key that runs v's command= for text: its words, split like a shell
 * would (with "..." and '...'), {program:name} becoming tts_program_<name>
 * (else name), {text} the text and {out} the file to write. */
static void tts_cmd(const struct voice *v, const char *text, char *out, size_t len)
{
	struct str s = { 0 };
	const char *p = v->tts_command;
	int args = 0;

	str_puts(&s, "exec:");
	while (*p) {
		char q = 0;
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		if (!*p) {
			break;
		}
		if (args++) {
			str_puts(&s, "\x1f");
		}
		for (; *p && (q || (*p != ' ' && *p != '\t')); p++) {
			if (!q && (*p == '"' || *p == '\'')) {
				q = *p;
			} else if (q && *p == q) {
				q = 0;
			} else if (!strncmp(p, "{text}", 6)) {
				str_puts(&s, text);
				p += 5;
			} else if (!strncmp(p, "{program:", 9) && strchr(p, '}')) {
				const char *close = strchr(p, '}');
				char name[48], prog[256];
				copy(name, p + 9, MIN(sizeof(name), (size_t) (close - p - 8)));
				tts_setting("program", name, name, prog, sizeof(prog));
				str_puts(&s, prog);
				p = close;
			} else {
				str_add(&s, p, 1);
			}
		}
	}
	copy(out, s.s, len);
	free(s.s);
}

/* ---- composing an announcement ---------------------------------------- */

struct piece {
	const int16_t *s;
	size_t n;
	int is_pips;
};

struct ann {
	long long j;            /* announcement index: its time is j * period */
	long long start;        /* first sample, counted from the epoch */
	int16_t *s;
	size_t n;
	int used;
	int pending;            /* its speech hadn't arrived: render it again */
	long long tried;        /* when it was last rendered (st->pos) */
};

struct sc_clock {
	struct voice *v;
	char zone[64];
	char sequence[256];
	int period;
	int int24;
	int wait;               /* 'w' */
	long long pos;          /* next sample to send, counted from the epoch */
	long long min_start;    /* 'w': skip announcements starting before this */
	long long last_sync;
	int wait_speech;        /* rendering: fetch speech before going on */
	struct ann r[NREND];
	int16_t pips[RATE * 3]; /* made-up pips, for voices without a pips clip */
};

struct say {
	const struct voice *v;
	struct piece p[MAX_PIECES];
	int np;
	int16_t *own[MAX_PIECES];  /* speech, freed after rendering */
	int nown;
	int pending;               /* speech that hasn't arrived yet */
	int how;                   /* SPEECH_PEEK, _ASK or _WAIT */
};

static int add(struct say *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static int add(struct say *s, const char *fmt, ...)
{
	char name[48];
	const struct clip *c;
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(name, sizeof(name), fmt, ap);
	va_end(ap);
	if (s->np >= MAX_PIECES || !(c = clip_find(s->v, name))) {
		return -1;
	}
	s->p[s->np].s = c->s;
	s->p[s->np].n = c->n;
	s->p[s->np].is_pips = !strcasecmp(name, "pips");
	s->np++;
	return 0;
}

static int has(const struct voice *v, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static int has(const struct voice *v, const char *fmt, ...)
{
	char name[48];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(name, sizeof(name), fmt, ap);
	va_end(ap);
	return clip_find(v, name) != NULL;
}

/* 0..59: a whole-number clip, or built from parts the way Steve's
 * Talking Clock voices are ("thir" + "teen", "for" + "ty5", "20" + "5") */
static void say_number(struct say *s, int n)
{
	static const char *pre[] = { [13] = "thir", [14] = "for", [15] = "fif",
		[16] = "six", [17] = "seven", [18] = "eight", [19] = "nine" };
	static const char *tens[] = { [2] = "twen", [3] = "thir", [4] = "for", [5] = "fif" };

	if (!add(s, "n%d", n)) {
		return;
	}
	if (n >= 13 && n <= 19) {
		if (add(s, "f_%s", pre[n])) {
			add(s, "n%d", n - 10);
		}
		add(s, "f_teen");
	} else if (n >= 20 && n < 60) {
		int t = n / 10, u = n % 10;
		if (has(s->v, "f_ty")) {
			add(s, "f_%s", tens[t]);
			if (u) {
				add(s, "f_ty%d", u);
			} else {
				add(s, "f_ty");
			}
		} else {
			add(s, "n%d", t * 10);
			if (u) {
				add(s, "n%d", u);
			}
		}
	}
}

static void say_minute(struct say *s, int m)
{
	if (m == 0) {
		add(s, "oclock");
	} else if (!add(s, "m%d", m)) {
		return;
	} else if (m < 10) {
		add(s, "oh");
		say_number(s, m);
	} else {
		say_number(s, m);
	}
}

static void say_hour(struct say *s, int h, int int24)
{
	if (!int24) {
		h = h % 12 ? h % 12 : 12;
		if (!add(s, "h%d", h)) {
			return;
		}
	}
	say_number(s, h);
}

static void compose(struct say *s, const char *sequence, const struct sc_tm *tm, int int24)
{
	char seq[256], *tok, *rest;
	int minute_done = 0;

	copy(seq, sequence, sizeof(seq));
	rest = seq;
	while ((tok = strsep(&rest, "+"))) {
		tok = strip(tok);
		if (!strcasecmp(tok, "greeting")) {
			add(s, "greet_%s", tm->hour < 12 ? "morning" :
				tm->hour < 18 ? "afternoon" : "evening");
		} else if (!strcasecmp(tok, "hour")) {
			int h12 = tm->hour % 12 ? tm->hour % 12 : 12;
			if (!tm->min && !int24 && tm->hour == 12 && !add(s, "noon")) {
				minute_done = 2;
			} else if (!tm->min && !int24 && tm->hour == 0 && !add(s, "midnight")) {
				minute_done = 2;
			} else if (!tm->min && !int24 && !add(s, "hoc%d", h12)) {
				minute_done = 1;
			} else {
				say_hour(s, tm->hour, int24);
			}
		} else if (!strcasecmp(tok, "minute")) {
			if (!minute_done) {
				say_minute(s, tm->min);
			}
		} else if (!strcasecmp(tok, "seconds")) {
			if (tm->sec == 0) {
				add(s, "precisely");
			} else {
				add(s, "s%d", tm->sec);
			}
		} else if (!strcasecmp(tok, "ampm")) {
			if (!int24 && minute_done != 2) {
				add(s, tm->hour < 12 ? "am" : "pm");
			}
		} else if (!strcasecmp(tok, "quarter")) {
			add(s, "q%02d", tm->min / 15 * 15);
		} else if (!strcasecmp(tok, "strikes")) {
			int i, h12 = tm->hour % 12 ? tm->hour % 12 : 12;
			if (tm->min < 15) {
				for (i = 0; i < h12; i++) {
					add(s, "chime");
				}
			}
		} else if (!strcasecmp(tok, "hourly")) {
			add(s, "hr%d", tm->hour % 12 ? tm->hour % 12 : 12);
		} else if (!strcasecmp(tok, "hourchime")) {
			if (tm->min == 0) {
				add(s, "hourchime1");
			}
		} else if (!strcasecmp(tok, "speech")) {
			char text[512], key[2048];
			int16_t *sp;
			size_t n;
			if (s->v->mode != MODE_TTS || s->np >= MAX_PIECES || s->nown >= MAX_PIECES) {
				continue;
			}
			tts_text(s->v->tts_text, tm, int24, text, sizeof(text));
			tts_cmd(s->v, text, key, sizeof(key));
			if (speech_get(key, &sp, &n, s->how)) {
				s->pending = 1;
				continue;
			}
			s->own[s->nown++] = sp;
			s->p[s->np].s = sp;
			s->p[s->np].n = n;
			s->p[s->np].is_pips = 0;
			s->np++;
		} else if (!strcasecmp(tok, "pips")) {
			if (add(s, "pips") && s->np < MAX_PIECES) {
				s->p[s->np].s = NULL;   /* made-up pips */
				s->p[s->np].n = RATE * 2 + RATE / 10;
				s->p[s->np].is_pips = 2;
				s->np++;
			}
		} else if (*tok) {
			add(s, "%s", tok);      /* any clip by name: its, intro, phrase, ... */
		}
	}
}

/* three 0.1 s 1 kHz pips a second apart; the time mark is the third */
static void make_pips(int16_t *p)
{
	int i, k;
	for (k = 0; k < 3; k++) {
		for (i = 0; i < RATE / 10; i++) {
			p[k * RATE + i] = (int16_t) (8000 * sin(2 * M_PI * 1000 * i / RATE));
		}
	}
}

static long long floordiv(long long a, long long b)
{
	return a >= 0 ? a / b : -((-a + b - 1) / b);
}

/* Render announcement j into r: the time it gives is j * period, and it
 * starts so that its time mark (the third pip, if the voice has one) falls
 * on that second exactly; without a mark it starts on that second. */
static void render(sc_clock *st, struct ann *r, long long j)
{
	struct say s = { .v = st->v };
	struct sc_tm tm;
	size_t i, total = 0, at = 0, mark = 0;
	int marked = 0;

	/* speech is fetched only for announcements that can still start in
	 * time: one whose mark is less than 8 s off (about a sentence and the
	 * pips) uses it only if it's already here */
	s.how = st->wait_speech ? SPEECH_WAIT :
		j * st->period * (long long) RATE < st->pos + 8 * RATE ? SPEECH_PEEK : SPEECH_ASK;
	tm_get(j * st->period, st->zone, &tm);
	compose(&s, st->sequence, &tm, st->int24);
	for (i = 0; i < (size_t) s.np; i++) {
		if (s.p[i].is_pips && !marked) {
			if (s.p[i].is_pips == 2) {
				mark = total + 2 * RATE;
				marked = 1;
			} else if (st->v->marker >= 0) {
				mark = total + (size_t) llround(st->v->marker * RATE);
				marked = 1;
			}
		}
		total += s.p[i].n;
	}

	free(r->s);
	r->j = j;
	r->used = 1;
	r->pending = s.pending;
	r->tried = st->pos;
	r->n = total;
	r->start = j * st->period * (long long) RATE - (marked ? (long long) mark : 0);
	r->s = calloc(total ? total : 1, sizeof(int16_t));
	if (!r->s) {
		r->n = 0;
		total = 0;
	}
	for (i = 0; r->s && i < (size_t) s.np; i++) {
		const int16_t *src = s.p[i].is_pips == 2 ? st->pips : s.p[i].s;
		memcpy(r->s + at, src, s.p[i].n * sizeof(int16_t));
		at += s.p[i].n;
	}
	for (i = 0; i < (size_t) s.nown; i++) {
		free(s.own[i]);
	}
}

static struct ann *ann_get(sc_clock *st, long long j)
{
	int i, oldest = 0;
	for (i = 0; i < NREND; i++) {
		if (st->r[i].used && st->r[i].j == j) {
			struct ann *r = &st->r[i];
			/* speech that was missing may be here now: try again, at most
			 * every 200 ms, while the announcement hasn't started, and keep
			 * the new one only if it can still start on time */
			if (r->pending && r->start > st->pos && st->pos - r->tried >= RATE / 5) {
				struct ann t = { 0 };
				render(st, &t, j);
				if (t.start >= st->pos) {
					free(r->s);
					*r = t;
				} else {
					free(t.s);
					r->tried = st->pos;
				}
			}
			return r;
		}
		if (!st->r[i].used || st->r[i].j < st->r[oldest].j) {
			oldest = i;
		}
	}
	render(st, &st->r[oldest], j);
	return &st->r[oldest];
}

/* Fill out[0..n) with the samples from st->pos on: every announcement that
 * overlaps them, mixed. */
void sc_clock_fill(sc_clock *st, int16_t *out, int n)
{
	long long ps = (long long) st->period * RATE, from = st->pos, to;
	long long j, j0, j1;
	int32_t mix[RATE];
	int i;

	if (n > RATE) {
		n = RATE;
	}
	to = from + n;
	j0 = floordiv(from, ps) - 1;
	j1 = floordiv(to, ps) + 2;
	memset(mix, 0, sizeof(int32_t) * n);
	for (j = j0; j <= j1; j++) {
		struct ann *r = ann_get(st, j);
		long long a = MAX(from, r->start), b = MIN(to, r->start + (long long) r->n);
		if (r->start < st->min_start) {
			continue;
		}
		for (; a < b; a++) {
			mix[a - from] += r->s[a - r->start];
		}
	}
	for (i = 0; i < n; i++) {
		out[i] = mix[i] > 32767 ? 32767 : mix[i] < -32768 ? -32768 : mix[i];
	}
	st->pos += n;
}

static long long now_samples(void)
{
	long long us = now_us();
	return floordiv(us, 1000000) * RATE + (us - floordiv(us, 1000000) * 1000000) * RATE / 1000000;
}

void sc_clock_play(sc_clock *st, int16_t *out, int n)
{
	long long now = now_samples();
	/* stay on the system clock: if the channel's timing has drifted more
	 * than 20 ms, jump (the old machines were run off a master clock too) */
	if (now - st->last_sync > RATE) {
		if (llabs(now - st->pos) > RATE / 50) {
			sc_log(SC_LOG_DEBUG, "SpeakingClock: %lld samples off, resynced", now - st->pos);
			st->pos = now;
		}
		st->last_sync = now;
	}
	sc_clock_fill(st, out, n);
}

void sc_clock_start(sc_clock *st)
{
	st->pos = st->last_sync = now_samples();
	st->min_start = st->wait ? st->pos : LLONG_MIN;
}

void sc_clock_render_from(sc_clock *st, long long t, const char *zone)
{
	if (zone && *zone) {
		copy(st->zone, zone, sizeof(st->zone));
	}
	st->pos = st->last_sync = t * RATE;
	st->min_start = LLONG_MIN;
	st->wait_speech = 1;
}

void sc_clock_free(sc_clock *st)
{
	int i;
	if (!st) {
		return;
	}
	for (i = 0; i < NREND; i++) {
		free(st->r[i].s);
	}
	voice_unref(st->v);
	free(st);
}

sc_clock *sc_clock_new(const char *voice_id, const struct sc_options *o)
{
	sc_clock *st = calloc(1, sizeof(*st));
	char id[96];

	if (!st) {
		return NULL;
	}
	pthread_mutex_lock(&conf_lock);
	copy(id, voice_id && *voice_id ? voice_id : default_voice, sizeof(id));
	copy(st->zone, default_zone, sizeof(st->zone));
	pthread_mutex_unlock(&conf_lock);
	if (!(st->v = voice_get(id))) {
		free(st);
		return NULL;
	}
	copy(st->sequence, st->v->sequence, sizeof(st->sequence));
	st->period = st->v->period;
	st->int24 = st->v->int24;
	if (o) {
		if (*o->zone) {
			copy(st->zone, o->zone, sizeof(st->zone));
		}
		if (*o->sequence) {
			copy(st->sequence, o->sequence, sizeof(st->sequence));
		}
		if (o->period > 0) {
			st->period = o->period;
		}
		if (o->int24) {
			st->int24 = 1;
		}
		st->wait = o->wait;
	}
	make_pips(st->pips);
	sc_clock_start(st);
	return st;
}

/* ---- options ---------------------------------------------------------- */

/* "t(Europe/London)d(300)hw": the letters SpeakingClock() takes */
int sc_options_parse(const char *s, struct sc_options *o)
{
	memset(o, 0, sizeof(*o));
	for (; s && *s; s++) {
		char opt = *s, arg[256] = "";
		if (isspace((unsigned char) opt) || opt == ',') {
			continue;
		}
		if (s[1] == '(') {
			const char *close = strchr(s + 2, ')');
			if (!close) {
				return -1;
			}
			copy(arg, s + 2, MIN(sizeof(arg), (size_t) (close - s - 1)));
			s = close;
		}
		switch (opt) {
		case 'd':
			o->has_duration = 1;
			o->duration = atoi(arg);
			break;
		case 'h':
			o->int24 = 1;
			break;
		case 'p':
			o->period = atoi(arg);
			break;
		case 's':
			copy(o->sequence, arg, sizeof(o->sequence));
			break;
		case 't':
			copy(o->zone, arg, sizeof(o->zone));
			break;
		case 'w':
			o->wait = 1;
			break;
		case 'x':
			o->exit_on_key = 1;
			break;
		default:
			sc_log(SC_LOG_WARNING, "SpeakingClock: unknown option '%c'", opt);
			return -1;
		}
	}
	return 0;
}

/* ---- listing ---------------------------------------------------------- */

int sc_voices_list(void (*cb)(void *arg, const char *id, const char *mode, const char *name),
	void *arg)
{
	char dir[PATH_MAX];
	struct dirent **list;
	int i, n, count = 0;

	sc_voicedir(dir, sizeof(dir));
	if ((n = scandir(dir, &list, NULL, alphasort)) < 0) {
		return -1;
	}
	for (i = 0; i < n; i++) {
		struct voice *v;
		char p[PATH_MAX + 512];
		struct stat sb;
		snprintf(p, sizeof(p), "%s/%s", dir, list[i]->d_name);
		if (id_ok(list[i]->d_name) && !stat(p, &sb) && S_ISDIR(sb.st_mode) &&
		    (v = voice_get(list[i]->d_name))) {
			cb(arg, v->id, mode_name(v->mode), v->name);
			voice_unref(v);
			count++;
		}
		free(list[i]);
	}
	free(list);
	return count;
}
