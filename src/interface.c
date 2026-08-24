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
#include "session.h"
#include "stats.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <ctype.h>
#include <time.h>

#ifdef DEBUG
int testing = 0;
#endif

/* Verbose console output: debug() prints to stderr in release builds when
 * the app is started with the "debug" argument (level 1 = resumen,
 * "debug full" = level 2 = detalle). */
int verbose_level = 0;

static void print_debug_to(char *format, va_list args, int level)
{
	char buf[768];
	vsnprintf(buf,sizeof(buf),format,args);
#ifdef DEBUG
	UNUSED(level);
	fputs(buf,stderr);
#else
	if(verbose_level >= level) fputs(buf,stderr);
#endif
	session_add_raw(g_get_real_time() / 1000, buf);
}

void print_debug(char *format,...)
{
	va_list args;
	va_start(args,format);
	print_debug_to(format, args, 1);
	va_end(args);
}

void print_debug_verbose(char *format,...)
{
	va_list args;
	va_start(args,format);
	print_debug_to(format, args, 2);
	va_end(args);
}

void error(char *format,...)
{
	char s[100];
	va_list args;

	va_start(args,format);
	int size = vsnprintf(s,100,format,args);
	va_end(args);

	char *t;
	if(size < 100) {
		t = s;
	} else {
		t = alloca(size+1);
		va_start(args,format);
		vsnprintf(t,size+1,format,args);
		va_end(args);
	}

	fprintf(stderr,"%s\n",t);

#ifdef DEBUG
	if(testing) return;
#endif

	GtkWidget *dialog = gtk_message_dialog_new(NULL,0,GTK_MESSAGE_ERROR,GTK_BUTTONS_CLOSE,"%s",t);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

static void refresh_results(struct main_window *w)
{
	w->active_snapshot->bph = w->bph;
	w->active_snapshot->la = w->la;
	w->active_snapshot->cal = w->cal;
	compute_results(w->active_snapshot);
}

static void handle_bph_change(GtkComboBox *b, struct main_window *w)
{
	if(!w->controls_active) return;
	char *s = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(b));
	if(s) {
		int bph;
		char *t;
		int n = (int)strtol(s,&t,10);
		if(*t || n < MIN_BPH || n > MAX_BPH) bph = 0;
		else bph = n;
		g_free(s);
		w->bph = bph;
		refresh_results(w);
		gtk_widget_queue_draw(w->notebook);
	}
}

static void handle_la_change(GtkSpinButton *b, struct main_window *w)
{
	if(!w->controls_active) return;
	double la = gtk_spin_button_get_value(b);
	if(la < MIN_LA || la > MAX_LA) la = DEFAULT_LA;
	w->la = la;
	refresh_results(w);
	gtk_widget_queue_draw(w->notebook);
}

static void handle_cal_change(GtkSpinButton *b, struct main_window *w)
{
	if(!w->controls_active) return;
	int cal = gtk_spin_button_get_value(b);
	w->cal = cal;
	refresh_results(w);
	gtk_widget_queue_draw(w->notebook);
}

static gboolean output_cal(GtkSpinButton *spin, gpointer data)
{
	UNUSED(data);
	GtkAdjustment *adj;
	gchar *text;
	int value;

	adj = gtk_spin_button_get_adjustment (spin);
	value = (int)gtk_adjustment_get_value (adj);
	text = g_strdup_printf ("%c%d.%d", value < 0 ? '-' : '+', abs(value)/10, abs(value)%10);
	gtk_entry_set_text (GTK_ENTRY (spin), text);
	g_free (text);

	return TRUE;
}

static gboolean input_cal(GtkSpinButton *spin, double *val, gpointer data)
{
	UNUSED(data);
	double x = 0;
	sscanf(gtk_entry_get_text (GTK_ENTRY (spin)), "%lf", &x);
	int n = round(x*10);
	if(n < MIN_CAL) n = MIN_CAL;
	if(n > MAX_CAL) n = MAX_CAL;
	*val = n;

	return TRUE;
}

static void on_shutdown(GApplication *app, void *p)
{
	UNUSED(p);
	debug("Main loop has terminated\n");
	struct main_window *w = g_object_get_data(G_OBJECT(app), "main-window");
	if(w) {
		save_config(w);
		if(get_recording()) stop_recording();
		if(w->computer) computer_destroy(w->computer);
		if(w->active_panel) op_destroy(w->active_panel);
		close_config(w);
		g_free(w->audio_file_name);
		g_free(w->pending_audio_file);
		free(w);
	}
	terminate_portaudio();
}

static void recompute(struct main_window *w);
static void computer_callback(void *w);
static void update_audio_mode_ui(struct main_window *w);

static guint computer_terminated(struct main_window *w)
{
	if(w->zombie) {
		debug("Closing main window\n");
		gtk_widget_destroy(w->window);
	} else {
		debug("Restarting computer\n");
		computer_destroy(w->computer);
		w->computer = NULL;

		if(w->restart_portaudio) {
			w->restart_portaudio = 0;
			debug("Re-opening audio with device: %s\n", w->input_device ? w->input_device : "(default)");
			terminate_portaudio();
			double real_sr;
			if(start_portaudio(&w->nominal_sr, &real_sr, w->input_device)) {
				g_source_remove(w->kick_timeout);
				g_source_remove(w->save_timeout);
				w->zombie = 1;
				error("Failed to re-open audio input device");
				gtk_widget_destroy(w->window);
				return FALSE;
			}
		}

		if(w->close_audio) {
			close_audio_file();
			w->close_audio = 0;
			w->audio_file_mode = 0;
			w->nominal_sr = PA_SAMPLE_RATE;
			g_free(w->audio_file_name);
			w->audio_file_name = NULL;
			w->is_light = w->saved_is_light;
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w->light_checkbox), w->saved_is_light);
		}

		unsigned file_rate = 0;
		if(w->pending_audio_file)
			audio_file_peek_rate(w->pending_audio_file, &file_rate);
		if(file_rate)
			w->nominal_sr = file_rate;

		struct computer *c = start_computer(w->nominal_sr, w->bph, w->la, w->cal, w->is_light);
		if(!c) {
			g_source_remove(w->kick_timeout);
			g_source_remove(w->save_timeout);
			w->zombie = 1;
			error("Failed to restart computation thread");
			gtk_widget_destroy(w->window);
		} else {
			w->active_panel->computer = w->computer = c;

			w->computer->callback = computer_callback;
			w->computer->callback_data = w;

			if(w->pending_audio_file) {
				if(load_audio_file(w->pending_audio_file)) {
					error("Failed to open recording: %s", w->pending_audio_file);
					w->audio_file_mode = 0;
					w->nominal_sr = PA_SAMPLE_RATE;
					w->is_light = w->saved_is_light;
					gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w->light_checkbox), w->saved_is_light);
					resume_portaudio();
					w->restart_computer = 1;   /* re-kick: recrear computer con ajustes de mic */
				} else {
					w->audio_file_mode = 1;
				}
				g_free(w->pending_audio_file);
				w->pending_audio_file = NULL;
			}
			update_audio_mode_ui(w);
			recompute(w);
		}
	}
	return FALSE;
}

