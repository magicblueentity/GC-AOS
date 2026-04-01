/*
 * GC-AOS Kernel - Atomic Operations
 * 
 * Minimal atomic operations for SMP safety
 */

#ifndef _KERNEL_ATOMIC_H
#define _KERNEL_ATOMIC_H

#include "types.h"

/* ===================================================================== */
/* Atomic Types */
/* ===================================================================== */

typedef struct {
    volatile int counter;
} atomic_t;

/* ===================================================================== */
/* Atomic Operations */
/* ===================================================================== */

static inline void atomic_set(atomic_t *v, int i)
{
    v->counter = i;
}

static inline void atomic_add(atomic_t *v, int i)
{
    v->counter += i;
}

static inline void atomic_sub(atomic_t *v, int i)
{
    v->counter -= i;
}

static inline int atomic_read(const atomic_t *v)
{
    return v->counter;
}

static inline int atomic_add_return(atomic_t *v, int i)
{
    return __sync_add_and_fetch(&v->counter, i);
}

static inline int atomic_sub_return(atomic_t *v, int i)
{
    return __sync_sub_and_fetch(&v->counter, i);
}

static inline int atomic_inc_return(atomic_t *v)
{
    return __sync_add_and_fetch(&v->counter, 1);
}

static inline int atomic_dec_return(atomic_t *v)
{
    return __sync_sub_and_fetch(&v->counter, 1);
}

static inline void atomic_inc(atomic_t *v)
{
    __sync_add_and_fetch(&v->counter, 1);
}

static inline void atomic_dec(atomic_t *v)
{
    __sync_sub_and_fetch(&v->counter, 1);
}

static inline int atomic_cmpxchg(atomic_t *v, int old, int new_val)
{
    return __sync_val_compare_and_swap(&v->counter, old, new_val);
}

/* ===================================================================== */
/* Atomic Initialization */
/* ===================================================================== */

#define ATOMIC_INIT(i) { .counter = (i) }

#endif /* _KERNEL_ATOMIC_H */
