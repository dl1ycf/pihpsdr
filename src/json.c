/* Copyright (C)
*  2026 - piHPSDR Modernisation
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "json.h"

//
// Recursive-descent parser state. "p" always points at the next character to
// be consumed; "error" latches the first failure so the caller bails out.
//
typedef struct {
  const char *p;
  int error;
} JsonParser;

static JsonValue *parse_value(JsonParser *ps);

static JsonValue *json_new(JsonType type) {
  JsonValue *v = (JsonValue *) calloc(1, sizeof(JsonValue));

  if (v != NULL) { v->type = type; }

  return v;
}

void json_free(JsonValue *v) {
  while (v != NULL) {
    JsonValue *next = v->next;

    if (v->child != NULL) { json_free(v->child); }

    free(v->key);
    free(v->string);
    free(v);
    v = next;
  }
}

static void skip_ws(JsonParser *ps) {
  const char *p = ps->p;

  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') { p++; }

  ps->p = p;
}

//
// Append one byte to a growing, heap-allocated buffer. Returns the (possibly
// reallocated) buffer, or NULL on allocation failure (in which case the old
// buffer is freed).
//
static char *buf_append(char *buf, size_t *len, size_t *cap, char c) {
  if (*len + 1 >= *cap) {
    size_t ncap = (*cap == 0) ? 16 : (*cap * 2);
    char *nbuf = (char *) realloc(buf, ncap);

    if (nbuf == NULL) {
      free(buf);
      return NULL;
    }

    buf = nbuf;
    *cap = ncap;
  }

  buf[(*len)++] = c;
  return buf;
}

//
// Encode a Unicode code point as UTF-8 into the buffer.
//
static char *buf_append_utf8(char *buf, size_t *len, size_t *cap, unsigned int cp) {
  if (cp < 0x80) {
    buf = buf_append(buf, len, cap, (char)(cp));
  } else if (cp < 0x800) {
    buf = buf_append(buf, len, cap, (char)(0xC0 | (cp >> 6)));
    if (buf) { buf = buf_append(buf, len, cap, (char)(0x80 | (cp & 0x3F))); }
  } else if (cp < 0x10000) {
    buf = buf_append(buf, len, cap, (char)(0xE0 | (cp >> 12)));
    if (buf) { buf = buf_append(buf, len, cap, (char)(0x80 | ((cp >> 6) & 0x3F))); }
    if (buf) { buf = buf_append(buf, len, cap, (char)(0x80 | (cp & 0x3F))); }
  } else {
    buf = buf_append(buf, len, cap, (char)(0xF0 | (cp >> 18)));
    if (buf) { buf = buf_append(buf, len, cap, (char)(0x80 | ((cp >> 12) & 0x3F))); }
    if (buf) { buf = buf_append(buf, len, cap, (char)(0x80 | ((cp >> 6) & 0x3F))); }
    if (buf) { buf = buf_append(buf, len, cap, (char)(0x80 | (cp & 0x3F))); }
  }

  return buf;
}

static int hex4(const char *p, unsigned int *out) {
  unsigned int value = 0;

  for (int i = 0; i < 4; i++) {
    char c = p[i];
    value <<= 4;

    if (c >= '0' && c <= '9') { value |= (unsigned int)(c - '0'); }
    else if (c >= 'a' && c <= 'f') { value |= (unsigned int)(c - 'a' + 10); }
    else if (c >= 'A' && c <= 'F') { value |= (unsigned int)(c - 'A' + 10); }
    else { return 0; }
  }

  *out = value;
  return 1;
}

//
// Parse a JSON string literal (the opening quote has NOT yet been consumed).
// Returns a newly allocated, NUL-terminated C string, or NULL on error.
//
static char *parse_string_raw(JsonParser *ps) {
  if (*ps->p != '"') { ps->error = 1; return NULL; }

  ps->p++;                               // consume opening quote
  char *buf = NULL;
  size_t len = 0, cap = 0;

  while (*ps->p != '\0' && *ps->p != '"') {
    unsigned char c = (unsigned char) *ps->p;

    if (c == '\\') {
      ps->p++;
      char e = *ps->p;

      switch (e) {
      case '"':  buf = buf_append(buf, &len, &cap, '"');  ps->p++; break;
      case '\\': buf = buf_append(buf, &len, &cap, '\\'); ps->p++; break;
      case '/':  buf = buf_append(buf, &len, &cap, '/');  ps->p++; break;
      case 'b':  buf = buf_append(buf, &len, &cap, '\b'); ps->p++; break;
      case 'f':  buf = buf_append(buf, &len, &cap, '\f'); ps->p++; break;
      case 'n':  buf = buf_append(buf, &len, &cap, '\n'); ps->p++; break;
      case 'r':  buf = buf_append(buf, &len, &cap, '\r'); ps->p++; break;
      case 't':  buf = buf_append(buf, &len, &cap, '\t'); ps->p++; break;

      case 'u': {
        unsigned int cp = 0;
        ps->p++;                         // consume 'u'

        if (!hex4(ps->p, &cp)) { ps->error = 1; free(buf); return NULL; }

        ps->p += 4;

        //
        // Handle UTF-16 surrogate pairs.
        //
        if (cp >= 0xD800 && cp <= 0xDBFF) {
          unsigned int low = 0;

          if (ps->p[0] == '\\' && ps->p[1] == 'u' && hex4(ps->p + 2, &low) &&
              low >= 0xDC00 && low <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            ps->p += 6;
          }
        }

        buf = buf_append_utf8(buf, &len, &cap, cp);
      }
      break;

      default:
        ps->error = 1;
        free(buf);
        return NULL;
      }

      if (buf == NULL) { ps->error = 1; return NULL; }

      continue;
    }

    buf = buf_append(buf, &len, &cap, (char) c);

    if (buf == NULL) { ps->error = 1; return NULL; }

    ps->p++;
  }

  if (*ps->p != '"') { ps->error = 1; free(buf); return NULL; }

  ps->p++;                               // consume closing quote
  buf = buf_append(buf, &len, &cap, '\0');

  if (buf == NULL) { ps->error = 1; return NULL; }

  return buf;
}

static JsonValue *parse_string(JsonParser *ps) {
  char *s = parse_string_raw(ps);

  if (s == NULL) { return NULL; }

  JsonValue *v = json_new(JSON_STRING);

  if (v == NULL) { free(s); ps->error = 1; return NULL; }

  v->string = s;
  return v;
}

static JsonValue *parse_number(JsonParser *ps) {
  char *end = NULL;
  double d = strtod(ps->p, &end);

  if (end == ps->p) { ps->error = 1; return NULL; }

  JsonValue *v = json_new(JSON_NUMBER);

  if (v == NULL) { ps->error = 1; return NULL; }

  v->number = d;
  ps->p = end;
  return v;
}

static JsonValue *parse_literal(JsonParser *ps) {
  if (strncmp(ps->p, "true", 4) == 0) {
    ps->p += 4;
    JsonValue *v = json_new(JSON_BOOL);

    if (v == NULL) { ps->error = 1; return NULL; }

    v->boolean = 1;
    return v;
  }

  if (strncmp(ps->p, "false", 5) == 0) {
    ps->p += 5;
    JsonValue *v = json_new(JSON_BOOL);

    if (v == NULL) { ps->error = 1; return NULL; }

    v->boolean = 0;
    return v;
  }

  if (strncmp(ps->p, "null", 4) == 0) {
    ps->p += 4;
    return json_new(JSON_NULL);
  }

  ps->error = 1;
  return NULL;
}

static JsonValue *parse_array(JsonParser *ps) {
  ps->p++;                               // consume '['
  JsonValue *arr = json_new(JSON_ARRAY);

  if (arr == NULL) { ps->error = 1; return NULL; }

  JsonValue *tail = NULL;
  skip_ws(ps);

  if (*ps->p == ']') { ps->p++; return arr; }

  for (;;) {
    JsonValue *elem = parse_value(ps);

    if (elem == NULL) { json_free(arr); return NULL; }

    if (tail == NULL) { arr->child = elem; } else { tail->next = elem; }

    tail = elem;
    skip_ws(ps);

    if (*ps->p == ',') { ps->p++; skip_ws(ps); continue; }

    if (*ps->p == ']') { ps->p++; break; }

    ps->error = 1;
    json_free(arr);
    return NULL;
  }

  return arr;
}

static JsonValue *parse_object(JsonParser *ps) {
  ps->p++;                               // consume '{'
  JsonValue *obj = json_new(JSON_OBJECT);

  if (obj == NULL) { ps->error = 1; return NULL; }

  JsonValue *tail = NULL;
  skip_ws(ps);

  if (*ps->p == '}') { ps->p++; return obj; }

  for (;;) {
    skip_ws(ps);

    if (*ps->p != '"') { ps->error = 1; json_free(obj); return NULL; }

    char *key = parse_string_raw(ps);

    if (key == NULL) { json_free(obj); return NULL; }

    skip_ws(ps);

    if (*ps->p != ':') { ps->error = 1; free(key); json_free(obj); return NULL; }

    ps->p++;                             // consume ':'
    JsonValue *val = parse_value(ps);

    if (val == NULL) { free(key); json_free(obj); return NULL; }

    val->key = key;

    if (tail == NULL) { obj->child = val; } else { tail->next = val; }

    tail = val;
    skip_ws(ps);

    if (*ps->p == ',') { ps->p++; continue; }

    if (*ps->p == '}') { ps->p++; break; }

    ps->error = 1;
    json_free(obj);
    return NULL;
  }

  return obj;
}

static JsonValue *parse_value(JsonParser *ps) {
  skip_ws(ps);

  switch (*ps->p) {
  case '{': return parse_object(ps);
  case '[': return parse_array(ps);
  case '"': return parse_string(ps);
  case 't':
  case 'f':
  case 'n': return parse_literal(ps);
  case '\0':
    ps->error = 1;
    return NULL;
  default:
    return parse_number(ps);
  }
}

JsonValue *json_parse(const char *text) {
  if (text == NULL) { return NULL; }

  JsonParser ps;
  ps.p = text;
  ps.error = 0;
  JsonValue *root = parse_value(&ps);

  if (root == NULL || ps.error) {
    json_free(root);
    return NULL;
  }

  skip_ws(&ps);

  //
  // Trailing garbage after the top-level value is a parse error.
  //
  if (*ps.p != '\0') {
    json_free(root);
    return NULL;
  }

  return root;
}

JsonValue *json_parse_file(const char *path) {
  FILE *f = fopen(path, "rb");

  if (f == NULL) { return NULL; }

  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }

  long size = ftell(f);

  if (size < 0) { fclose(f); return NULL; }

  rewind(f);
  char *text = (char *) malloc((size_t) size + 1);

  if (text == NULL) { fclose(f); return NULL; }

  size_t nread = fread(text, 1, (size_t) size, f);
  fclose(f);
  text[nread] = '\0';
  JsonValue *root = json_parse(text);
  free(text);
  return root;
}

JsonValue *json_object_get(const JsonValue *object, const char *key) {
  if (object == NULL || object->type != JSON_OBJECT || key == NULL) { return NULL; }

  for (JsonValue *m = object->child; m != NULL; m = m->next) {
    if (m->key != NULL && strcmp(m->key, key) == 0) { return m; }
  }

  return NULL;
}

int json_array_count(const JsonValue *array) {
  if (array == NULL || array->type != JSON_ARRAY) { return 0; }

  int n = 0;

  for (JsonValue *e = array->child; e != NULL; e = e->next) { n++; }

  return n;
}

JsonValue *json_array_get(const JsonValue *array, int index) {
  if (array == NULL || array->type != JSON_ARRAY || index < 0) { return NULL; }

  int i = 0;

  for (JsonValue *e = array->child; e != NULL; e = e->next, i++) {
    if (i == index) { return e; }
  }

  return NULL;
}

const char *json_as_string(const JsonValue *value, const char *fallback) {
  if (value != NULL && value->type == JSON_STRING) { return value->string; }

  return fallback;
}

double json_as_number(const JsonValue *value, double fallback) {
  if (value != NULL && value->type == JSON_NUMBER) { return value->number; }

  return fallback;
}

int json_as_bool(const JsonValue *value, int fallback) {
  if (value != NULL && value->type == JSON_BOOL) { return value->boolean; }

  return fallback;
}
