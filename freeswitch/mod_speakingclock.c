/*
 * mod_speakingclock - a telephone speaking clock for FreeSWITCH
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
 * The speakingclock dialplan application: the same clock as Asterisk's
 * SpeakingClock() (core/speakingclock.c does the work), reading the same
 * voice folders.  Each announcement is placed by sample against the
 * system clock, so the time mark falls exactly on the second it
 * announces; live voices (mode=tts) run a synthesiser on this machine.
 *
 *   <action application="speakingclock" data="tim-pat-simmons t(Europe/London)"/>
 *
 * API: speakingclock voices | render <voice> <unix time> <seconds> <file.sln> [zone] | reload
 */

#include <switch.h>
#include "speakingclock.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

SWITCH_MODULE_LOAD_FUNCTION(mod_speakingclock_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_speakingclock_shutdown);
SWITCH_MODULE_DEFINITION(mod_speakingclock, mod_speakingclock_load, mod_speakingclock_shutdown, NULL);

#define CONF "speakingclock.conf"
#define APP_SYNTAX "[voice] [options: t(zone) d(seconds) h p(seconds) s(a+b+c) w x]"
#define API_SYNTAX "voices | render <voice> <unix time> <seconds> <file.sln> [zone] | reload"

static switch_event_node_t *reload_node;

/* ---- what the core needs ---------------------------------------------- */

static void host_log(enum sc_log_level level, const char *msg)
{
	switch_log_level_t l = level == SC_LOG_ERROR ? SWITCH_LOG_ERROR :
		level == SC_LOG_WARNING ? SWITCH_LOG_WARNING :
		level == SC_LOG_NOTICE ? SWITCH_LOG_NOTICE : SWITCH_LOG_DEBUG;
	switch_log_printf(SWITCH_CHANNEL_LOG, l, "%s\n", msg);
}

/* FreeSWITCH's own time zones (timezones.conf.xml), else the system's */
static int host_localtime(long long t, const char *zone, struct sc_tm *tm)
{
	switch_time_exp_t e = { 0 };
	switch_time_t when = (switch_time_t) t * 1000000;

	if (zone && *zone) {
		if (switch_time_exp_tz_name(zone, &e, when) != SWITCH_STATUS_SUCCESS) {
			/* once, not every announcement (a race here only costs a log line) */
			static char last_bad[64];
			if (strncmp(last_bad, zone, sizeof(last_bad) - 1)) {
				switch_copy_string(last_bad, zone, sizeof(last_bad));
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
					"SpeakingClock: unknown time zone '%s' (see timezones.conf.xml), using the system's\n", zone);
			}
			return -1;
		}
	} else if (switch_time_exp_lt(&e, when) != SWITCH_STATUS_SUCCESS) {
		return -1;
	}
	tm->hour = e.tm_hour;
	tm->min = e.tm_min;
	tm->sec = e.tm_sec;
	return 0;
}

static long long host_now_us(void)
{
	return (long long) switch_micro_time_now();
}

static const struct sc_host host = {
	.log = host_log,
	.localtime = host_localtime,
	.now_us = host_now_us,
};

/* ---- configuration ---------------------------------------------------- */

static void load_config(void)
{
	char dir[1024];
	switch_xml_t cfg, xml, settings, param;

	switch_snprintf(dir, sizeof(dir), "%s%sspeaking-clock", SWITCH_GLOBAL_dirs.sounds_dir, SWITCH_PATH_SEPARATOR);
	sc_config_begin(dir);
	if ((xml = switch_xml_open_cfg(CONF, &cfg, NULL))) {
		if ((settings = switch_xml_child(cfg, "settings"))) {
			for (param = switch_xml_child(settings, "param"); param; param = param->next) {
				const char *name = switch_xml_attr_soft(param, "name");
				const char *value = switch_xml_attr_soft(param, "value");
				sc_config_set(name, value);
			}
		}
		switch_xml_free(xml);
	} else {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
			"SpeakingClock: no %s, using the defaults (voices in %s)\n", CONF, dir);
	}
	/* the voices load again on next use */
	sc_config_end();
}

static void on_reloadxml(switch_event_t *event)
{
	load_config();
}

/* ---- the application -------------------------------------------------- */

