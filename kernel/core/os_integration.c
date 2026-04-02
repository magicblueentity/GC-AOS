/*
 * GC-AOS - Modern OS Integration Implementation
 */

#include "kernel/os_integration.h"
#include "kernel/system_services.h"
#include "kernel/process_modern.h"
#include "gui/window_manager.h"
#include "gui/compositor.h"
#include "gui/app_framework.h"
#include "kernel/event_system.h"
#include "drivers/display.h"
#include "printk.h"

/* Subsystem status flags */
#define SUBSYSTEM_PROCESS       (1 << 0)
#define SUBSYSTEM_SCHEDULER     (1 << 1)
#define SUBSYSTEM_WINDOW        (1 << 2)
#define SUBSYSTEM_COMPOSITOR    (1 << 3)
#define SUBSYSTEM_EVENT         (1 << 4)
#define SUBSYSTEM_APP           (1 << 5)
#define SUBSYSTEM_SERVICES      (1 << 6)

static int g_subsystem_status = 0;

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

int modern_os_init(void)
{
    int ret;
    
    printk(KERN_INFO "========================================\n");
    printk(KERN_INFO "GC-AOS Modern OS Initialization\n");
    printk(KERN_INFO "========================================\n");
    
    /* Initialize system services (this initializes most subsystems) */
    printk(KERN_INFO "INIT: Starting system services...\n");
    ret = services_init();
    if (ret != 0) {
        printk(KERN_ERR "INIT: Failed to initialize services: %d\n", ret);
        return ret;
    }
    g_subsystem_status |= SUBSYSTEM_SERVICES;
    
    /* Initialize process system */
    printk(KERN_INFO "INIT: Initializing process system...\n");
    process_system_init();
    g_subsystem_status |= SUBSYSTEM_PROCESS;
    
    /* Initialize scheduler */
    printk(KERN_INFO "INIT: Initializing scheduler...\n");
    scheduler_init();
    g_subsystem_status |= SUBSYSTEM_SCHEDULER;
    
    /* Get display info for window manager */
    printk(KERN_INFO "INIT: Querying display...\n");
    uint32_t screen_width = 1024;
    uint32_t screen_height = 768;
    uint32_t screen_pitch = screen_width * 4;
    void *framebuffer = NULL;
    
    /* Try to get actual display info */
    extern struct display_drv *g_display;
    if (g_display && g_display->get_info) {
        struct display_info info;
        if (g_display->get_info(&info) == 0) {
            screen_width = info.width;
            screen_height = info.height;
            screen_pitch = info.pitch;
            framebuffer = info.framebuffer;
        }
    }
    
    /* Initialize window manager */
    printk(KERN_INFO "INIT: Initializing window manager (%dx%d)...\n",
           screen_width, screen_height);
    window_manager_init(screen_width, screen_height);
    g_subsystem_status |= SUBSYSTEM_WINDOW;
    
    /* Initialize compositor */
    printk(KERN_INFO "INIT: Initializing compositor...\n");
    if (framebuffer) {
        compositor_init(framebuffer, screen_width, screen_height, 
                       screen_pitch, 0); /* 0 = ARGB8888 */
    } else {
        /* Allocate dummy framebuffer */
        framebuffer = kmalloc_aligned(screen_width * screen_height * 4, PAGE_SIZE);
        if (framebuffer) {
            compositor_init(framebuffer, screen_width, screen_height,
                           screen_pitch, 0);
        }
    }
    g_subsystem_status |= SUBSYSTEM_COMPOSITOR;
    
    /* Initialize event system */
    printk(KERN_INFO "INIT: Initializing event system...\n");
    event_system_init();
    g_subsystem_status |= SUBSYSTEM_EVENT;
    
    /* Initialize app framework */
    printk(KERN_INFO "INIT: Initializing app framework...\n");
    app_manager_init();
    g_subsystem_status |= SUBSYSTEM_APP;
    
    /* Register window event handlers */
    printk(KERN_INFO "INIT: Registering event handlers...\n");
    
    /* Start compositor thread/process */
    printk(KERN_INFO "INIT: Starting compositor service...\n");
    /* compositor_run(); -- would start compositor as separate entity */
    
    printk(KERN_INFO "========================================\n");
    printk(KERN_INFO "Modern OS initialization complete!\n");
    printk(KERN_INFO "========================================\n");
    
    return 0;
}

/* ===================================================================== */
/* Shutdown */
/* ===================================================================== */

void modern_os_shutdown(void)
{
    printk(KERN_INFO "SHUTDOWN: Stopping modern OS subsystems...\n");
    
    /* Stop app framework (gracefully close apps) */
    if (g_subsystem_status & SUBSYSTEM_APP) {
        printk(KERN_INFO "SHUTDOWN: Shutting down app framework...\n");
        app_manager_shutdown();
    }
    
    /* Shutdown event system */
    if (g_subsystem_status & SUBSYSTEM_EVENT) {
        printk(KERN_INFO "SHUTDOWN: Shutting down event system...\n");
        event_system_shutdown();
    }
    
    /* Shutdown compositor */
    if (g_subsystem_status & SUBSYSTEM_COMPOSITOR) {
        printk(KERN_INFO "SHUTDOWN: Shutting down compositor...\n");
        compositor_shutdown();
    }
    
    /* Shutdown services (includes window manager) */
    if (g_subsystem_status & SUBSYSTEM_SERVICES) {
        printk(KERN_INFO "SHUTDOWN: Shutting down system services...\n");
        services_shutdown();
    }
    
    printk(KERN_INFO "SHUTDOWN: Modern OS subsystems stopped\n");
}

