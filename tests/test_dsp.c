#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dirent.h>
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
	/* amp=0.0 significa "no disponible": offline.c lo pone a 0 cuando
	 * la*amp queda fuera de [135,360] (algo.c:813). En los clips sintéticos
	 * siempre sale 0; el camino amp real lo cubren test_amp() y los
	 * fixtures de grabaciones reales (Fase 3). */
	printf("%-14s signal=%d bph=%d rate=%+.2f be=%.2f amp=%.1f\n",
	       tag, r.signal, r.guessed_bph, r.rate, r.be, r.amp);
}

/* Cobertura de amplitud: el sintético nunca produce amp válida (empírico).
 * compute_amplitude() (algo.c:757) mide el ancho de pulso en las ventanas
 * [tic-period/8, tic): el máximo de la envolvente del burst sintético cae
 * exactamente en tic (el borde de la ventana), así que la medida sale <= 0
 * y la validación 135 < la*amp < 360 (algo.c:813) rechaza siempre. Probado
 * con pulse_ms 2-20, portadora 2-4 kHz y tic/tock_amp 1.0-1.5: amp=0 en
 * todos. El camino amp real queda cubierto por los fixtures de grabaciones
 * reales (Fase 3); aquí se fija el comportamiento actual conocido. */
static void test_amp(void)
{
	char path[512];
	snprintf(path, sizeof(path), "test_dsp_out/amp.wav");
	gen_clip(path, 21600, 4.0, 4.0, 1.0, 1.0, 0.0, 0.0);

	struct offline_result r;
	CHECK(analyze_audio_file(path, 0, DEFAULT_LA, 0, &r) == 0, "analyze ok");
	CHECK(r.signal == 1, "signal");
	CHECK(r.guessed_bph == 21600, "bph");
	CHECK(r.amp == 0, "amp unavailable (clamped)");
	remove(path);
	printf("%-14s signal=%d bph=%d rate=%+.2f be=%.2f amp=%.1f\n",
	       "amp", r.signal, r.guessed_bph, r.rate, r.be, r.amp);
}

int main(void)
{
	session_init();
	mkdir("test_dsp_out", 0755);
	srand(42);

	int i;
	struct { int bph; int expect_signal; } cases[] = {
		/* 12000 (0.6 s) y 14400 (0.5 s) ahora soportados: el guard de
		 * process() usa un límite dinámico (1.2x el periodo nominal,
		 * port de xyzzy42/tg por Trent Piepho) en vez de 0.5 s fijo. */
		{ 12000, 1 }, { 14400, 1 },
		{ 18000, 1 }, { 21600, 1 }, { 28800, 1 }, { 36000, 1 },
	};
	for(i = 0; i < (int)(sizeof(cases)/sizeof(cases[0])); i++) {
		char tag[32];
		snprintf(tag, sizeof(tag), "bph%d", cases[i].bph);
		test_clip(cases[i].bph, 4.0, 14.0, 1.0, 0.8, 0.0, 0.0,
		          cases[i].expect_signal, 2.0, 1.0, tag);
	}

	/* Ruido: la detección debe mantenerse. */
	test_clip(18000, 4.0, 14.0, 1.0, 0.8, 0.0, 0.05, 1, 10.0, 1.5, "noise5");
	test_clip(21600, 4.0, 14.0, 1.0, 0.8, 0.0, 0.20, 1, 10.0, 1.5, "noise20");
	/* Beat error real: tock desplazado +2 ms y +5 ms. */
	test_clip(21600, 4.0, 14.0, 1.0, 0.8, 2.0, 0.0, 1, 2.0, 1.0, "be2");
	test_clip(21600, 4.0, 14.0, 1.0, 0.8, 5.0, 0.0, 1, 2.0, 1.0, "be5");
	test_amp();

	/* Fixtures reales opcionales: si tests/fixtures/*.wav existen, deben al
	 * menos detectar señal (aserción laxa, el valor exacto varía por grabación). */
	{
		DIR *d = opendir("tests/fixtures");
		if(d) {
			struct dirent *e;
			int nfix = 0;
			while((e = readdir(d))) {
				size_t l = strlen(e->d_name);
				if(l > 4 && !strcmp(e->d_name + l - 4, ".wav")) {
					char path[1024];
					snprintf(path, sizeof(path), "tests/fixtures/%s", e->d_name);
					struct offline_result r;
					if(!analyze_audio_file(path, 0, DEFAULT_LA, 0, &r)) {
						nfix++;
						CHECK(r.signal == 1, "fixture detects signal");
						if(r.signal)
							printf("fixture %-24s signal=1 bph=%d rate=%+.2f be=%.2f amp=%.1f\n",
							       e->d_name, r.guessed_bph, r.rate, r.be, r.amp);
						else
							printf("fixture %-24s signal=0\n", e->d_name);
					}
				}
			}
			closedir(d);
			if(nfix == 0)
				printf("no fixtures: tests/fixtures/*.wav (opcional)\n");
		}
	}

	rmdir("test_dsp_out");
	if(failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	printf("dsp tests passed\n");
	return 0;
}