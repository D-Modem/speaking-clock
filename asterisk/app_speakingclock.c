/*
 * SpeakingClock() - a telephone speaking clock for Asterisk
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
 * Plays the time from a voice folder (voices/, docs/voice-format.md) the
 * way the old machines did: one announcement every period, placed by
 * sample against the system clock, so the time mark (the third pip, the
 * tone) falls exactly on the second it announces, however long the call
 * has been up.  Callers join mid-announcement, as they did on the real
 * thing, unless they ask to wait for the next one.
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <dirent.h>
#include <sys/stat.h>
#include <math.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/localtime.h"
#include "asterisk/paths.h"
#include "asterisk/frame.h"
#include "asterisk/format_cache.h"
#include "asterisk/astobj2.h"
#include "asterisk/utils.h"
#include "asterisk/strings.h"
#include "asterisk/lock.h"

/*** DOCUMENTATION
	<application name="SpeakingClock" language="en_US">
		<synopsis>
			Announce the time like a telephone company speaking clock.
		</synopsis>
		<syntax>
			<parameter name="voice">
				<para>Folder name of the voice under <literal>voicedir</literal>
				(see speakingclock.conf). Defaults to <literal>default_voice</literal>.</para>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="d">
						<argument name="seconds" required="true" />
						<para>Hang up after this many seconds (0: never). Default
						<literal>maxduration</literal>.</para>
					</option>
					<option name="h">
						<para>24-hour clock.</para>
					</option>
					<option name="p">
						<argument name="seconds" required="true" />
						<para>Seconds between announcements, instead of the voice's.</para>
					</option>
					<option name="s">
						<argument name="sequence" required="true" />
						<para>What to say, instead of the voice's sequence: clip
						names and words joined by <literal>+</literal>.</para>
					</option>
					<option name="t">
						<argument name="zone" required="true" />
						<para>Time zone, e.g. <literal>Europe/London</literal>.</para>
					</option>
					<option name="w">
						<para>Wait in silence for the next announcement to start
						instead of joining the current one part way through.</para>
					</option>
					<option name="x">
						<para>Return when the caller presses a key.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Answers the channel and plays the time until the caller
			hangs up or the duration runs out.</para>
		</description>
	</application>
 ***/

#define RATE 8000
#define CONF "speakingclock.conf"
#define MAX_CLIPS 512
#define MAX_PIECES 32
#define NREND 4

static const char app[] = "SpeakingClock";

enum mode { MODE_TALKING, MODE_TIM, MODE_CHIME, MODE_HOURLY };

struct clip {
	char name[48];
	int16_t *s;
	size_t n;
};

struct voice {
	char id[96];
	char name[128];
	enum mode mode;
	char sequence[256];
	int period;          /* seconds */
	double marker;       /* seconds into the pips of the time mark, <0: none */
	int int24;           /* 24-hour clock */
	size_t nclips;
	struct clip clips[];
};

/* configuration, under conf_lock */
AST_MUTEX_DEFINE_STATIC(conf_lock);
static char voicedir[PATH_MAX];
static char default_voice[96] = "tim-pat-simmons";
static char default_zone[64];
static int maxduration = 180;
static struct ao2_container *voices;     /* loaded voices, by id */

/* ---- voices ----------------------------------------------------------- */

static int voice_cmp(void *obj, void *arg, int flags)
{
	struct voice *v = obj;
	const char *key = (flags & OBJ_SEARCH_KEY) ? arg : ((struct voice *) arg)->id;
	return strcmp(v->id, key) ? 0 : CMP_MATCH | CMP_STOP;
}

static void voice_destroy(void *obj)
{
	struct voice *v = obj;
	size_t i;
	for (i = 0; i < v->nclips; i++) {
		ast_free(v->clips[i].s);
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
				ast_log(LOG_WARNING, "%s: not 8 kHz 16-bit mono PCM, skipped\n", path);
				goto bad;
			}
			have_fmt = 1;
		} else if (!memcmp(ch, "data", 4) && have_fmt) {
			size_t i;
			unsigned char *raw = ast_malloc(len ? len : 1);
			if (!raw) {
				goto bad;
			}
			len = fread(raw, 1, len, f);
			c->n = len / 2;
			c->s = ast_malloc((c->n ? c->n : 1) * sizeof(int16_t));
			if (!c->s) {
				ast_free(raw);
				goto bad;
			}
			for (i = 0; i < c->n; i++) {
				c->s[i] = (int16_t) (raw[2 * i] | raw[2 * i + 1] << 8);
			}
			ast_free(raw);
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
	}
	return MODE_TALKING;
}

