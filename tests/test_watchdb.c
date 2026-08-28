#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include "watchdb.h"
#include "stats.h"

static int failures = 0;
#define CHECK(cond, msg) do { if(!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } } while(0)

int main(void)
{
	const char *dir = "test_watchdb_out";
	mkdir(dir, 0755);
	char dbpath[512];
	snprintf(dbpath, sizeof(dbpath), "%s/tg.db", dir);

	CHECK(watchdb_open(dbpath) == 0, "open");
	CHECK(watchdb_watch_count() == 0, "empty");

	CHECK(watchdb_add_watch("Omega 565", "Omega", "rueda de balance dañada") == 0, "add w1");
	CHECK(watchdb_add_watch("Longines 280", "Longines", "") == 0, "add w2");
	CHECK(watchdb_watch_count() == 2, "two watches");
	const struct watchdb_watch *w0 = watchdb_watch_at(0);
	CHECK(w0 && !strcmp(w0->name, "Omega 565") && !strcmp(w0->brand, "Omega"), "w0 fields");
	CHECK(w0->bph == 0 && w0->lift_angle == 0, "w0 defaults unset");

	CHECK(watchdb_rename_watch(0, "Omega 565 mod", "Omega", "nota", 21600, 52.0) == 0, "rename");
	w0 = watchdb_watch_at(0);
	CHECK(w0->bph == 21600 && fabs(w0->lift_angle - 52.0) < 1e-9, "defaults set");

	CHECK(watchdb_close() == 0, "close");
	CHECK(watchdb_open(dbpath) == 0, "reopen");
	CHECK(watchdb_watch_count() == 2, "persisted");
	w0 = watchdb_watch_at(0);
	CHECK(!strcmp(w0->name, "Omega 565 mod") && w0->bph == 21600, "persisted fields");

	CHECK(watchdb_remove_watch(0) == 0, "remove");
	CHECK(watchdb_watch_count() == 1, "one left");
	const struct watchdb_watch *w1 = watchdb_watch_at(0);
	CHECK(w1 && !strcmp(w1->name, "Longines 280"), "remaining");

	/* Sesiones */
	int64_t wid = w1->id;
	CHECK(watchdb_load_sessions(wid) == 0, "load sessions");
	CHECK(watchdb_session_count() == 0, "no sessions");
	CHECK(watchdb_capture_session(wid, 1000, 2000, POSITION_DU, "prueba",
	                              21600, 52.0, 0, 0.5, 3000) == 0, "capture");
	CHECK(watchdb_session_count() == 1, "one session");
	const struct watchdb_session *ss = watchdb_session_at(0);
	CHECK(ss->position == POSITION_DU && ss->bph == 21600 && fabs(ss->gain - 0.5) < 1e-9, "session fields");
	CHECK(watchdb_capture_session(wid, 3000, 4000, POSITION_DD, "", 21600, 52.0, 0, 0.5, 3000) == 0, "capture 2");
	CHECK(watchdb_session_count() == 2, "two sessions");

	/* Persistencia: cerrar y reabrir */
	CHECK(watchdb_close() == 0, "close 2");
	CHECK(watchdb_open(dbpath) == 0, "reopen 2");
	CHECK(watchdb_load_sessions(wid) == 0, "load sessions 2");
	CHECK(watchdb_session_count() == 2, "sessions persisted");

	/* Export JSON */
	char jpath[512];
	snprintf(jpath, sizeof(jpath), "%s/export.json", dir);
	CHECK(watchdb_export_watch_json(wid, jpath) == 0, "export ok");
	{
		FILE *f = fopen(jpath, "r");
		CHECK(f != NULL, "json exists");
		if(f) {
			char buf[8192] = {0};
			size_t r = fread(buf, 1, sizeof(buf)-1, f);
			buf[r] = 0;
			fclose(f);
			CHECK(strstr(buf, "\"sessions\"") != NULL, "json sessions");
			CHECK(strstr(buf, "dial up") != NULL, "json position");
			CHECK(strstr(buf, "\"name\":\"Longines 280\"") != NULL, "json watch name");
		}
	}
	remove(jpath);
	CHECK(watchdb_remove_session(watchdb_session_at(0)->id) == 0, "remove session");
	CHECK(watchdb_session_count() == 1, "one session left");

	watchdb_close();

	remove(dbpath);
	rmdir(dir);
	if(failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	printf("watchdb tests passed\n");
	return 0;
}
