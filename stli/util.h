#ifndef UTIL_H
#define UTIL_H
#include <stli/arena.h>
#include <stdio.h>
#include <ctype.h>

static int read_file(const char *path, Arena *arena, char **buffer_out, const char *mode)
{
	FILE *fp = fopen(path, mode);
	if(!fp)
		return 1;

	fseek(fp, 0, SEEK_END);
	int n = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	char *buffer = new(arena, char, n);
	fread(buffer, n, 1, fp);
	buffer[n] = 0;
	fclose(fp);
	*buffer_out = buffer;
	return 0;
}
static int read_text_file(const char *path, Arena *arena, char **buffer_out)
{
	return read_file(path, arena, buffer_out, "r");
}

unsigned int ticks();

// First field of structs in the sentinel array must be the name of type char*
static const void *sentinel_array_entry_by_name(const void *entries, const char *name)
{
    const char * const *entry = entries;

    while (*entry != NULL)
    {
        if (!strcmp(*entry, name))
        {
            return entry;
        }
        ++entry;
    }
    return NULL;
}

static void swap(void **a, void **b)
{
    void *tmp = *b;
    *b = *a;
    *a = tmp;
}

static void pathinfo(const char *path,
					 char *directory, size_t directory_max_length,
					 char *basename, size_t basename_max_length,
					 char *extension, size_t extension_max_length, char *sep)
{

	size_t offset = 0;
	const char *it = path;
	while(*it)
	{
		if(*it == '/' || *it == '\\')
		{
			if(sep)
				*sep = *it;
			offset = it - path;
		}
		++it;
	}
	directory[0] = 0;
	snprintf(directory, directory_max_length, "%.*s", offset, path);
	const char *filename = path + offset;
	
	if(*filename == '/' || *filename == '\\')
		++filename;

	char *delim = strrchr(filename, '.');
	basename[0] = 0;
	extension[0] = 0;
	if(!delim)
	{
		snprintf(basename, basename_max_length, "%s", filename);
	} else
	{
		snprintf(basename, basename_max_length, "%.*s", delim - filename, filename);
		snprintf(extension, extension_max_length, "%s", delim + 1);
	}
}

static void strtolower(char *str)
{
	for(char *p = str; *p; ++p)
		*p = tolower(*p);
}

static void strtoupper(char *str)
{
	for(char *p = str; *p; ++p)
		*p = toupper(*p);
}
#endif