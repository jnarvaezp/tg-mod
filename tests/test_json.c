#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "json.h"

static int failures = 0;
#define CHECK(cond, msg) do { if(!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } } while(0)

int main(void)
{
	/* --- Escritor --- */
	char buf[4096];
	struct json_writer w;
	jw_init(&w, buf, sizeof(buf));
	jw_object_begin(&w);
	jw_key(&w, "name");
	jw_string(&w, "Omega \"565\"");
	jw_key(&w, "bph");
	jw_int(&w, 21600);
	jw_key(&w, "rate");
	jw_double(&w, 177.556);
	jw_key(&w, "tags");
	jw_array_begin(&w);
	jw_string(&w, "a");
	jw_string(&w, "b");
	jw_array_end(&w);
	jw_key(&w, "ok");
	jw_bool(&w, 1);
	jw_key(&w, "none");
	jw_null(&w);
	jw_object_end(&w);
	CHECK(jw_ok(&w), "writer ok");
	CHECK(strstr(buf, "\"name\":\"Omega \\\"565\\\"\"") != NULL, "escape");
	CHECK(strstr(buf, "\"bph\":21600") != NULL, "int");
	CHECK(strstr(buf, "\"rate\":177.556000") != NULL, "double");
	CHECK(strstr(buf, "\"tags\":[\"a\",\"b\"]") != NULL, "array");
	CHECK(strstr(buf, "\"ok\":true") != NULL && strstr(buf, "\"none\":null") != NULL, "bool/null");

	/* --- Parser (round-trip) --- */
	struct json_value *v = json_parse(buf, strlen(buf));
	CHECK(v != NULL, "parse ok");
	CHECK(v->type == JSON_OBJECT, "root object");
	struct json_value *name = json_obj_get(v, "name");
	CHECK(name && name->type == JSON_STRING && !strcmp(name->u.str, "Omega \"565\""), "name round-trip");
	struct json_value *bph = json_obj_get(v, "bph");
	CHECK(bph && bph->type == JSON_NUMBER && fabs(bph->u.num - 21600) < 1e-9, "bph");
	struct json_value *tags = json_obj_get(v, "tags");
	CHECK(tags && tags->type == JSON_ARRAY && json_arr_size(tags) == 2, "tags array");
	struct json_value *t0 = json_arr_get(tags, 0);
	CHECK(t0 && t0->type == JSON_STRING && !strcmp(t0->u.str, "a"), "tag0");
	CHECK(json_obj_get(v, "missing") == NULL, "missing key");

	json_free(v);

	/* --- Malformado -> error sin crash --- */
	CHECK(json_parse("{", 1) == NULL, "truncated");
	CHECK(json_parse("{\"a\":}", 6) == NULL, "bad value");
	CHECK(json_parse("not json", 8) == NULL, "garbage");
	CHECK(json_parse("{\"a\":1},", 8) == NULL, "trailing");

	if(failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	printf("json tests passed\n");
	return 0;
}
