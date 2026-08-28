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

#include "watchdb.h"
#include "stats.h"
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>      /* g_get_real_time */
#include "json.h"

#define WATCHDB_MAX_WATCHES 256
#define WATCHDB_MAX_SESSIONS 4096
#define WATCHDB_EXPORT_BUF 65536

static sqlite3 *db = NULL;

/* The watch list is cached in a static array refreshed after every write.
   UI-thread only for now, so no locking; wrap every entry point with a
   mutex if this ever becomes shared between threads. */
static struct {
	struct watchdb_watch w[WATCHDB_MAX_WATCHES];
	int n;
} watches;

/* Same pattern for the sessions of one watch; loaded_watch_id < 0 = none. */
static struct {
	struct watchdb_session s[WATCHDB_MAX_SESSIONS];
	int n;
	int64_t loaded_watch_id;
} sessions;

static const char *schema_sql =
	"PRAGMA foreign_keys=ON;"
	"CREATE TABLE IF NOT EXISTS watches ("
	" id INTEGER PRIMARY KEY,"
	" name TEXT NOT NULL,"
	" brand TEXT DEFAULT '',"
	" model TEXT DEFAULT '',"
	" serial TEXT DEFAULT '',"
	" year TEXT DEFAULT '',"
	" notes TEXT DEFAULT '',"
	" created_ms INTEGER,"
	" bph INTEGER DEFAULT 0,"
	" lift_angle REAL DEFAULT 0);"
	"CREATE TABLE IF NOT EXISTS sessions ("
	" id INTEGER PRIMARY KEY, watch_id INTEGER NOT NULL REFERENCES watches(id) ON DELETE CASCADE,"
	" start_ms INTEGER, end_ms INTEGER, position INTEGER, note TEXT DEFAULT '',"
	" n INTEGER, mean REAL, sigma REAL, min REAL, max REAL,"
	" mean_be REAL, mean_amp REAL,"
	" bph INTEGER, lift_angle REAL, cal INTEGER, gain REAL, cutoff INTEGER)";

static void set_str(char *dst, int size, const char *src)
{
	if(!src) {
		dst[0] = '\0';
		return;
	}
	snprintf(dst, size, "%s", src);
}

static int refresh_watches(void)
{
	sqlite3_stmt *stmt;
	int rc;

	watches.n = 0;
	if(!db) return -1;
	rc = sqlite3_prepare_v2(db, "SELECT id, name, brand, model, serial, year,"
				   " notes, created_ms, bph, lift_angle"
				   " FROM watches ORDER BY id", -1, &stmt, NULL);
	if(rc != SQLITE_OK) return -1;
	while(sqlite3_step(stmt) == SQLITE_ROW && watches.n < WATCHDB_MAX_WATCHES) {
		struct watchdb_watch *w = &watches.w[watches.n++];
		w->id = sqlite3_column_int64(stmt, 0);
		set_str(w->name, sizeof(w->name), (const char *)sqlite3_column_text(stmt, 1));
		set_str(w->brand, sizeof(w->brand), (const char *)sqlite3_column_text(stmt, 2));
		set_str(w->model, sizeof(w->model), (const char *)sqlite3_column_text(stmt, 3));
		set_str(w->serial, sizeof(w->serial), (const char *)sqlite3_column_text(stmt, 4));
		set_str(w->year, sizeof(w->year), (const char *)sqlite3_column_text(stmt, 5));
		set_str(w->notes, sizeof(w->notes), (const char *)sqlite3_column_text(stmt, 6));
		w->created_ms = sqlite3_column_int64(stmt, 7);
		w->bph = sqlite3_column_int(stmt, 8);
		w->lift_angle = sqlite3_column_double(stmt, 9);
	}
	sqlite3_finalize(stmt);
	return 0;
}

int watchdb_open(const char *path)
{
	char *err = NULL;

	if(!path || !*path) return -1;
	if(db) watchdb_close();
	if(sqlite3_open(path, &db) != SQLITE_OK || !db) {
		watchdb_close();
		return -1;
	}
	if(sqlite3_exec(db, schema_sql, NULL, NULL, &err) != SQLITE_OK) {
		fprintf(stderr, "watchdb: %s\n", err ? err : "schema error");
		sqlite3_free(err);
		watchdb_close();
		return -1;
	}
	sessions.n = 0;
	sessions.loaded_watch_id = -1;
	return refresh_watches();
}

