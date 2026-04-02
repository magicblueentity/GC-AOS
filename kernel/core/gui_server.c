/*
 * GC-AOS Kernel - GUI Server Implementation
 * 
 * Minimal kernel-side GUI server that handles window management
 * and delegates all rendering to user-space GUI server.
 */

#include "kernel/gui_server.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "mm/slab.h"
#include "printk.h"
#include "sync/spinlock.h"
#include "string.h"

/* Error codes */
#define ENOMEM 12
#define EINVAL 22
#define ENODEV 19
#define ENOENT 2
#define EBUSY 16
#define EPERM 1

/* Page flags */
#define PAGE_KERNEL 0

/* Forward declarations */
extern void vmm_free(void *addr, size_t size);
extern void *vmm_alloc(size_t size, unsigned int flags);
extern void *vmm_map_to_userspace(void *addr, size_t size);
extern uint64_t arch_timer_get_ms(void);
extern int snprintf(char *str, size_t size, const char *format, ...);

/* Current task */
extern struct task_struct *current;

/* ===================================================================== */
/* GUI Server State */
/* ===================================================================== */

static struct {
    /* Framebuffer information */
    struct framebuffer_info fb_info;
    bool fb_initialized;
    
    /* Window management */
    struct list_head windows;           /* All windows */
    spinlock_t window_lock;             /* Window list lock */
    uint32_t next_window_id;           /* Next window ID */
    
    /* Surface management */
    struct list_head surfaces;          /* All surfaces */
    spinlock_t surface_lock;            /* Surface list lock */
    uint32_t next_surface_id;          /* Next surface ID */
    
    /* Process management */
    struct list_head process_windows;  /* Process->window mapping */
    spinlock_t process_lock;            /* Process lock */
    
    /* Event handling */
    struct kmem_cache *event_cache;     /* Event cache */
    
    /* Statistics */
    atomic_t window_count;              /* Number of windows */
    atomic_t surface_count;             /* Number of surfaces */
    atomic_t event_count;               /* Number of events */
    
    /* Server state */
    bool initialized;                   /* Server initialized */
} gui_server = {
    .initialized = false,
};

/* ===================================================================== */
/* Process Window Mapping */
/* ===================================================================== */

struct process_windows {
    pid_t pid;                          /* Process ID */
    struct list_head windows;           /* Windows owned by process */
    struct list_head list;              /* Process list */
};

/* ===================================================================== */
/* Surface Management */
/* ===================================================================== */

struct gui_surface {
    uint32_t id;                        /* Surface ID */
    uint32_t width;                     /* Surface width */
    uint32_t height;                    /* Surface height */
    uint32_t pitch;                     /* Surface pitch */
    virt_addr_t data;                   /* Surface data */
    size_t size;                        /* Surface size */
    bool shared;                        /* Shared with userspace */
    pid_t owner_pid;                    /* Owner process */
    atomic_t refcount;                  /* Reference count */
    struct list_head list;              /* Surface list */
};

/* ===================================================================== */
/* GUI Server Functions */
/* ===================================================================== */

/**
 * gui_server_init - Initialize GUI server interface
 */
int gui_server_init(void)
{
    printk(KERN_INFO "GUI Server: Initializing minimal kernel interface\n");
    
    if (gui_server.initialized) {
        printk(KERN_WARNING "GUI Server: Already initialized\n");
        return 0;
    }
    
    /* Initialize locks */
    spin_lock_init(&gui_server.window_lock);
    spin_lock_init(&gui_server.surface_lock);
    spin_lock_init(&gui_server.process_lock);
    
    /* Initialize lists */
    INIT_LIST_HEAD(&gui_server.windows);
    INIT_LIST_HEAD(&gui_server.surfaces);
    INIT_LIST_HEAD(&gui_server.process_windows);
    
    /* Initialize counters */
    atomic_set(&gui_server.window_count, 0);
    atomic_set(&gui_server.surface_count, 0);
    atomic_set(&gui_server.event_count, 0);
    gui_server.next_window_id = 1;
    gui_server.next_surface_id = 1;
    
    /* Create event cache */
    gui_server.event_cache = kmem_cache_create("gui_event", 
                                              sizeof(struct gui_event),
                                              0, SLAB_HWCACHE_ALIGN,
                                              NULL, NULL);
    if (!gui_server.event_cache) {
        printk(KERN_ERR "GUI Server: Failed to create event cache\n");
        return -ENOMEM;
    }
    
    /* Initialize framebuffer (if available) */
    extern int fb_init(void);
    extern void fb_get_info(uint32_t **buffer, uint32_t *width, uint32_t *height);
    
    if (fb_init() == 0) {
        uint32_t *fb_buffer;
        uint32_t fb_width, fb_height;
        
        fb_get_info(&fb_buffer, &fb_width, &fb_height);
        
        if (fb_buffer) {
            gui_server.fb_info.virt_base = (virt_addr_t)fb_buffer;
            gui_server.fb_info.width = fb_width;
            gui_server.fb_info.height = fb_height;
            gui_server.fb_info.pitch = fb_width * 4;  /* Assume 32bpp */
            gui_server.fb_info.bpp = 32;
            gui_server.fb_info.red_mask = 0x00FF0000;
            gui_server.fb_info.green_mask = 0x0000FF00;
            gui_server.fb_info.blue_mask = 0x000000FF;
            gui_server.fb_info.alpha_mask = 0xFF000000;
            gui_server.fb_initialized = true;
            
            printk(KERN_INFO "GUI Server: Framebuffer %dx%d initialized\n",
                   fb_width, fb_height);
        }
    }
    
    gui_server.initialized = true;
    printk(KERN_INFO "GUI Server: Ready\n");
    return 0;
}

