#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "session.h"

static int failures = 0;
#define CHECK(cond, msg) do { if(!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } } while(0)

int main(void)
{
	session_init();

	struct session_cycle c1 = { 1000, 1000, 4, 0, 0, 0,
		1.234567, 0.500000, 250.000000, 0.328160, 0.000038 };
	struct session_cycle c2 = { 2000, 2000, 0, 0, 0, 0,
		0.0, 0.0, 0.0, 0.0, 0.0 };
	session_add_cycle(&c1);
	session_add_cycle(&c2);
	session_add_raw(1000, "START OF COMPUTATION CYCLE\n");
	session_add_raw(2000, "no candidate period\n");

	const char *dir = "test_session_out";
	char json_path[1024], csv_path[1024], raw_path[1024];
	mkdir(dir, 0755);
	snprintf(json_path, sizeof(json_path), "%s/test_session.json", dir);
	snprintf(csv_path, sizeof(csv_path), "%s/test_session.csv", dir);
	snprintf(raw_path, sizeof(raw_path), "%s/test_session.raw", dir);

	CHECK(session_save(dir, "test_session") == 0, "save ok");

	/* JSON */
	{
		char buf[8192] = {0};
		FILE *f = fopen(json_path, "r");
		CHECK(f != NULL, "json exists");
		if(f) {
			size_t n = fread(buf, 1, sizeof(buf)-1, f);
			buf[n] = 0;
			fclose(f);
			CHECK(strstr(buf, "\"wall_ms\":1000") != NULL, "json c1 wall");
			CHECK(strstr(buf, "\"rate\":1.234567") != NULL, "json c1 rate");
			CHECK(strstr(buf, "\"period\":0.328160") != NULL, "json c1 period");
			CHECK(strstr(buf, "\"wall_ms\":2000") != NULL, "json c2 wall");
		}
	}

	/* CSV */
	{
		char buf[2048] = {0};
		FILE *f = fopen(csv_path, "r");
		CHECK(f != NULL, "csv exists");
		if(f) {
			size_t n = fread(buf, 1, sizeof(buf)-1, f);
			buf[n] = 0;
			fclose(f);
			CHECK(strstr(buf, "wall_ms,audio,signal,bph,rate,be,amp,period,sigma,calibrate,cal_state") != NULL,
			      "csv header");
			CHECK(strstr(buf, "1000,1000,4,0,1.234567,0.500000,250.000000,0.328160,0.000038,0,0") != NULL,
			      "csv c1 row");
		}
	}

	/* RAW */
	{
		char buf[1024] = {0};
		FILE *f = fopen(raw_path, "r");
		CHECK(f != NULL, "raw exists");
		if(f) {
			size_t n = fread(buf, 1, sizeof(buf)-1, f);
			buf[n] = 0;
			fclose(f);
			CHECK(strstr(buf, "[1000] START OF COMPUTATION CYCLE") != NULL, "raw line 1");
			CHECK(strstr(buf, "[2000] no candidate period") != NULL, "raw line 2");
		}
	}

	remove(json_path);
	remove(csv_path);
	remove(raw_path);
	rmdir(dir);

	if(failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	printf("session tests passed\n");
	return 0;
}