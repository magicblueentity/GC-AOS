/*
 * GC-AOS Kernel - User-Space GUI Server Interface
 * 
 * Defines the interface between kernel and user-space GUI server.
 * All GUI rendering logic is moved to user-space.
 */

#ifndef _KERNEL_GUI_SERVER_H
#define _KERNEL_GUI_SERVER_H

#include "types.h"
#include "sync/spinlock.h"
#include "kernel/list.h"
#include "sched/sched.h"

/* ===================================================================== */
/* GUI Server Protocol */
/* ===================================================================== */

/* GUI Server commands */
enum gui_cmd {
    GUI_CMD_INIT = 0,              /* Initialize GUI server */
    GUI_CMD_SHUTDOWN,              /* Shutdown GUI server */
    GUI_CMD_CREATE_WINDOW,         /* Create new window */
    GUI_CMD_DESTROY_WINDOW,        /* Destroy window */
    GUI_CMD_MOVE_WINDOW,           /* Move window */
    GUI_CMD_RESIZE_WINDOW,         /* Resize window */
    GUI_CMD_SHOW_WINDOW,           /* Show/hide window */
    GUI_CMD_SET_FOCUS,             /* Set window focus */
    GUI_CMD_INVALIDATE_REGION,     /* Invalidate screen region */
    GUI_CMD_BLIT_SURFACE,          /* Blit surface to screen */
    GUI_CMD_DRAW_TEXT,             /* Draw text */
    GUI_CMD_DRAW_RECT,             /* Draw rectangle */
    GUI_CMD_DRAW_LINE,             /* Draw line */
    GUI_CMD_DRAW_IMAGE,            /* Draw image */
    GUI_CMD_SET_CURSOR,            /* Set mouse cursor */
    GUI_CMD_GET_EVENT,             /* Get next event */
    GUI_CMD_SET_PROPERTY,          /* Set window property */
    GUI_CMD_GET_PROPERTY,          /* Get window property */
    GUI_CMD_MAX
};

/* GUI Server events */
enum gui_event_type {
    GUI_EVENT_KEY_DOWN = 0,        /* Key pressed */
    GUI_EVENT_KEY_UP,              /* Key released */
    GUI_EVENT_MOUSE_DOWN,          /* Mouse button pressed */
    GUI_EVENT_MOUSE_UP,            /* Mouse button released */
    GUI_EVENT_MOUSE_MOVE,          /* Mouse moved */
    GUI_EVENT_MOUSE_WHEEL,         /* Mouse wheel */
    GUI_EVENT_WINDOW_CLOSE,        /* Window close requested */
    GUI_EVENT_WINDOW_RESIZE,       /* Window resized */
    GUI_EVENT_WINDOW_MOVE,         /* Window moved */
    GUI_EVENT_WINDOW_FOCUS,        /* Window focus changed */
    GUI_EVENT_MAX
};

/* ===================================================================== */
/* Framebuffer Interface */
/* ===================================================================== */

struct framebuffer_info {
    phys_addr_t phys_base;          /* Physical base address */
    virt_addr_t virt_base;          /* Virtual base address */
    uint32_t width;                 /* Screen width */
    uint32_t height;                /* Screen height */
    uint32_t pitch;                 /* Bytes per line */
    uint32_t bpp;                   /* Bits per pixel */
    uint32_t red_mask;              /* Red color mask */
    uint32_t green_mask;            /* Green color mask */
    uint32_t blue_mask;             /* Blue color mask */
    uint32_t alpha_mask;            /* Alpha color mask */
};

/* Surface management */
struct surface {
    uint32_t id;                    /* Surface ID */
    uint32_t width;                 /* Surface width */
    uint32_t height;                /* Surface height */
    uint32_t pitch;                 /* Surface pitch */
    virt_addr_t data;               /* Surface data */
    bool shared;                    /* Shared with userspace */
};

/* ===================================================================== */
/* Window Structure (Kernel-side) */
/* ===================================================================== */

struct gui_window {
    uint32_t id;                    /* Window ID */
    uint32_t owner_pid;             /* Owner process ID */
    char title[256];                 /* Window title */
    
    /* Window geometry */
    int32_t x, y;                   /* Window position */
    uint32_t width, height;         /* Window size */
    uint32_t min_width, min_height;  /* Minimum size */
    uint32_t max_width, max_height;  /* Maximum size */
    
    /* Window state */
    bool visible;                   /* Window visible */
    bool focused;                   /* Window has focus */
    bool minimized;                 /* Window minimized */
    bool maximized;                 /* Window maximized */
    bool resizable;                 /* Window resizable */
    bool movable;                   /* Window movable */
    