/**
 * gui_server_shutdown - Shutdown GUI server interface
 */
void gui_server_shutdown(void)
{
    if (!gui_server.initialized) {
        return;
    }
    
    printk(KERN_INFO "GUI Server: Shutting down\n");
    
    /* Destroy all windows */
    spin_lock(&gui_server.window_lock);
    while (!list_empty(&gui_server.windows)) {
        struct gui_window *win = list_first_entry(&gui_server.windows,
                                                  struct gui_window, list);
        list_del(&win->list);
        kfree(win);
        atomic_dec(&gui_server.window_count);
    }
    spin_unlock(&gui_server.window_lock);
    
    /* Destroy all surfaces */
    spin_lock(&gui_server.surface_lock);
    while (!list_empty(&gui_server.surfaces)) {
        struct gui_surface *surf = list_first_entry(&gui_server.surfaces,
                                                    struct gui_surface, list);
        list_del(&surf->list);
        
        if (surf->data) {
            vmm_free((void*)surf->data, surf->size);
        }
        kfree(surf);
        atomic_dec(&gui_server.surface_count);
    }
    spin_unlock(&gui_server.surface_lock);
    
    /* Destroy process mappings */
    spin_lock(&gui_server.process_lock);
    while (!list_empty(&gui_server.process_windows)) {
        struct process_windows *proc = list_first_entry(&gui_server.process_windows,
                                                        struct process_windows, list);
        list_del(&proc->list);
        kfree(proc);
    }
    spin_unlock(&gui_server.process_lock);
    
    /* Destroy event cache */
    if (gui_server.event_cache) {
        kmem_cache_destroy(gui_server.event_cache);
    }
    
    gui_server.initialized = false;
    printk(KERN_INFO "GUI Server: Shutdown complete\n");
}

/**
 * gui_server_get_framebuffer - Get framebuffer information
 */
int gui_server_get_framebuffer(struct framebuffer_info *info)
{
    if (!gui_server.initialized || !info) {
        return -EINVAL;
    }
    
    if (!gui_server.fb_initialized) {
        return -ENODEV;
    }
    
    *info = gui_server.fb_info;
    return 0;
}

/**
 * gui_server_create_surface - Create a surface
 */
int gui_server_create_surface(uint32_t width, uint32_t height, bool shared)
{
    if (!gui_server.initialized || width == 0 || height == 0) {
        return -EINVAL;
    }
    
    /* Allocate surface structure */
    struct gui_surface *surf = kmalloc(sizeof(struct gui_surface), GFP_KERNEL);
    if (!surf) {
        return -ENOMEM;
    }
    
    /* Calculate surface size */
    uint32_t pitch = ALIGN(width * 4, 64);  /* 32bpp, 64-byte aligned */
    size_t size = pitch * height;
    
    /* Allocate surface memory */
    virt_addr_t data = (virt_addr_t)vmm_alloc(size, PAGE_KERNEL);
    if (!data) {
        kfree(surf);
        return -ENOMEM;
    }
    
    /* Initialize surface */
    surf->id = gui_server.next_surface_id++;
    surf->width = width;
    surf->height = height;
    surf->pitch = pitch;
    surf->data = data;
    surf->size = size;
    surf->shared = shared;
    surf->owner_pid = current->pid;
    atomic_set(&surf->refcount, 1);
    
    /* Add to surface list */
    spin_lock(&gui_server.surface_lock);
    list_add(&surf->list, &gui_server.surfaces);
    atomic_inc(&gui_server.surface_count);
    spin_unlock(&gui_server.surface_lock);
    
    printk(KERN_DEBUG "GUI Server: Created surface %d (%dx%d, shared=%s)\n",
           surf->id, width, height, shared ? "yes" : "no");
    
    return surf->id;
}

