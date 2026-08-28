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
#include "watch_panel.h"
#include "watchdb.h"
#include "stats.h"
#include "report.h"
#include <glib/gi18n.h>
#include <ctype.h>
#include <time.h>

static int blank_string(const char *s)
{
	if(!s) return 1;
	for(; *s; s++)
		if(!isspace((unsigned char)*s)) return 0;
	return 1;
}

static int find_watch_index(int64_t id)
{
	int i;

	for(i = 0; i < watchdb_watch_count(); i++) {
		const struct watchdb_watch *ww = watchdb_watch_at(i);
		if(ww && ww->id == id) return i;
	}
	return -1;
}

static void update_sensitivity(struct main_window *w)
{
	/* While a session is being recorded the selection must not change
	 * (Finish saves to the currently selected watch). */
	gtk_widget_set_sensitive(w->watch_list, !w->session_active);
	gtk_widget_set_sensitive(w->watch_delete_button,
	                         w->selected_watch_id >= 0 && !w->session_active);
	gtk_widget_set_sensitive(w->session_start_button,
	                         w->selected_watch_id >= 0 && !w->session_active);
	gtk_widget_set_sensitive(w->session_finish_button, w->session_active);
	gtk_widget_set_sensitive(w->watch_defaults_button,
	                         w->selected_watch_id >= 0 && !w->session_active);
	gtk_widget_set_sensitive(w->evolution_button, w->selected_watch_id >= 0);
	gtk_widget_set_sensitive(w->history_export_button,
	                         w->selected_watch_id >= 0 && !w->session_active);
}

static void update_session_status(struct main_window *w)
{
	char buf[160];

	if(w->session_active) {
		uint64_t now = (uint64_t)g_get_real_time() / 1000;
		uint64_t elapsed = now > w->session_start_ms ? now - w->session_start_ms : 0;
		snprintf(buf, sizeof(buf), _("recording session: %s (%02d:%02d)"),
		         w->selected_watch_name,
		         (int)(elapsed / 60000), (int)((elapsed / 1000) % 60));
		gtk_label_set_text(GTK_LABEL(w->session_status_label), buf);
	} else {
		gtk_label_set_text(GTK_LABEL(w->session_status_label), _("no session"));
	}
}

static void rebuild_session_tree(struct main_window *w)
{
	GtkListStore *store = gtk_list_store_new(8,
		G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
		G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT64);
	int i;

	for(i = 0; i < watchdb_session_count(); i++) {
		const struct watchdb_session *s = watchdb_session_at(i);
		char date[32], pos[32], nstr[32], mean[32], sigma[32], be[32], amp[32];
		time_t t;
		struct tm *lt;
		GtkTreeIter iter;

		if(!s) continue;
		t = (time_t)(s->start_ms / 1000);
		lt = localtime(&t);
		if(lt)
			strftime(date, sizeof(date), "%Y-%m-%d %H:%M", lt);
		else
			snprintf(date, sizeof(date), "?");
		snprintf(pos, sizeof(pos), "%s", position_name(s->position));
		snprintf(nstr, sizeof(nstr), "%d", s->n);
		if(s->n > 0) {
			/* Rates in s/d with explicit sign. */
			snprintf(mean, sizeof(mean), "%+.1f", s->mean);
			snprintf(sigma, sizeof(sigma), "%.1f", s->sigma);
			snprintf(be, sizeof(be), "%.1f", s->mean_be);
			snprintf(amp, sizeof(amp), "%.0f", s->mean_amp);
		} else {
			/* Session captured without a valid signal: not meaningful. */
			snprintf(mean, sizeof(mean), "---");
			snprintf(sigma, sizeof(sigma), "---");
			snprintf(be, sizeof(be), "---");
			snprintf(amp, sizeof(amp), "---");
		}
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter,
			0, date, 1, pos, 2, nstr, 3, mean, 4, sigma, 5, be, 6, amp,
			7, (gint64)s->id, -1);
	}
	gtk_tree_view_set_model(GTK_TREE_VIEW(w->session_tree), GTK_TREE_MODEL(store));
	g_object_unref(store);
	w->selected_session_id = -1;
	gtk_widget_set_sensitive(w->session_delete_button, FALSE);
}

