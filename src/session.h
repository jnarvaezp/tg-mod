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

#ifndef TG_SESSION_H
#define TG_SESSION_H

#include <stdint.h>

struct session_cycle {
	uint64_t wall_ms;    /* reloj de pared, ms desde epoch */
	uint64_t audio;      /* get_timestamp() (muestras del ring) */
	int signal;          /* nº de pasos listos / señal (0 = nada) */
	int guessed_bph;
	int calibrate;
	int cal_state;       /* estado de calibración (0..1) */
	double rate;         /* s/d */
	double be;           /* ms */
	double amp;          /* deg (0 = no disponible) */
	double period;       /* s */
	double sigma;        /* s */
};

/* Debe llamarse una vez al arrancar. */
void session_init(void);

/* Un registro por ciclo de cómputo publicado (anillo ~80 min). */
void session_add_cycle(const struct session_cycle *c);

/* Línea de texto crudo con timestamp (anillo ~20 000 líneas). */
void session_add_raw(uint64_t wall_ms, const char *line);

/* Escribe <dir>/<base>.{json,csv,raw}. Devuelve el nº de archivos con error (0 = ok). */
int session_save(const char *dir, const char *base);

#endif