static void computer_quit(void *w)
{
	gdk_threads_add_idle((GSourceFunc)computer_terminated,w);
}

static void kill_computer(struct main_window *w)
{
	w->computer->recompute = -1;
	w->computer->callback = computer_quit;
	w->computer->callback_data = w;
}

static gboolean quit(struct main_window *w)
{
	g_source_remove(w->kick_timeout);
	g_source_remove(w->save_timeout);
	if(w->level_timeout) g_source_remove(w->level_timeout);
	w->zombie = 1;
	lock_computer(w->computer);
	kill_computer(w);
	unlock_computer(w->computer);
	return FALSE;
}

static gboolean delete_event(GtkWidget *widget, GdkEvent *event, gpointer w)
{
	UNUSED(widget);
	UNUSED(event);
	debug("Received delete event\n");
	quit((struct main_window *)w);
	return TRUE;
}

static void handle_quit(GtkMenuItem *m, struct main_window *w)
{
	UNUSED(m);
	quit(w);
}

static void recompute(struct main_window *w)
{
	w->computer_timeout = 0;
	lock_computer(w->computer);
	if(w->computer->recompute >= 0) {
		if(w->is_light != w->computer->actv->is_light || w->restart_portaudio || w->restart_computer) {
			w->restart_computer = 0;
			kill_computer(w);
		} else {
			w->computer->bph = w->bph;
			w->computer->la = w->la;
			w->computer->calibrate = w->calibrate;
			w->computer->recompute = 1;
		}
	}
	unlock_computer(w->computer);
}

static guint kick_computer(struct main_window *w)
{
	w->computer_timeout++;
	if(w->calibrate && w->computer_timeout < 10) {
		return TRUE;
	} else {
		recompute(w);
		return TRUE;
	}
}

static void handle_calibrate(GtkCheckMenuItem *b, struct main_window *w)
{
	int button_state = gtk_check_menu_item_get_active(b) == TRUE;
	if(button_state != w->calibrate) {
		w->calibrate = button_state;
		recompute(w);
	}
}

static void handle_light(GtkCheckMenuItem *b, struct main_window *w)
{
	int button_state = gtk_check_menu_item_get_active(b) == TRUE;
	if(button_state != w->is_light) {
		w->is_light = button_state;
		recompute(w);
	}
}

static void handle_device_change(GtkComboBox *b, struct main_window *w)
{
	if(!w->controls_active) return;
	gchar *s = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(b));
	if(!s) return;
	// "System default" maps to empty string (PortAudio falls back to OS default
	// when *preferred == '\0'); this keeps w->input_device always non-NULL so
	// save_config can serialize it cleanly.
	gchar *new_device = strcmp(s, "System default") ? g_strdup(s) : g_strdup("");
	g_free(s);

	// No-op if unchanged (g_strcmp0 treats NULL == "" as different, but we keep
	// input_device non-NULL after the first change, so the typical path is ""=="")
	if(g_strcmp0(new_device, w->input_device) == 0) {
		g_free(new_device);
		return;
	}

	g_free(w->input_device);
	w->input_device = new_device;
	w->restart_portaudio = 1;
	recompute(w);
}

static void populate_devices(struct main_window *w)
{
	g_signal_handlers_block_by_func(w->device_combo_box, handle_device_change, w);
	gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(w->device_combo_box));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->device_combo_box), "System default");
	const char *match = match_input_device_name(w->input_device);
	int dev_count = get_input_device_count();
	int di, active = 0;
	for(di = 0; di < dev_count; di++) {
		const char *dname = get_input_device_name(di);
		if(!dname) continue;
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->device_combo_box), dname);
		if(match && dname == match)
			active = di + 1;
	}
	/* set_active mientras el handler está bloqueado: no dispara restart */
	gtk_combo_box_set_active(GTK_COMBO_BOX(w->device_combo_box), active);
	g_signal_handlers_unblock_by_func(w->device_combo_box, handle_device_change, w);
}

static void handle_gain_change(GtkSpinButton *b, struct main_window *w)
{
	if(!w->controls_active) return;
	double g = gtk_spin_button_get_value(b);
	if(g < 0.1) g = 0.1;
	if(g > 100.0) g = 100.0;
	w->gain = g;
	set_audio_gain(g);
}

static void handle_cutoff_change(GtkSpinButton *b, struct main_window *w)
{
	if(!w->controls_active) return;
	int fc = (int)gtk_spin_button_get_value(b);
	if(fc < 1000) fc = 1000;
	if(fc > 8000) fc = 8000;
	if(fc == w->filter_cutoff) return;
	w->filter_cutoff = fc;
	filter_cutoff = fc;
	/* Filter coefficients are baked into the processing_buffers at
	 * setup_buffers(), so changing the cutoff requires a full computer
	 * restart (same path as the light-mode switch). */
	recompute(w);
}

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
			g_free(w->audio_file_name);
			w->audio_file_name = g_path_get_basename(filename);
			g_free(w->pending_audio_file);
			w->pending_audio_file = g_strdup(filename);
			w->saved_is_light = w->is_light;
			w->is_light = 0;
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w->light_checkbox), FALSE);
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

