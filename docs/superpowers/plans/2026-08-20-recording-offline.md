# Grabación + Análisis Offline — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Permitir grabar el audio del micrófono a un WAV y analizar WAV existentes sin audio en vivo, reutilizando la misma pipeline DSP.

**Architecture:** Se añade un módulo WAV autónomo (`src/wav.c`/`src/wav.h`, sin dependencias externas) para lectura/escritura. En `audio.c` el ring buffer actual (`pa_buffers`/`write_pointer`/`timestamp`) se convierte en el almacén compartido: un «file source» bombea muestras del WAV al ring a velocidad real (o en modo rápido para headless) y PortAudio se pausa/reanuda al cambiar de fuente. Un hilo de grabación drena el ring a un WAV 16-bit. `src/offline.c` ofrece análisis headless (`--analyze`) que ejecuta la pipeline DSP directamente sobre el archivo cargado en memoria y devuelve rate/be/amp. La UI añade menús «Open recording…», «Record to file…»/«Stop recording» y un indicador de fuente, deshabilitando controles irrelevantes en modo archivo.

**Tech Stack:** C99, GTK3, PortAudio, FFTW (existente). Autotools (`make check` para tests).

**Rama:** `feature/recording-offline` (ya creada desde `master`).

---

## Estructura de archivos

- Create: `src/wav.h`, `src/wav.c`, `src/offline.c`, `tests/test_wav.c`
- Modify: `src/tg.h`, `src/audio.c`, `src/interface.c`, `Makefile.am`, `docs/ROADMAP.md`, `README.md`, `docs/tg-timer.1`

Responsabilidades:
- `src/wav.h`/`src/wav.c` — lector/escritor WAV mínimo (PCM 8/16/24/32-bit y float32, downmix a mono). Sin dependencias, testeable.
- `src/offline.c` — `analyze_audio_file()`: carga WAV a memoria y ejecuta `process()` sobre ventanas deslizantes; expone resultados.
- `src/audio.c` — file source (bombeo al ring), grabación (thread), pause/resume de PortAudio, helpers de modo archivo.
- `src/interface.c` — menús y manejo de modos.

---

### Task 1: Infraestructura de tests (`make check`)

**Files:**
- Create: `tests/test_wav.c`
- Modify: `Makefile.am`

- [ ] **Step 1: Crear el directorio y un test trivial**

Crea `tests/test_wav.c`:

```c
#include <stdio.h>

int main(void)
{
	printf("test_wav placeholder\n");
	return 0;
}
```

- [ ] **Step 2: Registrar `check_PROGRAMS`/`TESTS` en `Makefile.am`**

Añade al final de `Makefile.am`:

```make
check_PROGRAMS = tests/test_wav
tests_test_wav_SOURCES = tests/test_wav.c
TESTS = tests/test_wav
```

- [ ] **Step 3: Regenerar el build y ejecutar `make check`**

```bash
autoreconf -i 2>&1 | tail -5
./configure 2>&1 | tail -3
make check 2>&1 | tail -15
```

Expected: `tests/test_wav` compila, `PASS: tests/test_wav` y `Testsuite summary` con 0 FAIL.

- [ ] **Step 4: Commit**

```bash
git add tests/test_wav.c Makefile.am Makefile.in configure aclocal.m4 autom4te.cache
git commit -m "Add make check test harness"
```

(Nota: incluye archivos regenerados de autotools que haya tocado el autoreconf; verifica con `git status` y añade solo lo regenerado.)

---

### Task 2: WAV writer

**Files:**
- Create: `src/wav.h`, `src/wav.c`
- Modify: `tests/test_wav.c`, `Makefile.am`

- [ ] **Step 1: Escribir el test (writer → read-back del header)**

Sustituye `tests/test_wav.c`:

```c
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
```

- [ ] **Step 2: Ejecutar el test para verlo fallar**

```bash
make check 2>&1 | tail -15
```

Expected: compila pero falla (o no compila por falta de `wav.h`). Es el paso «failing test»; el fallo de compilación por header ausente cuenta como red.

- [ ] **Step 3: Implementar `src/wav.h`**

```c
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

struct wav_reader {
	FILE *f;
	unsigned rate;
	unsigned channels;
	unsigned bits;
	unsigned fmt;          /* 1 = PCM, 3 = IEEE float */
	uint64_t data_start;   /* byte offset of the data chunk */
	uint64_t data_bytes;
	uint64_t pos;          /* frames read so far */
	int ok;
};

/* Open a WAV file for reading. Returns 0 on success, -1 on error. */
int wav_open_read(const char *path, struct wav_reader *r);

/* Read up to `count` frames, downmixed to mono float. Returns frames read (0 at EOF). */
long wav_read_samples(struct wav_reader *r, float *out, long count);

/* Total number of frames in the file. */
uint64_t wav_get_length(const struct wav_reader *r);

int wav_reader_close(struct wav_reader *r);

#endif
```