int watchdb_close(void)
{
	int rc = 0;

	if(db) {
		rc = sqlite3_close(db) == SQLITE_OK ? 0 : -1;
		db = NULL;
	}
	sessions.n = 0;
	sessions.loaded_watch_id = -1;
	return rc;
}

int watchdb_watch_count(void)
{
	return watches.n;
}

const struct watchdb_watch *watchdb_watch_at(int i)
{
	if(i < 0 || i >= watches.n) return NULL;
	return &watches.w[i];
}

int watchdb_add_watch(const char *name, const char *brand, const char *notes)
{
	sqlite3_stmt *stmt;
	int rc;

	if(!db || !name || !*name || watches.n >= WATCHDB_MAX_WATCHES) return -1;
	rc = sqlite3_prepare_v2(db, "INSERT INTO watches"
				   " (name, brand, model, serial, year, notes, created_ms, bph, lift_angle)"
				   " VALUES (?1, ?2, '', '', '', ?3, ?4, 0, 0)", -1, &stmt, NULL);
	if(rc != SQLITE_OK) return -1;
	sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, brand ? brand : "", -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, notes ? notes : "", -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt, 4, g_get_real_time() / 1000);
	rc = sqlite3_step(stmt) == SQLITE_DONE ? 0 : -1;
	sqlite3_finalize(stmt);
	if(rc) return -1;
	return refresh_watches();
}

int watchdb_rename_watch(int idx, const char *name, const char *brand,
                         const char *notes, int bph, double lift_angle)
{
	sqlite3_stmt *stmt;
	int rc;

	if(!db || idx < 0 || idx >= watches.n || !name || !*name) return -1;
	rc = sqlite3_prepare_v2(db, "UPDATE watches SET name = ?1, brand = ?2,"
				   " notes = ?3, bph = ?4, lift_angle = ?5 WHERE id = ?6",
				   -1, &stmt, NULL);
	if(rc != SQLITE_OK) return -1;
	sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, brand ? brand : "", -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, notes ? notes : "", -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 4, bph);
	sqlite3_bind_double(stmt, 5, lift_angle);
	sqlite3_bind_int64(stmt, 6, watches.w[idx].id);
	rc = sqlite3_step(stmt) == SQLITE_DONE ? 0 : -1;
	sqlite3_finalize(stmt);
	if(rc) return -1;
	return refresh_watches();
}

int watchdb_remove_watch(int idx)
{
	sqlite3_stmt *stmt;
	int rc;

	if(!db || idx < 0 || idx >= watches.n) return -1;
	rc = sqlite3_prepare_v2(db, "DELETE FROM watches WHERE id = ?1", -1, &stmt, NULL);
	if(rc != SQLITE_OK) return -1;
	sqlite3_bind_int64(stmt, 1, watches.w[idx].id);
	rc = sqlite3_step(stmt) == SQLITE_DONE ? 0 : -1;
	sqlite3_finalize(stmt);
	if(rc) return -1;
	return refresh_watches();
}

int watchdb_load_sessions(int64_t watch_id)
{
	sqlite3_stmt *stmt;
	int rc;

	sessions.n = 0;
	sessions.loaded_watch_id = -1;
	if(!db) return -1;
	rc = sqlite3_prepare_v2(db, "SELECT id, watch_id, start_ms, end_ms, position, note,"
				   " n, mean, sigma, min, max, mean_be, mean_amp,"
				   " bph, cal, lift_angle, gain, cutoff"
				   " FROM sessions WHERE watch_id = ?1 ORDER BY start_ms",
				   -1, &stmt, NULL);
	if(rc != SQLITE_OK) return -1;
	sqlite3_bind_int64(stmt, 1, watch_id);
	while(sqlite3_step(stmt) == SQLITE_ROW && sessions.n < WATCHDB_MAX_SESSIONS) {
		struct watchdb_session *s = &sessions.s[sessions.n++];
		s->id = sqlite3_column_int64(stmt, 0);
		s->watch_id = sqlite3_column_int64(stmt, 1);
		s->start_ms = (uint64_t)sqlite3_column_int64(stmt, 2);
		s->end_ms = (uint64_t)sqlite3_column_int64(stmt, 3);
		s->position = sqlite3_column_int(stmt, 4);
		set_str(s->note, sizeof(s->note), (const char *)sqlite3_column_text(stmt, 5));
		s->n = sqlite3_column_int(stmt, 6);
		s->mean = sqlite3_column_double(stmt, 7);
		s->sigma = sqlite3_column_double(stmt, 8);
		s->min = sqlite3_column_double(stmt, 9);
		s->max = sqlite3_column_double(stmt, 10);
		s->mean_be = sqlite3_column_double(stmt, 11);
		s->mean_amp = sqlite3_column_double(stmt, 12);
		s->bph = sqlite3_column_int(stmt, 13);
		s->cal = sqlite3_column_int(stmt, 14);
		s->lift_angle = sqlite3_column_double(stmt, 15);
		s->gain = sqlite3_column_double(stmt, 16);
		s->cutoff = sqlite3_column_int(stmt, 17);
	}
	sqlite3_finalize(stmt);
	sessions.loaded_watch_id = watch_id;
	return 0;
}

