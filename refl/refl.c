// gcc parse.c -I~/git/project/stli/include && ./a.out ~/git/project/application/server/src/map.c
#include <stli/arena.h>
#include <stli/stream.h>
#include <stli/parse/lexer.h>
#include <stli/util.h>
#include <stli/hash_trie.h>
#include <stli/hash.h>
#include <assert.h>
#include <stdarg.h>
#include <inttypes.h>

typedef struct
{
	char string[1024];
	Lexer *lexer;
} Parser;

enum
{
	VALUE_INVALID,
	VALUE_INTEGER,
	VALUE_FLOAT,
	// VALUE_DECIMAL,
	VALUE_IDENTIFIER,
	VALUE_STRING
};

typedef struct
{
	int type;
	union
	{
		char *text;
		int integer;
		float value;
	} u;
} Value;

typedef struct
{
	char *type;
	char *name;
	int flags;
	int ptr;
	Value size;
	// int size; // >0 for fixed-size arrays, -1 for flexible array members, 0 if not an array
	int bits; // >0 bit-field
} Field;

typedef struct
{
	char *name;
	HashTrie fields;
} Struct;

static int accept(Parser *parser, int which, const char **strval)
{
	Lexer *l = parser->lexer;
	if(strval)
		*strval = NULL;
	Token t;
	if(which == -1)
	{
		if(lexer_step(l, &t))
			return -1;
	}
	else
	{
		if(lexer_accept(l, which, &t))
			return -1;
	}
	lexer_token_read_string(l, &t, parser->string, sizeof(parser->string));
	if(strval)
		*strval = parser->string;
	return t.token_type;
}
static void expect(Parser *parser, int which, const char **strval, char *errmsg)
{
	Lexer *l = parser->lexer;
	if(-1 == accept(parser, which, strval))
	{
		fprintf(stderr, "ERROR: %s\n", errmsg);
		longjmp(l->jmp_error, 1);
	}
}
#include <malloc.h>
static void *allocator_malloc_(void *ctx, size_t size)
{
	return malloc(size);
}
static void allocator_free_(void *ctx, void *ptr)
{
	free(ptr);
}
static Allocator malloc_allocator = { allocator_malloc_, allocator_free_, 0 };
Value value(Parser *parser)
{
	Value v = { 0 };
	v.type = VALUE_INVALID;
	Token t;
	if(lexer_step(parser->lexer, &t))
		return v;
	lexer_token_read_string(parser->lexer, &t, parser->string, sizeof(parser->string));

	switch(t.token_type)
	{
		case TOKEN_TYPE_IDENTIFIER:
			v.type = VALUE_IDENTIFIER;
			v.u.text = strdup(parser->string);
			break;
		case TOKEN_TYPE_STRING:
			v.type = VALUE_STRING;
			v.u.text = strdup(parser->string);
			break;
		case TOKEN_TYPE_NUMBER:
			v.type = t.flags & TOKEN_FLAG_DECIMAL_POINT ? VALUE_FLOAT : VALUE_INTEGER;
			if(v.type == VALUE_INTEGER)
			{
				v.u.integer = atoi(parser->string);
			}
			else
			{
				v.u.value = atof(parser->string);
			}
			break;
	}
	return v;
}
static void value_string(Value *v, char *str, int n)
{
	switch(v->type)
	{
		case VALUE_FLOAT: snprintf(str, n, "%f", v->u.value); break;
		case VALUE_INTEGER: snprintf(str, n, "%d", v->u.integer); break;
		case VALUE_STRING:
		case VALUE_IDENTIFIER: snprintf(str, n, "%s", v->u.text); break;
		default: snprintf(str, n, "?"); break;
	}
}
static void parse_array(Parser *parser, Field *f)
{
	const char *str;
	if(accept(parser, '[', NULL) > 0)
	{
		f->size = value(parser);
		// if(accept(parser, TOKEN_TYPE_NUMBER, &str) > 0)
		// {
		// 	f->size = atoi(str);
		// }
		// else
		// {
		// 	f->size = -1;
		// }
		expect(parser, ']', NULL, "Expected ]");
	}
}

