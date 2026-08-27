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
#include <portaudio.h>

/* Huge buffer of audio */
float pa_buffers[PA_BUFF_SIZE];
int write_pointer = 0;
uint64_t timestamp = 0;
pthread_mutex_t audio_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Nº real de muestras almacenadas en pa_buffers (distinto de timestamp en
 * modo light, donde timestamp cuenta frames originales pero se guarda la
 * mitad). Lo usa la grabación para drenar el ring. */
static uint64_t mic_written = 0;

static PaStream *pa_stream = NULL;

/* Input gain multiplier applied in the PA callback before storing into
 * pa_buffers. Default 1.0 (no amplification). Update via set_audio_gain(). */
static double audio_gain = 1.0;

static struct file_source {
	char *path;
	struct wav_reader rd;
	uint64_t length;      /* frames totales del archivo */
	uint64_t ring_pos;    /* frames bombeados al ring */
	int64_t start_clock;  /* g_get_monotonic_time() us en (re)start */
	int fast;             /* headless: bombear todo */
	int active;
} file_src;

static void file_pump_locked(void);

void set_audio_gain(double g)
{
	if(g < 0.1) g = 0.1;
	if(g > 100.0) g = 100.0;
	pthread_mutex_lock(&audio_mutex);
	audio_gain = g;
	pthread_mutex_unlock(&audio_mutex);
}

/* Data for PA callback to use */
static struct callback_info {
	int 	channels;	//!< Number of channels
	bool	light;		//!< Light algorithm in use, copy half data
} info;

static int paudio_callback(const void *input_buffer,
			   void *output_buffer,
			   unsigned long frame_count,
			   const PaStreamCallbackTimeInfo *time_info,
			   PaStreamCallbackFlags status_flags,
			   void *data)
{
	UNUSED(output_buffer);
	UNUSED(time_info);
	UNUSED(status_flags);
	const float *input_samples = (const float*)input_buffer;
	unsigned long i;
	const struct callback_info *info = data;
	unsigned wp = write_pointer;

	/* Snapshot the current gain once per callback. Held under the mutex to
	 * avoid tearing a double; the cost is negligible vs the rest of the
	 * callback. */
	pthread_mutex_lock(&audio_mutex);
	float g = (float)audio_gain;
	pthread_mutex_unlock(&audio_mutex);

	if (info->light) {
		static bool even = true;
		/* Copy every other sample.  It would be much more efficient to
		 * just drop the sample rate if the sound hardware supports it.
		 * This would also avoid the aliasing effects that this simple
		 * decimation without a low-pass filter causes.  */
		if(info->channels == 1) {
			for(i = even ? 0 : 1; i < frame_count; i += 2) {
				pa_buffers[wp++] = input_samples[i] * g;
				if (wp >= PA_BUFF_SIZE) wp -= PA_BUFF_SIZE;
			}
		} else {
			for(i = even ? 0 : 2; i < frame_count*2; i += 4) {
				pa_buffers[wp++] = (input_samples[i] + input_samples[i+1]) * g;
				if (wp >= PA_BUFF_SIZE) wp -= PA_BUFF_SIZE;
			}
		}
		/* Keep track if we have processed an even number of frames, so
		 * we know if we should drop the 1st or 2nd frame next callback. */
		if(frame_count % 2) even = !even;
	} else {
		const unsigned len = MIN(frame_count, PA_BUFF_SIZE - wp);
		if(info->channels == 1) {
			if(g == 1.0f) {
				memcpy(pa_buffers + wp, input_samples, len * sizeof(*pa_buffers));
				if(len < frame_count)
					memcpy(pa_buffers, input_samples + len, (frame_count - len) * sizeof(*pa_buffers));
			} else {
				for(i = 0; i < len; i++)
					pa_buffers[wp + i] = input_samples[i] * g;
				if(len < frame_count)
					for(i = len; i < frame_count; i++)
						pa_buffers[i - len] = input_samples[i] * g;
			}
		} else {
			for(i = 0; i < len; i++)
				pa_buffers[wp + i] = (input_samples[2u*i] + input_samples[2u*i + 1u]) * g;
			if(len < frame_count)
				for(i = len; i < frame_count; i++)
					pa_buffers[i - len] = (input_samples[2u*i] + input_samples[2u*i + 1u]) * g;
		}
		wp = (wp + frame_count) % PA_BUFF_SIZE;
	}
	pthread_mutex_lock(&audio_mutex);
	write_pointer = wp;
	timestamp += frame_count;
	mic_written += (uint64_t)(info->light ? (frame_count + 1) / 2 : frame_count);
	pthread_mutex_unlock(&audio_mutex);
	return 0;
}