static void on_session_selected(GtkTreeSelection *sel, struct main_window *w)
{
	GtkTreeModel *model;
	GtkTreeIter iter;

	w->selected_session_id = -1;
	if(gtk_tree_selection_get_selected(sel, &model, &iter))
		gtk_tree_model_get(model, &iter, 7, &w->selected_session_id, -1);
	gtk_widget_set_sensitive(w->session_delete_button, w->selected_session_id >= 0);
}

static void on_delete_session_clicked(GtkButton *button, struct main_window *w)
{
	GtkWidget *dialog;
	int response;

	UNUSED(button);
	if(w->selected_session_id < 0) return;

	dialog = gtk_message_dialog_new(GTK_WINDOW(w->window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL,
		_("Delete the selected session from the history?"));
	response = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	if(response != GTK_RESPONSE_OK) return;

	if(watchdb_remove_session(w->selected_session_id))
		error(_("Failed to delete session"));
	watchdb_load_sessions(w->selected_watch_id);
	rebuild_session_tree(w);
}

static void on_save_defaults_clicked(GtkButton *button, struct main_window *w)
{
	int idx;
	UNUSED(button);
	idx = find_watch_index(w->selected_watch_id);
	if(idx < 0) return;
	const struct watchdb_watch *ww = watchdb_watch_at(idx);
	if(!ww) return;
	if(watchdb_rename_watch(idx, ww->name, ww->brand, ww->notes, w->bph, w->la))
		error(_("Failed to save watch defaults"));
}

/* Copia de las sesiones para el diálogo de evolución. */
struct evo_data {
	char name[64];
	int n;
	struct watchdb_session *s;
};

static void evo_data_free(gpointer data)
{
	struct evo_data *ed = data;
	free(ed->s);
	free(ed);
}

static gboolean evo_draw_event(GtkWidget *widget, cairo_t *cr, gpointer data)
{
	struct evo_data *ed = g_object_get_data(G_OBJECT(widget), "evo-data");
	UNUSED(data);
	if(!ed || ed->n < 1) return FALSE;

	GtkAllocation temp;
	gtk_widget_get_allocation(widget, &temp);
	const int width = temp.width, height = temp.height;

	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_paint(cr);

	if(!ed || ed->n < 1) {
		cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
		cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
		cairo_set_font_size(cr, 12);
		cairo_move_to(cr, 10, height / 2.0);
		cairo_show_text(cr, _("No sessions recorded"));
		return FALSE;
	}

	double maxabs = 10;
	int i;
	for(i = 0; i < ed->n; i++) {
		double a = fabs(ed->s[i].mean);
		if(a > maxabs) maxabs = a;
	}
	maxabs *= 1.15;
	if(maxabs < 10) maxabs = 10;

	const double mid = height / 2.0;
	const double scale = (height - 50) / 2.0 / maxabs;
	const double x0 = 40, xw = width - 60;

	/* línea cero */
	cairo_set_line_width(cr, 1);
	cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);
	cairo_move_to(cr, x0, mid);
	cairo_line_to(cr, x0 + xw, mid);
	cairo_stroke(cr);
	/* límites ±maxabs */
	cairo_set_source_rgb(cr, 0.92, 0.92, 0.92);
	cairo_move_to(cr, x0, mid - scale * maxabs);
	cairo_line_to(cr, x0 + xw, mid - scale * maxabs);
	cairo_move_to(cr, x0, mid + scale * maxabs);
	cairo_line_to(cr, x0 + xw, mid + scale * maxabs);
	cairo_stroke(cr);

	/* polyline del rate por sesión */
	cairo_set_source_rgb(cr, 0, 0.6, 0);
	cairo_set_line_width(cr, 1.5);
	for(i = 0; i < ed->n; i++) {
		const double x = x0 + (ed->n > 1 ? (double)i / (ed->n - 1) * xw : xw / 2);
		const double y = mid - ed->s[i].mean * scale;
		if(!i) cairo_move_to(cr, x, y);
		else cairo_line_to(cr, x, y);
	}
	cairo_stroke(cr);

	/* puntos */
	cairo_set_source_rgb(cr, 0, 0, 0);
	for(i = 0; i < ed->n; i++) {
		const double x = x0 + (ed->n > 1 ? (double)i / (ed->n - 1) * xw : xw / 2);
		const double y = mid - ed->s[i].mean * scale;
		cairo_arc(cr, x, y, 3, 0, 2 * M_PI);
		cairo_stroke(cr);
	}

	/* resumen */
	char txt[160];
	double sum = 0, sq = 0;
	for(i = 0; i < ed->n; i++) { sum += ed->s[i].mean; sq += ed->s[i].mean * ed->s[i].mean; }
	double mean = sum / ed->n;
	double sigma = ed->n > 1 ? sqrt((sq - ed->n * mean * mean) / (ed->n - 1)) : 0;
	snprintf(txt, sizeof(txt), _("%s:  sessions %d,  mean %+.1f s/d,  sigma %.1f"),
	         ed->name, ed->n, mean, sigma);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 11);
	cairo_move_to(cr, x0, height - 8);
	cairo_show_text(cr, txt);
	return FALSE;
}