/**
 * gui_server_destroy_surface - Destroy a surface
 */
int gui_server_destroy_surface(uint32_t surface_id)
{
    if (!gui_server.initialized) {
        return -EINVAL;
    }
    
    spin_lock(&gui_server.surface_lock);
    
    /* Find surface */
    struct gui_surface *surf;
    list_for_each_entry(surf, &gui_server.surfaces, list) {
        if (surf->id == surface_id) {
            break;
        }
    }
    
    if (!surf || &surf->list == &gui_server.surfaces) {
        spin_unlock(&gui_server.surface_lock);
        return -ENOENT;
    }
    
    /* Check reference count */
    if (atomic_read(&surf->refcount) > 1) {
        spin_unlock(&gui_server.surface_lock);
        return -EBUSY;
    }
    
    /* Remove from list */
    list_del(&surf->list);
    atomic_dec(&gui_server.surface_count);
    
    spin_unlock(&gui_server.surface_lock);
    
    /* Free surface memory */
    if (surf->data) {
        vmm_free((void*)surf->data, surf->size);
    }
    
    kfree(surf);
    
    printk(KERN_DEBUG "GUI Server: Destroyed surface %d\n", surface_id);
    return 0;
}

/**
 * gui_server_map_surface - Map surface to userspace
 */
int gui_server_map_surface(uint32_t surface_id, virt_addr_t *addr)
{
    if (!gui_server.initialized || !addr) {
        return -EINVAL;
    }
    
    spin_lock(&gui_server.surface_lock);
    
    /* Find surface */
    struct gui_surface *surf;
    list_for_each_entry(surf, &gui_server.surfaces, list) {
        if (surf->id == surface_id) {
            break;
        }
    }
    
    if (!surf || &surf->list == &gui_server.surfaces) {
        spin_unlock(&gui_server.surface_lock);
        return -ENOENT;
    }
    
    if (!surf->shared) {
        spin_unlock(&gui_server.surface_lock);
        return -EPERM;
    }
    
    /* Map surface to userspace */
    virt_addr_t user_addr = (virt_addr_t)vmm_map_to_userspace((void*)surf->data, surf->size);
    if (!user_addr) {
        spin_unlock(&gui_server.surface_lock);
        return -ENOMEM;
    }
    
    atomic_inc(&surf->refcount);
    *addr = user_addr;
    
    spin_unlock(&gui_server.surface_lock);
    
    return 0;
}

/**
 * gui_server_unmap_surface - Unmap surface from userspace
 */
int gui_server_unmap_surface(uint32_t surface_id)
{
    if (!gui_server.initialized) {
        return -EINVAL;
    }
    
    spin_lock(&gui_server.surface_lock);
    
    /* Find surface */
    struct gui_surface *surf;
    list_for_each_entry(surf, &gui_server.surfaces, list) {
        if (surf->id == surface_id) {
            break;
        }
    }
    
    if (!surf || &surf->list == &gui_server.surfaces) {
        spin_unlock(&gui_server.surface_lock);
        return -ENOENT;
    }
    
    atomic_dec(&surf->refcount);
    
    spin_unlock(&gui_server.surface_lock);
    
    /* Note: Actual unmapping happens when process exits or surface is destroyed */
    return 0;
}

/**
 * gui_server_create_window - Create a window
 */