/* Enumerate all input-capable devices without probing format. Probing via
 * Pa_IsFormatSupported can block on PulseAudio for ~30s per device, making
 * the app take minutes to open. Listing everything and relying on Fix A
 * (fallback to system default) when a chosen device fails to open keeps the
 * app responsive while still exposing every available input. */
int get_input_device_count(void)
{
	int i, n = Pa_GetDeviceCount();
	if(n < 0) return 0;
	int count = 0;
	for(i = 0; i < n; i++) {
		const PaDeviceInfo *di = Pa_GetDeviceInfo(i);
		if(!di) continue;
		if(di->maxInputChannels > 0) count++;
	}
	return count;
}

const char *get_input_device_name(int index)
{
	if(index < 0) return NULL;
	int i, n = Pa_GetDeviceCount();
	int count = 0;
	for(i = 0; i < n; i++) {
		const PaDeviceInfo *di = Pa_GetDeviceInfo(i);
		if(!di) continue;
		if(di->maxInputChannels <= 0) continue;
		if(count == index) return di->name;
		count++;
	}
	return NULL;
}

/* Iguala dos nombres de dispositivo, ignorando el sufijo "(hw:X,Y)"
 * (el índice ALSA cambia al reconectar el USB). */
static int device_name_matches(const char *a, const char *b)
{
	if(!strcmp(a, b)) return 1;
	const char *pa = strrchr(a, '(');
	const char *pb = strrchr(b, '(');
	if(pa && pb && pa > a && pb > b) {
		size_t la = pa - a;
		size_t lb = pb - b;
		if(la == lb && !strncmp(a, b, la))
			return 1;
	}
	return 0;
}

int find_input_device_by_name(const char *name)
{
	if(!name || !*name) return paNoDevice;
	int i, n = Pa_GetDeviceCount();
	for(i = 0; i < n; i++) {
		const PaDeviceInfo *di = Pa_GetDeviceInfo(i);
		if(!di) continue;
		if(di->maxInputChannels <= 0) continue;
		if(di->name && device_name_matches(di->name, name))
			return i;
	}
	return paNoDevice;
}

/* Devuelve el nombre real del dispositivo que coincide (ignorando el sufijo
 * "(hw:X,Y)") con el preferido guardado, o NULL si no hay ninguno. */
const char *match_input_device_name(const char *preferred)
{
	if(!preferred || !*preferred) return NULL;
	int i, n = Pa_GetDeviceCount();
	for(i = 0; i < n; i++) {
		const PaDeviceInfo *di = Pa_GetDeviceInfo(i);
		if(!di || di->maxInputChannels <= 0) continue;
		if(di->name && device_name_matches(di->name, preferred))
			return di->name;
	}
	return NULL;
}

