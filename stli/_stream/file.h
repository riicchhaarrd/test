#pragma once

#include "stream.h"
#include <string.h>
#include <stdio.h>

typedef struct
{
    char path[256];
	FILE *fp;
} StreamFile;

static size_t stream_read_file_(struct Stream_s *stream, void *ptr, size_t size, size_t nmemb)
{
	StreamFile *sd = (StreamFile *)stream->ctx;
	return fread(ptr, size, nmemb, sd->fp);
}

static size_t stream_write_file_(struct Stream_s *stream, const void *ptr, size_t size, size_t nmemb)
{
	StreamFile *sd = (StreamFile *)stream->ctx;
    return fwrite(ptr, size, nmemb, sd->fp);
}

static int stream_eof_file_(struct Stream_s *stream)
{
	StreamFile *sd = (StreamFile *)stream->ctx;
    return feof(sd->fp);
}

static int stream_name_file_(struct Stream_s *s, char *buffer, size_t size)
{
	StreamFile *sd = (StreamFile *)s->ctx;
    snprintf(buffer, size, "%s", sd->path);
	return 0;
}

static int64_t stream_tell_file_(struct Stream_s *s)
{
	StreamFile *sd = (StreamFile *)s->ctx;
    return ftell(sd->fp);
}

static int stream_seek_file_(struct Stream_s *s, int64_t offset, int whence)
{
	StreamFile *sd = (StreamFile *)s->ctx;
	switch(whence)
	{
		case STREAM_SEEK_BEG:
		{
            return fseek(sd->fp, offset, SEEK_SET);
		}
		break;
		case STREAM_SEEK_CUR:
		{
            return fseek(sd->fp, offset, SEEK_CUR);
		}
		break;
		case STREAM_SEEK_END:
		{
            return fseek(sd->fp, offset, SEEK_END);
		}
		break;
	}
	return 0;
}

static int stream_open_file(Stream *s, const char *path, const char *mode)
{
    FILE *fp = fopen(path, mode);
    if(!fp)
        return 1;
    StreamFile *sf = malloc(sizeof(StreamFile));
    sf->fp = fp;
    snprintf(sf->path, sizeof(sf->path), "%s", path);
	s->ctx = sf;
	s->read = stream_read_file_;
	s->write = stream_write_file_;
	s->eof = stream_eof_file_;
	s->name = stream_name_file_;
	s->tell = stream_tell_file_;
	s->seek = stream_seek_file_;
    return 0;
}

static int stream_close_file(Stream *s)
{
    if(!s->ctx)
    {
        return 1;
    }
    StreamFile *sf = s->ctx;
    fclose(sf->fp);
    free(sf);
    s->ctx = NULL;
    return 0;
}
