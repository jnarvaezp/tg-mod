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

#include "tg.h"
#include "wav.h"
#include <string.h>

/* Cap on loaded audio to bound memory (4 bytes/frame). ~200M frames
 * ≈ 75 min at 44.1 kHz ≈ 800 MB. */
#define OFFLINE_MAX_FRAMES 200000000ull

static int guess_bph(double period)
{
	double bph = 7200 / period;
	double min = bph;
	int i, ret = 0;
	for(i = 0; preset_bph[i]; i++) {
		double diff = fabs(bph - preset_bph[i]);
		if(diff < min) { min = diff; ret = i; }
	}
	return preset_bph[ret];
}

int analyze_audio_file(const char *path, int bph, double la, double cal, struct offline_result *res)
{
	struct wav_reader rd;
	if(wav_open_read(path, &rd)) return 1;
	uint64_t nframes = wav_get_length(&rd);
	unsigned rate = rd.rate;
	if(nframes == 0 || rate == 0) { wav_reader_close(&rd); return 1; }
	if(nframes > OFFLINE_MAX_FRAMES) { wav_reader_close(&rd); return 1; }

	float *mono = malloc(nframes * sizeof(float));
	if(!mono) { wav_reader_close(&rd); return 1; }
	uint64_t got = 0;
	while(got < nframes) {
		long n = wav_read_samples(&rd, mono + got, (long)(nframes - got));
		if(n <= 0) break;
		got += n;
	}
	wav_reader_close(&rd);
	if(got == 0) { free(mono); return 1; }
	nframes = got;

	int i;
	struct processing_buffers p[NSTEPS];
	for(i = 0; i < NSTEPS; i++) {
		p[i].sample_rate = rate;
		p[i].sample_count = rate * (1 << (i + FIRST_STEP));
		setup_buffers(&p[i]);
	}

	double sr_eff = rate * (1 + (double)cal / (10 * 3600 * 24));
	int any = 0, best = -1;
	double prev_tic = 0;
	uint64_t pos = 0;
	uint64_t hop = rate / 10;   /* avanzar 100 ms por ciclo */

	while(pos <= nframes) {
		for(i = 0; i < NSTEPS; i++) {
			uint64_t n = p[i].sample_count;
			if(pos < n) continue;
			memcpy(p[i].samples, mono + (pos - n), n * sizeof(float));
			p[i].timestamp = pos;
			p[i].last_tic = prev_tic;
			p[i].last_toc = prev_tic;
			p[i].events_from = 0;
			process(&p[i], bph, la, 0);
			if(p[i].ready) prev_tic = p[i].last_tic;
		}
		int j;
		for(j = NSTEPS - 1; j >= 0; j--) {
			if(p[j].ready && p[j].sigma < p[j].period / 10000) { best = j; break; }
		}
		if(best >= 0) any = 1;
		pos += hop;
	}

	res->signal = 0;
	if(best >= 0 && p[best].ready) {
		double period = p[best].period;
		int guessed = bph ? bph : guess_bph(period / sr_eff);
		res->guessed_bph = guessed;
		res->rate = (7200 / (guessed * period / sr_eff) - 1) * 24 * 3600;
		res->be = fabs(p[best].be) * 1000 / sr_eff;
		res->amp = la * p[best].amp;
		if(res->amp < 135 || res->amp > 360) res->amp = 0;
		res->signal = any;
	} else {
		res->guessed_bph = bph ? bph : DEFAULT_BPH;
		res->rate = 0;
		res->be = 0;
		res->amp = 0;
	}

	for(i = 0; i < NSTEPS; i++) pb_destroy(&p[i]);
	free(mono);
	return 0;
}
