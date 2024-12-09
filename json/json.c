#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "json.h"

uint64_t json_hash64(JsonString s)
{
	uint64_t h = 0x100;
	for(size_t i = 0; i < s.length; i++)
	{
		h ^= s.data[i];
		h *= 1111111111111111111u;
	}
	return h ^ h >> 32;
}
static JsonString json_string_dup(JsonString str, JsonAllocatorFn allocator)
{
	JsonString dup = str;
	dup.data = allocator(str.length + 1);
	memcpy(dup.data, str.data, str.length);
	dup.data[str.length] = 0;
	return dup;
}
static bool json_string_cmp(JsonString a, JsonString b)
{
	// printf("a=%.*s,b=%.*s, a:%x,b:%x\n", a.length, a.data, b.length, b.data, json_hash64(a), json_hash64(b));
	return a.length == b.length && !memcmp(a.data, b.data, a.length);
}
JsonObjectEntry *json_object_upsert(JsonObject *map, JsonString key, JsonAllocatorFn allocator)
{
    JsonObjectEntry **m = &map->head;
	for(uint64_t h = json_hash64(key);; h <<= 2)
	{
		if(!*m)
		{
			if(!allocator)
			{
				return NULL;
			}
			JsonObjectEntry *obj = (JsonObjectEntry *)allocator(sizeof(JsonObjectEntry));
			memset(obj, 0, sizeof(JsonObjectEntry));
			// obj->key = json_strdup(key, allocator);
			obj->key = key;
			*m = obj;
			*map->tail = obj;
			map->tail = &obj->next;
			map->size++;
			return obj;
		}
		if(json_string_cmp(key, (*m)->key))
		{
			return *m;
		}
		m = &(*m)->child[h >> 62];
	}
	return NULL;
}

#include <pdjson/pdjson.h>

void json_value_print(JsonValue v)
{
	switch(v.type)
	{
		case JSON_NULL: printf("null"); break;
		case JSON_STRING: printf("%.*s", v.string.length, v.string.data); break;
		case JSON_NUMBER: printf("%f", v.u.number); break;
		case JSON_TRUE:
		case JSON_FALSE: printf("%d", v.u.boolean); break;
		case JSON_OBJECT: printf("{/*object*/}"); break;
		case JSON_ARRAY:
			printf("[/*array*/]");
			break;
		// case JSON_OBJECT:
		// {
		// 	for(JsonObjectEntry *it = v.u.object.head; it; it = it->next)
		// 	{
		// 		printf("%.*s=", it->key.length, it->key.data);
		// 		print_(it->value);
		// 	}
		// }
		// break;
	}
	// printf("\n");
}
JsonValue json_object_get(JsonValue jv, const char *path)
{
	if(jv.type != JSON_OBJECT)
		return (JsonValue) { .type = JSON_NULL };
	JsonObject *ob = &jv.u.object;
	JsonString str;
	char *sep = strchr(path, '.');
	if(sep)
	{
		str.data = (char *)path;
		str.length = sep - path;
	} else
	{
		str = json_string((char*)path);
	}
	// for(JsonObjectEntry *it = ob->head; it; it = it->next)
	// {
	// 	printf("%.*s %x\n", it->key.length, it->key.data, json_hash64(it->key));
	// }
	// printf("str = %.*s\n", str.length, str.data);
	JsonObjectEntry *entry = json_object_upsert(ob, str, NULL);
	if(!entry)
		return (JsonValue) { .type = JSON_NULL };
	if(sep && entry->value.type == JSON_OBJECT)
	{
		return json_object_get(entry->value, sep + 1);
	}
	return entry->value;
}

static JsonValue parse_json_value(json_stream *json, JsonAllocatorFn allocator)
{
    enum json_type type = json_next(json);
	JsonValue val = { .type = type };
	JsonString strval;
	strval.data = (char*)json_get_string(json, &strval.length);
	if(strval.length > 0)
		strval.length--;
	val.string = json_string_dup(strval, allocator);
	switch(type)
    {
		case JSON_NUMBER: val.u.number = atof(strval.data); break;
		case JSON_FALSE:
		case JSON_TRUE: val.u.boolean = type == JSON_TRUE; break;
		case JSON_OBJECT:
		{
			val.u.object.tail = &val.u.object.head;
			while(json_peek(json) != JSON_OBJECT_END && !json_get_error(json))
			{
				json_next(json);
				JsonString key;
				key.data = (char*)json_get_string(json, &key.length);
				if(key.length > 0)
					key.length--;
				key = json_string_dup(key, allocator);
				JsonValue v = parse_json_value(json, allocator);
				json_object_upsert(&val.u.object, key, allocator)->value = v;
			}
			json_next(json);
		}
		break;
		case JSON_ARRAY:
		{
			val.u.object.tail = &val.u.object.head;
			int index = 0;
			while(json_peek(json) != JSON_ARRAY_END && !json_get_error(json))
			{
			    JsonValue v = parse_json_value(json, allocator);
                char array_key[256];
				snprintf(array_key, sizeof(array_key), "%d", index++);
				json_object_upsert(&val.u.object, json_string_dup(json_string(array_key), allocator), allocator)->value = v;
			}
			json_next(json);
		}
		break;
	}
    return val;
}

JsonValue parse_json(const char *jstr, JsonAllocatorFn allocator)
{
	json_stream json;
	json_open_string(&json, jstr);
	json_set_streaming(&json, false);
	JsonValue val = parse_json_value(&json, allocator);
	json_close(&json);
    return val;
}