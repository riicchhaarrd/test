
#include <pdjson/pdjson.h>
#include <setjmp.h>
#include <stdlib.h>

typedef struct
{
	jmp_buf *jmp;
	json_stream *json;
} JsonMapParser;

typedef struct
{
    int *data;
    int height;
    int id;
    char name[64];
    int opacity;
    char type[64];
    bool visible;
    int width;
    int x;
    int y;
} TMJLayer;
#include "tmjlayer.refl.h"

#define TMJ_MAX_LAYERS (16)
typedef struct
{
    int compressionlevel;
    int height;
    int width;
    bool infinite;
    TMJLayer layers[TMJ_MAX_LAYERS];
    int numlayers;
    // ...
} TMJ;
#include "tmj.refl.h"

static void advance(JsonMapParser *parser, enum json_type type)
{
	if(json_next(parser->json) != type)
		longjmp(*parser->jmp, 1);
}

static void parse_json2(JsonMapParser *parser)
{
	TMJ tmj = { 0 };
	advance(parser, JSON_OBJECT);
    while(1)
    {
        enum json_type type = json_next(parser->json);
		if(type == JSON_DONE || type == JSON_ERROR)
			break;
        if(type != JSON_STRING)
            continue;
        const char *key = json_get_string(parser->json, NULL);
        ReflFieldInfo *field = refl_field(TMJ, key);
        type = json_next(parser->json);
        const char *value = json_get_string(parser->json, NULL);
		switch(type)
		{
			case JSON_FALSE:
			case JSON_TRUE:
			case JSON_STRING:
			case JSON_NUMBER:
			case JSON_NULL:
			{
				if(field)
				{
					char *ptr = (char *)(&tmj) + field->offset;
					field->from_string(ptr, value);
					printf("field->name:%s,value:%s\n", field->name, value);
					// char strval[256];
					// field->to_string(ptr, strval, sizeof(strval));
					// printf("key: %s, field: %x = %s\n", key, field, strval);
				}
			}
			break;
		}
	}

	for(int i = 0; TMJ_refl_fields[i].name; i++)
	{
		ReflFieldInfo *field = &TMJ_refl_fields[i];
		char *ptr = (char *)(&tmj) + field->offset;
		char strval[256];
		field->to_string(ptr, strval, sizeof(strval));
		printf("%s=%s\n", field->name, strval);
	}
}
#include "json.h"

void *allocator_(size_t n)
{
	return malloc(n);
}
static void deserialize_json_object(JsonValue object, void *inst, ReflFieldInfo *fields)
{
	for(int i = 0; fields[i].name; i++)
	{
		ReflFieldInfo *field = &fields[i];
		JsonValue jv = json_object_get(object, field->name);
		if(jv.type == JSON_NULL)
			continue;
		char *ptr = (char *)(inst) + field->offset;
		field->from_string(ptr, jv.string.data);
	}
}
int parse_map(const char *jstr)
{
    JsonValue val = parse_json(jstr, allocator_);
	if(val.type != JSON_OBJECT)
		return 1;
	// json_value_print(val);

	TMJ tmj = { 0 };
	deserialize_json_object(val, &tmj, TMJ_refl_fields);

	// JsonValue width = json_object_get(&val.u.object, "width");
	// json_value_print(width);
	// printf("\n");

	// JsonValue layers = json_object_get(&val.u.object, "layers");
	// if(layers.type == JSON_ARRAY)
	// {
	// 	for(JsonObjectEntry *it = layers.u.object.head; it; it = it->next)
	// 	{
	// 		printf("%.*s %x\n", it->key.length, it->key.data, json_hash64(it->key));
	// 	}
	// }
	
	for(int i = 0; TMJ_refl_fields[i].name; i++)
	{
		ReflFieldInfo *field = &TMJ_refl_fields[i];
		char *ptr = (char *)(&tmj) + field->offset;
		char strval[256];
		field->to_string(ptr, strval, sizeof(strval));
		printf("%s=%s\n", field->name, strval);
	}
	JsonValue layers = json_object_get(val, "layers");
	if(layers.type != JSON_ARRAY)
	{
		return 1;
	}
	JSON_FOREACH(layers, layer)
	{
		TMJLayer *tmj_layer = &tmj.layers[tmj.numlayers++];
		if(tmj.numlayers >= TMJ_MAX_LAYERS)
			return 1;
		if(layer->value.type != JSON_OBJECT)
			return 1;
		deserialize_json_object(layer->value, tmj_layer, TMJLayer_refl_fields);
		int cap = tmj.width * tmj.height;
		int *map = malloc(cap * sizeof(int));
		int index = 0;
		JSON_FOREACH(json_object_get(layer->value, "data"), data)
		{
			if(data->value.type != JSON_NUMBER)
				return 1;
			if(index >= cap)
				return 1;
			int v = atoi(data->value.string.data);
			// printf("%d = %d\n", index, v);
			map[index++] = v;
		}
		printf("layer name: %s\n", tmj_layer->name);
	}
	JSON_FOREACH(json_object_get(val, "tilesets"), tileset)
	{
		printf("tileset: %s\n", json_object_get(tileset->value, "source").string.data);
	}

	// json_stream json;
	// json_open_string(&json, jstr);
	// json_set_streaming(&json, false);
    // jmp_buf jmp;
    // if(setjmp(jmp))
    // {
    //     json_close(&json);
    //     return 1;
    // }
	// JsonMapParser parser = { .jmp = &jmp, .json = &json };
    // parse_json(&parser);

	// if(json_get_error(&json))
	// {
	// 	// fprintf(stderr, "error: %zu: %s\n", json_get_lineno(&json), json_get_error(&json));
	// 	// exit(EXIT_FAILURE);
	// 	json_close(&json);
	// 	return 1;
	// }
	// json_close(&json);
	return 0;
}