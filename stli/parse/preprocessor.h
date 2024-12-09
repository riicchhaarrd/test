#pragma once
#include <stli/buf.h>
#include <stli/hash_table.h>
#include <stli/stream.h>

typedef struct
{
    HashTable definitions;
    bool write_output;
} Preprocessor;

typedef struct
{
    const char *name;
	void (*fn)(Preprocessor*, Lexer*, Stream*, Token *);
} Directive;

typedef struct
{
    unsigned char *data;
    size_t length;
} Buffer;

typedef struct
{
    AssetHandle *dependencies;
    // Ping-pong buffer
    Buffer buffers[2];
    size_t buffer_index;
} Parser;

static void directive_define(Preprocessor *proc, Lexer *l, Stream *out, Token *prev_token);
static void directive_include(Preprocessor *proc, Lexer *l, Stream *out, Token *prev_token);
static void directive_undef(Preprocessor *proc, Lexer *l, Stream *out, Token *prev_token);

static const Directive directives[] = {
    {"define", directive_define},
    {"include", directive_include},
    {"undef", directive_undef},
    {NULL, 0}
};

static void directive_undef(Preprocessor *proc, Lexer *l, Stream *out, Token *prev_token)
{
    char key[256] = {0};
    Token t;
    lexer_expect(l, TOKEN_TYPE_IDENTIFIER, &t);
    lexer_token_read_string(l, &t, key, sizeof(key));
    hash_table_find(&proc->definitions, key);
    
    HashTableEntry *entry = hash_table_insert(&proc->definitions, key);

    if(entry->value)
    {
        free(entry->value);
        entry->value = NULL;
    }

}
static void directive_include(Preprocessor *proc, Lexer *l, Stream *out, Token *prev_token)
{
    char path[256] = {0};
    Token t;
    lexer_expect(l, TOKEN_TYPE_STRING, &t);
    lexer_token_read_string(l, &t, path, sizeof(path));

    Asset *dep = asset_find_entry(path);
    if(!dep)
        lexer_error(l, "Cannot find include path '%s'", path);
    RawFile *rf = asset_data(dep->handle);
    // stream_printf(out, "%.*s", rf->size, rf->buffer);
    // stream_print(out, "\n// ========================================================================================= BEFORE INCLUDE\n");
    stream_print(out, rf->buffer);
    // stream_print(out, "\n// ========================================================================================= AFTER INCLUDE\n");
}
static void directive_define(Preprocessor *proc, Lexer *l, Stream *out, Token *prev_token)
{
    char key[256] = {0};
    Token t;
    lexer_expect(l, TOKEN_TYPE_IDENTIFIER, &t);
    lexer_token_read_string(l, &t, key, sizeof(key));

    s64 beg = l->stream->tell(l->stream);
    u8 ch = 0;
    while(1)
    {
        if(0 == l->stream->read(l->stream, &ch, 1, 1))
        {
            lexer_error(l, "Unexpected EOF");
            return;
        }
		if(!ch)
			break;
		if(ch == '\r' || ch == '\n')
            break;
    }
    s64 nchars = l->stream->tell(l->stream) - beg - 1;
    char *str = malloc(nchars + 1);
    l->stream->seek(l->stream, beg, SEEK_SET);
    if(nchars > 0)
    {
        l->stream->read(l->stream, str, 1, nchars);
    }
    str[nchars] = 0;
    HashTableEntry *entry = hash_table_insert(&proc->definitions, key);
    if(entry->value)
    {
        free(entry->value);
    }
    entry->value = str;
}