// TODO: if parsing fails for a struct just skip to the next } and try to parse next struct
static void parse_fields(Parser *parser, HashTrie *fields)
{
	hash_trie_init(fields);
	const char *str;
	while(1)
	{
		Field *f = calloc(1, sizeof(Field));
		int result = accept(parser, -1, &str);
		if(result == '}')
			break;
		if(result != TOKEN_TYPE_IDENTIFIER)
		{
			fprintf(stderr, "ERROR: Expected identifier\n");
			longjmp(parser->lexer->jmp_error, 1);
		}
		// expect(parser, TOKEN_TYPE_IDENTIFIER, &str, "Expected type for field");
		if(!strcmp(str, "union") || !strcmp(str, "struct")) // Skip to first }
		{
			free(f);
			while(accept(parser, -1, NULL) != '}')
				;
			while(accept(parser, -1, NULL) != ';')
				;
			continue;
		}
		f->type = strdup(str);
		while(accept(parser, '*', NULL) > 0)
		{
			f->ptr++;
		}
		if(accept(parser, '(', NULL) > 0)
		{
			f->type = "()";
			f->ptr = 1;
			while(accept(parser, -1, &str) != '*')
				;
			expect(parser, TOKEN_TYPE_IDENTIFIER, &str, "Expected name for function pointer");
			f->name = strdup(str);
			parse_array(parser, f);
			while(accept(parser, -1, &str) != ')')
				;
		}
		else
		{
			result = accept(parser, -1, &str);
			if(result <= 0 || result == '}')
				break;
			f->name = strdup(str);
			// printf("%s %s\n", f.type.name, f.name);
			parse_array(parser, f);
		}
		while(accept(parser, -1, &str) != ';')
			;
		// printf("Field: %s %s [%d]\n", f->type, f->name, f->size);
		hash_trie_upsert(fields, f->name, &malloc_allocator, true)->value = f;
	}
}