static void on_evolution_clicked(GtkButton *button, struct main_window *w)
{
	UNUSED(button);
	if(w->selected_watch_id < 0) return;
	watchdb_load_sessions(w->selected_watch_id);
	int n = watchdb_session_count();
	if(!n) { error(_("No sessions recorded for this watch")); return; }

	struct evo_data *ed = malloc(sizeof(*ed));
	if(!ed) return;
	snprintf(ed->name, sizeof(ed->name), "%s", w->selected_watch_name);
	ed->n = n;
	ed->s = malloc(n * sizeof(*ed->s));
	int i;
	for(i = 0; i < n; i++) {
		const struct watchdb_session *s = watchdb_session_at(i);
		if(s) ed->s[i] = *s;
	}

	GtkWidget *dlg = gtk_dialog_new_with_buttons(_("Rate evolution"),
		GTK_WINDOW(w->window), GTK_DIALOG_DESTROY_WITH_PARENT,
		_("Close"), GTK_RESPONSE_CLOSE, NULL);
	GtkWidget *da = gtk_drawing_area_new();
	gtk_widget_set_size_request(da, 640, 400);
	g_object_set_data_full(G_OBJECT(da), "evo-data", ed, evo_data_free);
	g_signal_connect(da, "draw", G_CALLBACK(evo_draw_event), NULL);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
		da, TRUE, TRUE, 0);
	gtk_widget_show_all(dlg);
	gtk_dialog_run(GTK_DIALOG(dlg));
	gtk_widget_destroy(dlg);
}

static void on_export_history_clicked(GtkButton *button, struct main_window *w)
{
	int idx, n, err = 0;
	const struct watchdb_watch *ww;
	char *dir, safe[64], fullbase[256], path[1024];
	time_t t;

	UNUSED(button);
	idx = find_watch_index(w->selected_watch_id);
	if(idx < 0) { error(_("Select a watch first")); return; }
	ww = watchdb_watch_at(idx);
	if(!ww) return;

	n = watchdb_session_count();
	if(!n) { error(_("No sessions recorded for this watch")); return; }

	dir = g_build_filename(g_get_home_dir(), "tg-logs", NULL);
	g_mkdir_with_parents(dir, 0755);
	t = time(NULL);
	{
		char ts[32];
		strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", localtime(&t));
		/* Nombre de archivo seguro: alfanumérico + guiones. */
		char nm[64];
		size_t i, o = 0;
		snprintf(nm, sizeof(nm), "%s", w->selected_watch_name);
		for(i = 0; nm[i] && o + 1 < sizeof(safe); i++)
			safe[o++] = (nm[i] >= 'a' && nm[i] <= 'z') || (nm[i] >= 'A' && nm[i] <= 'Z') ||
			            (nm[i] >= '0' && nm[i] <= '9') ? nm[i] : '-';
		safe[o] = 0;
		snprintf(fullbase, sizeof(fullbase), "tg-history-%s-%s", safe, ts);
	}

	snprintf(path, sizeof(path), "%s/%s.csv", dir, fullbase);
	if(report_write_history_csv(path, w->selected_watch_name,
	                            watchdb_session_at(0), n)) err++;
	snprintf(path, sizeof(path), "%s/%s.pdf", dir, fullbase);
	if(report_write_history_pdf(path, w->selected_watch_name,
	                            watchdb_session_at(0), n)) err++;
	if(err)
		error(_("History export: %d file(s) failed in %s"), err, dir);
	else {
		GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(w->window), 0,
			GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
			_("History exported to\n%s/%s.{csv,pdf}"), dir, fullbase);
		gtk_dialog_run(GTK_DIALOG(d));
		gtk_widget_destroy(d);
	}
	g_free(dir);
}

