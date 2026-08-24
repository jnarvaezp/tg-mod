#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include "stats.h"
#include "report.h"

static int failures = 0;
#define CHECK(cond, msg) do { if(!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } } while(0)

int main(void)
{
	stats_init();
	mkdir("test_report_out", 0755);
	struct stats_point p = { 0, 0, 0, 0, 0 };
	int i;
	for(i = 0; i < 10; i++) {
		p.wall_ms = 1000 + i;
		p.rate = i;
		p.be = 1.0;
		p.amp = 180.0;
		p.position = POSITION_DU;
		stats_add(&p);
	}
	p.wall_ms = 2000; p.rate = 50.0; p.be = 2.0; p.amp = 190.0;
	p.position = POSITION_DD;
	stats_add(&p);

	struct report_row rows[7];
	int n = report_summary(rows, 7);
	CHECK(n == 2, "two positions");
	int du = -1, dd = -1;
	for(i = 0; i < n; i++) {
		if(rows[i].position == POSITION_DU) du = i;
		if(rows[i].position == POSITION_DD) dd = i;
	}
	CHECK(du >= 0 && dd >= 0, "rows found");
	CHECK(rows[du].n == 10, "du count");
	CHECK(fabs(rows[du].mean - 4.5) < 1e-9, "du mean");
	CHECK(fabs(rows[du].mean_be - 1.0) < 1e-9, "du mean_be");
	CHECK(fabs(rows[du].mean_amp - 180.0) < 1e-9, "du mean_amp");
	CHECK(rows[dd].n == 1 && fabs(rows[dd].max - 50.0) < 1e-9, "dd row");

	CHECK(report_write_csv("test_report_out/report.csv", rows, n) == 0, "csv ok");
	{
		char buf[4096] = {0};
		FILE *f = fopen("test_report_out/report.csv", "r");
		CHECK(f != NULL, "csv exists");
		if(f) {
			size_t r = fread(buf, 1, sizeof(buf) - 1, f);
			buf[r] = 0;
			fclose(f);
			CHECK(strstr(buf, "position,n,mean,sigma,min,max,mean_be,mean_amp") != NULL, "csv header");
			CHECK(strstr(buf, "dial up") != NULL, "csv du row");
			CHECK(strstr(buf, "dial down") != NULL, "csv dd row");
		}
	}

	CHECK(report_write_pdf("test_report_out/report.pdf", rows, n) == 0, "pdf ok");
	{
		FILE *f = fopen("test_report_out/report.pdf", "r");
		CHECK(f != NULL, "pdf exists");
		if(f) {
			char hdr[5] = {0};
			fread(hdr, 1, 4, f);
			fclose(f);
			CHECK(!strcmp(hdr, "%PDF"), "pdf header");
		}
	}

	remove("test_report_out/report.csv");
	remove("test_report_out/report.pdf");
	rmdir("test_report_out");

	if(failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	printf("report tests passed\n");
	return 0;
}