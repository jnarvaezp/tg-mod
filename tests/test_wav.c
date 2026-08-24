#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "wav.h"

static int failures = 0;
#define CHECK(cond, msg) do { if(!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } } while(0)

int main(void)
{
	const char *path = "test_wav_out.wav";
	unsigned rate = 44100;

	struct wav_writer w;
	CHECK(wav_open_write(path, rate, 1, 16, &w) == 0, "open write");
	float buf[44100];
	int i;
	for(i = 0; i < 44100; i++) buf[i] = (float)(i % 1000) / 1000.0f - 0.5f;
	CHECK(wav_write_samples(&w, buf, 44100) == 0, "write samples");
	CHECK(wav_close(&w) == 0, "close write");

	/* Verify the raw header bytes (LE PCM mono). */
	FILE *f = fopen(path, "rb");
	CHECK(f != NULL, "reopen for header check");
	if(f) {
		unsigned char hdr[44];
		CHECK(fread(hdr, 1, 44, f) == 44, "read header");
		CHECK(!memcmp(hdr, "RIFF", 4) && !memcmp(hdr + 8, "WAVE", 4), "riff/wave tags");
		CHECK(!memcmp(hdr + 12, "fmt ", 4) && !memcmp(hdr + 36, "data", 4), "fmt/data tags");
		unsigned rate_l = hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | ((unsigned)hdr[27] << 24);
		CHECK(rate_l == 44100, "rate field");
		CHECK(hdr[22] == 1, "channels == 1");
		CHECK(hdr[34] == 16, "bits == 16");
		uint32_t data_sz = hdr[40] | (hdr[41] << 8) | (hdr[42] << 16) | ((uint32_t)hdr[43] << 24);
		CHECK(data_sz == 44100u * 2, "data size field");
		fclose(f);
	}

	/* --- read-back round trip --- */
	struct wav_reader r;
	CHECK(wav_open_read(path, &r) == 0, "open read");
	CHECK(r.rate == 44100, "read rate");
	CHECK(r.channels == 1, "read channels");
	CHECK(wav_get_length(&r) == 44100, "read length");
	float out[44100];
	CHECK(wav_read_samples(&r, out, 44100) == 44100, "read all");
	CHECK(fabs(out[0] - buf[0]) < 1.0 / 16384.0, "sample 0 approx");
	CHECK(fabs(out[12345] - buf[12345]) < 1.0 / 16384.0, "sample 12345 approx");
	CHECK(wav_reader_close(&r) == 0, "close read");

	/* --- stereo downmix: write stereo, read as mono average --- */
	const char *st = "test_wav_st.wav";
	struct wav_writer sw;
	CHECK(wav_open_write(st, 44100, 2, 16, &sw) == 0, "open stereo write");
	float stbuf[2 * 4410];
	for(i = 0; i < 4410; i++) {
		stbuf[2 * i] = 0.25f;
		stbuf[2 * i + 1] = 0.75f;
	}
	CHECK(wav_write_samples(&sw, stbuf, 2 * 4410) == 0, "write stereo");
	CHECK(wav_close(&sw) == 0, "close stereo");
	struct wav_reader sr;
	CHECK(wav_open_read(st, &sr) == 0, "open stereo read");
	CHECK(sr.channels == 2, "stereo channels");
	float m[4410];
	CHECK(wav_read_samples(&sr, m, 4410) == 4410, "read stereo frames");
	CHECK(fabs(m[0] - 0.5f) < 1.0 / 16384.0, "downmix avg");
	CHECK(wav_reader_close(&sr) == 0, "close stereo read");
	remove(st);
	remove(path);

	if(failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	printf("wav tests passed\n");
	return 0;
}