static void on_watch_selected(GtkListBox *box, GtkListBoxRow *row, struct main_window *w)
{
	UNUSED(box);

	if(row) {
		int i;
		const struct watchdb_watch *ww = NULL;
		w->selected_watch_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "watch-id"));
		w->selected_watch_name[0] = '\0';
		for(i = 0; i < watchdb_watch_count(); i++) {
			const struct watchdb_watch *x = watchdb_watch_at(i);
			if(x && x->id == w->selected_watch_id) { ww = x; break; }
		}
		watchdb_load_sessions(w->selected_watch_id);

		/* Preload the watch's analysis defaults into the UI. */
		if(ww) {
			snprintf(w->selected_watch_name, sizeof(w->selected_watch_name), "%s", ww->name);
			if(ww->bph > 0) {
				int j, current = 0;
				for(j = 0; preset_bph[j]; j++)
					if(ww->bph == preset_bph[j]) { current = j + 1; break; }
				gtk_combo_box_set_active(GTK_COMBO_BOX(w->bph_combo_box), current);
				if(!current) {
					char t[32];
					snprintf(t, sizeof(t), "%d", ww->bph);
					gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(w->bph_combo_box))), t);
				}
			}
			if(ww->lift_angle > 0)
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->la_spin_button), ww->lift_angle);
		}
	} else {
		w->selected_watch_id = -1;
		w->selected_watch_name[0] = '\0';
	}
	rebuild_session_tree(w);
	update_sensitivity(w);
	update_session_status(w);
}

static void remove_child(GtkWidget *widget, gpointer data)
{
	gtk_container_remove(GTK_CONTAINER(data), widget);
}

static void rebuild_watch_list(struct main_window *w, int64_t select_id)
{
	int i;
	GtkListBoxRow *select_row = NULL;

	g_signal_handlers_block_by_func(w->watch_list, on_watch_selected, w);
	gtk_container_foreach(GTK_CONTAINER(w->watch_list), remove_child, w->watch_list);
	for(i = 0; i < watchdb_watch_count(); i++) {
		const struct watchdb_watch *ww = watchdb_watch_at(i);
		GtkWidget *row, *label;

		if(!ww) continue;
		label = gtk_label_new(ww->name);
		gtk_widget_set_halign(label, GTK_ALIGN_START);
		gtk_widget_set_margin_start(label, 4);
		row = gtk_list_box_row_new();
		gtk_container_add(GTK_CONTAINER(row), label);
		g_object_set_data(G_OBJECT(row), "watch-id", GINT_TO_POINTER((int)ww->id));
		gtk_widget_show_all(row);
		gtk_list_box_insert(GTK_LIST_BOX(w->watch_list), row, -1);
		if(ww->id == select_id) select_row = GTK_LIST_BOX_ROW(row);
	}
	g_signal_handlers_unblock_by_func(w->watch_list, on_watch_selected, w);

	if(select_row) {
		/* Fires on_watch_selected: loads sessions and rebuilds the tree. */
		gtk_list_box_select_row(GTK_LIST_BOX(w->watch_list), select_row);
	} else {
		w->selected_watch_id = -1;
		w->selected_watch_name[0] = '\0';
		watchdb_load_sessions(-1);
		rebuild_session_tree(w);
	}
}

void watch_panel_refresh(struct main_window *w)
{
	rebuild_watch_list(w, w->selected_watch_id);
	update_sensitivity(w);
	update_session_status(w);
}

static gboolean session_tick(gpointer data)
{
	struct main_window *w = data;

	if(w->session_active) update_session_status(w);
	return TRUE;
}

