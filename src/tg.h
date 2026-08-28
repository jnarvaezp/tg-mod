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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <complex.h>
#include <fftw3.h>
#include <stdarg.h>
#include <gtk/gtk.h>
#include <pthread.h>

#ifdef __CYGWIN__
#define _WIN32
#endif

#define CONFIG_FILE_NAME "tg-timer.ini"

#define DEFAULT_FILTER_CUTOFF 3000

#define CAL_DATA_SIZE 900

#define FIRST_STEP 1
#define FIRST_STEP_LIGHT 0

#define NSTEPS 4
#define PA_SAMPLE_RATE 44100u
#define PA_BUFF_SIZE (PA_SAMPLE_RATE << (NSTEPS + FIRST_STEP))

#define OUTPUT_FONT 40
#define OUTPUT_WINDOW_HEIGHT 70

#define POSITIVE_SPAN 10
#define NEGATIVE_SPAN 25

#define EVENTS_COUNT 10000
#define EVENTS_MAX 100
#define PAPERSTRIP_ZOOM 10
#define PAPERSTRIP_ZOOM_CAL 100
#define PAPERSTRIP_MARGIN .2

#define MIN_BPH 8100
#define TYP_BPH 12000
#define MAX_BPH 72000
#define DEFAULT_BPH 21600
#define MIN_LA 10 // deg
#define MAX_LA 90 // deg
#define DEFAULT_LA 52 // deg
#define MIN_CAL -1000 // 0.1 s/d
#define MAX_CAL 1000 // 0.1 s/d

#define PRESET_BPH { 12000, 14400, 17280, 18000, 19800, 21600, 25200, 28800, 36000, 43200, 72000, 0 };

/* Siempre activo: vierte al anillo de sesión (raw) en todos los builds.
 * A stderr: en builds DEBUG siempre; en release según verbose_level
 * (debug() >= 1, debugv() >= 2). */
#define debug(...) print_debug(__VA_ARGS__)
#define debugv(...) print_debug_verbose(__VA_ARGS__)

#define UNUSED(X) (void)(X)

/** Hold timestamp and type of a vibration. */
struct event {
	uint64_t pos;	//< Position, may be relative or absolute timestamp
	bool tictoc;	//< Is this vibration the tic or toc half-cycle?
};

/* algo.c */
struct processing_buffers {
	int sample_rate;
	int sample_count;
	float *samples, *samples_sc, *waveform, *waveform_sc, *tic_wf, *slice_wf, *tic_c;
	fftwf_complex *fft, *sc_fft, *tic_fft, *slice_fft;
	fftwf_plan plan_a, plan_b, plan_c, plan_d, plan_e, plan_f, plan_g;
	struct filter *hpf, *lpf;
	double period,sigma,be,waveform_max,phase,tic_pulse,toc_pulse,amp;
	int amp_valid;   /* 1 si la amplitud pasó la validación (135-360 deg) */
	double cal_phase;
	int waveform_max_i;
	int tic,toc;
	int ready;
	/* Three-phase decomposition of the tick waveform (samples). -1 if not
	 * detected. unlock/impulse/drop are the local maxima of the tic pulse;
	 * unlock2/impulse2/drop2 mirror them for the toc pulse. */
	int unlock, impulse, drop;
	int unlock2, impulse2, drop2;
	uint64_t timestamp, last_tic, last_toc, events_from;
	/** Dynamically allocated array of events, using absolute timestamps.  Terminated by 0 value for position. */
	struct event *events;
#ifdef DEBUG
	int debug_size;
	float *debug;
#endif
};

struct calibration_data {
	int wp;
	int size;
	int state;
	double calibration;
	uint64_t start_time;
	double *times;
	double *phases;
	uint64_t *events;
};

void setup_buffers(struct processing_buffers *b);
void pb_destroy(struct processing_buffers *b);
struct processing_buffers *pb_clone(struct processing_buffers *p);
void pb_destroy_clone(struct processing_buffers *p);
void process(struct processing_buffers *p, int bph, double la, int light);
void setup_cal_data(struct calibration_data *cd);
void cal_data_destroy(struct calibration_data *cd);
int test_cal(struct processing_buffers *p);
int process_cal(struct processing_buffers *p, struct calibration_data *cd);

/* audio.c */
struct processing_data {
	struct processing_buffers *buffers;
	uint64_t last_tic;
	int is_light;
};

int start_portaudio(int *nominal_sample_rate, double *real_sample_rate, const char *preferred);
int terminate_portaudio();
uint64_t get_timestamp(int light);
int analyze_pa_data(struct processing_data *pd, int bph, double la, uint64_t events_from);
int analyze_pa_data_cal(struct processing_data *pd, struct calibration_data *cd);
void set_audio_light(bool light);

/* offline.c */
struct offline_result {
	int signal;         /* 1 si se obtuvo un resultado bueno */
	int guessed_bph;
	double rate;        /* s/d */
	double be;          /* ms */
	double amp;         /* deg estimado (0 = no disponible) */
	int amp_valid;      /* 1 si la amplitud está dentro del rango físico */
};