static void handle_save_session_log(GtkMenuItem *m, struct main_window *w)
{
	UNUSED(m);
	char *dir = g_build_filename(g_get_home_dir(), "tg-logs", NULL);
	if(g_mkdir_with_parents(dir, 0755)) {
		error("Cannot create log directory %s", dir);
		g_free(dir);
		return;
	}
	char base[64];
	time_t t = time(NULL);
	strftime(base, sizeof(base), "tg-session-%Y%m%d-%H%M%S", localtime(&t));
	int err = session_save(dir, base);
	if(err)
		error("Session log: %d file(s) failed in %s", err, dir);
	else {
		GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(w->window), 0, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
			"Session log saved to\n%s/%s.{json,csv,raw}", dir, base);
		gtk_dialog_run(GTK_DIALOG(d));
		gtk_widget_destroy(d);
	}
	g_free(dir);
}

static gboolean update_level(struct main_window *w)
{
	float peak = 0;
	get_audio_level(&peak, NULL);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w->level_bar), peak);
	if(peak > 0.98f)
		gtk_widget_show(w->clip_label);
	else
		gtk_widget_hide(w->clip_label);
	return TRUE;
}

static void update_audio_mode_ui(struct main_window *w)
{
	int file_mode = w->audio_file_mode;
	int recording = get_recording();

	gtk_widget_set_sensitive(w->device_combo_box, !file_mode);
	gtk_widget_set_sensitive(w->dev_refresh, !file_mode);
	gtk_widget_set_sensitive(w->gain_spin_button, !file_mode);
	gtk_widget_set_sensitive(w->cal_button, !file_mode);
	gtk_widget_set_sensitive(w->light_checkbox, !file_mode && !recording);

	const char *txt;
	if(recording) {
		txt = "recording...";
	} else if(file_mode && w->audio_file_name) {
		txt = w->audio_file_name;
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

static void controls_active(struct main_window *w, int active)
{
	w->controls_active = active;
	gtk_widget_set_sensitive(w->bph_combo_box, active);
	gtk_widget_set_sensitive(w->la_spin_button, active);
	gtk_widget_set_sensitive(w->cal_spin_button, active);
	gtk_widget_set_sensitive(w->cal_button, active);
	if(active) {
		gtk_widget_show(w->snapshot_button);
		gtk_widget_hide(w->snapshot_name);
	} else {
		gtk_widget_hide(w->snapshot_button);
		gtk_widget_show(w->snapshot_name);
	}
}

static int blank_string(char *s)
{
	if(!s) return 1;
	for(;*s;s++)
		if(!isspace((unsigned char)*s)) return 0;
	return 1;
}

static void handle_tab_changed(GtkNotebook *nbk, GtkWidget *panel, guint x, struct main_window *w)
{
	UNUSED(nbk);
	UNUSED(x);
	// These are NULL for the Real Time tab
	struct output_panel *op = g_object_get_data(G_OBJECT(panel), "op-pointer");
	char *tab_name = g_object_get_data(G_OBJECT(panel), "tab-name");

	controls_active(w, !op);

	int bph, cal;
	double la;
	struct snapshot *snap;
	if(op) {
		gtk_entry_set_text(GTK_ENTRY(w->snapshot_name_entry), tab_name ? tab_name : "");
		bph = op->snst->bph;
		cal = op->snst->cal;
		la = op->snst->la;
		snap = op->snst;
	} else {
		bph = w->bph;
		cal = w->cal;
		la = w->la;
		snap = w->active_snapshot;
	}

	int i,current = 0;
	for(i = 0; preset_bph[i]; i++) {
		if(bph == preset_bph[i]) {
			current = i+1;
			break;
		}
	}
	if(current || bph == 0)
		gtk_combo_box_set_active(GTK_COMBO_BOX(w->bph_combo_box), current);
	else {
		char s[32];
		sprintf(s,"%d",bph);
		GtkEntry *e = GTK_ENTRY(gtk_bin_get_child(GTK_BIN(w->bph_combo_box)));
		gtk_entry_set_text(e,s);
	}

	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->la_spin_button), la);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->cal_spin_button), cal);
	gtk_widget_set_sensitive(w->save_item, !snap->calibrate && snap->pb);
}

static void handle_tab_closed(GtkNotebook *nbk, GtkWidget *panel, guint x, struct main_window *w)
{
	UNUSED(x);
	if(gtk_notebook_get_n_pages(nbk) == 1 && !w->zombie) {
		gtk_notebook_set_show_tabs(GTK_NOTEBOOK(nbk), FALSE);
		gtk_notebook_set_show_border(GTK_NOTEBOOK(nbk), FALSE);
		gtk_widget_set_sensitive(w->save_all_item, FALSE);
		gtk_widget_set_sensitive(w->close_all_item, FALSE);
	}
	// Now, are we sure that we are not going to segfault?
	struct output_panel *op = g_object_get_data(G_OBJECT(panel), "op-pointer");
	if(op) op_destroy(op);
	free(g_object_get_data(G_OBJECT(panel), "tab-name"));
}

static void handle_close_tab(GtkButton *b, struct output_panel *p)
{
	UNUSED(b);
	gtk_widget_destroy(p->panel);
}

static void handle_name_change(GtkEntry *e, struct main_window *w)
{
	int p = gtk_notebook_get_current_page(GTK_NOTEBOOK(w->notebook));
	GtkWidget *panel = gtk_notebook_get_nth_page(GTK_NOTEBOOK(w->notebook), p);
	GtkLabel *label = g_object_get_data(G_OBJECT(panel), "tab-label");
	free( g_object_get_data(G_OBJECT(panel), "tab-name") );
	char *name = (char *)gtk_entry_get_text(e);
	name = blank_string(name) ? NULL : strdup(name);
	g_object_set_data(G_OBJECT(panel), "tab-name", name);
	gtk_label_set_text(label, name ? name : "Snapshot");
}