    /* Window properties */
    uint32_t flags;                 /* Window flags */
    uint32_t class;                 /* Window class */
    uint32_t type;                  /* Window type */
    
    /* Surface management */
    uint32_t surface_id;            /* Primary surface ID */
    struct list_head surfaces;      /* Additional surfaces */
    
    /* Event handling */
    struct list_head event_queue;    /* Pending events */
    spinlock_t event_lock;          /* Event queue lock */
    
    /* Reference counting */
    atomic_t refcount;              /* Reference count */
    
    /* List linkage */
    struct list_head list;          /* Window list */
    struct list_head owner_list;    /* Owner's window list */
};

/* Window flags */
#define GUI_WINDOW_FLAG_MODAL       (1 << 0)  /* Modal window */
#define GUI_WINDOW_FLAG_POPUP       (1 << 1)  /* Popup window */
#define GUI_WINDOW_FLAG_TOOLTIP     (1 << 2)  /* Tooltip window */
#define GUI_WINDOW_FLAG_NO_BORDER   (1 << 3)  /* No border */
#define GUI_WINDOW_FLAG_NO_RESIZE   (1 << 4)  /* Not resizable */
#define GUI_WINDOW_FLAG_NO_MOVE     (1 << 5)  /* Not movable */
#define GUI_WINDOW_FLAG_ALWAYS_ON_TOP (1 << 6) /* Always on top */
#define GUI_WINDOW_FLAG_SKIP_TASKBAR (1 << 7)  /* Skip taskbar */

/* Window types */
#define GUI_WINDOW_TYPE_NORMAL      0          /* Normal window */
#define GUI_WINDOW_TYPE_DESKTOP     1          /* Desktop window */
#define GUI_WINDOW_TYPE_DOCK        2          /* Dock window */
#define GUI_WINDOW_TYPE_MENU        3          /* Menu window */
#define GUI_WINDOW_TYPE_TOOLTIP     4          /* Tooltip window */
#define GUI_WINDOW_TYPE_DIALOG      5          /* Dialog window */

/* ===================================================================== */
/* Event Structure */
/* ===================================================================== */

struct gui_event {
    enum gui_event_type type;            /* Event type */
    uint32_t window_id;             /* Target window ID */
    uint64_t timestamp;             /* Event timestamp */
    
    union {
        struct {
            uint32_t keycode;       /* Key code */
            uint32_t scancode;      /* Scan code */
            bool shift;             /* Shift key state */
            bool ctrl;              /* Ctrl key state */
            bool alt;               /* Alt key state */
        } key;
        
        struct {
            int32_t x, y;           /* Mouse position */
            uint32_t buttons;       /* Mouse button state */
            int32_t dx, dy;         /* Mouse delta */
            int32_t wheel;          /* Mouse wheel delta */
        } mouse;
        
        struct {
            uint32_t width, height; /* New window size */
            uint32_t edge;          /* Resize edge */
        } resize;
        
        struct {
            int32_t x, y;           /* New window position */
        } move;
    } data;
    
    struct list_head list;          /* List linkage */
};

/* ===================================================================== */
/* GUI Server Interface Functions */
/* ===================================================================== */

/**
 * gui_server_init - Initialize GUI server interface
 * 
 * Return: 0 on success, negative error on failure
 */
int gui_server_init(void);

/**
 * gui_server_shutdown - Shutdown GUI server interface
 */
void gui_server_shutdown(void);

/**
 * gui_server_get_framebuffer - Get framebuffer information
 * @info: Output framebuffer information
 * 
 * Return: 0 on success, negative error on failure
 */
int gui_server_get_framebuffer(struct framebuffer_info *info);

/**
 * gui_server_create_surface - Create a surface
 * @width: Surface width
 * @height: Surface height
 * @shared: Whether surface should be shared with userspace
 * 
 * Return: Surface ID or negative error on failure
 */
int gui_server_create_surface(uint32_t width, uint32_t height, bool shared);

/**
 * gui_server_destroy_surface - Destroy a surface
 * @surface_id: Surface ID
 * 
 * Return: 0 on success, negative error on failure
 */
int gui_server_destroy_surface(uint32_t surface_id);

/**
 * gui_server_map_surface - Map surface to userspace
 * @surface_id: Surface ID
 * @addr: Output virtual address
 * 
 * Return: 0 on success, negative error on failure
 */
int gui_server_map_surface(uint32_t surface_id, virt_addr_t *addr);

/**
 * gui_server_unmap_surface - Unmap surface from userspace
 * @surface_id: Surface ID
 * 
 * Return: 0 on success, negative error on failure
 */
int gui_server_unmap_surface(uint32_t surface_id);