- [ ] **Step 4: Implementar `src/wav.c` (writer primero)**

```c
/*
    tg
    ... (misma cabecera de licencia que wav.h) ...
*/

#include "tg.h"
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

	fwrite("RIFF", 1, 4, w->f);
	write_le32(w->f, 0);            /* patched in wav_close */
	fwrite("WAVE", 1, 4, w->f);
	fwrite("fmt ", 1, 4, w->f);
	write_le32(w->f, 16);
	write_le16(w->f, 1);            /* PCM */
	write_le16(w->f, channels);
	write_le32(w->f, rate);
	write_le32(w->f, rate * channels * (bits / 8));
	write_le16(w->f, channels * (bits / 8));
	write_le16(w->f, bits);
	fwrite("data", 1, 4, w->f);
	write_le32(w->f, 0);            /* patched in wav_close */
	return 0;
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
	if(fseek(w->f, 4, SEEK_SET)) { fclose(w->f); return -1; }
	write_le32(w->f, 36 + w->data_bytes);
	if(fseek(w->f, 40, SEEK_SET)) { fclose(w->f); return -1; }
	write_le32(w->f, w->data_bytes);
	int rc = fclose(w->f);
	w->ok = 0;
	return rc ? -1 : 0;
}
```

(El lector — `wav_open_read`, `wav_read_samples`, `wav_get_length`, `wav_reader_close` — se añade en la Task 3. Nota: el close del lector se llama `wav_reader_close` para no chocar con `wav_close(struct wav_writer*)`.)

- [ ] **Step 5: Enlazar `wav.c` al test y compilar**

En `Makefile.am`, actualiza:

```make
check_PROGRAMS = tests/test_wav
tests_test_wav_SOURCES = tests/test_wav.c src/wav.c
tests_test_wav_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/src
TESTS = tests/test_wav
```

```bash
autoreconf -i 2>&1 | tail -3 && ./configure 2>&1 | tail -2 && make check 2>&1 | tail -15
```

Expected: `PASS: tests/test_wav`.

- [ ] **Step 6: Commit**

```bash
git add src/wav.h src/wav.c tests/test_wav.c Makefile.am
git commit -m "Add WAV writer module with tests"
```

---

### Task 3: WAV reader

**Files:**
- Modify: `src/wav.c`, `tests/test_wav.c`

- [ ] **Step 1: Ampliar el test (round-trip write→read + downmix)**

Añade al final de `main()` en `tests/test_wav.c` (antes de `remove(path)`), y cambia `remove(path)` para que se ejecute al final:

```c
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
```

- [ ] **Step 2: Ejecutar el test para verlo fallar**

```bash
make check 2>&1 | tail -15
```

Expected: falla (funciones de lectura aún no definidas).

- [ ] **Step 3: Implementar el lector en `src/wav.c`**

```c
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
		fseek(r->f, (long)(csize + (csize & 1)), SEEK_CUR);   /* WAV chunks are even-padded */
	}
	if(!have_fmt || (fmt != 1 && fmt != 3) || channels == 0 || rate == 0 ||
	   bits < 8 || bits > 32 || bits % 8 != 0) goto error;

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
		fseek(r->f, (long)(csize + (csize & 1)), SEEK_CUR);   /* WAV chunks are even-padded */
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
			unsigned s;
			if(read_le16(r->f, &s)) return -1;
			v = (int16_t)s / 32768.0;
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
```

- [ ] **Step 4: Ejecutar el test para verlo pasar**

```bash
make check 2>&1 | tail -15
```

Expected: `PASS: tests/test_wav`.

- [ ] **Step 5: Commit**

```bash
git add src/wav.c tests/test_wav.c
git commit -m "Add WAV reader with mono downmix and tests"
```

---

### Task 4: File source — bombeo del WAV al ring buffer + pause/resume de PortAudio

**Files:**
- Modify: `src/tg.h`, `src/audio.c`

- [ ] **Step 1: Prototipos en `src/tg.h`**

Añade al bloque de audio.c (tras `void set_audio_light(bool light);`, tg.h:138):

```c
/* --- offline / file source --- */
int load_audio_file(const char *path);    /* 0 = ok, -1 = error */
int close_audio_file(void);               /* 0 = ok */
int get_audio_file_mode(void);            /* 1 si hay un archivo activo */
uint64_t get_audio_file_length(void);     /* frames totales */
uint64_t get_audio_file_position(void);   /* frames bombeados */
unsigned get_audio_file_rate(void);
void audio_file_restart(void);
void audio_file_set_fast(int fast);       /* headless: bombea todo de una vez */

/* Mic stream control (sin reiniciar Pa): */
int pause_portaudio(void);
int resume_portaudio(void);

/* --- recording --- */
int start_recording(const char *path);    /* 0 = ok */
int stop_recording(void);                 /* 0 = ok */
int get_recording(void);                  /* 1 si grabando */
```

- [ ] **Step 2: Estado del file source y helpers en `src/audio.c`**

