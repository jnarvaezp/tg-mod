#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include "tg.h"
#include "wav.h"
#include "session.h"

static int failures = 0;
#define CHECK(cond, msg) do { if(!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } } while(0)

/* print_debug lives in interface.c (GTK); provide headless stubs. */
void print_debug(char *format,...)
{
	(void)format;
}

void print_debug_verbose(char *format,...)
{
	(void)format;
}

/* Genera un clip sintético tipo "timegrapher": tic y tock como bursts senoidales
 * decrecientes, separados period/2 (+ be_offset), con ruido blanco opcional. */
static void gen_clip(const char *path, int bph, double dur, double pulse_ms,
                     double tic_amp, double tock_amp, double be_offset_ms,
                     double noise_amp)
{
	unsigned rate = 44100;
	double period = 7200.0 / bph;
	int n = (int)(rate * dur);
	struct wav_writer w;
	if(wav_open_write(path, rate, 1, 16, &w)) return;
	float *buf = malloc(n * sizeof(float));
	int i;
	for(i = 0; i < n; i++) {
		double t = (double)i / rate;
		double beat = floor(t / period);
		double tic_t = beat * period;
		double toc_t = tic_t + period / 2 + be_offset_ms / 1000.0;
		double v = 0, dt;
		dt = t - tic_t;
		if(dt >= 0 && dt < pulse_ms / 1000.0)
			v += tic_amp * sin(2 * M_PI * 3000 * dt) * exp(-dt * 900);
		dt = t - toc_t;
		if(dt >= 0 && dt < pulse_ms / 1000.0)
			v += tock_amp * sin(2 * M_PI * 3000 * dt) * exp(-dt * 900);
		if(noise_amp > 0)
			v += noise_amp * (rand() / (double)RAND_MAX * 2 - 1);
		buf[i] = v;
	}
	wav_write_samples(&w, buf, n);
	wav_close(&w);
	free(buf);
}

static void test_clip(int bph, double dur, double pulse_ms, double tic_amp,
                      double tock_amp, double be_offset_ms, double noise_amp,
                      int expect_signal, double rate_tol, double be_tol,
                      const char *tag)
{
	char path[512];
	snprintf(path, sizeof(path), "test_dsp_out/%s.wav", tag);
	gen_clip(path, bph, dur, pulse_ms, tic_amp, tock_amp, be_offset_ms, noise_amp);

	struct offline_result r;
	CHECK(analyze_audio_file(path, 0, DEFAULT_LA, 0, &r) == 0, "analyze ok");
	CHECK(r.signal == expect_signal, "signal");
	if(r.signal) {
		CHECK(r.guessed_bph == bph, "bph");
		CHECK(fabs(r.rate) < rate_tol, "rate");
		CHECK(fabs(r.be - be_offset_ms) < be_tol, "be");
	}
	remove(path);
	printf("%-14s signal=%d bph=%d rate=%+.2f be=%.2f amp=%.1f\n",
	       tag, r.signal, r.guessed_bph, r.rate, r.be, r.amp);
}

int main(void)
{
	session_init();
	mkdir("test_dsp_out", 0755);
	srand(42);

	int i;
	struct { int bph; int expect_signal; } cases[] = {
		/* 12000 (0.6 s) y 14400 (0.5 s): el guard de process()
		 * (algo.c: "Detected period too long") rechaza periodos >= 0.5 s
		 * a 44.1 kHz; el generador no puede sortear esa limitación. */
		{ 12000, 0 }, { 14400, 0 },
		{ 18000, 1 }, { 21600, 1 }, { 28800, 1 }, { 36000, 1 },
	};
	for(i = 0; i < (int)(sizeof(cases)/sizeof(cases[0])); i++) {
		char tag[32];
		snprintf(tag, sizeof(tag), "bph%d", cases[i].bph);
		test_clip(cases[i].bph, 4.0, 14.0, 1.0, 0.8, 0.0, 0.0,
		          cases[i].expect_signal, 2.0, 1.0, tag);
	}

	rmdir("test_dsp_out");
	if(failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	printf("dsp tests passed\n");
	return 0;
}