/**
 * gui_server_create_window - Create a window
 * @owner_pid: Owner process ID
 * @title: Window title
 * @x, y: Initial position
 * @width, height: Initial size
 * @flags: Window flags
 * 
 * Return: Window ID or negative error on failure
 */
int gui_server_create_window(pid_t owner_pid, const char *title,
                           int32_t x, int32_t y, uint32_t width, uint32_t height,
                           uint32_t flags);

/**
 * gui_server_destroy_window - Destroy a window
 * @window_id: Window ID
 * 
 * Return: 0 on success, negative error on failure
 */
int gui_server_destroy_window(uint32_t window_id);

/**
 * gui_server_set_window_geometry - Set window geometry
 * @window_id: Window ID
 * @x, y: Window position
 * @width, height: Window size
 * 
 * Return: 0 on success, negative error on failure
 */
int gui_server_set_window_geometry(uint32_t window_id, int32_t x, int32_t y,
                                   uint32_t width, uint32_t height);

/**
 * gui_server_set_window_state - Set window state
 * @window_id: Window ID
 * @visible: Window visibility
 * @focused: Window focus
 * @minimized: Window minimized state
 * @maximized: Window maximized state
 * 
 * Return: 0 on success, negative error on failure
 */
int gui_server_set_window_state(uint32_t window_id, bool visible, bool focused,
                                bool minimized, bool maximized);

/**
 * gui_server_post_event - Post event to window
 * @window_id: Window ID
 * @event: Event to post
 * 
 * Return: 0 on success, negative error on failure
 */
int gui_server_post_event(uint32_t window_id, const struct gui_event *event);

/**
 * gui_server_get_event - Get next event for window
 * @window_id: Window ID
 * @event: Output event
 * @timeout: Timeout in milliseconds (0 = no timeout)
 * 
 * Return: 0 on success, negative error on failure
 */
int gui_server_get_event(uint32_t window_id, struct gui_event *event, uint32_t timeout);

/**
 * gui_server_invalidate_region - Invalidate screen region
 * @x, y: Region position
 * @width, height: Region size
 * 
 * Return: 0 on success, negative error on failure
 */
int gui_server_invalidate_region(int32_t x, int32_t y, uint32_t width, uint32_t height);

/**
 * gui_server_present - Present surface to screen
 * @surface_id: Surface ID
 * @x, y: Destination position
 * @src_x, src_y: Source position
 * @width, height: Blit size
 * 
 * Return: 0 on success, negative error on failure
 */
int gui_server_present(uint32_t surface_id, int32_t x, int32_t y,
                      uint32_t src_x, uint32_t src_y, uint32_t width, uint32_t height);

/* ===================================================================== */
/* Input Event Handling */
/* ===================================================================== */

/**
 * gui_server_handle_key_event - Handle keyboard event
 * @keycode: Key code
 * @scancode: Scan code
 * @pressed: Key pressed state
 * @shift: Shift key state
 * @ctrl: Ctrl key state
 * @alt: Alt key state
 */
void gui_server_handle_key_event(uint32_t keycode, uint32_t scancode, bool pressed,
                                 bool shift, bool ctrl, bool alt);

/**
 * gui_server_handle_mouse_event - Handle mouse event
 * @x, y: Mouse position
 * @buttons: Mouse button state
 * @dx, dy: Mouse delta
 * @wheel: Mouse wheel delta
 */
void gui_server_handle_mouse_event(int32_t x, int32_t y, uint32_t buttons,
                                   int32_t dx, int32_t dy, int32_t wheel);

/* ===================================================================== */
/* Process Management */
/* ===================================================================== */

/**
 * gui_server_process_exit - Handle process exit
 * @pid: Process ID
 */
void gui_server_process_exit(pid_t pid);

/**
 * gui_server_get_windows_by_pid - Get windows owned by process
 * @pid: Process ID
 * @windows: Output window ID array
 * @max_windows: Maximum number of windows
 * 
 * Return: Number of windows found
 */
int gui_server_get_windows_by_pid(pid_t pid, uint32_t *windows, uint32_t max_windows);

/* ===================================================================== */
/* Debugging and Statistics */
/* ===================================================================== */

/**
 * gui_server_get_stats - Get GUI server statistics
 * @buf: Buffer to write stats to
 * @size: Buffer size
 * 
 * Return: Number of bytes written
 */
int gui_server_get_stats(char *buf, size_t size);

/**
 * gui_server_validate - Validate GUI server state
 * 
 * Return: 0 if valid, negative error if corruption detected
 */
int gui_server_validate(void);

#endif /* _KERNEL_GUI_SERVER_H */