int analyze_audio_file(const char *path, int bph, double la, double cal, struct offline_result *res);

/* --- offline / file source --- */
int load_audio_file(const char *path);    /* 0 = ok, -1 = error */
int close_audio_file(void);               /* 0 = ok */
int get_audio_file_mode(void);            /* 1 si hay un archivo activo */
uint64_t get_audio_file_length(void);     /* frames totales */
uint64_t get_audio_file_position(void);   /* frames bombeados */
unsigned get_audio_file_rate(void);
void audio_file_restart(void);
void audio_file_set_fast(int fast);       /* headless: bombea todo de una vez */
int audio_file_peek_rate(const char *path, unsigned *rate);  /* 0 = ok */

/* Mic stream control (sin reiniciar Pa): */
int pause_portaudio(void);
int resume_portaudio(void);

/* --- recording --- */
int start_recording(const char *path);    /* 0 = ok */
int stop_recording(void);                 /* 0 = ok */
int get_recording(void);                  /* 1 si grabando */

/* Copy the most recent audio samples for live visualization (oscilloscope).
 * Returns the number of samples actually copied. */
int get_recent_audio(float *out, int count);

/* Peak and RMS level of the most recent ~100 ms of input (0..1). */
int get_audio_level(float *peak, float *rms);

/* Set the input gain multiplier applied in the PA callback (0.1 .. 100.0, <1 attenuates). */
void set_audio_gain(double g);

/* Bandpass cutoff frequency in Hz used by algo.c:setup_buffers. */
extern int filter_cutoff;

/* Input device enumeration / selection (must be called after Pa_Initialize, i.e. after start_portaudio) */
int get_input_device_count(void);
const char *get_input_device_name(int index);
int find_input_device_by_name(const char *name);
const char *match_input_device_name(const char *preferred);

/* computer.c */
struct display;

struct snapshot {
	struct processing_buffers *pb;
	int is_old;
	uint64_t timestamp;
	int is_light;

	int nominal_sr;
	int calibrate;
	int bph;
	double la; // deg
	int cal; // 0.1 s/d

	int events_count;
	uint64_t *events; // used in cal+timegrapher mode
	unsigned char *events_tictoc;	//< Tic or Toc for each event
	int events_wp; // used in cal+timegrapher mode
	uint64_t events_from; // used only in timegrapher mode

	/** Allocated arrays of amplitude measurements and timestamps */
	float *amps;		//< Circ buffer with history of amplitude measurements
	uint64_t *amps_time;	//< Timestamps for amps
	int amps_wp;		//< Index of recent amp in amps
	int amps_count;		//< Number of amplitude samples

	int signal;

	int cal_state;
	int cal_percent;
	int cal_result; // 0.1 s/d

	// data dependent on bph, la, cal
	double sample_rate;
	int guessed_bph;
	double rate;
	double be;
	double amp;
	int amp_valid;   /* 1 = amplitud dentro del rango físico (135-360 deg) */

	// State related to displaying the snapshot, not generated by computer
	struct display *d;
};

struct computer {
	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_cond_t cond;

// controlled by interface
	int recompute;
	int calibrate;
	int bph;
	double la; // deg
	int clear_trace;
	void (*callback)(void *);
	void *callback_data;

	struct processing_data *pdata;
	struct calibration_data *cdata;

	struct snapshot *actv;
	struct snapshot *curr;
};

struct snapshot *snapshot_clone(struct snapshot *s);
void snapshot_destroy(struct snapshot *s);
void computer_destroy(struct computer *c);
struct computer *start_computer(int nominal_sr, int bph, double la, int cal, int light);
void lock_computer(struct computer *c);
void unlock_computer(struct computer *c);
void compute_results(struct snapshot *s);

/* output_panel.c */
/* Snapshot display parameters, e.g. scale, centering. */
struct display {
	// Scaling factor for each beat.  1 means the chart is 1 beat wide, 0.5
	// means half a beat, etc.
	double beat_scale;
	/* Time of point used to anchor the paperstrip.  Each paperstrip point's position is
	 * relative to the previous point.  This point is the one with an absolute position that
	 * is kept the same, so that all the dots do not shift side to side as they scroll.  */
	uint64_t anchor_time;
	// Phase offset of point at anchor_time
	double anchor_offset;
};

struct output_panel {
	GtkWidget *panel;

	GtkWidget *output_drawing_area;
	GtkWidget *tic_drawing_area;
	GtkWidget *toc_drawing_area;
	GtkWidget *period_drawing_area;
	GtkWidget *paperstrip_drawing_area;
	GtkWidget *trend_drawing_area;
	GtkWidget *spectrum_drawing_area;
	GtkWidget *clear_button;
	GtkWidget *zoom_button;
	GtkWidget *zoom_orig_button;
	guint spectrum_timeout;
	guint scope_timeout;
#ifdef DEBUG
	GtkWidget *debug_drawing_area;
#endif
	struct computer *computer;
	struct snapshot *snst;
};