static void on_new_watch_clicked(GtkButton *button, struct main_window *w)
{
	UNUSED(button);

	GtkWidget *dialog = gtk_dialog_new_with_buttons(_("New watch"),
		GTK_WINDOW(w->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		_("Cancel"), GTK_RESPONSE_CANCEL, _("OK"), GTK_RESPONSE_ACCEPT, NULL);
	GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	GtkWidget *grid = gtk_grid_new();
	GtkWidget *name_entry = gtk_entry_new();
	GtkWidget *brand_entry = gtk_entry_new();
	GtkWidget *model_entry = gtk_entry_new();
	char name[64];
	int added = 0;

	gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
	gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Name")), 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), name_entry, 1, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Brand")), 0, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), brand_entry, 1, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Model")), 0, 2, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), model_entry, 1, 2, 1, 1);
	gtk_box_pack_start(GTK_BOX(area), grid, TRUE, TRUE, 0);
	gtk_entry_set_activates_default(GTK_ENTRY(name_entry), TRUE);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
	gtk_widget_show_all(dialog);
	gtk_widget_grab_focus(name_entry);

	name[0] = '\0';
	for(;;) {
		const char *t;
		if(gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) break;
		t = gtk_entry_get_text(GTK_ENTRY(name_entry));
		if(blank_string(t)) {
			error(_("Watch name is required"));
			continue;
		}
		if(watchdb_add_watch(t, gtk_entry_get_text(GTK_ENTRY(brand_entry)),
		                     gtk_entry_get_text(GTK_ENTRY(model_entry)))) {
			error(_("Cannot add watch"));
			break;
		}
		snprintf(name, sizeof(name), "%s", t);
		added = 1;
		break;
	}
	gtk_widget_destroy(dialog);

	if(added) {
		int64_t new_id = -1;
		int i, count = watchdb_watch_count();
		for(i = 0; i < count; i++) {
			const struct watchdb_watch *ww = watchdb_watch_at(i);
			if(ww && !strcmp(ww->name, name)) new_id = ww->id;
		}
		if(new_id < 0 && count > 0) {
			const struct watchdb_watch *ww = watchdb_watch_at(count - 1);
			if(ww) new_id = ww->id;
		}
		if(new_id >= 0) w->selected_watch_id = new_id;
		watch_panel_refresh(w);
	}
}

static void on_delete_watch_clicked(GtkButton *button, struct main_window *w)
{
	UNUSED(button);

	int idx = find_watch_index(w->selected_watch_id);
	if(idx < 0) return;

	char name[64];
	snprintf(name, sizeof(name), "%s", w->selected_watch_name);
	GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(w->window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL,
		_("Delete watch %s and all its sessions?"), name);
	gint resp = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	if(resp != GTK_RESPONSE_OK) return;

	if(watchdb_remove_watch(idx))
		error(_("Cannot delete watch"));
	w->selected_watch_id = -1;
	w->selected_watch_name[0] = '\0';
	watch_panel_refresh(w);
}

static void on_start_session_clicked(GtkButton *button, struct main_window *w)
{
	UNUSED(button);

	if(w->selected_watch_id < 0 || w->session_active) return;
	w->session_active = 1;
	w->session_start_ms = (uint64_t)g_get_real_time() / 1000;
	update_sensitivity(w);
	update_session_status(w);
}

static void on_finish_session_clicked(GtkButton *button, struct main_window *w)
{
	UNUSED(button);

	if(!w->session_active || w->selected_watch_id < 0) return;
	if(watchdb_capture_session(w->selected_watch_id, w->session_start_ms,
	                           (uint64_t)g_get_real_time() / 1000, w->position,
	                           gtk_entry_get_text(GTK_ENTRY(w->session_note_entry)),
	                           w->bph, w->la, w->cal, w->gain, w->filter_cutoff))
		error(_("Cannot save session"));
	w->session_active = 0;
	gtk_entry_set_text(GTK_ENTRY(w->session_note_entry), "");
	watch_panel_refresh(w);
}