/* data: "[voice] [options]", the options in SpeakingClock()'s letters.
 * The voice may be left out ("" or a word that starts with an option and
 * a parenthesis is taken as options).  Asterisk's "voice,options" works
 * too. */
static int parse_args(const char *data, char *voice, size_t vlen, struct sc_options *o)
{
	char *buf = strdup(data ? data : ""), *p = buf, *sep;
	int res;

	if (!buf) {
		return -1;
	}
	while (*p == ' ' || *p == '\t') {
		p++;
	}
	voice[0] = '\0';
	sep = p + strcspn(p, " \t,");
	/* a voice name has no parentheses */
	if (!memchr(p, '(', sep - p)) {
		switch_copy_string(voice, p, MIN(vlen, (size_t) (sep - p) + 1));
		p = *sep ? sep + 1 : sep;
	}
	res = sc_options_parse(p, o);
	free(buf);
	return res;
}

SWITCH_STANDARD_APP(speakingclock_function)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_memory_pool_t *pool = switch_core_session_get_pool(session);
	switch_codec_implementation_t impl = { 0 };
	switch_codec_t codec = { 0 };
	switch_timer_t timer = { 0 };
	switch_frame_t wf = { 0 };
	struct sc_options o;
	char voice[96];
	int16_t buf[SC_RATE / 10];
	int ms, samples, duration;
	switch_time_t end = 0;
	sc_clock *clk;

	if (parse_args(data, voice, sizeof(voice), &o)) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
			"SpeakingClock: bad options '%s'; usage: speakingclock %s\n", switch_str_nil(data), APP_SYNTAX);
		return;
	}
	duration = o.has_duration ? o.duration : sc_maxduration();
	if (!(clk = sc_clock_new(voice, &o))) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
			"SpeakingClock: no voice '%s'\n", *voice ? voice : "(default)");
		return;
	}
	if (switch_channel_answer(channel) != SWITCH_STATUS_SUCCESS) {
		sc_clock_free(clk);
		return;
	}

	/* frames as long as the call's, in 8 kHz signed linear: FreeSWITCH
	 * changes them to the call's codec and rate */
	switch_core_session_get_read_impl(session, &impl);
	ms = impl.microseconds_per_packet / 1000;
	if (ms < 10 || ms > 100) {
		ms = 20;
	}
	samples = SC_RATE * ms / 1000;
	if (switch_core_codec_init(&codec, "L16", NULL, NULL, SC_RATE, ms, 1,
			SWITCH_CODEC_FLAG_ENCODE | SWITCH_CODEC_FLAG_DECODE, NULL, pool) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
			"SpeakingClock: can't set up L16 at 8 kHz\n");
		sc_clock_free(clk);
		return;
	}
	if (switch_core_timer_init(&timer, "soft", ms, samples, pool) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
			"SpeakingClock: can't start a timer\n");
		switch_core_codec_destroy(&codec);
		sc_clock_free(clk);
		return;
	}
	wf.codec = &codec;
	wf.data = buf;
	wf.buflen = sizeof(buf);
	wf.rate = SC_RATE;
	wf.channels = 1;

	switch_channel_flush_dtmf(channel);
	if (duration > 0) {
		end = switch_micro_time_now() + (switch_time_t) duration * 1000000;
	}
	sc_clock_start(clk);
	while (switch_channel_ready(channel)) {
		switch_frame_t *rf;
		switch_status_t status;

		if (end && switch_micro_time_now() >= end) {
			break;
		}
		/* read what the caller sends (and their keys), without waiting:
		 * the timer sets the pace, and the clock its position */
		status = switch_core_session_read_frame(session, &rf, SWITCH_IO_FLAG_NOBLOCK, 0);
		if (!SWITCH_READ_ACCEPTABLE(status)) {
			break;
		}
		if (switch_channel_has_dtmf(channel)) {
			switch_dtmf_t dtmf = { 0 };
			switch_channel_dequeue_dtmf(channel, &dtmf);
			if (o.exit_on_key) {
				switch_channel_set_variable_printf(channel, "speakingclock_key", "%c", dtmf.digit);
				break;
			}
		}
		sc_clock_play(clk, buf, samples);
		wf.datalen = samples * sizeof(int16_t);
		wf.samples = samples;
		if (switch_core_session_write_frame(session, &wf, SWITCH_IO_FLAG_NONE, 0) != SWITCH_STATUS_SUCCESS) {
			break;
		}
		switch_core_timer_next(&timer);
	}
	switch_core_timer_destroy(&timer);
	switch_core_codec_destroy(&codec);
	sc_clock_free(clk);
}