void initialize_palette();
struct output_panel *init_output_panel(struct computer *comp, struct snapshot *snst, int border);
void redraw_op(struct output_panel *op);
void op_set_snapshot(struct output_panel *op, struct snapshot *snst);
void op_set_border(struct output_panel *op, int i);
void op_destroy(struct output_panel *op);

/* interface.c */
struct main_window {
	GtkApplication *app;

	GtkWidget *window;
	GtkWidget *bph_combo_box;
	GtkWidget *la_spin_button;
	GtkWidget *cal_spin_button;
	GtkWidget *device_combo_box;
	GtkWidget *dev_refresh;
	GtkWidget *gain_spin_button;
	GtkWidget *cutoff_spin_button;
	GtkWidget *snapshot_button;
	GtkWidget *snapshot_name;
	GtkWidget *snapshot_name_entry;
	GtkWidget *cal_button;
	GtkWidget *notebook;
	GtkWidget *save_item;
	GtkWidget *save_all_item;
	GtkWidget *close_all_item;
	struct output_panel *active_panel;

	struct computer *computer;
	struct snapshot *active_snapshot;
	int computer_timeout;

	int is_light;
	int zombie;
	int controls_active;
	int calibrate;
	int bph;
	double la; // deg
	int cal; // 0.1 s/d
	gchar *input_device; // preferred input device name (NULL = system default)
	int restart_portaudio; // flag: re-open PortAudio when restarting computer
	double gain; // audio input gain multiplier (0.1 .. 100, <1 attenuates)
	int filter_cutoff; // bandpass cutoff frequency in Hz (default 3000)
	int nominal_sr;

	GtkWidget *source_label;
	GtkWidget *level_bar;
	GtkWidget *clip_label;
	GtkWidget *position_combo;
	int position;   /* POSITION_* actual (etiqueta de los ciclos en vivo) */
	guint level_timeout;

	/* Watch panel (watchmaker's logbook) */
	GtkWidget *paned;                    /* left panel + main content */
	GtkWidget *watch_list;               /* GtkListBox of watches */
	GtkWidget *session_tree;             /* GtkTreeView of sessions */
	GtkWidget *session_note_entry;
	GtkWidget *session_start_button;
	GtkWidget *session_finish_button;
	GtkWidget *watch_delete_button;
	GtkWidget *session_status_label;
	int64_t selected_watch_id;           /* -1 = none */
	char selected_watch_name[64];
	int64_t selected_session_id;         /* sesión seleccionada en el historial, -1 = ninguna */
	GtkWidget *session_delete_button;
	GtkWidget *watch_defaults_button;    /* guarda bph/lift actuales como defaults del reloj */
	GtkWidget *history_export_button;    /* export CSV/PDF del historial */
	GtkWidget *evolution_button;         /* gráfico de evolución del rate por sesión */
	int session_active;
	uint64_t session_start_ms;
	guint session_timeout;
	GtkWidget *record_item;
	GtkWidget *stop_record_item;
	GtkWidget *close_rec_item;
	GtkWidget *light_checkbox;
	gchar *audio_file_name;      /* nombre del archivo en modo archivo (label) */
	gchar *pending_audio_file;   /* ruta a cargar tras reiniciar computer */
	int close_audio;             /* cerrar archivo al reiniciar computer */
	int restart_computer;        /* forzar reinicio de computer sin tocar Pa */
	int audio_file_mode;         /* 1 = analizando un archivo */
	int saved_is_light;          /* estado de is_light antes de abrir un archivo */

	GKeyFile *config_file;
	gchar *config_file_name;
	struct conf_data *conf_data;

	guint kick_timeout;
	guint save_timeout;
};

extern int preset_bph[];

/* Verbose console output (debug() to stderr) in all builds.
 * 0 = quiet, 1 = resumen (debug), 2 = detalle (debugv). */
extern int verbose_level;

#ifdef DEBUG
extern int testing;
#endif

void print_debug(char *format,...);
void print_debug_verbose(char *format,...);
void error(char *format,...);

/* config.c */
/* cfg_string is used by CONFIG_FIELDS to declare string-typed config entries.
 * Defined here (not in config.c) because the macro expands into struct conf_data
 * below, which is included by every translation unit. */
typedef gchar *cfg_string;

#define CONFIG_FIELDS(OP) \
	OP(bph, bph, int) \
	OP(lift_angle, la, double) \
	OP(calibration, cal, int) \
	OP(light_algorithm, is_light, int) \
	OP(input_device, input_device, cfg_string) \
	OP(gain, gain, double) \
	OP(filter_cutoff, filter_cutoff, int)

struct conf_data {
#define DEF(NAME,PLACE,TYPE) TYPE PLACE;
	CONFIG_FIELDS(DEF)
};

void load_config(struct main_window *w);
void save_config(struct main_window *w);
void save_on_change(struct main_window *w);
void close_config(struct main_window *w);

/* serializer.c */
int write_file(FILE *f, struct snapshot **s, char **names, uint64_t cnt);
int read_file(FILE *f, struct snapshot ***s, char ***names, uint64_t *cnt);
