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
	remove(path);

	if(failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	printf("wav tests passed\n");
	return 0;
}
