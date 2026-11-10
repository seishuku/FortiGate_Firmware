#ifndef _HEAP_H_
#define _HEAP_H_

#include <stddef.h>

int   heap_init(void);
void *heap_alloc(size_t size);
void  heap_free(void *ptr);

size_t heap_available(void);
size_t heap_largest_free(void);

#endif