static bool preprocess_parse_dependencies(Parser *parser, Asset *asset, unsigned char *buffer, size_t length, size_t *numincludes)
{
    Stream s = {0};
    StreamBuffer sb = {0};
    init_stream_from_buffer(&s, &sb, buffer, length);
    Lexer l = {0};
	l.flags |= LEXER_FLAG_SKIP_COMMENTS;
    lexer_init(&l, NULL, &s);
    if(setjmp(l.jmp_error))
    {
        return false;
    }
    // First pass
    // Get all the (potentially) included files and add them as dependencies and wait for them to be loaded.
    // Even if it's ifdef'd, still consider it a possibility and load it just in case.
    *numincludes = 0;
    while(1)
    {
        Token t;
        if(lexer_step(&l, &t))
			break;
        char ident[256] = {0};
        switch(t.token_type)
        {
            case '#':
            lexer_expect(&l, TOKEN_TYPE_IDENTIFIER, &t);
            lexer_token_read_string(&l, &t, ident, sizeof(ident));
            if(!strcmp(ident, "include"))
            {
                char path[256] = {0};
                lexer_expect(&l, TOKEN_TYPE_STRING, &t);
                lexer_token_read_string(&l, &t, path, sizeof(path));
                Asset *dep = asset_find_entry(path);
                bool duplicate = false;
                if(dep)
                {
                    for(size_t k = 0; k < buf_size(parser->dependencies); ++k)
                    {
                        if(parser->dependencies[k] == dep->handle)
                        {
                            duplicate = true;
                            break;
                        }
                    }
                }
                if(!duplicate)
                {
                    Asset *dep = asset_add_dependency(asset->handle, path, "raw", asset_manager_fsm_raw_file, NULL);
                    buf_push(parser->dependencies, dep->handle);
                }
                *numincludes += 1;
            }
            break;
        }
    }
    return true;
}

static const Directive *directive_by_name(const char *name)
{
    for(size_t i = 0; directives[i].name; ++i)
    {
        const Directive *d = &directives[i];
        if(!strcmp(d->name, name))
            return d;
    }
    return NULL;
}

static bool directive_enabled(const char **enabled, const char *name)
{
    for(size_t i = 0; enabled[i]; ++i)
    {
        if(!strcmp(enabled[i], name))
            return true;
    }
    return false;
}

static bool preprocess(Preprocessor *pre, Stream *in, Stream *out, size_t *numdirectives, const char **enabled_directives)
{
    *numdirectives = 0;

	Lexer l = {0};
    lexer_init(&l, NULL, in);
	// l.flags |= k_ELexerFlagSkipComments;
    if(setjmp(l.jmp_error))
    {
        return false;
    }
    while(1)
    {
        Token t;
        s64 beg = in->tell(in);
        if(lexer_step(&l, &t))
			break;
        s64 cur = in->tell(in);
        char ident[256] = {0};
		bool write = pre->write_output;
		switch(t.token_type)
        {
			case TOKEN_TYPE_COMMENT: write = false; break;
			case TOKEN_TYPE_IDENTIFIER:
            {
				lexer_token_read_string(&l, &t, ident, sizeof(ident));
                HashTableEntry *entry = hash_table_find(&pre->definitions, ident);
                if(entry && entry->value)
                {
                    stream_print(out, entry->value);
                    *numdirectives += 1;
                    write = false;
                }
            } break;
            
            case '#':
            if(!lexer_accept(&l, TOKEN_TYPE_IDENTIFIER, &t))
            {
				lexer_token_read_string(&l, &t, ident, sizeof(ident));
                const Directive *d = directive_by_name(ident);
                if(d && directive_enabled(enabled_directives, ident))
                {
                    d->fn(pre, &l, out, &t);
                    *numdirectives += 1;
                    write = false;
                } else 
                {
                    lexer_unget_token(&l, &t);
                }
			}
            break;
        }
        if(write)
        {
            char tmp[4096];

            s64 save = in->tell(in);
            in->seek(in, beg, SEEK_SET);
			s64 n = cur - beg;
            if(n >= sizeof(tmp))
            {
                printf("n(%d) >= sizeof(tmp)\n", n);
                return false;
            }
            in->read(in, tmp, 1, n);

            out->write(out, tmp, 1, n);
            u8 zero = 0;
            out->write(out, &zero, 1, 1);
            stream_unget(out);

            in->seek(in, save, SEEK_SET);
        }
    }
    return true;
}