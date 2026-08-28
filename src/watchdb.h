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

#ifndef TG_WATCHDB_H
#define TG_WATCHDB_H

#include <stdint.h>

struct watchdb_watch {
	int64_t id;
	char name[64];        /* obligatorio */
	char brand[64];
	char model[64];
	char serial[64];
	char year[16];
	char notes[256];
	uint64_t created_ms;
	int bph;              /* 0 = sin definir */
	double lift_angle;    /* 0 = sin definir */
};

/* Abre (y crea si falta) la base en `path`. 0 = ok. */
int watchdb_open(const char *path);
int watchdb_close(void);

int watchdb_watch_count(void);
const struct watchdb_watch *watchdb_watch_at(int i);
/* add devuelve 0 = ok. rename actualiza campos + defaults de análisis. */
int watchdb_add_watch(const char *name, const char *brand, const char *notes);
int watchdb_rename_watch(int idx, const char *name, const char *brand,
                         const char *notes, int bph, double lift_angle);
int watchdb_remove_watch(int idx);   /* borra el reloj y sus sesiones (CASCADE) */

struct watchdb_session {
	int64_t id;
	int64_t watch_id;
	uint64_t start_ms, end_ms;
	int position;        /* POSITION_* */
	char note[128];
	int n;
	double mean, sigma, min, max;
	double mean_be, mean_amp;
	int bph, cal;
	double lift_angle, gain;
	int cutoff;
};

int watchdb_load_sessions(int64_t watch_id);   /* carga el índice en memoria */
int watchdb_session_count(void);
const struct watchdb_session *watchdb_session_at(int i);
/* Crea el registro: toma n/mean/sigma/min/max/mean_be/mean_amp de stats_summary(0)
 * y la config actual de los parámetros. watch_id debe existir. 0 = ok. */
int watchdb_capture_session(int64_t watch_id, uint64_t start_ms, uint64_t end_ms,
                            int position, const char *note,
                            int bph, double lift_angle, int cal, double gain, int cutoff);
int watchdb_remove_session(int64_t session_id);

/* Exporta un reloj + sus sesiones como JSON (vía json.c). 0 = ok. */
int watchdb_export_watch_json(int64_t watch_id, const char *path);

#endif
