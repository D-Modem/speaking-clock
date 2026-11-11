/*
 * sclock-render - write what a speaking clock caller would hear, without a
 * PBX: for checking voices and the core (core/speakingclock.c).
 *
 * Copyright (C) 2026 D-Modem Authors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *   sclock-render [-v voicedir] [-o key=value]... <voice> <unix time> <seconds> <out.sln> [zone]
 *   sclock-render [-v voicedir] -l
 *
 * The output is 8 kHz 16-bit signed linear, the same as Asterisk's
 * "speakingclock render" and FreeSWITCH's "speakingclock render".  -o
 * takes any setting speakingclock.conf has (tts_program_dectalk=...).
 */

#define _GNU_SOURCE
#include "speakingclock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void log_line(enum sc_log_level level, const char *msg)
{
	if (level >= SC_LOG_NOTICE) {
		fprintf(stderr, "%s\n", msg);
	}
}

/* single-threaded: TZ is set for each call */
static int zone_time(long long t, const char *zone, struct sc_tm *tm)
{
	time_t tt = (time_t) t;
	struct tm l;
	if (zone && *zone) {
		setenv("TZ", zone, 1);
	} else {
		unsetenv("TZ");
	}
	tzset();
	localtime_r(&tt, &l);
	tm->hour = l.tm_hour;
	tm->min = l.tm_min;
	tm->sec = l.tm_sec;
	return 0;
}

static void show(void *arg, const char *id, const char *mode, const char *name)
{
	(void) arg;
	printf("%-36s %-8s %s\n", id, mode, name);
}

int main(int argc, char **argv)
{
	static const struct sc_host host = { .log = log_line, .localtime = zone_time };
	const char *voicedir = "voices";
	char *opts[32];
	int nopts = 0, list = 0, c, i, seconds;
	sc_clock *clk;
	FILE *f;

	while ((c = getopt(argc, argv, "v:o:l")) != -1) {
		if (c == 'v') {
			voicedir = optarg;
		} else if (c == 'o' && nopts < 32 && strchr(optarg, '=')) {
			opts[nopts++] = optarg;
		} else if (c == 'l') {
			list = 1;
		} else {
			goto usage;
		}
	}
	sc_init(&host);
	sc_config_begin(voicedir);
	for (i = 0; i < nopts; i++) {
		char *eq = strchr(opts[i], '=');
		*eq = '\0';
		sc_config_set(opts[i], eq + 1);
	}
	sc_config_end();
	if (list) {
		printf("%-36s %-8s %s\n", "Voice", "Mode", "Name");
		c = sc_voices_list(show, NULL) < 0;
		sc_shutdown();
		return c;
	}
	if (argc - optind < 4) {
		goto usage;
	}
	seconds = atoi(argv[optind + 2]);
	if (seconds <= 0 || seconds > 3600 || !(clk = sc_clock_new(argv[optind], NULL))) {
		fprintf(stderr, "No voice '%s', or bad seconds\n", argv[optind]);
		sc_shutdown();
		return 1;
	}
	if (!(f = fopen(argv[optind + 3], "wb"))) {
		perror(argv[optind + 3]);
		return 1;
	}
	sc_clock_render_from(clk, strtoll(argv[optind + 1], NULL, 10),
		argc - optind > 4 ? argv[optind + 4] : NULL);
	for (i = 0; i < seconds * 10; i++) {
		int16_t out[SC_RATE / 10];
		sc_clock_fill(clk, out, SC_RATE / 10);
		fwrite(out, sizeof(out), 1, f);
	}
	fclose(f);
	sc_clock_free(clk);
	sc_shutdown();
	return 0;
usage:
	fprintf(stderr, "usage: sclock-render [-v voicedir] [-o key=value]... <voice> <unix time> <seconds> <out.sln> [zone]\n"
		"       sclock-render [-v voicedir] -l\n");
	return 2;
}