int watchdb_session_count(void)
{
	return sessions.n;
}

const struct watchdb_session *watchdb_session_at(int i)
{
	if(i < 0 || i >= sessions.n) return NULL;
	return &sessions.s[i];
}

/* 1 = existe, 0 = no existe, -1 = error */
static int watch_exists(int64_t watch_id)
{
	sqlite3_stmt *stmt;
	int found = -1;

	if(!db) return -1;
	if(sqlite3_prepare_v2(db, "SELECT 1 FROM watches WHERE id = ?1",
			      -1, &stmt, NULL) != SQLITE_OK) return -1;
	sqlite3_bind_int64(stmt, 1, watch_id);
	found = sqlite3_step(stmt) == SQLITE_ROW;
	sqlite3_finalize(stmt);
	return found;
}

int watchdb_capture_session(int64_t watch_id, uint64_t start_ms, uint64_t end_ms,
                            int position, const char *note,
                            int bph, double lift_angle, int cal, double gain, int cutoff)
{
	sqlite3_stmt *stmt;
	struct stats_summary sum;
	int rc;

	if(!db) return -1;
	if(position < POSITION_NONE || position > POSITION_CR) return -1;
	if(watch_exists(watch_id) != 1) return -1;
	if(sessions.loaded_watch_id == watch_id && sessions.n >= WATCHDB_MAX_SESSIONS)
		return -1;
	stats_summary(0, &sum);
	rc = sqlite3_prepare_v2(db, "INSERT INTO sessions"
				   " (watch_id, start_ms, end_ms, position, note,"
				   " n, mean, sigma, min, max, mean_be, mean_amp,"
				   " bph, lift_angle, cal, gain, cutoff)"
				   " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10,"
				   " ?11, ?12, ?13, ?14, ?15, ?16, ?17)", -1, &stmt, NULL);
	if(rc != SQLITE_OK) return -1;
	sqlite3_bind_int64(stmt, 1, watch_id);
	sqlite3_bind_int64(stmt, 2, (int64_t)start_ms);
	sqlite3_bind_int64(stmt, 3, (int64_t)end_ms);
	sqlite3_bind_int(stmt, 4, position);
	sqlite3_bind_text(stmt, 5, note ? note : "", -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 6, sum.n);
	sqlite3_bind_double(stmt, 7, sum.mean);
	sqlite3_bind_double(stmt, 8, sum.sigma);
	sqlite3_bind_double(stmt, 9, sum.min);
	sqlite3_bind_double(stmt, 10, sum.max);
	sqlite3_bind_double(stmt, 11, sum.mean_be);
	sqlite3_bind_double(stmt, 12, sum.mean_amp);
	sqlite3_bind_int(stmt, 13, bph);
	sqlite3_bind_double(stmt, 14, lift_angle);
	sqlite3_bind_int(stmt, 15, cal);
	sqlite3_bind_double(stmt, 16, gain);
	sqlite3_bind_int(stmt, 17, cutoff);
	rc = sqlite3_step(stmt) == SQLITE_DONE ? 0 : -1;
	sqlite3_finalize(stmt);
	if(rc) return -1;
	if(sessions.loaded_watch_id == watch_id) return watchdb_load_sessions(watch_id);
	return 0;
}

