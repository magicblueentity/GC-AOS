/*
 * GC-AOS Kernel - Page Management
 * 
 * Page flags and memory management utilities
 */

#ifndef _KERNEL_MM_PAGE_H
#define _KERNEL_MM_PAGE_H

#include "types.h"

/* Page protection flags */
#define PAGE_READONLY    (1 << 0)
#define PAGE_WRITE       (1 << 1)
#define PAGE_EXEC        (1 << 2)
#define PAGE_KERNEL      (PAGE_WRITE | PAGE_EXEC)

/* Page allocation flags */
#define PAGE_ALLOC_ZERO  (1 << 0)
#define PAGE_ALLOC_USER  (1 << 1)
#define PAGE_ALLOC_KERNEL (1 << 2)

#endif /* _KERNEL_MM_PAGE_H */