Añade tras el bloque `filter_cutoff` (audio.c:34):

```c
#include "wav.h"

static struct file_source {
	char *path;
	struct wav_reader rd;
	uint64_t length;      /* frames totales del archivo */
	uint64_t ring_pos;    /* frames bombeados al ring */
	int64_t start_clock;  /* g_get_monotonic_time() us en (re)start */
	int fast;             /* headless: bombear todo */
	int active;
} file_src;
```

- [ ] **Step 3: `pause_portaudio` / `resume_portaudio`**

En `start_portaudio` guarda el stream en una estática y añade las dos funciones:

```c
static PaStream *pa_stream = NULL;
```

Dentro de `start_portaudio`, tras `Pa_StartStream(stream);` añade `pa_stream = stream;`.

Añade al final de `audio.c`:

```c
int pause_portaudio(void)
{
	if(pa_stream) {
		PaError err = Pa_StopStream(pa_stream);
		if(err != paNoError) {
			error("Error pausing audio: %s", Pa_GetErrorText(err));
			return 1;
		}
	}
	return 0;
}

int resume_portaudio(void)
{
	if(pa_stream) {
		PaError err = Pa_StartStream(pa_stream);
		if(err != paNoError) {
			error("Error resuming audio: %s", Pa_GetErrorText(err));
			return 1;
		}
	}
	return 0;
}
```

- [ ] **Step 4: Bombeo del archivo al ring (bloquea bajo `audio_mutex`)**

Añade a `audio.c`:

```c
/* Debe llamarse con audio_mutex tomado. */
static void file_pump_locked(void)
{
	if(!file_src.active) return;
	uint64_t target;
	if(file_src.fast) {
		target = file_src.length;
	} else {
		int64_t elapsed = g_get_monotonic_time() - file_src.start_clock;
		uint64_t sec_frames = elapsed > 0
			? (uint64_t)((double)elapsed * file_src.rd.rate / 1e6)
			: 0;
		target = sec_frames < file_src.length ? sec_frames : file_src.length;
	}
	while(file_src.ring_pos < target) {
		long want = (long)(target - file_src.ring_pos);
		if(want > 4096) want = 4096;
		float tmp[4096];
		long got = wav_read_samples(&file_src.rd, tmp, want);
		if(got <= 0) { file_src.ring_pos = target; break; }
		unsigned wp = write_pointer;
		unsigned len = MIN((unsigned)got, PA_BUFF_SIZE - wp);
		memcpy(pa_buffers + wp, tmp, len * sizeof(float));
		if((unsigned)got > len)
			memcpy(pa_buffers, tmp + len, (got - len) * sizeof(float));
		write_pointer = (wp + (unsigned)got) % PA_BUFF_SIZE;
		timestamp += (uint64_t)got;
		file_src.ring_pos += (uint64_t)got;
	}
}
```

- [ ] **Step 5: Cargar/cerrar archivo y helpers**

Añade a `audio.c`:

```c
int load_audio_file(const char *path)
{
	struct file_source fs;
	memset(&fs, 0, sizeof(fs));
	if(wav_open_read(path, &fs.rd)) return -1;
	fs.length = wav_get_length(&fs.rd);
	fs.path = strdup(path);
	fs.active = 1;
	/* Inicializar el reloj ANTES de publicar file_src: si el compute thread
	 * entra en file_pump_locked() con start_clock == 0, elapsed seria enorme y
	 * volcaria todo el archivo al ring de golpe. */
	fs.start_clock = g_get_monotonic_time();

	pthread_mutex_lock(&audio_mutex);
	if(file_src.active) {
		wav_reader_close(&file_src.rd);
		free(file_src.path);
	}
	file_src = fs;
	info.light = false;
	memset(pa_buffers, 0, sizeof(pa_buffers));
	write_pointer = 0;
	timestamp = 0;
	pthread_mutex_unlock(&audio_mutex);

	audio_file_restart();
	return 0;
}

int close_audio_file(void)
{
	pthread_mutex_lock(&audio_mutex);
	if(file_src.active) {
		wav_reader_close(&file_src.rd);
		free(file_src.path);
		memset(&file_src, 0, sizeof(file_src));
		memset(pa_buffers, 0, sizeof(pa_buffers));
		write_pointer = 0;
		timestamp = 0;
	}
	pthread_mutex_unlock(&audio_mutex);
	return 0;
}

int get_audio_file_mode(void)
{
	int m;
	pthread_mutex_lock(&audio_mutex);
	m = file_src.active;
	pthread_mutex_unlock(&audio_mutex);
	return m;
}

uint64_t get_audio_file_length(void)
{
	uint64_t v;
	pthread_mutex_lock(&audio_mutex);
	v = file_src.active ? file_src.length : 0;
	pthread_mutex_unlock(&audio_mutex);
	return v;
}

uint64_t get_audio_file_position(void)
{
	uint64_t v;
	pthread_mutex_lock(&audio_mutex);
	v = file_src.active ? file_src.ring_pos : 0;
	pthread_mutex_unlock(&audio_mutex);
	return v;
}

unsigned get_audio_file_rate(void)
{
	unsigned v = 0;
	pthread_mutex_lock(&audio_mutex);
	if(file_src.active) v = file_src.rd.rate;
	pthread_mutex_unlock(&audio_mutex);
	return v;
}

void audio_file_restart(void)
{
	pthread_mutex_lock(&audio_mutex);
	if(file_src.active) {
		fseek(file_src.rd.f, (long)file_src.rd.data_start, SEEK_SET);
		file_src.rd.pos = 0;
		file_src.ring_pos = 0;
		file_src.start_clock = g_get_monotonic_time();
		memset(pa_buffers, 0, sizeof(pa_buffers));
		write_pointer = 0;
		timestamp = 0;
	}
	pthread_mutex_unlock(&audio_mutex);
}

void audio_file_set_fast(int fast)
{
	pthread_mutex_lock(&audio_mutex);
	file_src.fast = fast;
	pthread_mutex_unlock(&audio_mutex);
}
```

