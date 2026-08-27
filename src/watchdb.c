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
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>      /* g_get_real_time */

#define WATCHDB_MAX_WATCHES 256

static sqlite3 *db = NULL;

/* The watch list is cached in a static array refreshed after every write.
   UI-thread only for now, so no locking; wrap every entry point with a
   mutex if this ever becomes shared between threads. */
static struct {
	struct watchdb_watch w[WATCHDB_MAX_WATCHES];
	int n;
} watches;

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
	return refresh_watches();
}

int watchdb_close(void)
{
	int rc = 0;

	if(db) {
		rc = sqlite3_close(db) == SQLITE_OK ? 0 : -1;
		db = NULL;
	}
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
