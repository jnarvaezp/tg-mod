/*
    tg
    Copyright (C) 2015 Marcello Mamino

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2 as
    published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define SESSION_CYCLES 50000
#define SESSION_RAW_LINES 20000
#define SESSION_RAW_LEN 160

static struct {
	pthread_mutex_t m;
	struct session_cycle cycles[SESSION_CYCLES];
	int cycles_n, cycles_wp;
	struct {
		uint64_t wall_ms;
		char text[SESSION_RAW_LEN];
	} raw[SESSION_RAW_LINES];
	int raw_n, raw_wp;
} ses;

void session_init(void)
{
	pthread_mutex_init(&ses.m, NULL);
	ses.cycles_n = ses.cycles_wp = 0;
	ses.raw_n = ses.raw_wp = 0;
}

void session_add_cycle(const struct session_cycle *c)
{
	pthread_mutex_lock(&ses.m);
	ses.cycles[ses.cycles_wp] = *c;
	ses.cycles_wp = (ses.cycles_wp + 1) % SESSION_CYCLES;
	if(ses.cycles_n < SESSION_CYCLES) ses.cycles_n++;
	pthread_mutex_unlock(&ses.m);
}

void session_add_raw(uint64_t wall_ms, const char *line)
{
	pthread_mutex_lock(&ses.m);
	strncpy(ses.raw[ses.raw_wp].text, line, SESSION_RAW_LEN - 1);
	ses.raw[ses.raw_wp].text[SESSION_RAW_LEN - 1] = 0;
	ses.raw[ses.raw_wp].wall_ms = wall_ms;
	ses.raw_wp = (ses.raw_wp + 1) % SESSION_RAW_LINES;
	if(ses.raw_n < SESSION_RAW_LINES) ses.raw_n++;
	pthread_mutex_unlock(&ses.m);
}

/* Índice del registro más antiguo (para escribir en orden cronológico). */
static int cycle_start(void)
{
	return ses.cycles_n < SESSION_CYCLES ? 0 : ses.cycles_wp;
}

static int raw_start(void)
{
	return ses.raw_n < SESSION_RAW_LINES ? 0 : ses.raw_wp;
}

static void write_json(FILE *f)
{
	int start = cycle_start();
	int i;
	fprintf(f, "{\n  \"cycles\": [\n");
	for(i = 0; i < ses.cycles_n; i++) {
		const struct session_cycle *c = &ses.cycles[(start + i) % SESSION_CYCLES];
		fprintf(f,
			"    {\"wall_ms\":%llu,\"audio\":%llu,\"signal\":%d,\"bph\":%d,"
			"\"rate\":%.6f,\"be\":%.6f,\"amp\":%.6f,\"period\":%.6f,\"sigma\":%.6f,"
			"\"calibrate\":%d,\"cal_state\":%d}%s\n",
			(unsigned long long)c->wall_ms, (unsigned long long)c->audio,
			c->signal, c->guessed_bph, c->rate, c->be, c->amp, c->period, c->sigma,
			c->calibrate, c->cal_state,
			i + 1 < ses.cycles_n ? "," : "");
	}
	fprintf(f, "  ]\n}\n");
}

static void write_csv(FILE *f)
{
	int start = cycle_start();
	int i;
	fprintf(f, "wall_ms,audio,signal,bph,rate,be,amp,period,sigma,calibrate,cal_state\n");
	for(i = 0; i < ses.cycles_n; i++) {
		const struct session_cycle *c = &ses.cycles[(start + i) % SESSION_CYCLES];
		fprintf(f, "%llu,%llu,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%d\n",
			(unsigned long long)c->wall_ms, (unsigned long long)c->audio,
			c->signal, c->guessed_bph, c->rate, c->be, c->amp, c->period, c->sigma,
			c->calibrate, c->cal_state);
	}
}

static void write_raw(FILE *f)
{
	int start = raw_start();
	int i;
	for(i = 0; i < ses.raw_n; i++) {
		int idx = (start + i) % SESSION_RAW_LINES;
		fprintf(f, "[%llu] %s", (unsigned long long)ses.raw[idx].wall_ms, ses.raw[idx].text);
	}
}

int session_save(const char *dir, const char *base)
{
	char path[1024];
	int errs = 0;
	FILE *f;

	pthread_mutex_lock(&ses.m);

	snprintf(path, sizeof(path), "%s/%s.json", dir, base);
	f = fopen(path, "w");
	if(f) { write_json(f); if(fclose(f)) errs++; } else errs++;

	snprintf(path, sizeof(path), "%s/%s.csv", dir, base);
	f = fopen(path, "w");
	if(f) { write_csv(f); if(fclose(f)) errs++; } else errs++;

	snprintf(path, sizeof(path), "%s/%s.raw", dir, base);
	f = fopen(path, "w");
	if(f) { write_raw(f); if(fclose(f)) errs++; } else errs++;

	pthread_mutex_unlock(&ses.m);
	return errs;
}