int gui_server_create_window(pid_t owner_pid, const char *title,
                           int32_t x, int32_t y, uint32_t width, uint32_t height,
                           uint32_t flags)
{
    if (!gui_server.initialized || !title) {
        return -EINVAL;
    }
    
    /* Allocate window structure */
    struct gui_window *win = kmalloc(sizeof(struct gui_window), GFP_KERNEL);
    if (!win) {
        return -ENOMEM;
    }
    
    /* Initialize window */
    memset(win, 0, sizeof(struct gui_window));
    win->id = gui_server.next_window_id++;
    win->owner_pid = owner_pid;
    strncpy(win->title, title, sizeof(win->title) - 1);
    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->min_width = 100;
    win->min_height = 100;
    win->max_width = 4096;
    win->max_height = 4096;
    win->visible = true;
    win->focused = false;
    win->minimized = false;
    win->maximized = false;
    win->resizable = !(flags & GUI_WINDOW_FLAG_NO_RESIZE);
    win->movable = !(flags & GUI_WINDOW_FLAG_NO_MOVE);
    win->flags = flags;
    win->type = GUI_WINDOW_TYPE_NORMAL;
    
    /* Initialize event queue */
    INIT_LIST_HEAD(&win->event_queue);
    spin_lock_init(&win->event_lock);
    atomic_set(&win->refcount, 1);
    
    /* Add to window list */
    spin_lock(&gui_server.window_lock);
    list_add(&win->list, &gui_server.windows);
    atomic_inc(&gui_server.window_count);
    spin_unlock(&gui_server.window_lock);
    
    /* Add to process mapping */
    spin_lock(&gui_server.process_lock);
    struct process_windows *proc;
    list_for_each_entry(proc, &gui_server.process_windows, list) {
        if (proc->pid == owner_pid) {
            break;
        }
    }
    
    if (!proc || &proc->list == &gui_server.process_windows) {
        /* Create new process mapping */
        proc = kmalloc(sizeof(struct process_windows), GFP_KERNEL);
        if (proc) {
            proc->pid = owner_pid;
            INIT_LIST_HEAD(&proc->windows);
            list_add(&proc->list, &gui_server.process_windows);
        }
    }
    
    if (proc) {
        list_add(&win->owner_list, &proc->windows);
    }
    
    spin_unlock(&gui_server.process_lock);
    
    printk(KERN_DEBUG "GUI Server: Created window %d for PID %d: '%s'\n",
           win->id, owner_pid, title);
    
    return win->id;
}

/**
 * gui_server_destroy_window - Destroy a window
 */
int gui_server_destroy_window(uint32_t window_id)
{
    if (!gui_server.initialized) {
        return -EINVAL;
    }
    
    spin_lock(&gui_server.window_lock);
    
    /* Find window */
    struct gui_window *win;
    list_for_each_entry(win, &gui_server.windows, list) {
        if (win->id == window_id) {
            break;
        }
    }
    
    if (!win || &win->list == &gui_server.windows) {
        spin_unlock(&gui_server.window_lock);
        return -ENOENT;
    }
    
    /* Remove from window list */
    list_del(&win->list);
    atomic_dec(&gui_server.window_count);
    
    spin_unlock(&gui_server.window_lock);
    
    /* Remove from process mapping */
    spin_lock(&gui_server.process_lock);
    list_del(&win->owner_list);
    spin_unlock(&gui_server.process_lock);
    
    /* Free event queue */
    spin_lock(&win->event_lock);
    while (!list_empty(&win->event_queue)) {
        struct gui_event *event = list_first_entry(&win->event_queue,
                                                   struct gui_event, list);
        list_del(&event->list);
        kmem_cache_free(gui_server.event_cache, event);
    }
    spin_unlock(&win->event_lock);
    
    kfree(win);
    
    printk(KERN_DEBUG "GUI Server: Destroyed window %d\n", window_id);
    return 0;
}

/**
 * gui_server_post_event - Post event to window
 */
int gui_server_post_event(uint32_t window_id, const struct gui_event *event)
{
    if (!gui_server.initialized || !event) {
        return -EINVAL;
    }
    
    spin_lock(&gui_server.window_lock);
    
    /* Find window */
    struct gui_window *win;
    list_for_each_entry(win, &gui_server.windows, list) {
        if (win->id == window_id) {
            break;
        }
    }
    
    if (!win || &win->list == &gui_server.windows) {
        spin_unlock(&gui_server.window_lock);
        return -ENOENT;
    }
    
    /* Allocate event */
    struct gui_event *new_event = kmem_cache_alloc(gui_server.event_cache, GFP_ATOMIC);
    if (!new_event) {
        spin_unlock(&gui_server.window_lock);
        return -ENOMEM;
    }
    
    /* Copy event */
    *new_event = *event;
    new_event->window_id = window_id;
    new_event->timestamp = arch_timer_get_ms();
    
    /* Add to event queue */
    spin_lock(&win->event_lock);
    list_add_tail(&new_event->list, &win->event_queue);
    spin_unlock(&win->event_lock);
    
    atomic_inc(&gui_server.event_count);
    
    spin_unlock(&gui_server.window_lock);
    
    return 0;
}

/**
 * gui_server_handle_key_event - Handle keyboard event
 */
void gui_server_handle_key_event(uint32_t keycode, uint32_t scancode, bool pressed,
                                 bool shift, bool ctrl, bool alt)
{
    if (!gui_server.initialized) {
        return;
    }
    
    /* Create key event */
    struct gui_event event = {
        .type = pressed ? GUI_EVENT_KEY_DOWN : GUI_EVENT_KEY_UP,
        .data.key = {
            .keycode = keycode,
            .scancode = scancode,
            .shift = shift,
            .ctrl = ctrl,
            .alt = alt,
        }
    };
    
    /* Post to focused window (for now, just post to first window) */
    spin_lock(&gui_server.window_lock);
    if (!list_empty(&gui_server.windows)) {
        struct gui_window *win = list_first_entry(&gui_server.windows,
                                                  struct gui_window, list);
        gui_server_post_event(win->id, &event);
    }
    spin_unlock(&gui_server.window_lock);
}