int watchdb_remove_session(int64_t session_id)
{
	sqlite3_stmt *stmt;
	int rc;
	int64_t watch_id = -1;

	if(!db) return -1;
	if(sessions.loaded_watch_id >= 0) {
		if(sqlite3_prepare_v2(db, "SELECT watch_id FROM sessions WHERE id = ?1",
				      -1, &stmt, NULL) != SQLITE_OK) return -1;
		sqlite3_bind_int64(stmt, 1, session_id);
		if(sqlite3_step(stmt) == SQLITE_ROW) watch_id = sqlite3_column_int64(stmt, 0);
		sqlite3_finalize(stmt);
	}
	if(sqlite3_prepare_v2(db, "DELETE FROM sessions WHERE id = ?1",
			      -1, &stmt, NULL) != SQLITE_OK) return -1;
	sqlite3_bind_int64(stmt, 1, session_id);
	rc = sqlite3_step(stmt) == SQLITE_DONE ? 0 : -1;
	sqlite3_finalize(stmt);
	if(rc) return -1;
	if(watch_id >= 0 && watch_id == sessions.loaded_watch_id)
		return watchdb_load_sessions(watch_id);
	return 0;
}

static struct watchdb_watch *find_watch(int64_t id)
{
	int i;

	for(i = 0; i < watches.n; i++)
		if(watches.w[i].id == id) return &watches.w[i];
	return NULL;
}

int watchdb_export_watch_json(int64_t watch_id, const char *path)
{
	char *buf = NULL;
	struct json_writer w;
	struct watchdb_watch *watch;
	FILE *f;
	int i, rc = -1;

	if(!db || !path || !*path) return -1;
	watch = find_watch(watch_id);
	if(!watch) {
		if(refresh_watches()) return -1;
		watch = find_watch(watch_id);
	}
	if(!watch) return -1;
	if(watchdb_load_sessions(watch_id)) return -1;
	buf = malloc(WATCHDB_EXPORT_BUF);
	if(!buf) return -1;
	jw_init(&w, buf, WATCHDB_EXPORT_BUF);
	jw_object_begin(&w);
	jw_key(&w, "watch");
	jw_object_begin(&w);
	jw_key(&w, "id");
	jw_int(&w, (long long)watch->id);
	jw_key(&w, "name");
	jw_string(&w, watch->name);
	jw_key(&w, "brand");
	jw_string(&w, watch->brand);
	jw_key(&w, "model");
	jw_string(&w, watch->model);
	jw_key(&w, "serial");
	jw_string(&w, watch->serial);
	jw_key(&w, "year");
	jw_string(&w, watch->year);
	jw_key(&w, "notes");
	jw_string(&w, watch->notes);
	jw_key(&w, "bph");
	jw_int(&w, watch->bph);
	jw_key(&w, "lift_angle");
	jw_double(&w, watch->lift_angle);
	jw_object_end(&w);
	jw_key(&w, "sessions");
	jw_array_begin(&w);
	for(i = 0; i < sessions.n; i++) {
		const struct watchdb_session *s = &sessions.s[i];
		jw_object_begin(&w);
		jw_key(&w, "start_ms");
		jw_int(&w, (long long)s->start_ms);
		jw_key(&w, "end_ms");
		jw_int(&w, (long long)s->end_ms);
		jw_key(&w, "position");
		jw_string(&w, position_name(s->position));
		jw_key(&w, "note");
		jw_string(&w, s->note);
		jw_key(&w, "n");
		jw_int(&w, s->n);
		jw_key(&w, "mean");
		jw_double(&w, s->mean);
		jw_key(&w, "sigma");
		jw_double(&w, s->sigma);
		jw_key(&w, "min");
		jw_double(&w, s->min);
		jw_key(&w, "max");
		jw_double(&w, s->max);
		jw_key(&w, "mean_be");
		jw_double(&w, s->mean_be);
		jw_key(&w, "mean_amp");
		jw_double(&w, s->mean_amp);
		jw_object_end(&w);
	}
	jw_array_end(&w);
	jw_object_end(&w);
	if(!jw_ok(&w)) goto out;
	f = fopen(path, "w");
	if(!f) goto out;
	rc = fwrite(buf, 1, w.pos, f) == w.pos ? 0 : -1;
	if(fclose(f) && !rc) rc = -1;
out:
	free(buf);
	return rc;
}
