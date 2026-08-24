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

#include "wav.h"
#include <string.h>

static int write_le16(FILE *f, unsigned v)
{
	unsigned char b[2] = { v & 0xff, (v >> 8) & 0xff };
	return fwrite(b, 1, 2, f) == 2 ? 0 : -1;
}

static int write_le32(FILE *f, uint32_t v)
{
	unsigned char b[4] = { v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >> 24) & 0xff };
	return fwrite(b, 1, 4, f) == 4 ? 0 : -1;
}

int wav_open_write(const char *path, unsigned rate, unsigned channels, unsigned bits, struct wav_writer *w)
{
	memset(w, 0, sizeof(*w));
	if(bits != 16) return -1;
	w->f = fopen(path, "wb");
	if(!w->f) return -1;
	w->rate = rate;
	w->channels = channels;
	w->bits = bits;
	w->ok = 1;

	if(fwrite("RIFF", 1, 4, w->f) != 4) goto fail;
	if(write_le32(w->f, 0)) goto fail;      /* patched in wav_close */
	if(fwrite("WAVE", 1, 4, w->f) != 4) goto fail;
	if(fwrite("fmt ", 1, 4, w->f) != 4) goto fail;
	if(write_le32(w->f, 16)) goto fail;
	if(write_le16(w->f, 1)) goto fail;      /* PCM */
	if(write_le16(w->f, channels)) goto fail;
	if(write_le32(w->f, rate)) goto fail;
	if(write_le32(w->f, rate * channels * (bits / 8))) goto fail;
	if(write_le16(w->f, channels * (bits / 8))) goto fail;
	if(write_le16(w->f, bits)) goto fail;
	if(fwrite("data", 1, 4, w->f) != 4) goto fail;
	if(write_le32(w->f, 0)) goto fail;      /* patched in wav_close */
	return 0;

fail:
	fclose(w->f);
	w->f = NULL;
	w->ok = 0;
	return -1;
}

int wav_write_samples(struct wav_writer *w, const float *samples, int count)
{
	if(!w->ok) return -1;
	int i;
	for(i = 0; i < count; i++) {
		float s = samples[i];
		if(s > 1.0f) s = 1.0f;
		if(s < -1.0f) s = -1.0f;
		int16_t v = (int16_t)(s * 32767.0f);
		unsigned char b[2] = { (unsigned char)v, (unsigned char)(v >> 8) };
		if(fwrite(b, 1, 2, w->f) != 2) return -1;
		w->data_bytes += 2;
	}
	return 0;
}

int wav_close(struct wav_writer *w)
{
	if(!w->ok) return -1;
	if(fseek(w->f, 4, SEEK_SET)) {
		fclose(w->f);
		w->f = NULL;
		w->ok = 0;
		return -1;
	}
	write_le32(w->f, 36 + w->data_bytes);
	if(fseek(w->f, 40, SEEK_SET)) {
		fclose(w->f);
		w->f = NULL;
		w->ok = 0;
		return -1;
	}
	write_le32(w->f, w->data_bytes);
	int rc = fclose(w->f);
	w->f = NULL;
	w->ok = 0;
	return rc ? -1 : 0;
}