#ifdef WIN_XP
static GtkWidget *image_from_file(char *filename)
{
	char *dir = g_win32_get_package_installation_directory_of_module(NULL);
	char *s;
	if(dir) {
		s = alloca( strlen(dir) + strlen(filename) + 2 );
		sprintf(s, "%s/%s", dir, filename);
	} else {
		s = filename;
	}
	GtkWidget *img = gtk_image_new_from_file(s);
	g_free(dir);
	return img;
}
#endif

static GtkWidget *make_tab_label(char *name, struct output_panel *panel_to_close)
{
	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	char *nm = panel_to_close ? name ? name : "Snapshot" : "Real time";
	GtkWidget *label = gtk_label_new(nm);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);

	if(panel_to_close) {
#ifdef WIN_XP
		GtkWidget *image = image_from_file("window-close.png");
#else
		GtkWidget *image = gtk_image_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
#endif
		GtkWidget *button = gtk_button_new();
		gtk_button_set_image(GTK_BUTTON(button), image);
		gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
		g_signal_connect(button, "clicked", G_CALLBACK(handle_close_tab), panel_to_close);
		gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
		g_object_set_data(G_OBJECT(panel_to_close->panel), "op-pointer", panel_to_close);
		g_object_set_data(G_OBJECT(panel_to_close->panel), "tab-label", label);
		g_object_set_data(G_OBJECT(panel_to_close->panel), "tab-name", name ? strdup(name) : NULL);
	}

	gtk_widget_show_all(hbox);

	return hbox;
}

static void add_new_tab(struct snapshot *s, char *name, struct main_window *w)
{
	struct output_panel *op = init_output_panel(NULL, s, 5);
	GtkWidget *label = make_tab_label(name, op);
	gtk_widget_show_all(op->panel);

	op_set_border(w->active_panel, 5);
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(w->notebook), TRUE);
	gtk_notebook_set_show_border(GTK_NOTEBOOK(w->notebook), TRUE);
	gtk_notebook_append_page(GTK_NOTEBOOK(w->notebook), op->panel, label);
	gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(w->notebook), op->panel, TRUE);
	gtk_widget_set_sensitive(w->save_all_item, TRUE);
	gtk_widget_set_sensitive(w->close_all_item, TRUE);
}

static void handle_snapshot(GtkButton *b, struct main_window *w)
{
	UNUSED(b);
	if(w->active_snapshot->calibrate) return;
	struct snapshot *s = snapshot_clone(w->active_snapshot);
	s->timestamp = get_timestamp(s->is_light);
	add_new_tab(s, NULL, w);
}

static void chooser_set_filters(GtkFileChooser *chooser)
{
	GtkFileFilter *tgj_filter = gtk_file_filter_new();
	gtk_file_filter_set_name(tgj_filter, ".tgj");
	gtk_file_filter_add_pattern(tgj_filter, "*.tgj");
	gtk_file_chooser_add_filter(chooser, tgj_filter);

	GtkFileFilter *all_filter = gtk_file_filter_new();
	gtk_file_filter_set_name(all_filter, "All files");
	gtk_file_filter_add_pattern(all_filter, "*");
	gtk_file_chooser_add_filter(chooser, all_filter);

	// On windows seems not to work...
	gtk_file_chooser_set_filter(chooser, tgj_filter);
}

static FILE *fopen_check(char *filename, char *mode, struct main_window *w)
{
	FILE *f = NULL;

#ifdef _WIN32
	wchar_t *name = NULL;
	wchar_t *md = NULL;

	name = (wchar_t*)g_convert(filename, -1, "UTF-16LE", "UTF-8", NULL, NULL, NULL);
	if(!name) goto error;

	md = (wchar_t*)g_convert(mode, -1, "UTF-16LE", "UTF-8", NULL, NULL, NULL);
	if(!md) goto error;

	f = _wfopen(name, md);

error:	g_free(name);
	g_free(md);
#else
	f = fopen(filename, mode);
#endif

	if(!f) {
		GtkWidget *dialog;
		dialog = gtk_message_dialog_new(GTK_WINDOW(w->window),0,GTK_MESSAGE_ERROR,GTK_BUTTONS_CLOSE,
					"Error opening file\n");
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
	}

	return f;
}

static FILE *choose_file_for_save(struct main_window *w, char *title, char *suggestion)
{
	FILE *f = NULL;
	GtkWidget *dialog = gtk_file_chooser_dialog_new (title,
			GTK_WINDOW(w->window),
			GTK_FILE_CHOOSER_ACTION_SAVE,
			"Cancel",
			GTK_RESPONSE_CANCEL,
			"Save",
			GTK_RESPONSE_ACCEPT,
			NULL);
	GtkFileChooser *chooser = GTK_FILE_CHOOSER (dialog);
	if(suggestion)
		gtk_file_chooser_set_current_name(chooser, suggestion);

	chooser_set_filters(chooser);

	if(GTK_RESPONSE_ACCEPT == gtk_dialog_run (GTK_DIALOG (dialog)))
	{
		GFile *gf = gtk_file_chooser_get_file(chooser);
		char *filename = g_file_get_path(gf);
		g_object_unref(gf);
		if(!strcmp(".tgj", gtk_file_filter_get_name(gtk_file_chooser_get_filter(chooser)))) {
			char *s = strdup(filename);
			if(strlen(s) > 3 && strcasecmp(".tgj", s + strlen(s) - 4)) {
				char *t = g_malloc(strlen(filename)+5);
				sprintf(t,"%s.tgj",filename);
				g_free(filename);
				filename = t;
			}
			free(s);
		}
		struct stat stst;
		int do_open = 0;
		if(!stat(filename, &stst)) {
			GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(w->window),
				GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_MESSAGE_QUESTION,
				GTK_BUTTONS_OK_CANCEL,
				"File %s already exists. Do you want to replace it?",
				filename);
			do_open = GTK_RESPONSE_OK == gtk_dialog_run(GTK_DIALOG(dialog));
			gtk_widget_destroy(dialog);
		} else
			do_open = 1;
		if(do_open) {
			f = fopen_check(filename, "wb", w);
			if(f) {
				char *uri = g_filename_to_uri(filename,NULL,NULL);
				if(f && uri)
					gtk_recent_manager_add_item(
						gtk_recent_manager_get_default(), uri);
				g_free(uri);
			}
		}
		g_free (filename);
	}

	gtk_widget_destroy(dialog);

	return f;
}

