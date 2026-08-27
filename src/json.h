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

#ifndef TG_JSON_H
#define TG_JSON_H

#include <stdio.h>
#include <stddef.h>

/* --- Streaming writer (to buffer) --- */
struct json_writer {
	char *buf;
	size_t size, pos;
	int err;
};

/* Initialize the writer over a caller-provided buffer. */
void jw_init(struct json_writer *w, char *buf, size_t size);

/* Returns 1 unless the buffer has overflowed (subsequent calls become no-ops). */
int jw_ok(const struct json_writer *w);

void jw_object_begin(struct json_writer *w);
void jw_object_end(struct json_writer *w);
void jw_array_begin(struct json_writer *w);
void jw_array_end(struct json_writer *w);
void jw_key(struct json_writer *w, const char *key);
void jw_string(struct json_writer *w, const char *s);
void jw_int(struct json_writer *w, long long v);
void jw_double(struct json_writer *w, double v);
void jw_bool(struct json_writer *w, int b);
void jw_null(struct json_writer *w);

/* --- DOM reader --- */
enum json_type { JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT };

struct json_value {
	enum json_type type;
	union {
		int b;
		double num;
		char *str;
		struct json_value *child;   /* first member (objects); build list head for arrays */
		struct { struct json_value **items; int n; } arr;
	} u;
	char *key;                 /* key when this value is an object member */
	struct json_value *next;   /* next sibling */
};

/* Parse a complete JSON document. Returns NULL on any syntax error. */
struct json_value *json_parse(const char *text, size_t len);

/* Recursively free a parsed tree. NULL is allowed. */
void json_free(struct json_value *v);

/* Look up an object member by key. Returns NULL if absent or not an object. */
struct json_value *json_obj_get(struct json_value *obj, const char *key);

/* Number of elements of an array value (0 if not an array). */
int json_arr_size(struct json_value *arr);

/* Element at index i of an array value. Returns NULL if out of range or not an array. */
struct json_value *json_arr_get(struct json_value *arr, int i);

#endif