GtkWidget *watch_panel_build(struct main_window *w)
{
	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	GtkWidget *scroll, *hbox, *label;
	int i;

	w->selected_watch_id = -1;
	w->selected_watch_name[0] = '\0';
	w->session_active = 0;
	w->session_start_ms = 0;

	gtk_widget_set_size_request(vbox, 280, -1);

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), _("<b>Watches</b>"));
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

	w->watch_list = gtk_list_box_new();
	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scroll), w->watch_list);
	gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
	g_signal_connect(w->watch_list, "row-selected", G_CALLBACK(on_watch_selected), w);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	GtkWidget *new_button = gtk_button_new_with_label(_("New watch..."));
	g_signal_connect(new_button, "clicked", G_CALLBACK(on_new_watch_clicked), w);
	gtk_box_pack_start(GTK_BOX(hbox), new_button, TRUE, FALSE, 0);
	w->watch_delete_button = gtk_button_new_with_label(_("Delete"));
	g_signal_connect(w->watch_delete_button, "clicked", G_CALLBACK(on_delete_watch_clicked), w);
	gtk_widget_set_sensitive(w->watch_delete_button, FALSE);
	gtk_box_pack_start(GTK_BOX(hbox), w->watch_delete_button, TRUE, FALSE, 0);
	w->watch_defaults_button = gtk_button_new_with_label(_("Save cfg"));
	gtk_widget_set_tooltip_text(w->watch_defaults_button,
		_("Save the current BPH and lift angle as this watch's defaults"));
	g_signal_connect(w->watch_defaults_button, "clicked", G_CALLBACK(on_save_defaults_clicked), w);
	gtk_widget_set_sensitive(w->watch_defaults_button, FALSE);
	gtk_box_pack_start(GTK_BOX(hbox), w->watch_defaults_button, TRUE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(vbox),
		gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), _("<b>Session</b>"));
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

	w->session_note_entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(w->session_note_entry), _("session note"));
	gtk_box_pack_start(GTK_BOX(vbox), w->session_note_entry, FALSE, FALSE, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	w->session_start_button = gtk_button_new_with_label(_("Start session"));
	gtk_widget_set_tooltip_text(w->session_start_button,
		"Start recording a measurement session for the selected watch");
	g_signal_connect(w->session_start_button, "clicked", G_CALLBACK(on_start_session_clicked), w);
	gtk_box_pack_start(GTK_BOX(hbox), w->session_start_button, TRUE, FALSE, 0);
	w->session_finish_button = gtk_button_new_with_label(_("Finish & save"));
	g_signal_connect(w->session_finish_button, "clicked", G_CALLBACK(on_finish_session_clicked), w);
	gtk_widget_set_sensitive(w->session_finish_button, FALSE);
	gtk_box_pack_start(GTK_BOX(hbox), w->session_finish_button, TRUE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	w->session_status_label = gtk_label_new(_("no session"));
	gtk_widget_set_halign(w->session_status_label, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(vbox), w->session_status_label, FALSE, FALSE, 0);

	w->session_tree = gtk_tree_view_new();
	{
		static const char *titles[7] = {
			N_("Date"), N_("Position"), N_("n"), N_("Mean (s/d)"), N_("σ"), N_("BE (ms)"), N_("Amp (deg)")
		};
		for(i = 0; i < 7; i++) {
			GtkCellRenderer *rend = gtk_cell_renderer_text_new();
			GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(
				_(titles[i]), rend, "text", i, NULL);
			gtk_tree_view_column_set_resizable(col, TRUE);
			gtk_tree_view_append_column(GTK_TREE_VIEW(w->session_tree), col);
		}
		/* Column 7 (session id) exists in the store but is not shown. */
	}
	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scroll), w->session_tree);
	gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

	w->session_delete_button = gtk_button_new_with_label(_("Delete session"));
	g_signal_connect(w->session_delete_button, "clicked",
		G_CALLBACK(on_delete_session_clicked), w);
	gtk_widget_set_sensitive(w->session_delete_button, FALSE);
	gtk_box_pack_start(GTK_BOX(vbox), w->session_delete_button, FALSE, FALSE, 0);
	{
		GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(w->session_tree));
		g_signal_connect(sel, "changed", G_CALLBACK(on_session_selected), w);
	}

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	w->evolution_button = gtk_button_new_with_label(_("Evolution"));
	gtk_widget_set_tooltip_text(w->evolution_button, _("Rate evolution across sessions"));
	g_signal_connect(w->evolution_button, "clicked", G_CALLBACK(on_evolution_clicked), w);
	gtk_widget_set_sensitive(w->evolution_button, FALSE);
	gtk_box_pack_start(GTK_BOX(hbox), w->evolution_button, TRUE, FALSE, 0);
	w->history_export_button = gtk_button_new_with_label(_("Export history..."));
	gtk_widget_set_tooltip_text(w->history_export_button,
		_("Export this watch's session history to CSV and PDF"));
	g_signal_connect(w->history_export_button, "clicked", G_CALLBACK(on_export_history_clicked), w);
	gtk_widget_set_sensitive(w->history_export_button, FALSE);
	gtk_box_pack_start(GTK_BOX(hbox), w->history_export_button, TRUE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	w->session_timeout = g_timeout_add(1000, session_tick, w);

	watch_panel_refresh(w);

	return vbox;
}
