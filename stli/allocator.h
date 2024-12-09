#pragma once
#include <stddef.h>

// https://nullprogram.com/blog/2023/12/17/

// typedef struct
// {
// 	void *ptr;
// 	ptrdiff_t size;
// } MemBlock;

typedef struct
{
	void *(*malloc)(void *ctx, size_t size);
	// Too lazy to pass size to free every time.
	void (*free)(void *ctx, void *ptr);

	// I prefer having two seperate functions
	// void *lua_Alloc(void *ctx, void *ptr, size_t old, size_t new);
	void *ctx;
} Allocator;

#ifdef ALLOCATOR_MALLOC_WRAPPER
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
#endif