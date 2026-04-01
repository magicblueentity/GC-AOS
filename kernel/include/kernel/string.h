/*
 * GC-AOS Kernel - String Operations
 * 
 * Minimal kernel-safe string functions
 */

#ifndef _KERNEL_STRING_H
#define _KERNEL_STRING_H

#include "types.h"

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
char *strncpy(char *dest, const char *src, size_t n);

/**
 * snprintf - Format string with size limit
 * @buf: Buffer to write to
 * @size: Buffer size
 * @fmt: Format string
 * @...: Format arguments
 * 
 * Return: Number of characters written (excluding null terminator)
 */
int snprintf(char *buf, size_t size, const char *fmt, ...);

#endif /* _KERNEL_STRING_H */
