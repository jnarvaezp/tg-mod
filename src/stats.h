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

#ifndef TG_STATS_H
#define TG_STATS_H

#include <stdint.h>

struct stats_point {
	uint64_t wall_ms;   /* reloj de pared, ms desde epoch */
	double rate;        /* s/d */
	double be;          /* ms */
	double amp;         /* deg (0 = no disponible) */
};

struct stats_summary {
	int n;
	double mean;        /* s/d */
	double sigma;       /* desviación estándar (n-1), s/d */
	double min;
	double max;
};

void stats_init(void);
void stats_add(const struct stats_point *p);

/* Resumen del rate sobre los últimos window_ms (0 = todos). Devuelve n. */
int stats_summary(uint64_t window_ms, struct stats_summary *out);

/* Copia en `out` (máx `max`) los puntos con wall_ms >= from_ms, en orden
 * cronológico. Devuelve el nº copiado. */
int stats_get_range(uint64_t from_ms, struct stats_point *out, int max);

void stats_clear(void);

#endif