static const char *mode_name(enum mode m)
{
	static const char *names[] = { "talking", "tim", "chime", "hourly" };
	return names[m];
}

/* A voice folder's id is its name: letters, digits, '-', '_' and '.' only,
 * so it can't climb out of voicedir. */
static int id_ok(const char *id)
{
	if (ast_strlen_zero(id) || id[0] == '.') {
		return 0;
	}
	for (; *id; id++) {
		if (!isalnum((unsigned char) *id) && !strchr("-_.", *id)) {
			return 0;
		}
	}
	return 1;
}

static struct voice *voice_load(const char *dir, const char *id)
{
	char path[PATH_MAX + 512];
	struct ast_flags cfg_flags = { 0 };
	struct ast_config *cfg;
	struct ast_variable *var;
	struct voice *v;
	struct dirent *de;
	DIR *d;

	snprintf(path, sizeof(path), "%s/%s", dir, id);
	if (!(d = opendir(path))) {
		ast_log(LOG_WARNING, "SpeakingClock: no voice '%s' in %s\n", id, dir);
		return NULL;
	}
	v = ao2_alloc(sizeof(*v) + MAX_CLIPS * sizeof(struct clip), voice_destroy);
	if (!v) {
		closedir(d);
		return NULL;
	}
	ast_copy_string(v->id, id, sizeof(v->id));
	ast_copy_string(v->name, id, sizeof(v->name));
	v->marker = -1;
	v->period = 0;

	snprintf(path, sizeof(path), "%s/%s/voice.conf", dir, id);
	cfg = ast_config_load2(path, "app_speakingclock", cfg_flags);
	if (cfg && cfg != CONFIG_STATUS_FILEINVALID) {
		for (var = ast_variable_browse(cfg, "voice"); var; var = var->next) {
			if (!strcasecmp(var->name, "name")) {
				ast_copy_string(v->name, var->value, sizeof(v->name));
			} else if (!strcasecmp(var->name, "mode")) {
				v->mode = mode_parse(var->value);
			} else if (!strcasecmp(var->name, "sequence")) {
				ast_copy_string(v->sequence, var->value, sizeof(v->sequence));
			} else if (!strcasecmp(var->name, "period")) {
				v->period = atoi(var->value);
			} else if (!strcasecmp(var->name, "marker")) {
				v->marker = atof(var->value);
			} else if (!strcasecmp(var->name, "hours")) {
				v->int24 = atoi(var->value) == 24;
			}
		}
		ast_config_destroy(cfg);
	}

	while ((de = readdir(d)) && v->nclips < MAX_CLIPS) {
		size_t len = strlen(de->d_name);
		struct clip *c = &v->clips[v->nclips];
		if (len < 5 || len - 4 >= sizeof(c->name) || strcasecmp(de->d_name + len - 4, ".wav")) {
			continue;
		}
		snprintf(path, sizeof(path), "%s/%s/%s", dir, id, de->d_name);
		ast_copy_string(c->name, de->d_name, len - 3);
		if (!wav_load(path, c)) {
			v->nclips++;
		}
	}
	closedir(d);

	if (ast_strlen_zero(v->sequence)) {
		static const char *seq[] = {
			[MODE_TALKING] = "intro+its+hour+minute+ampm",
			[MODE_TIM] = "phrase+hour+minute+seconds+pips",
			[MODE_CHIME] = "quarter+strikes",
			[MODE_HOURLY] = "hourly",
		};
		ast_copy_string(v->sequence, seq[v->mode], sizeof(v->sequence));
	}
	if (v->period <= 0) {
		v->period = v->mode == MODE_CHIME ? 30 : 10;
	}
	if (!v->nclips) {
		ast_log(LOG_WARNING, "SpeakingClock: voice '%s' has no usable clips\n", id);
		ao2_ref(v, -1);
		return NULL;
	}
	return v;
}

/* The voice, loaded on first use and kept until reload. */
static struct voice *voice_get(const char *id)
{
	struct voice *v;
	char dir[PATH_MAX];

