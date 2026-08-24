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

static int read_le16(FILE *f, unsigned *v)
{
	unsigned char b[2];
	if(fread(b, 1, 2, f) != 2) return -1;
	if(v) *v = b[0] | (b[1] << 8);
	return 0;
}

static int read_le32(FILE *f, uint32_t *v)
{
	unsigned char b[4];
	if(fread(b, 1, 4, f) != 4) return -1;
	if(v) *v = b[0] | (b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
	return 0;
}

int wav_open_read(const char *path, struct wav_reader *r)
{
	memset(r, 0, sizeof(*r));
	r->f = fopen(path, "rb");
	if(!r->f) return -1;

	char tag[4];
	if(fread(tag, 1, 4, r->f) != 4 || memcmp(tag, "RIFF", 4)) goto error;
	uint32_t riff_size;
	if(read_le32(r->f, &riff_size)) goto error;
	if(fread(tag, 1, 4, r->f) != 4 || memcmp(tag, "WAVE", 4)) goto error;

	/* Pass 1: find and parse the fmt chunk. */
	unsigned fmt = 0, channels = 0, bits = 0;
	uint32_t rate = 0;
	int have_fmt = 0;
	while(fread(tag, 1, 4, r->f) == 4) {
		uint32_t csize;
		if(read_le32(r->f, &csize)) goto error;
		if(!memcmp(tag, "fmt ", 4) && csize >= 16 && !have_fmt) {
			if(read_le16(r->f, &fmt) || read_le16(r->f, &channels) ||
			   read_le32(r->f, &rate) || read_le32(r->f, NULL) ||
			   read_le16(r->f, NULL) || read_le16(r->f, &bits))
				goto error;
			have_fmt = 1;
			break;
		}
		fseek(r->f, (long)csize, SEEK_CUR);
	}
	if(!have_fmt || (fmt != 1 && fmt != 3) || channels == 0 || rate == 0) goto error;

	/* Pass 2: find the data chunk. */
	fseek(r->f, 12, SEEK_SET);
	uint64_t data_start = 0, data_bytes = 0;
	while(fread(tag, 1, 4, r->f) == 4) {
		uint32_t csize;
		if(read_le32(r->f, &csize)) goto error;
		if(!memcmp(tag, "data", 4)) {
			data_start = (uint64_t)ftell(r->f);
			data_bytes = csize;
			break;
		}
		fseek(r->f, (long)csize, SEEK_CUR);
	}
	if(!data_bytes) goto error;

	r->fmt = fmt;
	r->channels = channels;
	r->rate = rate;
	r->bits = bits;
	r->data_start = data_start;
	r->data_bytes = data_bytes;
	r->pos = 0;
	r->ok = 1;
	fseek(r->f, (long)data_start, SEEK_SET);
	return 0;

error:
	fclose(r->f);
	r->f = NULL;
	return -1;
}

uint64_t wav_get_length(const struct wav_reader *r)
{
	return r->data_bytes / (r->channels * (r->bits / 8));
}

int wav_reader_close(struct wav_reader *r)
{
	if(!r->f) return -1;
	int rc = fclose(r->f);
	r->f = NULL;
	return rc ? -1 : 0;
}

/* Read one frame (all channels) as a mono float. Returns 0 on success, -1 at EOF/error. */
static int read_one_frame(struct wav_reader *r, float *out)
{
	if(r->pos >= wav_get_length(r)) return -1;
	double acc = 0;
	int c;
	for(c = 0; c < (int)r->channels; c++) {
		double v = 0;
		if(r->fmt == 3) {
			float f;
			if(fread(&f, 4, 1, r->f) != 1) return -1;
			v = f;
		} else if(r->bits == 8) {
			unsigned char b;
			if(fread(&b, 1, 1, r->f) != 1) return -1;
			v = ((int)b - 128) / 128.0;
		} else if(r->bits == 16) {
			unsigned u;
			if(read_le16(r->f, &u)) return -1;
			int16_t s = (int16_t)u;
			v = s / 32768.0;
		} else if(r->bits == 24) {
			unsigned char b[3];
			if(fread(b, 1, 3, r->f) != 3) return -1;
			int32_t s = (b[0] | (b[1] << 8) | (b[2] << 16));
			if(s & 0x800000) s |= ~0xffffff;   /* sign extend */
			v = s / 8388608.0;
		} else if(r->bits == 32) {
			uint32_t u;
			if(read_le32(r->f, &u)) return -1;
			v = (int32_t)u / 2147483648.0;
		} else {
			return -1;
		}
		acc += v;
	}
	*out = (float)(acc / r->channels);
	r->pos++;
	return 0;
}

long wav_read_samples(struct wav_reader *r, float *out, long count)
{
	long n = 0;
	while(n < count) {
		if(read_one_frame(r, &out[n])) break;
		n++;
	}
	return n;
}