- [ ] **Step 6: Enlazar el pump en `fill_buffers` y `get_recent_audio`**

En `fill_buffers` (audio.c:288), tras `pthread_mutex_lock(&audio_mutex);` y antes de leer `ts`, añade:

```c
	if(file_src.active) file_pump_locked();
```

En `get_recent_audio` (audio.c:373), tras `pthread_mutex_lock(&audio_mutex);` añade:

```c
	if(file_src.active) file_pump_locked();
```

- [ ] **Step 7: Compilar el binario principal**

Añade `src/wav.c` a `tg_timer_SOURCES` en `Makefile.am`:

```make
tg_timer_SOURCES = src/algo.c \
		   src/audio.c \
		   src/computer.c \
		   src/config.c \
		   src/interface.c \
		   src/output_panel.c \
		   src/serializer.c \
		   src/tg.h \
		   src/wav.c
```

(`src/offline.c` se añade a `tg_timer_SOURCES` en la Task 5, que es cuando se crea; así este commit compila.)

```bash
make tg-timer 2>&1 | grep -iE "warning|error"; echo "build done"
```

Expected: sin errores nuevos. El pump con `audio_file_set_fast` y `--analyze` se verifican en la Task 5.

- [ ] **Step 8: Commit**

```bash
git add src/tg.h src/audio.c Makefile.am
git commit -m "Add offline file source feeding the ring buffer"
```

---

### Task 5: Análisis offline headless (`src/offline.c` + `--analyze`)

**Files:**
- Create: `src/offline.c`
- Modify: `src/tg.h`, `src/interface.c`, `Makefile.am`

- [ ] **Step 1: Declarar la API en `src/tg.h`**

Añade el bloque de offline.c (tras los de audio.c):

```c
/* offline.c */
struct offline_result {
	int signal;         /* 1 si se obtuvo un resultado bueno */
	int guessed_bph;
	double rate;        /* s/d */
	double be;          /* ms */
	double amp;         /* deg (0 = no disponible) */
};

int analyze_audio_file(const char *path, int bph, double la, double cal, struct offline_result *res);
```

- [ ] **Step 2: Implementar `src/offline.c`**

```c
/*
    tg
    ... (misma cabecera de licencia) ...
*/

#include "tg.h"
#include "wav.h"
#include <string.h>

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
```

(Nota de revisión: añadir un tope de memoria antes del `malloc`, p. ej.
`#define OFFLINE_MAX_FRAMES 200000000ull` y `if(nframes > OFFLINE_MAX_FRAMES) { wav_reader_close(&rd); return 1; }`.)

- [ ] **Step 3: Hook `--analyze` en `main()` (`src/interface.c`)**

En `main()`, justo tras `gtk_disable_setlocale();` (interface.c:1115) y dentro de `#ifdef DEBUG`:

```c
#ifdef DEBUG
	if(argc > 2 && !strcmp("analyze", argv[1])) {
		struct offline_result r;
		if(analyze_audio_file(argv[2], 0, DEFAULT_LA, 0, &r)) {
			fprintf(stderr, "analyze failed for %s\n", argv[2]);
			return 1;
		}
		printf("signal %d\nbph %d\nrate %.3f s/d\nbe %.3f ms\namp %.1f deg\n",
		       r.signal, r.guessed_bph, r.rate, r.be, r.amp);
		return 0;
	}
#endif
```

- [ ] **Step 4: Añadir `src/offline.c` al binario y regenerar**

En `Makefile.am`, añade `src/offline.c` a `tg_timer_SOURCES` (junto a `src/wav.c`):

```make
tg_timer_SOURCES = src/algo.c \
		   src/audio.c \
		   src/computer.c \
		   src/config.c \
		   src/interface.c \
		   src/offline.c \
		   src/output_panel.c \
		   src/serializer.c \
		   src/tg.h \
		   src/wav.c
```