static void save_current(GtkMenuItem *m, struct main_window *w)
{
	UNUSED(m);
	int p = gtk_notebook_get_current_page(GTK_NOTEBOOK(w->notebook));
	GtkWidget *tab = gtk_notebook_get_nth_page(GTK_NOTEBOOK(w->notebook), p);
	struct output_panel *op = g_object_get_data(G_OBJECT(tab), "op-pointer");
	struct snapshot *snapshot = op ? op->snst : w->active_snapshot;
	char *name = g_object_get_data(G_OBJECT(tab), "tab-name");

	if(snapshot->calibrate || !snapshot->pb) return;

	snapshot = snapshot_clone(snapshot);

	if(!snapshot->timestamp)
		snapshot->timestamp = get_timestamp(snapshot->is_light);

	FILE *f = choose_file_for_save(w, "Save current display", name);

	if(f) {
		if(write_file(f, &snapshot, &name, 1)) {
			GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(w->window),0,GTK_MESSAGE_ERROR,GTK_BUTTONS_CLOSE,
						"Error writing file");
			gtk_dialog_run(GTK_DIALOG(dialog));
			gtk_widget_destroy(dialog);
		}
		fclose(f);
	}

	snapshot_destroy(snapshot);
}

static void close_all(GtkMenuItem *m, struct main_window *w)
{
	UNUSED(m);
	int i = 0;
	while(i < gtk_notebook_get_n_pages(GTK_NOTEBOOK(w->notebook))) {
		GtkWidget *tab = gtk_notebook_get_nth_page(GTK_NOTEBOOK(w->notebook), i);
		struct output_panel *op = g_object_get_data(G_OBJECT(tab), "op-pointer");
		if(!op) {  // This one is the real-time tab
			i++;
			continue;
		}
		gtk_widget_destroy(tab);
	}
}

static void save_all(GtkMenuItem *m, struct main_window *w)
{
	UNUSED(m);
	FILE *f = choose_file_for_save(w, "Save all snapshots", NULL);
	if(!f) return;

	int i, j, tabs = gtk_notebook_get_n_pages(GTK_NOTEBOOK(w->notebook));
	struct snapshot *s[tabs];
	char *names[tabs];

	for(i = j = 0; i < tabs; i++) {
		GtkWidget *tab = gtk_notebook_get_nth_page(GTK_NOTEBOOK(w->notebook), i);
		struct output_panel *op = g_object_get_data(G_OBJECT(tab), "op-pointer");
		if(!op) continue; // This one is the real-time tab
		s[j] = op->snst;
		names[j++] = g_object_get_data(G_OBJECT(tab), "tab-name");
	}

	if(write_file(f, s, names, j)) {
		GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(w->window),0,GTK_MESSAGE_ERROR,GTK_BUTTONS_CLOSE,
					"Error writing file");
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
	}

	fclose(f);
}

static void load_snapshots(FILE *f, char *name, struct main_window *w)
{
	struct snapshot **s;
	char **names;
	uint64_t cnt;
	if(!read_file(f, &s, &names, &cnt)) {
		uint64_t i;
		for(i = 0; i < cnt; i++) {
			add_new_tab(s[i], names[i] ? names[i] : name, w);
			free(names[i]);
		}
		free(s);
		free(names);
	} else {
		GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(w->window),0,GTK_MESSAGE_ERROR,GTK_BUTTONS_CLOSE,
					"Error reading file: %s", name);
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
	}
}

static void load_from_file(char *filename, struct main_window *w)
{
	FILE *f = fopen_check(filename, "rb", w);
	if(f) {
		char *filename_cpy = strdup(filename);
		char *name = basename(filename_cpy);
		name = g_filename_to_utf8(name, -1, NULL, NULL, NULL);
		if(name && strlen(name) > 3 && !strcasecmp(".tgj", name + strlen(name) - 4))
			name[strlen(name) - 4] = 0;
		load_snapshots(f, name, w);
		free(filename_cpy);
		g_free(name);
		fclose(f);
	}
}

static void load(GtkMenuItem *m, struct main_window *w)
{
	UNUSED(m);
	GtkWidget *dialog = gtk_file_chooser_dialog_new ("Open",
			GTK_WINDOW(w->window),
			GTK_FILE_CHOOSER_ACTION_OPEN,
			"Cancel",
			GTK_RESPONSE_CANCEL,
			"Open",
			GTK_RESPONSE_ACCEPT,
			NULL);
	GtkFileChooser *chooser = GTK_FILE_CHOOSER (dialog);

	chooser_set_filters(chooser);

	if(GTK_RESPONSE_ACCEPT == gtk_dialog_run (GTK_DIALOG (dialog)))
	{
		GFile *gf = gtk_file_chooser_get_file(chooser);
		char *filename = g_file_get_path(gf);
		g_object_unref(gf);
		load_from_file(filename, w);
		g_free (filename);
	}

	gtk_widget_destroy(dialog);
}

