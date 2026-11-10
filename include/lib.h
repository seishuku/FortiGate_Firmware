#ifndef __LIB_H__
#define __LIB_H__

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);

size_t strlen(const char *s);
void strcpy(char *dest, const char *src);

#endif