int start_portaudio(int *nominal_sample_rate, double *real_sample_rate, const char *preferred)
{
	PaStream *stream;

	PaError err = Pa_Initialize();
	if(err!=paNoError)
		goto error;

#ifdef DEBUG
	if(testing) {
		*nominal_sample_rate = PA_SAMPLE_RATE;
		*real_sample_rate = PA_SAMPLE_RATE;
		goto end;
	}
#endif

	/* Choose input device: preferred (by name) if provided, else system default */
	PaDeviceIndex chosen = Pa_GetDefaultInputDevice();
	if(preferred && *preferred) {
		PaDeviceIndex found = find_input_device_by_name(preferred);
		if(found != paNoDevice) {
			chosen = found;
			debug("Using saved input device '%s' (index %d)\n", preferred, found);
		} else {
			debug("Saved input device '%s' not found; falling back to default\n", preferred);
			error("Saved input device '%s' not found; using the system default", preferred);
		}
	}
	if(chosen == paNoDevice) {
		error("No audio input device found");
		return 1;
	}
	const PaDeviceInfo *chosen_info = Pa_GetDeviceInfo(chosen);
	if(!chosen_info) {
		error("Cannot read info for the selected audio device");
		return 1;
	}
	long channels = chosen_info->maxInputChannels;
	if(channels == 0) {
		error("Selected audio device has no input channels");
		return 1;
	}
	if(channels > 2) channels = 2;
	info.channels = channels;
	info.light = false;

	PaStreamParameters in_params;
	memset(&in_params, 0, sizeof(in_params));
	in_params.device = chosen;
	in_params.channelCount = channels;
	in_params.sampleFormat = paFloat32;
	in_params.suggestedLatency = Pa_GetDeviceInfo(chosen)->defaultLowInputLatency;

	err = Pa_OpenStream(&stream, &in_params, NULL, PA_SAMPLE_RATE, paFramesPerBufferUnspecified, paNoFlag, paudio_callback, &info);

	/* If the preferred device failed (USB unplugged, format rejected by the
	 * hardware, sample rate not supported natively, etc.), fall back to the
	 * system default input. This keeps the app usable instead of crashing. */
	if(err != paNoError) {
		debug("Failed to open device '%s' (%s); falling back to system default\n",
		      (preferred && *preferred) ? preferred : "(default)",
		      Pa_GetErrorText(err));
		PaDeviceIndex def = Pa_GetDefaultInputDevice();
		if(def == paNoDevice) {
			error("No default audio input device found");
			return 1;
		}
		const PaDeviceInfo *def_info = Pa_GetDeviceInfo(def);
		if(!def_info) {
			error("Cannot read info for the default audio device");
			return 1;
		}
		long def_channels = def_info->maxInputChannels;
		if(def_channels == 0) {
			error("Default audio device has no input channels");
			return 1;
		}
		if(def_channels > 2) def_channels = 2;
		info.channels = def_channels;

		memset(&in_params, 0, sizeof(in_params));
		in_params.device = def;
		in_params.channelCount = def_channels;
		in_params.sampleFormat = paFloat32;
		in_params.suggestedLatency = Pa_GetDeviceInfo(def)->defaultLowInputLatency;

		err = Pa_OpenStream(&stream, &in_params, NULL, PA_SAMPLE_RATE, paFramesPerBufferUnspecified, paNoFlag, paudio_callback, &info);
		if(err != paNoError)
			goto error;
	}

	err = Pa_StartStream(stream);
	if(err!=paNoError)
		goto error;
	pa_stream = stream;

	const PaStreamInfo *stream_info = Pa_GetStreamInfo(stream);
	*nominal_sample_rate = PA_SAMPLE_RATE;
	*real_sample_rate = stream_info ? stream_info->sampleRate : PA_SAMPLE_RATE;
#ifdef DEBUG
end:
#endif
	debug("sample rate: nominal = %d real = %f\n",*nominal_sample_rate,*real_sample_rate);

	return 0;

error:
	error("Error opening audio input: %s", Pa_GetErrorText(err));
	return 1;
}

int terminate_portaudio()
{
	debug("Closing portaudio\n");
	PaError err = Pa_Terminate();
	if(err != paNoError) {
		error("Error closing audio: %s", Pa_GetErrorText(err));
		return 1;
	}
	pa_stream = NULL;
	return 0;
}

uint64_t get_timestamp(int light)
{
	pthread_mutex_lock(&audio_mutex);
	uint64_t ts = light ? timestamp / 2 : timestamp;
	pthread_mutex_unlock(&audio_mutex);
	return ts;
}

static void fill_buffers(struct processing_buffers *ps, int light)
{
	pthread_mutex_lock(&audio_mutex);
	if(file_src.active) file_pump_locked();
	uint64_t ts = timestamp;
	int wp = write_pointer;
	pthread_mutex_unlock(&audio_mutex);

	if(light)
		ts /= 2;

	int i;
	for(i = 0; i < NSTEPS; i++) {
		ps[i].timestamp = ts;

		int start = wp - ps[i].sample_count;
		if (start < 0) start += PA_BUFF_SIZE;
		int len = MIN((unsigned)ps[i].sample_count, PA_BUFF_SIZE - start);
		memcpy(ps[i].samples, pa_buffers + start, len * sizeof(*pa_buffers));
		if (len < ps[i].sample_count)
			memcpy(ps[i].samples + len, pa_buffers, (ps[i].sample_count - len) * sizeof(*pa_buffers));
	}
}

int analyze_pa_data(struct processing_data *pd, int bph, double la, uint64_t events_from)
{
	struct processing_buffers *p = pd->buffers;
	fill_buffers(p, pd->is_light);

	int i;
	debug("\nSTART OF COMPUTATION CYCLE\n\n");
	for(i=0; i<NSTEPS; i++) {
		p[i].last_tic = pd->last_tic;
		p[i].events_from = events_from;
		process(&p[i], bph, la, pd->is_light);
		if( !p[i].ready ) break;
		debug("step %d : %f +- %f\n",i,p[i].period/p[i].sample_rate,p[i].sigma/p[i].sample_rate);
	}
	if(i) {
		pd->last_tic = p[i-1].last_tic;
		debug("%f +- %f\n",p[i-1].period/p[i-1].sample_rate,p[i-1].sigma/p[i-1].sample_rate);
	} else
		debug("---\n");
	return i;
}