/* Set up the main window and populate with widgets */
static void init_main_window(struct main_window *w)
{
	w->window = gtk_application_window_new(w->app);

	gtk_widget_set_size_request(w->window, 950, 700);

	gtk_container_set_border_width(GTK_CONTAINER(w->window), 10);
	g_signal_connect(w->window, "delete_event", G_CALLBACK(delete_event), w);

	gtk_window_set_title(GTK_WINDOW(w->window), PROGRAM_NAME " " VERSION);
	gtk_window_set_icon_name (GTK_WINDOW(w->window), PACKAGE);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_container_add(GTK_CONTAINER(w->window), vbox);

	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	// BPH label
	GtkWidget *label = gtk_label_new("bph");
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	// BPH combo box
	w->bph_combo_box = gtk_combo_box_text_new_with_entry();
	gtk_box_pack_start(GTK_BOX(hbox), w->bph_combo_box, FALSE, FALSE, 0);
	// Fill in pre-defined values
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->bph_combo_box), "guess");
	int i,current = 0;
	for(i = 0; preset_bph[i]; i++) {
		char s[100];
		sprintf(s,"%d", preset_bph[i]);
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->bph_combo_box), s);
		if(w->bph == preset_bph[i]) current = i+1;
	}
	if(current || w->bph == 0)
		gtk_combo_box_set_active(GTK_COMBO_BOX(w->bph_combo_box), current);
	else {
		char s[32];
		sprintf(s,"%d",w->bph);
		GtkEntry *e = GTK_ENTRY(gtk_bin_get_child(GTK_BIN(w->bph_combo_box)));
		gtk_entry_set_text(e,s);
	}
	g_signal_connect (w->bph_combo_box, "changed", G_CALLBACK(handle_bph_change), w);

	// Lift angle label
	label = gtk_label_new("lift angle");
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	// Lift angle spin button
	w->la_spin_button = gtk_spin_button_new_with_range(MIN_LA, MAX_LA, 1);
	gtk_box_pack_start(GTK_BOX(hbox), w->la_spin_button, FALSE, FALSE, 0);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->la_spin_button), w->la);
	g_signal_connect(w->la_spin_button, "value_changed", G_CALLBACK(handle_la_change), w);

	// Calibration label
	label = gtk_label_new("cal");
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	// Calibration spin button
	w->cal_spin_button = gtk_spin_button_new_with_range(MIN_CAL, MAX_CAL, 1);
	gtk_box_pack_start(GTK_BOX(hbox), w->cal_spin_button, FALSE, FALSE, 0);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->cal_spin_button), w->cal);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(w->cal_spin_button), FALSE);
	gtk_entry_set_width_chars(GTK_ENTRY(w->cal_spin_button), 6);
	g_signal_connect(w->cal_spin_button, "value_changed", G_CALLBACK(handle_cal_change), w);
	g_signal_connect(w->cal_spin_button, "output", G_CALLBACK(output_cal), NULL);
	g_signal_connect(w->cal_spin_button, "input", G_CALLBACK(input_cal), NULL);

	// Audio input device label + combo box
	label = gtk_label_new("mic");
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	w->device_combo_box = gtk_combo_box_text_new();
	gtk_box_pack_start(GTK_BOX(hbox), w->device_combo_box, FALSE, FALSE, 0);
	populate_devices(w);
	g_signal_connect(w->device_combo_box, "changed", G_CALLBACK(handle_device_change), w);

	w->dev_refresh = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_MENU);
	gtk_box_pack_start(GTK_BOX(hbox), w->dev_refresh, FALSE, FALSE, 0);
	g_signal_connect_swapped(w->dev_refresh, "clicked", G_CALLBACK(populate_devices), w);

	// Gain label + spin button
	label = gtk_label_new("gain");
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	w->gain_spin_button = gtk_spin_button_new_with_range(0.1, 100.0, 0.1);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->gain_spin_button), w->gain);
	gtk_spin_button_set_digits(GTK_SPIN_BUTTON(w->gain_spin_button), 1);
	gtk_entry_set_width_chars(GTK_ENTRY(w->gain_spin_button), 5);
	gtk_box_pack_start(GTK_BOX(hbox), w->gain_spin_button, FALSE, FALSE, 0);
	g_signal_connect(w->gain_spin_button, "value_changed", G_CALLBACK(handle_gain_change), w);

	// Cutoff label + spin button
	label = gtk_label_new("cutoff");
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	w->cutoff_spin_button = gtk_spin_button_new_with_range(1000, 8000, 100);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->cutoff_spin_button), w->filter_cutoff);
	gtk_entry_set_width_chars(GTK_ENTRY(w->cutoff_spin_button), 5);
	gtk_box_pack_start(GTK_BOX(hbox), w->cutoff_spin_button, FALSE, FALSE, 0);
	g_signal_connect(w->cutoff_spin_button, "value_changed", G_CALLBACK(handle_cutoff_change), w);

	// Source indicator
	label = gtk_label_new("mic");
	w->source_label = label;
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	// Level meter
	w->level_bar = gtk_progress_bar_new();
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w->level_bar), 0);
	gtk_widget_set_size_request(w->level_bar, 120, -1);
	gtk_box_pack_start(GTK_BOX(hbox), w->level_bar, FALSE, FALSE, 0);
	w->clip_label = gtk_label_new("CLIP");
	gtk_box_pack_start(GTK_BOX(hbox), w->clip_label, FALSE, FALSE, 0);
	gtk_widget_hide(w->clip_label);

	// Is there a more elegant way?
	GtkWidget *empty = gtk_label_new("");
	gtk_box_pack_start(GTK_BOX(hbox), empty, TRUE, FALSE, 0);

	// Snapshot button
	w->snapshot_button = gtk_button_new_with_label("Take Snapshot");
	gtk_box_pack_start(GTK_BOX(hbox), w->snapshot_button, FALSE, FALSE, 0);
	gtk_widget_set_sensitive(w->snapshot_button, FALSE);
	g_signal_connect(w->snapshot_button, "clicked", G_CALLBACK(handle_snapshot), w);

	// Snapshot name field
	GtkWidget *name_label = gtk_label_new("Current snapshot:");
	w->snapshot_name_entry = gtk_entry_new();
	w->snapshot_name = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(w->snapshot_name), name_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(w->snapshot_name), w->snapshot_name_entry, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), w->snapshot_name, FALSE, FALSE, 0);
	g_signal_connect(w->snapshot_name_entry, "changed", G_CALLBACK(handle_name_change), w);

	empty = gtk_label_new("");
	gtk_box_pack_start(GTK_BOX(hbox), empty, TRUE, FALSE, 0);

	// Command menu
	GtkWidget *command_menu = gtk_menu_new();
	GtkWidget *command_menu_button = gtk_menu_button_new();
#ifdef WIN_XP
	GtkWidget *image = image_from_file("open-menu.png");
#else
	GtkWidget *image = gtk_image_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
