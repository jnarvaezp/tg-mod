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

#include "json.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#define JSON_MAX_DEPTH 32

/* --- Writer --- */

static void jw_append(struct json_writer *w, const char *s)
{
	if(w->err) return;
	size_t len = strlen(s);
	if(len >= w->size - w->pos) {
		w->err = 1;
		return;
	}
	memcpy(w->buf + w->pos, s, len + 1);
	w->pos += len;
}

static void jw_printf(struct json_writer *w, const char *fmt, ...)
{
	if(w->err) return;
	va_list ap;
	va_start(ap, fmt);
	int ret = vsnprintf(w->buf + w->pos, w->size - w->pos, fmt, ap);
	va_end(ap);
	if(ret < 0 || (size_t)ret >= w->size - w->pos) {
		w->err = 1;
		return;
	}
	w->pos += ret;
}

/* Commas: a value/element is needed before any emit unless the last byte
   written was a container opener or the colon of a key. This is stateless
   and valid because the writer never emits whitespace. */
static void jw_comma(struct json_writer *w)
{
	if(w->err || !w->pos) return;
	char last = w->buf[w->pos - 1];
	if(last != '{' && last != '[' && last != ':') jw_append(w, ",");
}

static void jw_string_escaped(struct json_writer *w, const char *s)
{
	jw_append(w, "\"");
	while(*s && !w->err) {
		unsigned char c = *s++;
		switch(c) {
		case '"': jw_append(w, "\\\""); break;
		case '\\': jw_append(w, "\\\\"); break;
		case '\b': jw_append(w, "\\b"); break;
		case '\f': jw_append(w, "\\f"); break;
		case '\n': jw_append(w, "\\n"); break;
		case '\r': jw_append(w, "\\r"); break;
		case '\t': jw_append(w, "\\t"); break;
		default:
			if(c < 0x20) jw_printf(w, "\\u%04x", c);
			else {
				char tmp[2] = { (char)c, '\0' };
				jw_append(w, tmp);
			}
			break;
		}
	}
	jw_append(w, "\"");
}

void jw_init(struct json_writer *w, char *buf, size_t size)
{
	w->buf = buf;
	w->size = size;
	w->pos = 0;
	w->err = 0;
	if(size) buf[0] = '\0';
}

int jw_ok(const struct json_writer *w)
{
	return !w->err;
}

void jw_object_begin(struct json_writer *w)
{
	jw_comma(w);
	jw_append(w, "{");
}

void jw_object_end(struct json_writer *w)
{
	jw_append(w, "}");
}

void jw_array_begin(struct json_writer *w)
{
	jw_comma(w);
	jw_append(w, "[");
}

void jw_array_end(struct json_writer *w)
{
	jw_append(w, "]");
}

void jw_key(struct json_writer *w, const char *key)
{
	jw_comma(w);
	jw_string_escaped(w, key);
	jw_append(w, ":");
}

void jw_string(struct json_writer *w, const char *s)
{
	jw_comma(w);
	jw_string_escaped(w, s);
}

void jw_int(struct json_writer *w, long long v)
{
	jw_comma(w);
	jw_printf(w, "%lld", v);
}

void jw_double(struct json_writer *w, double v)
{
	jw_comma(w);
	if(isfinite(v)) jw_printf(w, "%.6f", v);
	else jw_append(w, "null");
}

void jw_bool(struct json_writer *w, int b)
{
	jw_comma(w);
	jw_append(w, b ? "true" : "false");
}

void jw_null(struct json_writer *w)
{
	jw_comma(w);
	jw_append(w, "null");
}

/* --- Reader --- */

struct json_parser {
	const char *p;
	const char *end;
	int depth;
};

static struct json_value *jp_value(struct json_parser *ps);

