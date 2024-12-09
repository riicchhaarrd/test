#pragma once
#include <stdbool.h>
#include <stddef.h>

// typedef enum
// {
// 	JSON_STRING,
// 	JSON_NUMBER,
// 	JSON_OBJECT,
// 	JSON_ARRAY,
// 	JSON_BOOLEAN,
// 	JSON_NULL
// } JsonType;
#define JSON_IS_ITERABLE(JV) ((JV).type == JSON_OBJECT || (JV).type == JSON_ARRAY)
#define JSON_FOREACH(JV, ITERATOR) \
	for(JsonObjectEntry *ITERATOR = (JV).u.object.head; JSON_IS_ITERABLE(JV) && ITERATOR; ITERATOR = (ITERATOR)->next)
typedef struct JsonString JsonString;
typedef struct JsonObjectEntry JsonObjectEntry;
typedef struct JsonValue JsonValue;

struct JsonString
{
	size_t length;
	char *data;
};

uint64_t json_hash64(JsonString s);
static JsonString json_string(char *s)
{
	return (JsonString) { .length = strlen(s), .data = s };
}

typedef struct
{
	JsonObjectEntry *head;
	JsonObjectEntry **tail;
	size_t size;
} JsonObject;

struct JsonValue
{
	// JsonType type;
    int type;
	JsonString string;
	union
	{
		float number;
		JsonObject object;
		bool boolean;
	} u;
};

struct JsonObjectEntry
{
	JsonObjectEntry *child[4];
	JsonString key;
	JsonValue value;
	JsonObjectEntry *next;
};

typedef void *(*JsonAllocatorFn)(size_t);
JsonValue parse_json(const char *jstr, JsonAllocatorFn allocator);
JsonObjectEntry *json_object_upsert(JsonObject *map, JsonString key, JsonAllocatorFn allocator);

void json_value_print(JsonValue v);
JsonValue json_object_get(JsonValue, const char *path);