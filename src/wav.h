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

#ifndef TG_WAV_H
#define TG_WAV_H

#include <stdio.h>
#include <stdint.h>

struct wav_writer {
	FILE *f;
	unsigned rate;
	unsigned channels;
	unsigned bits;
	uint32_t data_bytes;
	int ok;
};

/* Create a new PCM WAV file at `path`. Returns 0 on success, -1 on error. */
int wav_open_write(const char *path, unsigned rate, unsigned channels, unsigned bits, struct wav_writer *w);

/* Append `count` interleaved float samples (each -1..1). Returns 0 on success, -1 on error. */
int wav_write_samples(struct wav_writer *w, const float *samples, int count);

/* Finalize headers and close. Returns 0 on success, -1 on error. */
int wav_close(struct wav_writer *w);

#endif
