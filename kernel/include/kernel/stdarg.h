/*
 * GC-AOS Kernel - Standard Argument Support
 * 
 * Kernel-safe stdarg.h replacement for variadic functions
 */

#ifndef _KERNEL_STDARG_H
#define _KERNEL_STDARG_H

#include "types.h"

/* Use builtin variadic support */
typedef __builtin_va_list va_list;

/* Variadic macros */
#define va_start(v,l) __builtin_va_start(v,l)
#define va_end(v)   __builtin_va_end(v)
#define va_arg(v,l)  __builtin_va_arg(v,l)

#endif /* _KERNEL_STDARG_H */
