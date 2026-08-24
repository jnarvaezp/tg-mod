#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "stats.h"

static int failures = 0;
#define CHECK(cond, msg) do { if(!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } } while(0)

int main(void)
{
	stats_init();

	/* 5 puntos con rate 0,1,2,3,4 */
	int i;
	struct stats_point p = { 0, 0, 0, 0 };
	for(i = 0; i < 5; i++) {
		p.wall_ms = 1000 + i;
		p.rate = i;
		stats_add(&p);
	}

	struct stats_summary s;
	CHECK(stats_summary(0, &s) == 5, "count");
	CHECK(fabs(s.mean - 2.0) < 1e-9, "mean");
	CHECK(fabs(s.min - 0.0) < 1e-9, "min");
	CHECK(fabs(s.max - 4.0) < 1e-9, "max");
	CHECK(fabs(s.sigma - sqrt(2.5)) < 1e-9, "sigma (n-1)");

	/* ventana: solo los últimos 2 (wall_ms 1003,1004) */
	CHECK(stats_summary(1, &s) == 2, "window count");
	CHECK(fabs(s.mean - 3.5) < 1e-9, "window mean");

	/* rango: copia desde 1003 -> 2 puntos en orden */
	struct stats_point out[8];
	int n = stats_get_range(1003, out, 8);
	CHECK(n == 2 && out[0].rate == 3.0 && out[1].rate == 4.0, "get_range");

	stats_clear();
	CHECK(stats_summary(0, &s) == 0, "cleared");

	if(failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	printf("stats tests passed\n");
	return 0;
}