```bash
make tg-timer-dbg 2>&1 | grep -iE "warning|error"; echo "build done"
```

- [ ] **Step 5: Generar un clip de prueba sintético y verificar**

Crea `tests/gen_tick.py` (genera un pulso resonante cada beat a 18000 bph, 3 s, 44100 Hz mono 16-bit):

```python
#!/usr/bin/env python3
import math, struct, sys

rate = 44100
bph = 18000
period = 7200 / bph          # s por beat (0.4 s)
dur = 3.0
freq = 3000.0                # frecuencia de resonancia del tick

def tick_wave(t, t0):
    dt = t - t0
    if dt < 0 or dt > 0.012:
        return 0.0
    return math.sin(2 * math.pi * freq * dt) * math.exp(-dt * 900.0)

n = int(rate * dur)
samples = []
for i in range(n):
    t = i / rate
    v = tick_wave(t, (i // int(period * rate)) * period)
    samples.append(v)

with open(sys.argv[1], "wb") as f:
    data = b"".join(struct.pack("<h", max(-32768, min(32767, int(s * 32767)))) for s in samples)
    f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE")
    f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16))
    f.write(b"data" + struct.pack("<I", len(data)) + data)
```

```bash
python3 tests/gen_tick.py /tmp/opencode/tick.wav
make tg-timer-dbg 2>&1 | grep -iE "warning|error"
./tg-timer-dbg analyze /tmp/opencode/tick.wav
```

Expected: `signal 1`, `bph 18000`, `rate` ≈ 0 (±1 s/d), y valores coherentes.

- [ ] **Step 5: Commit**

```bash
git add src/offline.c src/tg.h src/interface.c tests/gen_tick.py
git commit -m "Add headless offline analysis (--analyze)"
```

---

### Task 6: Grabación de micrófono a WAV

**Files:**
- Modify: `src/audio.c`, `src/tg.h`

- [ ] **Step 1: Prototipos** (ya añadidos en Task 4 Step 1).

- [ ] **Step 2: Contador de muestras escritas por el callback**

Añade el global junto a `timestamp` (audio.c:25):

```c
/* Nº real de muestras almacenadas en pa_buffers (distinto de timestamp en
 * modo light, donde timestamp cuenta frames originales pero se guarda la
 * mitad). Lo usa la grabación para drenar el ring. */
static uint64_t mic_written = 0;
```

En el callback, tras actualizar `write_pointer` y `timestamp` bajo el mutex (audio.c:116-119), añade:

```c
		mic_written += (uint64_t)(info->light ? (frame_count + 1) / 2 : frame_count);
```

(Colócalo dentro del mismo bloque con mutex, después de `timestamp += frame_count;`.
`mic_written` aproxima las muestras reales almacenadas (en light se guarda ~la
mitad); los índices de anillo resultantes siempre están en rango, así que un
pequeño sobreconteo en light solo puede leer muestras algo más antiguas.)

- [ ] **Step 3: Estado del recorder y el thread de drenaje**

Añade al final de `audio.c`:

```c
static struct recorder {
	int active;
	char *path;
	pthread_t thread;
	struct wav_writer w;
	uint64_t start;      /* mic_written al comenzar la grabación */
	uint64_t recorded;   /* muestras ya escritas al archivo */
} rec;

static void record_drain(void)
{
	pthread_mutex_lock(&audio_mutex);
	if(file_src.active) { pthread_mutex_unlock(&audio_mutex); return; }
	uint64_t target = mic_written;
	uint64_t first = rec.start + rec.recorded;
	if(first >= target) { pthread_mutex_unlock(&audio_mutex); return; }
	uint64_t n = target - first;
	/* Si nos quedamos atrás y el inicio se salió del ring (32 s), saltar. */
	if(n > PA_BUFF_SIZE) { first = target - PA_BUFF_SIZE; n = PA_BUFF_SIZE; }
	/* Copiar hasta 1 s por pasada para acotar el lock. */
	if(n > (uint64_t)PA_SAMPLE_RATE) n = PA_SAMPLE_RATE;

	float *tmp = malloc(n * sizeof(float));
	if(!tmp) { pthread_mutex_unlock(&audio_mutex); return; }
	uint64_t k;
	for(k = 0; k < n; k++) {
		uint64_t idx = (first + k) % PA_BUFF_SIZE;
		tmp[k] = pa_buffers[idx];
	}
	rec.recorded = first + n - rec.start;
	pthread_mutex_unlock(&audio_mutex);

	if(wav_write_samples(&rec.w, tmp, (int)n)) {
		stop_recording();
	}
	free(tmp);
}

static void *record_thread(void *unused)
{
	UNUSED(unused);
	for(;;) {
		g_usleep(100000);   /* 100 ms */
		pthread_mutex_lock(&audio_mutex);
		int active = rec.active;
		pthread_mutex_unlock(&audio_mutex);
		if(!active) break;
		record_drain();
	}
	return NULL;
}

int start_recording(const char *path)
{
	pthread_mutex_lock(&audio_mutex);
	if(rec.active || file_src.active) { pthread_mutex_unlock(&audio_mutex); return -1; }
	unsigned rate = info.light ? PA_SAMPLE_RATE / 2 : PA_SAMPLE_RATE;
	pthread_mutex_unlock(&audio_mutex);

	if(wav_open_write(path, rate, 1, 16, &rec.w)) return -1;
	rec.path = strdup(path);
	pthread_mutex_lock(&audio_mutex);
	rec.start = mic_written;
	rec.recorded = 0;
	rec.active = 1;
	pthread_mutex_unlock(&audio_mutex);
	if(pthread_create(&rec.thread, NULL, record_thread, NULL)) {
		pthread_mutex_lock(&audio_mutex);
		rec.active = 0;
		pthread_mutex_unlock(&audio_mutex);
		wav_close(&rec.w);
		free(rec.path);
		memset(&rec, 0, sizeof(rec));
		return -1;
	}
	return 0;
}

int stop_recording(void)
{
	pthread_mutex_lock(&audio_mutex);
	if(!rec.active) { pthread_mutex_unlock(&audio_mutex); return -1; }
	rec.active = 0;
	pthread_mutex_unlock(&audio_mutex);

	/* Si stop_recording se llama desde el propio record_thread (p. ej. ante un
	 * fallo de escritura), no hay que joinearnos a nosotros mismos (EDEADLK). */
	if(!pthread_equal(pthread_self(), rec.thread))
		pthread_join(rec.thread, NULL);
	/* drenar lo que quede */
	record_drain();
	wav_close(&rec.w);
	free(rec.path);
	memset(&rec, 0, sizeof(rec));
	return 0;
}

int get_recording(void)
{
	int a;
	pthread_mutex_lock(&audio_mutex);
	a = rec.active;
	pthread_mutex_unlock(&audio_mutex);
	return a;
}
```