#endif
	gtk_button_set_image(GTK_BUTTON(command_menu_button), image);
	g_object_set(G_OBJECT(command_menu_button), "direction", GTK_ARROW_DOWN, NULL);
	g_object_set(G_OBJECT(command_menu), "halign", GTK_ALIGN_END, NULL);
	gtk_menu_button_set_popup(GTK_MENU_BUTTON(command_menu_button), command_menu);
	gtk_box_pack_end(GTK_BOX(hbox), command_menu_button, FALSE, FALSE, 0);
	
	// ... Open
	GtkWidget *open_item = gtk_menu_item_new_with_label("Open");
	gtk_menu_shell_append(GTK_MENU_SHELL(command_menu), open_item);
	g_signal_connect(open_item, "activate", G_CALLBACK(load), w);

	// ... Save
	w->save_item = gtk_menu_item_new_with_label("Save current display");
	gtk_menu_shell_append(GTK_MENU_SHELL(command_menu), w->save_item);
	g_signal_connect(w->save_item, "activate", G_CALLBACK(save_current), w);
	gtk_widget_set_sensitive(w->save_item, FALSE);

	// ... Save all
	w->save_all_item = gtk_menu_item_new_with_label("Save all snapshots");
	gtk_menu_shell_append(GTK_MENU_SHELL(command_menu), w->save_all_item);
	g_signal_connect(w->save_all_item, "activate", G_CALLBACK(save_all), w);
	gtk_widget_set_sensitive(w->save_all_item, FALSE);

	// ... Save session log
	GtkWidget *session_item = gtk_menu_item_new_with_label("Save session log");
	gtk_menu_shell_append(GTK_MENU_SHELL(command_menu), session_item);
	g_signal_connect(session_item, "activate", G_CALLBACK(handle_save_session_log), w);

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

	// ... Light checkbox
	w->light_checkbox = gtk_check_menu_item_new_with_label("Light algorithm");
	gtk_menu_shell_append(GTK_MENU_SHELL(command_menu), w->light_checkbox);
	g_signal_connect(w->light_checkbox, "toggled", G_CALLBACK(handle_light), w);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w->light_checkbox), w->is_light);

	// ... Calibrate checkbox
	w->cal_button = gtk_check_menu_item_new_with_label("Calibrate");
	gtk_menu_shell_append(GTK_MENU_SHELL(command_menu), w->cal_button);
	g_signal_connect(w->cal_button, "toggled", G_CALLBACK(handle_calibrate), w);

	gtk_menu_shell_append(GTK_MENU_SHELL(command_menu), gtk_separator_menu_item_new());

	// ... Close all
	w->close_all_item = gtk_menu_item_new_with_label("Close all snapshots");
	gtk_menu_shell_append(GTK_MENU_SHELL(command_menu), w->close_all_item);
	g_signal_connect(w->close_all_item, "activate", G_CALLBACK(close_all), w);
	gtk_widget_set_sensitive(w->close_all_item, FALSE);

	// ... Quit
	GtkWidget *quit_item = gtk_menu_item_new_with_label("Quit");
	gtk_menu_shell_append(GTK_MENU_SHELL(command_menu), quit_item);
	g_signal_connect(quit_item, "activate", G_CALLBACK(handle_quit), w);

	gtk_widget_show_all(command_menu);

	// The tabs' container
	w->notebook = gtk_notebook_new();
	gtk_box_pack_start(GTK_BOX(vbox), w->notebook, TRUE, TRUE, 0);
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(w->notebook), TRUE);
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(w->notebook), FALSE);
	gtk_notebook_set_show_border(GTK_NOTEBOOK(w->notebook), FALSE);
	g_signal_connect(w->notebook, "page-removed", G_CALLBACK(handle_tab_closed), w);
	g_signal_connect_after(w->notebook, "switch-page", G_CALLBACK(handle_tab_changed), w);

	// The main tab
	GtkWidget *tab_label = make_tab_label(NULL, NULL);
	gtk_notebook_append_page(GTK_NOTEBOOK(w->notebook), w->active_panel->panel, tab_label);
	gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(w->notebook), w->active_panel->panel, TRUE);

	gtk_window_maximize(GTK_WINDOW(w->window));
	gtk_widget_show_all(w->window);
	gtk_widget_hide(w->snapshot_name);
	gtk_window_set_focus(GTK_WINDOW(w->window), NULL);
}

guint save_on_change_timer(struct main_window *w)
{
	save_on_change(w);
	return TRUE;
}

guint refresh(struct main_window *w)
{
	lock_computer(w->computer);
	struct snapshot *s = w->computer->curr;
	if(s) {
		double trace_centering = w->active_snapshot->trace_centering;
		snapshot_destroy(w->active_snapshot);
		w->active_snapshot = s;
		w->computer->curr = NULL;
		s->trace_centering = trace_centering;
		if(w->computer->clear_trace && !s->calibrate)
			memset(s->events,0,s->events_count*sizeof(uint64_t));
		if(s->calibrate && s->cal_state == 1 && s->cal_result != w->cal) {
			w->cal = s->cal_result;
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->cal_spin_button), s->cal_result);
		}
	}
	unlock_computer(w->computer);
	refresh_results(w);
	op_set_snapshot(w->active_panel, w->active_snapshot);

	int p = gtk_notebook_get_current_page(GTK_NOTEBOOK(w->notebook));
	GtkWidget *panel = gtk_notebook_get_nth_page(GTK_NOTEBOOK(w->notebook), p);
	int photogenic = 0;
	if(!g_object_get_data(G_OBJECT(panel), "op-pointer")) {
		photogenic = !w->active_snapshot->calibrate && w->active_snapshot->pb;
		gtk_widget_set_sensitive(w->save_item, photogenic);
		gtk_widget_queue_draw(w->notebook);
	}
	gtk_widget_set_sensitive(w->snapshot_button, photogenic);

	struct snapshot *sn = w->active_snapshot;
	struct session_cycle sc;
	memset(&sc, 0, sizeof(sc));
	sc.wall_ms = g_get_real_time() / 1000;
	sc.audio = get_timestamp(sn->is_light);
	sc.signal = sn->signal;
	sc.guessed_bph = sn->guessed_bph;
	sc.rate = sn->rate;
	sc.be = sn->be;
	sc.amp = sn->amp;
	sc.calibrate = sn->calibrate;
	sc.cal_state = sn->cal_state;
	if(sn->pb) {
		sc.period = sn->pb->period / sn->pb->sample_rate;
		sc.sigma = sn->pb->sigma / sn->pb->sample_rate;
	}
	session_add_cycle(&sc);

	struct stats_point sp;
	memset(&sp, 0, sizeof(sp));
	sp.wall_ms = sc.wall_ms;
	sp.rate = sc.rate;
	sp.be = sc.be;
	sp.amp = sc.amp;
	stats_add(&sp);

	return FALSE;
}