/* ---- the API ---------------------------------------------------------- */

static void list_voice(void *arg, const char *id, const char *mode, const char *name)
{
	switch_stream_handle_t *stream = arg;
	stream->write_function(stream, "%-36s %-8s %s\n", id, mode, name);
}

SWITCH_STANDARD_API(speakingclock_api)
{
	char *argv[8] = { 0 }, *mycmd = NULL;
	int argc = 0;

	if (!zstr(cmd) && (mycmd = strdup(cmd))) {
		argc = switch_separate_string(mycmd, ' ', argv, (sizeof(argv) / sizeof(argv[0])));
	}
	if (argc >= 1 && !strcasecmp(argv[0], "voices")) {
		char dir[1024];
		stream->write_function(stream, "%-36s %-8s %s\n", "Voice", "Mode", "Name");
		if (sc_voices_list(list_voice, stream) < 0) {
			sc_voicedir(dir, sizeof(dir));
			stream->write_function(stream, "-ERR can't read %s\n", dir);
		}
	} else if (argc >= 5 && !strcasecmp(argv[0], "render")) {
		/* what a caller would hear, without a call: for checking a voice */
		int seconds = atoi(argv[3]), i;
		sc_clock *clk;
		FILE *f;
		if (seconds <= 0 || seconds > 3600) {
			stream->write_function(stream, "-ERR seconds: 1 to 3600\n");
		} else if (!(clk = sc_clock_new(argv[1], NULL))) {
			stream->write_function(stream, "-ERR no voice '%s'\n", argv[1]);
		} else if (!(f = fopen(argv[4], "wb"))) {
			stream->write_function(stream, "-ERR can't write %s\n", argv[4]);
			sc_clock_free(clk);
		} else {
			sc_clock_render_from(clk, strtoll(argv[2], NULL, 10), argc > 5 ? argv[5] : NULL);
			for (i = 0; i < seconds * 10; i++) {
				int16_t out[SC_RATE / 10];
				sc_clock_fill(clk, out, SC_RATE / 10);
				fwrite(out, sizeof(out), 1, f);
			}
			fclose(f);
			sc_clock_free(clk);
			stream->write_function(stream, "+OK wrote %d s to %s\n", seconds, argv[4]);
		}
	} else if (argc >= 1 && !strcasecmp(argv[0], "reload")) {
		load_config();
		stream->write_function(stream, "+OK\n");
	} else {
		stream->write_function(stream, "-USAGE: speakingclock %s\n", API_SYNTAX);
	}
	switch_safe_free(mycmd);
	return SWITCH_STATUS_SUCCESS;
}

/* ---- module ----------------------------------------------------------- */

SWITCH_MODULE_LOAD_FUNCTION(mod_speakingclock_load)
{
	switch_application_interface_t *app_interface;
	switch_api_interface_t *api_interface;

	*module_interface = switch_loadable_module_create_module_interface(pool, modname);
	if (sc_init(&host)) {
		return SWITCH_STATUS_FALSE;
	}
	load_config();
	if (switch_event_bind_removable(modname, SWITCH_EVENT_RELOADXML, NULL, on_reloadxml, NULL,
			&reload_node) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
			"SpeakingClock: reloadxml won't reread %s (speakingclock reload will)\n", CONF);
	}
	SWITCH_ADD_APP(app_interface, "speakingclock", "Telephone speaking clock",
		"Answers and plays the time like a telephone company speaking clock, with the "
		"time mark on the exact second, until the caller hangs up or the duration runs out.",
		speakingclock_function, APP_SYNTAX, SAF_NONE);
	SWITCH_ADD_API(api_interface, "speakingclock", "Speaking clock voices and rendering",
		speakingclock_api, API_SYNTAX);
	switch_console_set_complete("add speakingclock voices");
	switch_console_set_complete("add speakingclock render");
	switch_console_set_complete("add speakingclock reload");
	return SWITCH_STATUS_SUCCESS;
}

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_speakingclock_shutdown)
{
	switch_event_unbind(&reload_node);
	switch_console_set_complete("del speakingclock");
	sc_shutdown();
	return SWITCH_STATUS_SUCCESS;
}