static void parse(Parser *parser, HashTrie *header_exports, HashTrie *structs)
{
	hash_trie_init(structs);
	hash_trie_init(header_exports);
	Lexer *l = parser->lexer;
	const char *id;
	while(1)
	{
		int type = accept(parser, -1, &id);
		if(type <= 0)
		{
			break;
		}

		if(type == TOKEN_TYPE_IDENTIFIER)
		{
			if(!strcmp(id, "typedef"))
			{
				expect(parser, TOKEN_TYPE_IDENTIFIER, &id, "Expected identifier after typedef");
				if(!strcmp(id, "struct"))
				{
					Struct *s = calloc(1, sizeof(Struct));
					if(!lexer_accept(l, '{', NULL))
					{
						parse_fields(parser, &s->fields);
					}
					expect(parser, TOKEN_TYPE_IDENTIFIER, &id, "Expected struct name");
					s->name = strdup(id);
					hash_trie_upsert(structs, s->name, &malloc_allocator, false)->value = s;
				}
			}
			else if(!strcmp(id, "struct"))
			{
				Struct *s = calloc(1, sizeof(Struct));
				expect(parser, TOKEN_TYPE_IDENTIFIER, &id, "Expected struct type name");
				s->name = strdup(id);
				if(!lexer_accept(l, '{', NULL))
				{
					parse_fields(parser, &s->fields);
				}
				hash_trie_upsert(structs, s->name, &malloc_allocator, false)->value = s;
			}
		} else if(type == TOKEN_TYPE_STRING)
		{
			char *ptr = strstr(id, ".refl.h");
			if(ptr)
			{
				*ptr = 0;
				printf("id:%s\n", id);
				hash_trie_upsert(header_exports, id, &malloc_allocator, false);
			}
		}
	}
}
int main(int argc, char **argv)
{
	assert(argc > 1);
	const char *filename = argv[1];
	// char *source;
	// Arena arena;
	// int bufsz = (32 * 1024 * 1024);
	// char *buf = malloc(bufsz);
	// arena_init(&arena, buf, bufsz);
	// if(read_text_file(filename, &arena, &source))
	// 	return 1;
	Stream stream = { 0 };
	if(stream_open_file(&stream, filename, "r"))
		return 1;
	Lexer lexer = { 0 };
	Parser parser = { 0 };
	parser.lexer = &lexer;
	lexer_init(&lexer, NULL, &stream);
	lexer.flags |= LEXER_FLAG_SKIP_COMMENTS;
	if(setjmp(lexer.jmp_error))
	{
		fprintf(stderr, "Failed to parse '%s'\n", filename);
		return 1;
	}
	HashTrie header_exports;
	HashTrie structs;
	parse(&parser, &header_exports, &structs);
	HashTrie types;
	hash_trie_init(&types);
	for(HashTrieNode *it = structs.head; it; it = it->next)
	{
		Struct *s = it->value;
		printf("%s\n", s->name);
		for(HashTrieNode *field_it = s->fields.head; field_it; field_it = field_it->next)
		{
			Field *f = field_it->value;
			value_string(&f->size, parser.string, sizeof(parser.string));
			printf("\t%s %s %s\n", f->name, f->type, parser.string);
			hash_trie_upsert(&types, f->type, &malloc_allocator, false);
		}
	}
	char directory[512];
	char basename[512];
	char extension[64];
	char sep;
	pathinfo(filename, directory, sizeof(directory), basename, sizeof(basename), extension, sizeof(extension), &sep);
	char header_path[512];
	for(HashTrieNode *it = header_exports.head; it; it = it->next)
	{
		HashTrieNode *n = hash_trie_upsert(&structs, it->key, NULL, false);
		if(!n)
			continue;
		Struct *s = n->value;
		snprintf(header_path, sizeof(header_path), "%s%c%s.refl.h", directory, sep, it->key);
		FILE *hdr = fopen(header_path, "w");
		if(!hdr)
			continue;
fprintf(hdr, "#pragma once\n");
fprintf(hdr, "#include <stddef.h>\n");
fprintf(hdr, "#include <string.h>\n");
fprintf(hdr, "#include <stdbool.h>\n");
fprintf(hdr, "#include <stdint.h>\n");
fprintf(hdr, "#include <inttypes.h>\n");
fprintf(hdr, "#ifndef REFL_TYPES\n");
fprintf(hdr, "#define REFL_TYPES\n");
fprintf(hdr, "\n");
fprintf(hdr, "#define REFL_INTEGER_STRING(INTTYPE)                                     \\\n");
fprintf(hdr, "	static void refl_##INTTYPE##_to_string(void *ptr, char *str, int n)  \\\n");
fprintf(hdr, "	{                                                                    \\\n");
fprintf(hdr, "		snprintf(str, n, \"%%\" PRId64, (int64_t)(*(INTTYPE *)ptr));        \\\n");
fprintf(hdr, "	}                                                                    \\\n");
fprintf(hdr, "	static void refl_##INTTYPE##_from_string(void *ptr, const char *str) \\\n");
fprintf(hdr, "	{                                                                    \\\n");
fprintf(hdr, "		*(INTTYPE *)ptr = (INTTYPE)strtoll(str, NULL, 10);               \\\n");
fprintf(hdr, "	}\n");
fprintf(hdr, "REFL_INTEGER_STRING(char)\n");
fprintf(hdr, "REFL_INTEGER_STRING(short)\n");
fprintf(hdr, "REFL_INTEGER_STRING(int)\n");
fprintf(hdr, "REFL_INTEGER_STRING(long)\n");
fprintf(hdr, "REFL_INTEGER_STRING(intptr_t)\n");
fprintf(hdr, "REFL_INTEGER_STRING(uintptr_t)\n");
fprintf(hdr, "REFL_INTEGER_STRING(size_t)\n");
fprintf(hdr, "REFL_INTEGER_STRING(int32_t)\n");
fprintf(hdr, "REFL_INTEGER_STRING(int64_t)\n");
fprintf(hdr, "REFL_INTEGER_STRING(int16_t)\n");
fprintf(hdr, "REFL_INTEGER_STRING(int8_t)\n");
fprintf(hdr, "REFL_INTEGER_STRING(uint32_t)\n");
fprintf(hdr, "REFL_INTEGER_STRING(uint64_t)\n");
fprintf(hdr, "REFL_INTEGER_STRING(uint16_t)\n");
fprintf(hdr, "REFL_INTEGER_STRING(uint8_t)\n");
fprintf(hdr, "static void refl_float_to_string(void *ptr, char *str, int n)\n");
fprintf(hdr, "{\n");
fprintf(hdr, "	snprintf(str, n, \"%%f\", *(float *)ptr);\n");
fprintf(hdr, "}\n");
fprintf(hdr, "static void refl_float_from_string(void *ptr, const char *str)\n");
fprintf(hdr, "{\n");
fprintf(hdr, "	*(float *)ptr = atof(str);\n");
fprintf(hdr, "}\n");
fprintf(hdr, "static void refl_double_to_string(void *ptr, char *str, int n)\n");
fprintf(hdr, "{\n");
fprintf(hdr, "	snprintf(str, n, \"%%f\", *(double *)ptr);\n");
fprintf(hdr, "}\n");
fprintf(hdr, "static void refl_double_from_string(void *ptr, const char *str)\n");
fprintf(hdr, "{\n");
fprintf(hdr, "	*(double *)ptr = atof(str);\n");
fprintf(hdr, "}\n");

fprintf(hdr, "static void refl_bool_to_string(void *ptr, char *str, int n)\n");
fprintf(hdr, "{\n");
fprintf(hdr, "	snprintf(str, n, \"%%s\", *(bool *)ptr ? \"true\" : \"false\");\n");
fprintf(hdr, "}\n");
fprintf(hdr, "static void refl_bool_from_string(void *ptr, const char *str)\n");
fprintf(hdr, "{\n");
fprintf(hdr, "	*(bool *)ptr = !strcmp(str, \"true\");\n");
fprintf(hdr, "}\n");

fprintf(hdr, "typedef struct\n");
fprintf(hdr, "{\n");
fprintf(hdr, "	const char *name;\n");
fprintf(hdr, "	int offset;\n");
fprintf(hdr, "	const char *type;\n");
fprintf(hdr, "	void (*to_string)(void *ptr, char *str, int n);\n");
fprintf(hdr, "	void (*from_string)(void *ptr, const char *str);\n");
fprintf(hdr, "} ReflFieldInfo;\n");
fprintf(hdr, "typedef struct {\n");
fprintf(hdr, "	const char *name;\n");
fprintf(hdr, "	int size;\n");
fprintf(hdr, "	ReflFieldInfo *fields;\n");
fprintf(hdr, "	int field_count;\n");
fprintf(hdr, "} ReflTypeInfo;\n");
fprintf(hdr, "#define refl_field(TYPE, KEY) refl_field_(TYPE ## _refl_fields, KEY)\n");
fprintf(hdr, "static ReflFieldInfo *refl_field_(ReflFieldInfo *fields, const char *key)\n");
fprintf(hdr, "{\n");
fprintf(hdr, "	ReflFieldInfo *field = NULL;\n");
fprintf(hdr, "	for(int i = 0; fields[i].name; i++)\n");
fprintf(hdr, "	{\n");
fprintf(hdr, "		if(!strcmp(fields[i].name, key))\n");
fprintf(hdr, "		{\n");
fprintf(hdr, "			field = &fields[i];\n");
fprintf(hdr, "			break;\n");
fprintf(hdr, "		}\n");
fprintf(hdr, "	}\n");
fprintf(hdr, "	return field;\n");
fprintf(hdr, "}\n");
fprintf(hdr, "#endif\n");
		fprintf(hdr, "ReflFieldInfo %s_refl_fields[] = {\n", s->name);
		for(HashTrieNode *field_it = s->fields.head; field_it; field_it = field_it->next)
		{
			Field *f = field_it->value;
			if(f->ptr)
				continue;
			if(f->size.type != VALUE_INVALID)
			{
				if(f->size.type != VALUE_INTEGER || f->size.u.integer != 0)
					continue;
			}
			fprintf(hdr, "\t{\"%s\", offsetof(%s, %s), \"%s\", refl_%s_to_string, refl_%s_from_string},\n", f->name, s->name, f->name, f->type, f->type, f->type);
		}
		fprintf(hdr, "\t{NULL, 0, NULL}\n");
		fprintf(hdr, "};");
	}
	return 0;
}