(Nota: el contador `mic_written` debe ponerse a 0 en TODOS los sitios que
resetean el ring (`write_pointer = 0; timestamp = 0;`): `set_audio_light`,
`audio_file_restart`, `load_audio_file` y `close_audio_file`. De lo contrario
se rompe la invarianza `mic_written % PA_BUFF_SIZE == write_pointer` y la
grabación tras un cambio de modo light/archivo lee posiciones desalineadas.)

- [ ] **Step 4: Compilar y verificar**

```bash
make tg-timer-dbg 2>&1 | grep -iE "warning|error"; echo "build done"
```

Expected: sin errores.

- [ ] **Step 5: Commit**

```bash
git add src/audio.c
git commit -m "Add microphone recording to WAV"
```

---

### Task 7: UI — menús, indicador de fuente y modos

**Files:**
- Modify: `src/interface.c`, `src/tg.h`

- [ ] **Step 1: Campos en `struct main_window` (`src/tg.h`)**

Añade dentro de `struct main_window` (tras `filter_cutoff`, tg.h:280):

```c
	GtkWidget *source_label;
	GtkWidget *record_item;
	GtkWidget *stop_record_item;
	GtkWidget *close_rec_item;
	gchar *pending_audio_file;   /* ruta a cargar tras reiniciar computer */
	int close_audio;             /* cerrar archivo al reiniciar computer */
	int restart_computer;        /* forzar reinicio de computer sin tocar Pa */
	int audio_file_mode;         /* 1 = analizando un archivo */
```

- [ ] **Step 2: Handler de fuente/archivo en `recompute` y `computer_terminated`**

En `recompute()` (interface.c:243), cambia la condición:

```c
		if(w->is_light != w->computer->actv->is_light || w->restart_portaudio || w->restart_computer) {
			w->restart_computer = 0;
			kill_computer(w);
```

En `computer_terminated()` (interface.c:161), dentro del bloque `else`, tras `w->restart_portaudio = 0;` y antes de reabrir audio, añade:

```c
		if(w->close_audio) {
			close_audio_file();
			w->close_audio = 0;
			w->audio_file_mode = 0;
			w->nominal_sr = PA_SAMPLE_RATE;
		}
```

Y tras `struct computer *c = start_computer(w->nominal_sr, w->bph, w->la, w->cal, w->is_light);` y su comprobación de éxito, añade:

```c
		if(w->pending_audio_file) {
			if(load_audio_file(w->pending_audio_file)) {
				error("Failed to open recording: %s", w->pending_audio_file);
				w->audio_file_mode = 0;
				w->nominal_sr = PA_SAMPLE_RATE;
				resume_portaudio();
			} else {
				w->audio_file_mode = 1;
				w->nominal_sr = get_audio_file_rate();
			}
			g_free(w->pending_audio_file);
			w->pending_audio_file = NULL;
		}
```

(En modo archivo, `is_light` debe estar a 0: el handler de open lo fuerza.)

- [ ] **Step 3: Handlers de menú**

Añade tras `handle_cutoff_change` (interface.c:336):