int analyze_pa_data_cal(struct processing_data *pd, struct calibration_data *cd)
{
	struct processing_buffers *p = pd->buffers;
	fill_buffers(p, pd->is_light);

	int i,j;
	debug("\nSTART OF CALIBRATION CYCLE\n\n");
	for(j=0; p[j].sample_count < 2*p[j].sample_rate; j++);
	for(i=0; i+j<NSTEPS-1; i++)
		if(test_cal(&p[i+j]))
			return i ? i+j : 0;
	if(process_cal(&p[NSTEPS-1], cd))
		return NSTEPS-1;
	return NSTEPS;
}

/** Change to light mode
 *
 * Call to enable or disable light mode.  Changing the mode will empty the audio
 * buffer.  Nothing will happen if the mode doesn't actually change.
 *
 * @param light True for light mode, false for normal
 */
void set_audio_light(bool light)
{
	if(info.light != light) {
		pthread_mutex_lock(&audio_mutex);
		info.light = light;
		memset(pa_buffers, 0, sizeof(pa_buffers));
		write_pointer = 0;
		timestamp = 0;
		mic_written = 0;
		pthread_mutex_unlock(&audio_mutex);
	}
}

/** Copy the most recent `count` audio samples into `out`.
 *
 * Returns the number of samples actually copied (may be less than `count` if
 * the buffer has not yet accumulated that many). Safe to call from any thread;
 * holds audio_mutex only for the duration of the copy. */
int get_recent_audio(float *out, int count)
{
	if(count <= 0 || !out) return 0;
	pthread_mutex_lock(&audio_mutex);
	if(file_src.active) file_pump_locked();
	int wp = write_pointer;
	/* Number of samples currently available (timestamp counts frames
	 * written since last reset). For light mode timestamp is in half-rate
	 * frames, but pa_buffers holds the decimated samples — use wp directly
	 * capped by PA_BUFF_SIZE. */
	int available = wp; /* wp == 0 means empty after a reset */
	if((unsigned)available > PA_BUFF_SIZE) available = PA_BUFF_SIZE;
	int n = count < available ? count : available;
	if(n > 0) {
		int start = wp - n;
		if(start < 0) start += PA_BUFF_SIZE;
		int len = PA_BUFF_SIZE - start;
		if(len >= n) {
			memcpy(out, pa_buffers + start, n * sizeof(float));
		} else {
			memcpy(out, pa_buffers + start, len * sizeof(float));
			memcpy(out + len, pa_buffers, (n - len) * sizeof(float));
		}
	}
	pthread_mutex_unlock(&audio_mutex);
	return n;
}

/** Nivel de la entrada más reciente (~100 ms): pico y RMS en [0,1].
 *  Safe to call from any thread. */
int get_audio_level(float *peak, float *rms)
{
	pthread_mutex_lock(&audio_mutex);
	if(file_src.active) file_pump_locked();
	int wp = write_pointer;
	int n = wp;
	if((unsigned)n > PA_SAMPLE_RATE / 10) n = PA_SAMPLE_RATE / 10;
	float pm = 0, sum = 0;
	int i;
	for(i = 0; i < n; i++) {
		int idx = wp - n + i;
		if(idx < 0) idx += PA_BUFF_SIZE;
		float a = pa_buffers[idx];
		if(a < 0) a = -a;
		if(a > pm) pm = a;
		sum += a * a;
	}
	pthread_mutex_unlock(&audio_mutex);
	if(peak) *peak = pm;
	if(rms) *rms = n > 0 ? sqrtf(sum / n) : 0;
	return n;
}

int pause_portaudio(void)
{
	if(pa_stream) {
		PaError err = Pa_StopStream(pa_stream);
		if(err != paNoError && err != paStreamIsStopped) {
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

int load_audio_file(const char *path)
{
	struct file_source fs;
	memset(&fs, 0, sizeof(fs));
	if(wav_open_read(path, &fs.rd)) return -1;
	fs.length = wav_get_length(&fs.rd);
	fs.path = strdup(path);
	fs.active = 1;
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
	mic_written = 0;
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
		mic_written = 0;
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
		mic_written = 0;
	}
	pthread_mutex_unlock(&audio_mutex);
}

void audio_file_set_fast(int fast)
{
	pthread_mutex_lock(&audio_mutex);
	file_src.fast = fast;
	pthread_mutex_unlock(&audio_mutex);
}

int audio_file_peek_rate(const char *path, unsigned *rate)
{
	struct wav_reader rd;
	if(wav_open_read(path, &rd)) return -1;
	*rate = rd.rate;
	wav_reader_close(&rd);
	return 0;
}

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

	/* stop_recording puede llegar desde record_drain (fallo de wav_write):
	 * en ese caso no hay que unir el propio hilo. */
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
