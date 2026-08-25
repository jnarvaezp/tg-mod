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

#ifndef TG_REPORT_H
#define TG_REPORT_H

#include "stats.h"

/* none + 6 posiciones + total */
#define REPORT_MAX_ROWS (POSITION_CR + 2)

struct report_row {
	int position;
	int n;
	double mean, sigma, min, max;
	double mean_be, mean_amp;
};

/* Resumen por posición desde el anillo de stats (excluye POSITION_NONE).
 * Devuelve el nº de filas escritas (<= max_rows). */
int report_summary(struct report_row *rows, int max_rows);

/* Escribe el informe CSV en `path`. Devuelve 0 = ok. */
int report_write_csv(const char *path, const struct report_row *rows, int n);

/* Escribe el informe PDF (A4, Cairo) en `path`. Devuelve 0 = ok. */
int report_write_pdf(const char *path, const struct report_row *rows, int n);

#endif