```c
static void update_audio_mode_ui(struct main_window *w);

static void handle_open_recording(GtkMenuItem *m, struct main_window *w)
{
	UNUSED(m);
	if(get_recording()) return;
	GtkWidget *dialog = gtk_file_chooser_dialog_new("Open recording",
			GTK_WINDOW(w->window), GTK_FILE_CHOOSER_ACTION_OPEN,
			"Cancel", GTK_RESPONSE_CANCEL, "Open", GTK_RESPONSE_ACCEPT, NULL);
	GtkFileFilter *f = gtk_file_filter_new();
	gtk_file_filter_set_name(f, ".wav");
	gtk_file_filter_add_pattern(f, "*.wav");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), f);
	gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(dialog), f);

	if(GTK_RESPONSE_ACCEPT == gtk_dialog_run(GTK_DIALOG(dialog))) {
		GFile *gf = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
		char *filename = g_file_get_path(gf);
		g_object_unref(gf);
		if(filename) {
			if(w->audio_file_mode) close_audio_file();
			w->pending_audio_file = g_strdup(filename);
			w->is_light = 0;
			w->restart_computer = 1;
			pause_portaudio();
			recompute(w);
			g_free(filename);
		}
	}
	gtk_widget_destroy(dialog);
}

static void handle_close_recording(GtkMenuItem *m, struct main_window *w)
{
	UNUSED(m);
	if(get_recording()) return;
	w->close_audio = 1;
	w->restart_computer = 1;
	resume_portaudio();
	recompute(w);
}

static void handle_start_recording(GtkMenuItem *m, struct main_window *w)
{
	UNUSED(m);
	if(w->audio_file_mode || get_recording()) return;
	GtkWidget *dialog = gtk_file_chooser_dialog_new("Record to file",
			GTK_WINDOW(w->window), GTK_FILE_CHOOSER_ACTION_SAVE,
			"Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "recording.wav");
	GtkFileFilter *f = gtk_file_filter_new();
	gtk_file_filter_set_name(f, ".wav");
	gtk_file_filter_add_pattern(f, "*.wav");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), f);

	if(GTK_RESPONSE_ACCEPT == gtk_dialog_run(GTK_DIALOG(dialog))) {
		GFile *gf = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
		char *filename = g_file_get_path(gf);
		g_object_unref(gf);
		if(filename) {
			if(start_recording(filename))
				error("Failed to start recording to %s", filename);
			else
				update_audio_mode_ui(w);
			g_free(filename);
		}
	}
	gtk_widget_destroy(dialog);
}

static void handle_stop_recording(GtkMenuItem *m, struct main_window *w)
{
	UNUSED(m);
	stop_recording();
	update_audio_mode_ui(w);
}
```

- [ ] **Step 4: `update_audio_mode_ui` y el indicador de fuente**

```c
static void update_audio_mode_ui(struct main_window *w)
{
	int file_mode = w->audio_file_mode;
	int recording = get_recording();

	gtk_widget_set_sensitive(w->device_combo_box, !file_mode);
	gtk_widget_set_sensitive(w->gain_spin_button, !file_mode);
	gtk_widget_set_sensitive(w->cal_button, !file_mode);

	const char *txt;
	if(recording) {
		txt = "recording...";
	} else if(file_mode && w->pending_audio_file) {
		txt = w->pending_audio_file;
	} else if(file_mode) {
		txt = "recording";
	} else {
		txt = "mic";
	}
	gtk_label_set_text(GTK_LABEL(w->source_label), txt);

	gtk_widget_set_sensitive(w->record_item, !file_mode && !recording);
	gtk_widget_set_sensitive(w->stop_record_item, recording);
	gtk_widget_set_sensitive(w->close_rec_item, file_mode && !recording);
}
```

(Nota: para mostrar el nombre corto del archivo en el label, en `handle_open_recording` guarda también `g_strdup(basename)` en una variable para el label; se simplifica aquí usando el path. Ajusta si quieres solo el basename.)

- [ ] **Step 5: Añadir widgets en `init_main_window`**

Tras el cutoff spin (interface.c:870), añade el indicador:

```c
	// Source indicator
	label = gtk_label_new("mic");
	w->source_label = label;
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
```

En el menú de comandos, tras el ítem «Save all snapshots» (interface.c:923) y antes del separador, añade:

```c
	// ... Open recording
	GtkWidget *open_rec_item = gtk_menu_item_new_with_label("Open recording...");
	gtk_menu_shell_append(GTK_MENU_SHELL(command_menu), open_rec_item);
	g_signal_connect(open_rec_item, "activate", G_CALLBACK(handle_open_recording), w);

	// ... Close recording
	w->close_rec_item = gtk_menu_item_new_with_label("Close recording");
	gtk_menu_shell_append(GTK_MENU_SHELL(command_menu), w->close_rec_item);
	g_signal_connect(w->close_rec_item, "activate", G_CALLBACK(handle_close_recording), w);
	gtk_widget_set_sensitive(w->close_rec_item, FALSE);

	gtk_menu_shell_append(GTK_MENU_SHELL(command_menu), gtk_separator_menu_item_new());

	// ... Start recording
	w->record_item = gtk_menu_item_new_with_label("Record to file...");
	gtk_menu_shell_append(GTK_MENU_SHELL(command_menu), w->record_item);
	g_signal_connect(w->record_item, "activate", G_CALLBACK(handle_start_recording), w);

	// ... Stop recording
	w->stop_record_item = gtk_menu_item_new_with_label("Stop recording");
	gtk_menu_shell_append(GTK_MENU_SHELL(command_menu), w->stop_record_item);
	g_signal_connect(w->stop_record_item, "activate", G_CALLBACK(handle_stop_recording), w);
	gtk_widget_set_sensitive(w->stop_record_item, FALSE);
```

En `start_interface`, tras `init_main_window(w);` (interface.c:1074), inicializa el label de modo:

```c
	update_audio_mode_ui(w);
```

- [ ] **Step 6: Compilar y verificar**

```bash
make tg-timer-dbg 2>&1 | grep -iE "warning|error"; echo "build done"
make test 2>&1 | tail -5
```

Expected: compila sin errores; `make test` (smoke test 3 s) termina sin crash.

- [ ] **Step 7: Commit**

```bash
git add src/interface.c src/tg.h
git commit -m "Add recording/offline UI (open, record, source indicator)"
```

---

### Task 8: Verificación final, docs e integración

**Files:**
- Modify: `docs/ROADMAP.md`, `README.md`, `docs/tg-timer.1`

- [ ] **Step 1: Verificación completa**

```bash
make check 2>&1 | tail -10
make tg-timer 2>&1 | grep -iE "warning|error"; echo "no warnings"
python3 tests/gen_tick.py /tmp/opencode/tick.wav
./tg-timer-dbg analyze /tmp/opencode/tick.wav
```

Expected: `make check` PASS; sin warnings; analyze reporta signal=1, bph≈18000, rate≈0.

- [ ] **Step 2: Documentar en `docs/ROADMAP.md`**

Marca la Fase 1 como completada: cambia el título a `## Fase 1 — Grabación + análisis offline (completada)` y bajo el encabezado añade:

```markdown
**Estado:** completada. Añade `src/wav.c`, `src/offline.c` y grabación a WAV.
Uso: menú Command → «Open recording…» para analizar un WAV sin micrófono;
«Record to file…» para grabar; CLI `tg-timer-dbg analyze <archivo.wav>`.
```

- [ ] **Step 3: Documentar en `README.md`**

Tras la sección de instalación, añade una sección breve:

```markdown
## Recording and offline analysis

- **Record**: Command menu → *Record to file...* saves the mic input to a
  WAV file; *Stop recording* finalizes it.
- **Analyze offline**: Command menu → *Open recording...* loads a WAV and
  runs the same analysis pipeline without needing live audio. While a
  recording is open, the timegrapher replays it at real time.
- **Headless**: `tg-timer-dbg analyze file.wav` (debug build) prints
  rate / beat error / amplitude / BPH and exits.
```

- [ ] **Step 4: Actualizar `docs/tg-timer.1`**

Añade una línea de sinopsis y una sección `RECORDING AND OFFLINE ANALYSIS` corta:

```markdown
.SH SYNOPSIS
.B tg-timer-dbg analyze
.IR file.wav
.RI (debug build)

.SH RECORDING AND OFFLINE ANALYSIS
Record the microphone to a WAV with the \fBRecord to file...\fP menu item.
Open a WAV with \fBOpen recording...\fP to analyze it without live audio.
The debug build also supports \fBtg-timer-dbg analyze file.wav\fP for
headless analysis.
```

- [ ] **Step 5: Commit final y push de la rama**

```bash
git add -A
git commit -m "Document recording and offline analysis (Fase 1)"
git push origin feature/recording-offline
```

- [ ] **Step 6: Integrar a `master` (tras aprobación/PR)**

```bash
git checkout master && git merge feature/recording-offline && git push origin master
git branch -d feature/recording-offline
```

---

## Self-review

- **Cobertura spec:** grabar → Task 6 + 7; abrir WAV offline → Task 4 + 7; fuente abstracta (ring compartido) → Task 4; base para tests → Task 1-3 + 5. Todos los puntos del roadmap Fase 1 cubiertos.
- **Placeholders:** no hay TBD/TODO. Los pasos contienen código completo.
- **Consistencia:** `file_src`, `rec`, `audio_file_*`, `analyze_audio_file`, `--analyze`, `update_audio_mode_ui`, `pending_audio_file` se usan con el mismo nombre en todas las tasks. `offline.c` se añade a `tg_timer_SOURCES` en Task 4 Step 7 (creado en Task 5) — el orden de commits está pensado para que cada commit compile (ver nota de Task 4 Step 7: crear stub o compilar tras Task 5).
