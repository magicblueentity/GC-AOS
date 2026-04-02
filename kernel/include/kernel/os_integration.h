/*
 * GC-AOS - Modern OS Integration
 * 
 * Main integration file that ties all new subsystems together
 * and provides clean initialization without breaking existing boot.
 */

#ifndef _OS_INTEGRATION_H
#define _OS_INTEGRATION_H

/**
 * modern_os_init - Initialize all modern OS subsystems
 * 
 * Called during kernel boot after basic initialization.
 * Sets up: process system, scheduler, window manager, compositor,
 * event system, app framework, and system services.
 * 
 * Return: 0 on success, negative on failure
 */
int modern_os_init(void);

/**
 * modern_os_shutdown - Shut down all modern OS subsystems
 * 
 * Called during kernel shutdown. Cleans up all resources
 * and terminates running applications gracefully.
 */
void modern_os_shutdown(void);

/**
 * modern_os_main_loop - Main system loop
 * 
 * Replaces the old kernel idle loop. Coordinates:
 * - Event dispatch
 * - App updates
 * - Window compositing
 * - Process scheduling
 */
void modern_os_main_loop(void);

/**
 * modern_os_validate - Validate system is functioning correctly
 * 
 * Checks all subsystems and reports status.
 * 
 * Return: 0 if all systems operational, bitmask of failing systems otherwise
 */
int modern_os_validate(void);

#endif /* _OS_INTEGRATION_H */