	if (!id_ok(id)) {
		ast_log(LOG_WARNING, "SpeakingClock: bad voice name '%s'\n", id);
		return NULL;
	}
	ao2_lock(voices);
	v = ao2_find(voices, id, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (!v) {
		ast_mutex_lock(&conf_lock);
		ast_copy_string(dir, voicedir, sizeof(dir));
		ast_mutex_unlock(&conf_lock);
		if ((v = voice_load(dir, id))) {
			ao2_link_flags(voices, v, OBJ_NOLOCK);
		}
	}
	ao2_unlock(voices);
	return v;
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
};

struct clock_state {
	struct voice *v;
	char zone[64];
	char sequence[256];
	int period;
	int int24;
	long long pos;          /* next sample to send, counted from the epoch */
	long long min_start;    /* 'w': skip announcements starting before this */
	long long last_sync;
	struct ann r[NREND];
	int16_t pips[RATE * 3];  /* made-up pips, for voices without a pips clip */
	int16_t buf[AST_FRIENDLY_OFFSET / 2 + RATE];
};

struct say {
	const struct voice *v;
	struct piece p[MAX_PIECES];
	int np;
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

static void compose(struct say *s, const char *sequence, const struct ast_tm *tm, int int24)
{
	char seq[256], *tok, *rest;
	int minute_done = 0;

	ast_copy_string(seq, sequence, sizeof(seq));
	rest = seq;
	while ((tok = strsep(&rest, "+"))) {
		tok = ast_strip(tok);
		if (!strcasecmp(tok, "greeting")) {
			add(s, "greet_%s", tm->tm_hour < 12 ? "morning" :
				tm->tm_hour < 18 ? "afternoon" : "evening");
		} else if (!strcasecmp(tok, "hour")) {
			int h12 = tm->tm_hour % 12 ? tm->tm_hour % 12 : 12;
			if (!tm->tm_min && !int24 && tm->tm_hour == 12 && !add(s, "noon")) {
				minute_done = 2;
			} else if (!tm->tm_min && !int24 && tm->tm_hour == 0 && !add(s, "midnight")) {
				minute_done = 2;
			} else if (!tm->tm_min && !int24 && !add(s, "hoc%d", h12)) {
				minute_done = 1;
			} else {
				say_hour(s, tm->tm_hour, int24);
			}
		} else if (!strcasecmp(tok, "minute")) {
			if (!minute_done) {
				say_minute(s, tm->tm_min);
			}
		} else if (!strcasecmp(tok, "seconds")) {
			if (tm->tm_sec == 0) {
				add(s, "precisely");
			} else {
				add(s, "s%d", tm->tm_sec);
			}
		} else if (!strcasecmp(tok, "ampm")) {
			if (!int24 && minute_done != 2) {
				add(s, tm->tm_hour < 12 ? "am" : "pm");
			}
		} else if (!strcasecmp(tok, "quarter")) {
			add(s, "q%02d", tm->tm_min / 15 * 15);
		} else if (!strcasecmp(tok, "strikes")) {
			int i, h12 = tm->tm_hour % 12 ? tm->tm_hour % 12 : 12;
			if (tm->tm_min < 15) {
				for (i = 0; i < h12; i++) {
					add(s, "chime");
				}
			}
		} else if (!strcasecmp(tok, "hourly")) {
			add(s, "hr%d", tm->tm_hour % 12 ? tm->tm_hour % 12 : 12);
		} else if (!strcasecmp(tok, "hourchime")) {
			if (tm->tm_min == 0) {
				add(s, "hourchime1");
			}
		} else if (!strcasecmp(tok, "pips")) {
			if (add(s, "pips") && s->np < MAX_PIECES) {
				s->p[s->np].s = NULL;   /* made-up pips */
				s->p[s->np].n = RATE * 2 + RATE / 10;
				s->p[s->np].is_pips = 2;
				s->np++;
			}
		} else if (!ast_strlen_zero(tok)) {
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
static void render(struct clock_state *st, struct ann *r, long long j)
{
	struct say s = { .v = st->v };
	struct timeval tv = { .tv_sec = j * st->period };
	struct ast_tm tm;
	size_t i, total = 0, at = 0, mark = 0;
	int marked = 0;

	ast_localtime(&tv, &tm, ast_strlen_zero(st->zone) ? NULL : st->zone);
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

	ast_free(r->s);
	r->j = j;
	r->used = 1;
	r->n = total;
	r->start = j * st->period * (long long) RATE - (marked ? (long long) mark : 0);
	r->s = ast_calloc(total ? total : 1, sizeof(int16_t));
	if (!r->s) {
		r->n = 0;
		return;
	}
	for (i = 0; i < (size_t) s.np; i++) {
		const int16_t *src = s.p[i].is_pips == 2 ? st->pips : s.p[i].s;
		memcpy(r->s + at, src, s.p[i].n * sizeof(int16_t));
		at += s.p[i].n;
	}
}

static struct ann *ann_get(struct clock_state *st, long long j)
{
	int i, oldest = 0;
	for (i = 0; i < NREND; i++) {
		if (st->r[i].used && st->r[i].j == j) {
			return &st->r[i];
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
static void fill(struct clock_state *st, int16_t *out, int n)
{
	long long ps = (long long) st->period * RATE, from = st->pos, to = st->pos + n;
	long long j, j0 = floordiv(from, ps) - 1, j1 = floordiv(to, ps) + 2;
	int32_t mix[RATE];
	int i;

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
	struct timeval tv = ast_tvnow();
	return (long long) tv.tv_sec * RATE + tv.tv_usec * (long long) RATE / 1000000;
}

static void state_free(struct clock_state *st)
{
	int i;
	if (!st) {
		return;
	}
	for (i = 0; i < NREND; i++) {
		ast_free(st->r[i].s);
	}
	ao2_cleanup(st->v);
	ast_free(st);
}

/* ---- the generator ---------------------------------------------------- */

static void *gen_alloc(struct ast_channel *chan, void *params)
{
	return params;
}

static void gen_release(struct ast_channel *chan, void *data)
{
	/* the state belongs to the application, which frees it */
}

static int gen_generate(struct ast_channel *chan, void *data, int len, int samples)
{
	struct clock_state *st = data;
	struct ast_frame f = {
		.frametype = AST_FRAME_VOICE,
		.src = app,
		.offset = AST_FRIENDLY_OFFSET,
	};
	int16_t *out = st->buf + AST_FRIENDLY_OFFSET / 2;
	long long now = now_samples();

	if (samples > RATE) {
		samples = RATE;
	}
	/* stay on the system clock: if the channel's timing has drifted more
	 * than 20 ms, jump (the old machines were run off a master clock too) */
	if (now - st->last_sync > RATE) {
		if (llabs(now - st->pos) > RATE / 50) {
			ast_debug(1, "SpeakingClock: %lld samples off, resynced\n", now - st->pos);
			st->pos = now;
		}
		st->last_sync = now;
	}
	fill(st, out, samples);

	f.subclass.format = ast_format_slin;
	f.data.ptr = out;
	f.datalen = samples * sizeof(int16_t);
	f.samples = samples;
	return ast_write(chan, &f) < 0 ? -1 : 0;
}

static struct ast_generator clock_gen = {
	.alloc = gen_alloc,
	.release = gen_release,
	.generate = gen_generate,
};

/* ---- the application -------------------------------------------------- */

enum {
	OPT_DURATION = (1 << 0),
	OPT_24H = (1 << 1),
	OPT_PERIOD = (1 << 2),
	OPT_SEQUENCE = (1 << 3),
	OPT_ZONE = (1 << 4),
	OPT_WAIT = (1 << 5),
	OPT_EXIT = (1 << 6),
};

enum {
	OPT_ARG_DURATION,
	OPT_ARG_PERIOD,
	OPT_ARG_SEQUENCE,
	OPT_ARG_ZONE,
	OPT_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(clock_opts, {
	AST_APP_OPTION_ARG('d', OPT_DURATION, OPT_ARG_DURATION),
	AST_APP_OPTION('h', OPT_24H),
	AST_APP_OPTION_ARG('p', OPT_PERIOD, OPT_ARG_PERIOD),
	AST_APP_OPTION_ARG('s', OPT_SEQUENCE, OPT_ARG_SEQUENCE),
	AST_APP_OPTION_ARG('t', OPT_ZONE, OPT_ARG_ZONE),
	AST_APP_OPTION('w', OPT_WAIT),
	AST_APP_OPTION('x', OPT_EXIT),
});

static struct clock_state *state_new(const char *voice_id, struct ast_flags *flags, char **opt)
{
	struct clock_state *st = ast_calloc(1, sizeof(*st));
	if (!st) {
		return NULL;
	}
	if (!(st->v = voice_get(voice_id))) {
		ast_free(st);
		return NULL;
	}
	ast_mutex_lock(&conf_lock);
	ast_copy_string(st->zone, default_zone, sizeof(st->zone));
	ast_mutex_unlock(&conf_lock);
	ast_copy_string(st->sequence, st->v->sequence, sizeof(st->sequence));
	st->period = st->v->period;
	st->int24 = st->v->int24;
	if (flags && ast_test_flag(flags, OPT_ZONE) && !ast_strlen_zero(opt[OPT_ARG_ZONE])) {
		ast_copy_string(st->zone, opt[OPT_ARG_ZONE], sizeof(st->zone));
	}
	if (flags && ast_test_flag(flags, OPT_SEQUENCE) && !ast_strlen_zero(opt[OPT_ARG_SEQUENCE])) {
		ast_copy_string(st->sequence, opt[OPT_ARG_SEQUENCE], sizeof(st->sequence));
	}
	if (flags && ast_test_flag(flags, OPT_PERIOD) && atoi(opt[OPT_ARG_PERIOD]) > 0) {
		st->period = atoi(opt[OPT_ARG_PERIOD]);
	}
	if (flags && ast_test_flag(flags, OPT_24H)) {
		st->int24 = 1;
	}
	make_pips(st->pips);
	st->pos = st->last_sync = now_samples();
	st->min_start = flags && ast_test_flag(flags, OPT_WAIT) ? st->pos : LLONG_MIN;
	return st;
}

static int clock_exec(struct ast_channel *chan, const char *data)
{
	char *parse, *opt[OPT_ARG_ARRAY_SIZE] = { NULL };
	struct ast_flags flags = { 0 };
	struct clock_state *st;
	struct timeval end = { 0 };
	int duration, res = 0;
	char voice_id[96];
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(voice);
		AST_APP_ARG(options);
	);

	parse = ast_strdupa(S_OR(data, ""));
	AST_STANDARD_APP_ARGS(args, parse);
	if (!ast_strlen_zero(args.options) &&
	    ast_app_parse_options(clock_opts, &flags, opt, args.options)) {
		return -1;
	}
	ast_mutex_lock(&conf_lock);
	ast_copy_string(voice_id, S_OR(args.voice, default_voice), sizeof(voice_id));
	duration = maxduration;
	ast_mutex_unlock(&conf_lock);
	if (ast_test_flag(&flags, OPT_DURATION)) {
		duration = atoi(S_OR(opt[OPT_ARG_DURATION], "0"));
	}

	if (!(st = state_new(voice_id, &flags, opt))) {
		return -1;
	}
	if (ast_channel_state(chan) != AST_STATE_UP && ast_answer(chan)) {
		state_free(st);
		return -1;
	}
	if (ast_set_write_format(chan, ast_format_slin)) {
		ast_log(LOG_WARNING, "SpeakingClock: %s can't take signed linear\n",
			ast_channel_name(chan));
		state_free(st);
		return -1;
	}
	st->pos = st->last_sync = now_samples();
	if (ast_test_flag(&flags, OPT_WAIT)) {
		st->min_start = st->pos;
	}
	if (ast_activate_generator(chan, &clock_gen, st)) {
		state_free(st);
		return -1;
	}
	if (duration > 0) {
		end = ast_tvadd(ast_tvnow(), ast_samp2tv(duration, 1));
	}
	for (;;) {
		struct ast_frame *f;
		int ms = 1000;
		if (duration > 0) {
			long left = ast_tvdiff_ms(end, ast_tvnow());
			if (left <= 0) {
				break;
			}
			ms = MIN(ms, left);
		}
		if ((res = ast_waitfor(chan, ms)) < 0) {
			break;
		}
		if (!res) {
			continue;
		}
		if (!(f = ast_read(chan))) {
			res = -1;
			break;
		}
		if (f->frametype == AST_FRAME_DTMF_END && ast_test_flag(&flags, OPT_EXIT)) {
			ast_frfree(f);
			res = 0;
			break;
		}
		ast_frfree(f);
	}
	ast_deactivate_generator(chan);
	state_free(st);
	return res < 0 ? -1 : 0;
}

/* ---- CLI -------------------------------------------------------------- */

static char *cli_voices(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char dir[PATH_MAX];
	struct dirent **list;
	int i, n;

	switch (cmd) {
	case CLI_INIT:
		e->command = "speakingclock show voices";
		e->usage = "Usage: speakingclock show voices\n"
			"       List the voice folders in voicedir.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	ast_mutex_lock(&conf_lock);
	ast_copy_string(dir, voicedir, sizeof(dir));
	ast_mutex_unlock(&conf_lock);
	if ((n = scandir(dir, &list, NULL, alphasort)) < 0) {
		ast_cli(a->fd, "Can't read %s\n", dir);
		return CLI_FAILURE;
	}
	ast_cli(a->fd, "%-36s %-8s %s\n", "Voice", "Mode", "Name");
	for (i = 0; i < n; i++) {
		struct voice *v;
		char p[PATH_MAX + 512];
		struct stat sb;
		snprintf(p, sizeof(p), "%s/%s", dir, list[i]->d_name);
		if (id_ok(list[i]->d_name) && !stat(p, &sb) && S_ISDIR(sb.st_mode) &&
		    (v = voice_get(list[i]->d_name))) {
			ast_cli(a->fd, "%-36s %-8s %s\n", v->id, mode_name(v->mode), v->name);
			ao2_ref(v, -1);
		}
		ast_std_free(list[i]);
	}
	ast_std_free(list);
	return CLI_SUCCESS;
}

/* Write what a caller would hear, without a call: for checking a voice. */
static char *cli_render(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct clock_state *st;
	long long start;
	int seconds, i;
	FILE *f;

	switch (cmd) {
	case CLI_INIT:
		e->command = "speakingclock render";
		e->usage = "Usage: speakingclock render <voice> <unix time> <seconds> <file.sln> [zone]\n"
			"       Write <seconds> of the clock from <unix time> on, as 8 kHz\n"
			"       16-bit signed linear, to <file.sln>.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc < 6) {
		return CLI_SHOWUSAGE;
	}
	start = strtoll(a->argv[3], NULL, 10);
	seconds = atoi(a->argv[4]);
	if (seconds <= 0 || seconds > 3600) {
		return CLI_SHOWUSAGE;
	}
	if (!(st = state_new(a->argv[2], NULL, NULL))) {
		ast_cli(a->fd, "No voice '%s'\n", a->argv[2]);
		return CLI_FAILURE;
	}
	if (a->argc > 6) {
		ast_copy_string(st->zone, a->argv[6], sizeof(st->zone));
	}
	if (!(f = fopen(a->argv[5], "wb"))) {
		ast_cli(a->fd, "Can't write %s\n", a->argv[5]);
		state_free(st);
		return CLI_FAILURE;
	}
	st->pos = start * RATE;
	for (i = 0; i < seconds * 10; i++) {
		int16_t out[RATE / 10];
		fill(st, out, RATE / 10);
		fwrite(out, sizeof(out), 1, f);
	}
	fclose(f);
	state_free(st);
	ast_cli(a->fd, "Wrote %d s to %s\n", seconds, a->argv[5]);
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli[] = {
	AST_CLI_DEFINE(cli_voices, "List speaking clock voices"),
	AST_CLI_DEFINE(cli_render, "Render the speaking clock to a file"),
};

/* ---- module ----------------------------------------------------------- */

static int load_config(int reload)
{
	struct ast_flags flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	struct ast_config *cfg = ast_config_load(CONF, flags);
	const char *s;

	if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}
	ast_mutex_lock(&conf_lock);
	snprintf(voicedir, sizeof(voicedir), "%s/sounds/speaking-clock", ast_config_AST_DATA_DIR);
	ast_copy_string(default_voice, "tim-pat-simmons", sizeof(default_voice));
	default_zone[0] = '\0';
	maxduration = 180;
	if (cfg && cfg != CONFIG_STATUS_FILEINVALID) {
		if ((s = ast_variable_retrieve(cfg, "general", "voicedir"))) {
			ast_copy_string(voicedir, s, sizeof(voicedir));
		}
		if ((s = ast_variable_retrieve(cfg, "general", "default_voice"))) {
			ast_copy_string(default_voice, s, sizeof(default_voice));
		}
		if ((s = ast_variable_retrieve(cfg, "general", "timezone"))) {
			ast_copy_string(default_zone, s, sizeof(default_zone));
		}
		if ((s = ast_variable_retrieve(cfg, "general", "maxduration"))) {
			maxduration = atoi(s);
		}
		ast_config_destroy(cfg);
	}
	ast_mutex_unlock(&conf_lock);
	/* voices load again on next use */
	ao2_callback(voices, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL);
	return 0;
}

static int unload_module(void)
{
	int res = ast_unregister_application(app);
	ast_cli_unregister_multiple(cli, ARRAY_LEN(cli));
	ao2_cleanup(voices);
	voices = NULL;
	return res;
}

static int load_module(void)
{
	voices = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, voice_cmp);
	if (!voices) {
		return AST_MODULE_LOAD_DECLINE;
	}
	load_config(0);
	ast_cli_register_multiple(cli, ARRAY_LEN(cli));
	if (ast_register_application_xml(app, clock_exec)) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int reload_module(void)
{
	return load_config(1);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Telephone speaking clock",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
);
