/* lib/string.c — implementacoes freestanding (sem libc) */
#include <stddef.h>
#include <stdint.h>
#include "string.h"

/* funcoes de memoria e string freestanding */

void *memset(void *dst, int c, size_t n) {
  unsigned char *d = dst;
  for (size_t i = 0; i < n; i++)
    d[i] = (unsigned char)c;
  return dst;
}
void *memcpy(void *dst, const void *src, size_t n) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  for (size_t i = 0; i < n; i++)
    d[i] = s[i];
  return dst;
}
void *memmove(void *dst, const void *src, size_t n) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  if (d < s)
    for (size_t i = 0; i < n; i++)
      d[i] = s[i];
  else
    for (size_t i = n; i > 0; i--)
      d[i - 1] = s[i - 1];
  return dst;
}
int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *x = a, *y = b;
  for (size_t i = 0; i < n; i++)
    if (x[i] != y[i])
      return x[i] - y[i];
  return 0;
}

int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) { a++; b++; }
  return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (a[i] != b[i])
      return (unsigned char)a[i] - (unsigned char)b[i];
    if (a[i] == 0) break;
  }
  return 0;
}

char *strcpy(char *dst, const char *src) {
  char *d = dst;
  while ((*d++ = *src++));
  return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
  size_t i;
  for (i = 0; i < n && src[i]; i++)
    dst[i] = src[i];
  for (; i < n; i++)
    dst[i] = 0;
  return dst;
}

size_t strlen(const char *s) {
  size_t n = 0;
  while (s[n]) n++;
  return n;
}

char *strtok(char *str, const char *delim) {
  static char *g_saved = NULL;
  if (str) g_saved = str;
  if (!g_saved) return NULL;

  while (*g_saved) {
    int sep = 0;
    for (const char *d = delim; *d; d++) {
      if (*g_saved == *d) { sep = 1; break; }
    }
    if (!sep) break;
    g_saved++;
  }
  if (!*g_saved) { g_saved = NULL; return NULL; }

  char *start = g_saved;
  while (*g_saved) {
    int sep = 0;
    for (const char *d = delim; *d; d++) {
      if (*g_saved == *d) { sep = 1; break; }
    }
    if (sep) {
      *g_saved = '\0';
      g_saved++;
      return start;
    }
    g_saved++;
  }
  g_saved = NULL;
  return start;
}