static void computer_callback(void *w)
{
	gdk_threads_add_idle((GSourceFunc)refresh,w);
}

static void start_interface(GApplication* app, void *p)
{
	UNUSED(p);
	double real_sr;

	initialize_palette();

	struct main_window *w = malloc(sizeof(struct main_window));
	memset(w, 0, sizeof(struct main_window));

	session_init();
	stats_init();

	w->app = GTK_APPLICATION(app);

	w->zombie = 0;
	w->controls_active = 1;
	w->cal = MIN_CAL - 1;
	w->bph = 0;
	w->la = DEFAULT_LA;
	w->calibrate = 0;
	w->is_light = 0;
	w->input_device = NULL;
	w->restart_portaudio = 0;
	w->gain = 1.0;
	w->filter_cutoff = DEFAULT_FILTER_CUTOFF;

	load_config(w);

	/* Apply loaded config to runtime. */
	set_audio_gain(w->gain);
	filter_cutoff = w->filter_cutoff;

	if(start_portaudio(&w->nominal_sr, &real_sr, w->input_device)) {
		g_application_quit(app);
		return;
	}

	if(w->la < MIN_LA || w->la > MAX_LA) w->la = DEFAULT_LA;
	if(w->bph < MIN_BPH || w->bph > MAX_BPH) w->bph = 0;
	if(w->cal < MIN_CAL || w->cal > MAX_CAL)
		w->cal = (real_sr - w->nominal_sr) * (3600*24) / w->nominal_sr;

	w->computer_timeout = 0;

	w->computer = start_computer(w->nominal_sr, w->bph, w->la, w->cal, w->is_light);
	if(!w->computer) {
		error("Error starting computation thread");
		g_application_quit(app);
		return;
	}
	w->computer->callback = computer_callback;
	w->computer->callback_data = w;

	w->active_snapshot = w->computer->curr;
	w->computer->curr = NULL;
	compute_results(w->active_snapshot);

	w->active_panel = init_output_panel(w->computer, w->active_snapshot, 0);

	init_main_window(w);

	update_audio_mode_ui(w);

	w->kick_timeout = g_timeout_add_full(G_PRIORITY_LOW,100,(GSourceFunc)kick_computer,w,NULL);
	w->level_timeout = g_timeout_add_full(G_PRIORITY_LOW, 100, (GSourceFunc)update_level, w, NULL);
	w->save_timeout = g_timeout_add_full(G_PRIORITY_LOW,10000,(GSourceFunc)save_on_change_timer,w,NULL);
#ifdef DEBUG
	if(testing)
		g_timeout_add_full(G_PRIORITY_LOW,3000,(GSourceFunc)quit,w,NULL);
#endif

	g_object_set_data(G_OBJECT(app), "main-window", w);
}

static void handle_activate(GApplication* app, void *p)
{
	UNUSED(p);
	struct main_window *w = g_object_get_data(G_OBJECT(app), "main-window");
	if(w) gtk_window_present(GTK_WINDOW(w->window));
}

static void handle_open(GApplication* app, GFile **files, int cnt, char *hint, void *p)
{
	UNUSED(hint);
	UNUSED(p);
	struct main_window *w = g_object_get_data(G_OBJECT(app), "main-window");
	if(w) {
		int i;
		for(i = 0; i < cnt; i++) {
			char *path = g_file_get_path(files[i]);
			// This partially works around a bug in XP (i.e. gtk+ bundle 3.6.4)
			path = g_locale_to_utf8(path, -1, NULL, NULL, NULL);
			if(!path) continue;
			load_from_file(path, w);
			g_free(path);
		}
		gtk_notebook_set_current_page(GTK_NOTEBOOK(w->notebook), -1);
		gtk_window_present(GTK_WINDOW(w->window));
	}
}

int main(int argc, char **argv)
{
	gtk_disable_setlocale();

	int ai;
	for(ai = 1; ai < argc; ai++)
		if(!strcmp(argv[ai], "--help") || !strcmp(argv[ai], "-h")) {
			printf("Usage: %s [options]\n", argv[0]);
			printf("  debug            verbose console output (DSP diagnostics)\n");
			printf("  debug full       verbose including per-detection details\n");
#ifdef DEBUG
			printf("  analyze <wav>    headless analysis of an audio file\n");
			printf("  test             GUI smoke test (3 seconds)\n");
#endif
			printf("  -h, --help       show this help\n");
			return 0;
		}

	/* "debug": verbose console output (also in release builds).
	 * Level 1 = resumen por ciclo; "debug full" = level 2 = detalle. */
	if(argc > 1 && !strcmp("debug", argv[1])) {
		verbose_level = 1;
		argv++; argc--;
		if(argc > 1 && !strcmp("full", argv[1])) {
			verbose_level = 2;
			argv++; argc--;
		}
	}

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
#ifdef DEBUG
	if(argc > 1 && !strcmp("test",argv[1])) {
		testing = 1;
		argv++; argc--;
	}
#endif

	GtkApplication *app = gtk_application_new ("li.ciovil.tg", G_APPLICATION_HANDLES_OPEN);
	g_signal_connect (app, "startup", G_CALLBACK (start_interface), NULL);
	g_signal_connect (app, "activate", G_CALLBACK (handle_activate), NULL);
	g_signal_connect (app, "open", G_CALLBACK (handle_open), NULL);
	g_signal_connect (app, "shutdown", G_CALLBACK (on_shutdown), NULL);
	int ret = g_application_run (G_APPLICATION (app), argc, argv);
	g_object_unref (app);

	debug("Interface exited with status %d\n",ret);

	return ret;
}