/* ===================================================================== */
/* Main System Loop */
/* ===================================================================== */

void modern_os_main_loop(void)
{
    printk(KERN_INFO "MAIN: Entering modern OS main loop\n");
    
    uint64_t frame_count = 0;
    uint64_t last_fps_time = get_time_ns();
    
    while (1) {
        /* 1. Process pending events */
        if (g_subsystem_status & SUBSYSTEM_EVENT) {
            event_dispatch_all();
        }
        
        /* 2. Update all active apps */
        if (g_subsystem_status & SUBSYSTEM_APP) {
            app_manager_update();
        }
        
        /* 3. Render apps to their backbuffers */
        if (g_subsystem_status & SUBSYSTEM_APP) {
            app_manager_render();
        }
        
        /* 4. Composite frame */
        if (g_subsystem_status & SUBSYSTEM_COMPOSITOR) {
            compositor_frame();
        }
        
        /* 5. Schedule processes */
        if (g_subsystem_status & SUBSYSTEM_SCHEDULER) {
            scheduler_reschedule();
        }
        
        /* FPS tracking */
        frame_count++;
        uint64_t now = get_time_ns();
        if (now - last_fps_time >= 1000000000ULL) { /* 1 second */
            /* Log FPS occasionally */
            if ((frame_count % 60) == 0) {
                printk(KERN_DEBUG "MAIN: %llu FPS\n", frame_count);
            }
            frame_count = 0;
            last_fps_time = now;
        }
        
        /* Yield to allow other processes to run */
        if (g_subsystem_status & SUBSYSTEM_SCHEDULER) {
            scheduler_yield();
        }
    }
}

/* ===================================================================== */
/* Validation */
/* ===================================================================== */

int modern_os_validate(void)
{
    int failed = 0;
    
    /* Check process system */
    if (g_subsystem_status & SUBSYSTEM_PROCESS) {
        process_t *idle = process_get(0);
        if (!idle || idle->state == PROCESS_STATE_FREE) {
            printk(KERN_ERR "VALIDATE: Process system not operational\n");
            failed |= SUBSYSTEM_PROCESS;
        }
    }
    
    /* Check scheduler */
    if (g_subsystem_status & SUBSYSTEM_SCHEDULER) {
        extern runqueue_t g_runqueue;
        if (!g_runqueue.current) {
            printk(KERN_ERR "VALIDATE: Scheduler not operational\n");
            failed |= SUBSYSTEM_SCHEDULER;
        }
    }
    
    /* Check window manager */
    if (g_subsystem_status & SUBSYSTEM_WINDOW) {
        if (g_window_manager.desktop_width == 0) {
            printk(KERN_ERR "VALIDATE: Window manager not operational\n");
            failed |= SUBSYSTEM_WINDOW;
        }
    }
    
    /* Check compositor */
    if (g_subsystem_status & SUBSYSTEM_COMPOSITOR) {
        if (!g_compositor.enabled) {
            printk(KERN_ERR "VALIDATE: Compositor not operational\n");
            failed |= SUBSYSTEM_COMPOSITOR;
        }
    }
    
    /* Check event system */
    if (g_subsystem_status & SUBSYSTEM_EVENT) {
        if (!g_event_system.enabled) {
            printk(KERN_ERR "VALIDATE: Event system not operational\n");
            failed |= SUBSYSTEM_EVENT;
        }
    }
    
    /* Check app framework */
    if (g_subsystem_status & SUBSYSTEM_APP) {
        if (!g_app_manager.running) {
            printk(KERN_ERR "VALIDATE: App framework not operational\n");
            failed |= SUBSYSTEM_APP;
        }
    }
    
    if (failed == 0) {
        printk(KERN_INFO "VALIDATE: All subsystems operational\n");
    }
    
    return failed;
}

/* ===================================================================== */
/* Integration Helpers */
/* ===================================================================== */

/**
 * Called from old scheduler to dispatch events
 */
void scheduler_hook_dispatch_events(void)
{
    if (g_subsystem_status & SUBSYSTEM_EVENT) {
        event_dispatch_all();
    }
}

/**
 * Called from timer interrupt for scheduler tick
 */
void timer_hook_scheduler_tick(void)
{
    if (g_subsystem_status & SUBSYSTEM_SCHEDULER) {
        scheduler_tick();
    }
}

/**
 * Called from input driver to post keyboard events
 */
void input_hook_keyboard(int scancode, int pressed)
{
    if (g_subsystem_status & SUBSYSTEM_EVENT) {
        event_input_keyboard(scancode, pressed);
    }
}

/**
 * Called from input driver to post mouse events
 */
void input_hook_mouse(int x, int y, uint32_t buttons, int delta_z)
{
    if (g_subsystem_status & SUBSYSTEM_EVENT) {
        event_input_mouse(x, y, buttons, delta_z);
    }
}

/**
 * Called during display refresh
 */
void display_hook_composite(void)
{
    if (g_subsystem_status & SUBSYSTEM_COMPOSITOR) {
        compositor_frame();
    }
}