static void jp_skip_ws(struct json_parser *ps)
{
	while(ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')) ps->p++;
}

static struct json_value *json_new(enum json_type t)
{
	struct json_value *v = calloc(1, sizeof(*v));
	if(v) v->type = t;
	return v;
}

static int jp_hex4(const char *p, unsigned *out)
{
	unsigned v = 0;
	for(int i = 0; i < 4; i++) {
		unsigned d;
		if(p[i] >= '0' && p[i] <= '9') d = p[i] - '0';
		else if(p[i] >= 'a' && p[i] <= 'f') d = p[i] - 'a' + 10;
		else if(p[i] >= 'A' && p[i] <= 'F') d = p[i] - 'A' + 10;
		else return -1;
		v = (v << 4) | d;
	}
	*out = v;
	return 0;
}

static char *jp_string(struct json_parser *ps)
{
	if(ps->p >= ps->end || *ps->p != '"') return NULL;
	ps->p++;
	size_t cap = ps->end - ps->p;
	char *out = malloc(cap + 1);
	if(!out) return NULL;
	size_t n = 0;
	while(ps->p < ps->end) {
		unsigned char c = *ps->p++;
		if(c == '"') {
			out[n] = '\0';
			return out;
		}
		if(c == '\\') {
			if(ps->p >= ps->end) break;
			char e = *ps->p++;
			switch(e) {
			case '"': out[n++] = '"'; break;
			case '\\': out[n++] = '\\'; break;
			case '/': out[n++] = '/'; break;
			case 'b': out[n++] = '\b'; break;
			case 'f': out[n++] = '\f'; break;
			case 'n': out[n++] = '\n'; break;
			case 'r': out[n++] = '\r'; break;
			case 't': out[n++] = '\t'; break;
			case 'u': {
				/* \uXXXX is decoded to UTF-8; lone surrogates become U+FFFD */
				unsigned cp;
				if(ps->end - ps->p < 4 || jp_hex4(ps->p, &cp) < 0) goto fail;
				ps->p += 4;
				if(cp >= 0xd800 && cp < 0xdc00 && ps->end - ps->p >= 6 && ps->p[0] == '\\' && ps->p[1] == 'u') {
					unsigned lo;
					if(jp_hex4(ps->p + 2, &lo) == 0 && lo >= 0xdc00 && lo < 0xe000) {
						cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
						ps->p += 6;
					}
				}
				if(cp >= 0xd800 && cp < 0xe000) cp = 0xfffd;
				if(cp < 0x80) {
					out[n++] = cp;
				} else if(cp < 0x800) {
					out[n++] = 0xc0 | (cp >> 6);
					out[n++] = 0x80 | (cp & 0x3f);
				} else if(cp < 0x10000) {
					out[n++] = 0xe0 | (cp >> 12);
					out[n++] = 0x80 | ((cp >> 6) & 0x3f);
					out[n++] = 0x80 | (cp & 0x3f);
				} else {
					out[n++] = 0xf0 | (cp >> 18);
					out[n++] = 0x80 | ((cp >> 12) & 0x3f);
					out[n++] = 0x80 | ((cp >> 6) & 0x3f);
					out[n++] = 0x80 | (cp & 0x3f);
				}
				break;
			}
			default: goto fail;
			}
		} else if(c >= 0x20) {
			out[n++] = c;
		} else {
			goto fail;
		}
	}
fail:
	free(out);
	return NULL;
}

static struct json_value *jp_lit(struct json_parser *ps, const char *lit, enum json_type t, int b)
{
	size_t len = strlen(lit);
	if((size_t)(ps->end - ps->p) < len || memcmp(ps->p, lit, len)) return NULL;
	ps->p += len;
	struct json_value *v = json_new(t);
	if(v && t == JSON_BOOL) v->u.b = b;
	return v;
}

static struct json_value *jp_number(struct json_parser *ps)
{
	const char *start = ps->p;
	while(ps->p < ps->end) {
		char c = *ps->p;
		if((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') ps->p++;
		else break;
	}
	size_t len = ps->p - start;
	if(!len) return NULL;
	char *tmp = malloc(len + 1);
	if(!tmp) return NULL;
	memcpy(tmp, start, len);
	tmp[len] = '\0';
	char *endp;
	double d = strtod(tmp, &endp);
	int ok = endp == tmp + len && endp != tmp;
	free(tmp);
	if(!ok) return NULL;
	struct json_value *v = json_new(JSON_NUMBER);
	if(v) v->u.num = d;
	return v;
}

static struct json_value *jp_object(struct json_parser *ps)
{
	struct json_value *obj = json_new(JSON_OBJECT);
	struct json_value *tail = NULL;
	if(!obj) return NULL;
	ps->p++;
	jp_skip_ws(ps);
	if(ps->p < ps->end && *ps->p == '}') {
		ps->p++;
		return obj;
	}
	while(ps->p < ps->end) {
		char *key = jp_string(ps);
		if(!key) goto fail;
		jp_skip_ws(ps);
		if(ps->p >= ps->end || *ps->p != ':') {
			free(key);
			goto fail;
		}
		ps->p++;
		struct json_value *val = jp_value(ps);
		if(!val) {
			free(key);
			goto fail;
		}
		val->key = key;
		if(tail) tail->next = val;
		else obj->u.child = val;
		tail = val;
		jp_skip_ws(ps);
		if(ps->p < ps->end && *ps->p == ',') {
			ps->p++;
			continue;
		}
		if(ps->p < ps->end && *ps->p == '}') {
			ps->p++;
			return obj;
		}
		goto fail;
	}
fail:
	json_free(obj);
	return NULL;
}

static struct json_value *jp_array(struct json_parser *ps)
{
	struct json_value *arr = json_new(JSON_ARRAY);
	struct json_value *head = NULL, *tail = NULL;
	int n = 0;
	if(!arr) return NULL;
	ps->p++;
	jp_skip_ws(ps);
	if(ps->p < ps->end && *ps->p == ']') {
		ps->p++;
		return arr;
	}
	while(ps->p < ps->end) {
		struct json_value *val = jp_value(ps);
		if(!val) goto fail;
		if(tail) tail->next = val;
		else head = val;
		tail = val;
		n++;
		jp_skip_ws(ps);
		if(ps->p < ps->end && *ps->p == ',') {
			ps->p++;
			continue;
		}
		if(ps->p < ps->end && *ps->p == ']') {
			ps->p++;
			break;
		}
		goto fail;
	}
	struct json_value **items = malloc(n * sizeof(*items));
	if(!items) goto fail;
	int i = 0;
	for(struct json_value *v = head; v; v = v->next) items[i++] = v;
	arr->u.arr.items = items;
	arr->u.arr.n = n;
	return arr;
fail:
	while(head) {
		struct json_value *next = head->next;
		json_free(head);
		head = next;
	}
	json_free(arr);
	return NULL;
}

static struct json_value *jp_value(struct json_parser *ps)
{
	jp_skip_ws(ps);
	if(ps->p >= ps->end) return NULL;
	char c = *ps->p;
	if((c == '{' || c == '[') && ps->depth >= JSON_MAX_DEPTH) return NULL;
	struct json_value *v = NULL;
	switch(c) {
	case '{':
		ps->depth++;
		v = jp_object(ps);
		ps->depth--;
		break;
	case '[':
		ps->depth++;
		v = jp_array(ps);
		ps->depth--;
		break;
	case '"':
		v = json_new(JSON_STRING);
		if(v) {
			v->u.str = jp_string(ps);
			if(!v->u.str) {
				free(v);
				v = NULL;
			}
		}
		break;
	case 't': v = jp_lit(ps, "true", JSON_BOOL, 1); break;
	case 'f': v = jp_lit(ps, "false", JSON_BOOL, 0); break;
	case 'n': v = jp_lit(ps, "null", JSON_NULL, 0); break;
	default: v = jp_number(ps); break;
	}
	return v;
}

struct json_value *json_parse(const char *text, size_t len)
{
	struct json_parser ps = { text, text + len, 0 };
	struct json_value *v = jp_value(&ps);
	if(!v) return NULL;
	jp_skip_ws(&ps);
	if(ps.p != ps.end) {
		json_free(v);
		return NULL;
	}
	return v;
}

void json_free(struct json_value *v)
{
	if(!v) return;
	if(v->type == JSON_STRING) {
		free(v->u.str);
	} else if(v->type == JSON_ARRAY) {
		for(int i = 0; i < v->u.arr.n; i++) json_free(v->u.arr.items[i]);
		free(v->u.arr.items);
	} else if(v->type == JSON_OBJECT) {
		struct json_value *m = v->u.child;
		while(m) {
			struct json_value *next = m->next;
			json_free(m);
			m = next;
		}
	}
	free(v->key);
	free(v);
}

struct json_value *json_obj_get(struct json_value *obj, const char *key)
{
	if(!obj || obj->type != JSON_OBJECT) return NULL;
	for(struct json_value *m = obj->u.child; m; m = m->next)
		if(m->key && !strcmp(m->key, key)) return m;
	return NULL;
}

int json_arr_size(struct json_value *arr)
{
	if(!arr || arr->type != JSON_ARRAY) return 0;
	return arr->u.arr.n;
}

struct json_value *json_arr_get(struct json_value *arr, int i)
{
	if(!arr || arr->type != JSON_ARRAY || i < 0 || i >= arr->u.arr.n) return NULL;
	return arr->u.arr.items[i];
}
