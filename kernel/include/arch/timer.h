/*
 * GC-AOS Kernel - Architecture Timer Interface
 * 
 * Common timer interface for all architectures
 */

#ifndef _ARCH_TIMER_H
#define _ARCH_TIMER_H

#include "types.h"

/* ===================================================================== */
/* Timer Interface Functions */
/* ===================================================================== */

/**
 * arch_timer_get_ms - Get current time in milliseconds
 * 
 * Return: Current time in milliseconds since boot
 */
uint64_t arch_timer_get_ms(void);

/**
 * arch_timer_get_us - Get current time in microseconds
 * 
 * Return: Current time in microseconds since boot
 */
uint64_t arch_timer_get_us(void);

/**
 * arch_timer_delay_ms - Delay for specified milliseconds
 * @ms: Milliseconds to delay
 */
void arch_timer_delay_ms(uint32_t ms);

/**
 * arch_timer_delay_us - Delay for specified microseconds
 * @us: Microseconds to delay
 */
void arch_timer_delay_us(uint32_t us);

/**
 * arch_timer_init - Initialize architecture timer
 * 
 * Return: 0 on success, negative error on failure
 */
int arch_timer_init(void);

#endif /* _ARCH_TIMER_H */
