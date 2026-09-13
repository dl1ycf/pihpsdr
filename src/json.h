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

/*
 * Small, self-contained JSON reader used for the piHPSDR runtime
 * configuration files (colour themes, VFO panel layout, band plan).
 *
 * It is a strict, standards-compliant JSON parser (RFC 8259) supporting
 * objects, arrays, strings (including \uXXXX escapes and surrogate pairs),
 * numbers, booleans and null. It is intentionally minimal: enough to read
 * hand-edited configuration files, with tolerant accessors that always fall
 * back to a caller-supplied default when a key is missing or has the wrong
 * type.
 */

#ifndef _JSON_H_
#define _JSON_H_

#include <stddef.h>

typedef enum {
  JSON_NULL,
  JSON_BOOL,
  JSON_NUMBER,
  JSON_STRING,
  JSON_ARRAY,
  JSON_OBJECT
} JsonType;

typedef struct JsonValue {
  JsonType type;
  char *key;                 // member key when this value is part of an object, else NULL
  int boolean;               // valid when type == JSON_BOOL
  double number;             // valid when type == JSON_NUMBER
  char *string;              // valid when type == JSON_STRING
  struct JsonValue *child;   // first child (array elements or object members)
  struct JsonValue *next;    // next sibling in the parent array/object
} JsonValue;

//
// Parse a NUL-terminated JSON text (or a whole file). Returns a newly
// allocated tree that the caller must release with json_free(), or NULL on
// a syntax error / I/O error.
//
JsonValue *json_parse(const char *text);
JsonValue *json_parse_file(const char *path);
void       json_free(JsonValue *value);

//
// Navigation helpers. All are NULL-safe.
//
JsonValue *json_object_get(const JsonValue *object, const char *key);
int        json_array_count(const JsonValue *array);
JsonValue *json_array_get(const JsonValue *array, int index);

//
// Tolerant value accessors: return the requested value if present and of the
// expected type, otherwise the supplied fallback.
//
const char *json_as_string(const JsonValue *value, const char *fallback);
double      json_as_number(const JsonValue *value, double fallback);
int         json_as_bool(const JsonValue *value, int fallback);

//
// Iterate over the elements of an array or the members of an object.
//
#define JSON_FOREACH(elem, parent) \
  for ((elem) = ((parent) != NULL ? (parent)->child : NULL); \
       (elem) != NULL; \
       (elem) = (elem)->next)

#endif