/**
 * gui_server_handle_mouse_event - Handle mouse event
 */
void gui_server_handle_mouse_event(int32_t x, int32_t y, uint32_t buttons,
                                   int32_t dx, int32_t dy, int32_t wheel)
{
    (void)wheel;  /* Suppress unused parameter warning */
    
    if (!gui_server.initialized) {
        return;
    }
    
    /* Create mouse move event */
    if (dx != 0 || dy != 0) {
        struct gui_event event = {
            .type = GUI_EVENT_MOUSE_MOVE,
            .data.mouse = {
                .x = x,
                .y = y,
                .buttons = buttons,
                .dx = dx,
                .dy = dy,
                .wheel = 0,
            }
        };
        
        /* Post to window under cursor (simplified) */
        spin_lock(&gui_server.window_lock);
        struct gui_window *win;
        list_for_each_entry(win, &gui_server.windows, list) {
            if (win->visible && x >= win->x && x < win->x + (int32_t)win->width &&
                y >= win->y && y < win->y + (int32_t)win->height) {
                gui_server_post_event(win->id, &event);
                break;
            }
        }
        spin_unlock(&gui_server.window_lock);
    }
    
    /* Create mouse button events */
    static uint32_t last_buttons = 0;
    uint32_t changed = buttons ^ last_buttons;
    
    for (int i = 0; i < 3; i++) {
        if (changed & (1 << i)) {
            struct gui_event event = {
                .type = (buttons & (1 << i)) ? GUI_EVENT_MOUSE_DOWN : GUI_EVENT_MOUSE_UP,
                .data.mouse = {
                    .x = x,
                    .y = y,
                    .buttons = buttons,
                    .dx = 0,
                    .dy = 0,
                    .wheel = 0,
                }
            };
            
            /* Post to window under cursor */
            spin_lock(&gui_server.window_lock);
            struct gui_window *win;
            list_for_each_entry(win, &gui_server.windows, list) {
                if (win->visible && x >= win->x && x < win->x + (int32_t)win->width &&
                    y >= win->y && y < win->y + (int32_t)win->height) {
                    gui_server_post_event(win->id, &event);
                    break;
                }
            }
            spin_unlock(&gui_server.window_lock);
        }
    }
    
    last_buttons = buttons;
}

/**
 * gui_server_process_exit - Handle process exit
 */
void gui_server_process_exit(pid_t pid)
{
    if (!gui_server.initialized) {
        return;
    }
    
    printk(KERN_INFO "GUI Server: Process %d exited, cleaning up windows\n", pid);
    
    spin_lock(&gui_server.process_lock);
    
    /* Find process mapping */
    struct process_windows *proc;
    list_for_each_entry(proc, &gui_server.process_windows, list) {
        if (proc->pid == pid) {
            break;
        }
    }
    
    if (proc && &proc->list != &gui_server.process_windows) {
        /* Destroy all windows owned by process */
        while (!list_empty(&proc->windows)) {
            struct gui_window *win = list_first_entry(&proc->windows,
                                                      struct gui_window, owner_list);
            list_del(&win->owner_list);
            
            /* Also remove from global window list */
            spin_lock(&gui_server.window_lock);
            list_del(&win->list);
            atomic_dec(&gui_server.window_count);
            spin_unlock(&gui_server.window_lock);
            
            /* Free window */
            kfree(win);
        }
        
        /* Remove process mapping */
        list_del(&proc->list);
        kfree(proc);
    }
    
    spin_unlock(&gui_server.process_lock);
}

/**
 * gui_server_get_stats - Get GUI server statistics
 */
int gui_server_get_stats(char *buf, size_t size)
{
    if (!gui_server.initialized || !buf || size == 0) {
        return -EINVAL;
    }
    
    int offset = snprintf(buf, size,
                         "GUI Server Statistics:\n"
                         "  Windows: %d\n"
                         "  Surfaces: %d\n"
                         "  Events: %d\n"
                         "  Framebuffer: %s\n",
                         atomic_read(&gui_server.window_count),
                         atomic_read(&gui_server.surface_count),
                         atomic_read(&gui_server.event_count),
                         gui_server.fb_initialized ? "initialized" : "not available");
    
    return offset;
}
