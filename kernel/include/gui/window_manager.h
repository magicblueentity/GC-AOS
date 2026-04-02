/*
 * GC-AOS - Modern Window Manager
 * 
 * Structured window system with z-order management,
 * focus tracking, and per-window event routing.
 */

#ifndef _WINDOW_MANAGER_H
#define _WINDOW_MANAGER_H

#include "types.h"
#include "kernel/list.h"
#include "sync/spinlock.h"
#include "kernel/process_modern.h"

/* ===================================================================== */
/* Window Limits */
/* ===================================================================== */

#define MAX_WINDOWS         64
#define WINDOW_TITLE_MAX    128
#define MIN_WINDOW_WIDTH    100
#define MIN_WINDOW_HEIGHT   50
#define MAX_WINDOW_WIDTH    4096
#define MAX_WINDOW_HEIGHT   2160

/* Window decoration sizes */
#define WINDOW_TITLEBAR_HEIGHT  28
#define WINDOW_BORDER_WIDTH     1
#define WINDOW_RESIZE_HANDLE    12

/* ===================================================================== */
/* Window States */
/* ===================================================================== */

typedef enum {
    WINDOW_STATE_NORMAL = 0,    /* Normal visible window */
    WINDOW_STATE_MINIMIZED,     /* Minimized to taskbar */
    WINDOW_STATE_MAXIMIZED,     /* Full screen (not desktop) */
    WINDOW_STATE_HIDDEN,        /* Hidden but not destroyed */
    WINDOW_STATE_DESTROYED      /* Being destroyed */
} window_state_t;

typedef enum {
    WINDOW_FLAG_NONE = 0,
    WINDOW_FLAG_RESIZABLE = (1 << 0),
    WINDOW_FLAG_MOVABLE = (1 << 1),
    WINDOW_FLAG_CLOSABLE = (1 << 2),
    WINDOW_FLAG_MINIMIZABLE = (1 << 3),
    WINDOW_FLAG_MAXIMIZABLE = (1 << 4),
    WINDOW_FLAG_BORDERLESS = (1 << 5),
    WINDOW_FLAG_ALWAYS_ON_TOP = (1 << 6),
    WINDOW_FLAG_MODAL = (1 << 7),
    WINDOW_FLAG_DEFAULT = (WINDOW_FLAG_RESIZABLE | WINDOW_FLAG_MOVABLE | 
                          WINDOW_FLAG_CLOSABLE | WINDOW_FLAG_MINIMIZABLE | 
                          WINDOW_FLAG_MAXIMIZABLE)
} window_flags_t;

/* ===================================================================== */
/* Window Structure */
/* ===================================================================== */

typedef struct window {
    /* Identity */
    int id;                         /* Unique window ID */
    char title[WINDOW_TITLE_MAX];
    
    /* Position and Size */
    int x, y;                       /* Position */
    int width, height;              /* Size (client area) */
    int z_index;                    /* Z-order (higher = on top) */
    
    /* State */
    window_state_t state;
    window_flags_t flags;
    int focused;                    /* Is this window focused? */
    int visible;                    /* Is window visible? */
    
    /* Owner */
    process_t *owner;               /* Owning process */
    int owner_pid;                  /* Owner PID (for validation) */
    
    /* Decorations */
    int has_titlebar;               /* Show titlebar */
    int has_border;                 /* Show border */
    
    /* Previous state (for restore from maximized) */
    int prev_x, prev_y;
    int prev_width, prev_height;
    
    /* Event handling state */
    int is_dragging;                /* Currently being dragged */
    int is_resizing;                /* Currently being resized */
    int drag_start_x, drag_start_y;
    int resize_start_w, resize_start_h;
    int resize_edge;                /* Which edge is being resized */
    
    /* Content buffer (reference to compositor buffer) */
    void *backbuffer;               /* Offscreen buffer */
    int buffer_pitch;               /* Bytes per line */
    
    /* User data */
    void *user_data;
    
    /* Links */
    struct list_head list;          /* Global window list */
    struct list_head z_list;        /* Z-order list */
} window_t;

/* ===================================================================== */
/* Window Manager State */
/* ===================================================================== */

struct window_manager {
    spinlock_t lock;
    
    /* Window table */
    window_t *windows[MAX_WINDOWS];
    int window_count;
    int next_window_id;
    int max_z_index;
    
    /* Lists */
    struct list_head all_windows;       /* All windows */
    struct list_head z_ordered;         /* Z-ordered (front to back) */
    struct list_head visible_windows;   /* Visible windows only */
    
    /* Focus */
    window_t *focused_window;
    window_t *previous_focus;         /* For focus restoration */
    
    /* Desktop */
    int desktop_width;
    int desktop_height;
    
    /* Modal state */
    window_t *modal_window;             /* Current modal window */
};

extern struct window_manager g_window_manager;

/* ===================================================================== */
/* Window Manager API */
/* ===================================================================== */

/**
 * window_manager_init - Initialize window manager
 * @width: Desktop width
 * @height: Desktop height
 */
void window_manager_init(int width, int height);

/**
 * window_create - Create a new window
 * @title: Window title
 * @x, @y: Initial position
 * @width, @height: Initial size
 * @flags: Window flags
 * @owner: Owning process
 * 
 * Return: New window pointer, or NULL on failure
 */
window_t *window_create(const char *title, int x, int y, 
                        int width, int height, window_flags_t flags,
                        process_t *owner);

/**
 * window_destroy - Destroy a window
 * @win: Window to destroy
 */
void window_destroy(window_t *win);

/**
 * window_show - Show a window
 * @win: Window to show
 */
void window_show(window_t *win);

/**
 * window_hide - Hide a window
 * @win: Window to hide
 */
void window_hide(window_t *win);

/**
 * window_minimize - Minimize a window
 * @win: Window to minimize
 */
void window_minimize(window_t *win);

/**
 * window_maximize - Maximize a window
 * @win: Window to maximize
 */
void window_maximize(window_t *win);

/**
 * window_restore - Restore minimized/maximized window
 * @win: Window to restore
 */
void window_restore(window_t *win);

/* ===================================================================== */
/* Window Operations */
/* ===================================================================== */

/**
 * window_move - Move window to new position
 * @win: Window to move
 * @x, @y: New position
 */
void window_move(window_t *win, int x, int y);

/**
 * window_resize - Resize window
 * @win: Window to resize
 * @width, @height: New size
 */
void window_resize(window_t *win, int width, int height);

/**
 * window_set_title - Change window title
 * @win: Window to modify
 * @title: New title
 */
void window_set_title(window_t *win, const char *title);

/**
 * window_set_flags - Change window flags
 * @win: Window to modify
 * @flags: New flags
 */
void window_set_flags(window_t *win, window_flags_t flags);

/* ===================================================================== */
/* Z-Order Management */
/* ===================================================================== */

/**
 * window_raise - Bring window to front
 * @win: Window to raise
 */
void window_raise(window_t *win);

/**
 * window_lower - Send window to back
 * @win: Window to lower
 */
void window_lower(window_t *win);

/**
 * window_raise_above - Raise window above another
 * @win: Window to raise
 * @above: Window to raise above
 */
void window_raise_above(window_t *win, window_t *above);

/**
 * window_z_order_changed - Notify that window z-order changed
 * @win: Window that changed
 */
void window_z_order_changed(window_t *win);

/* ===================================================================== */
/* Focus Management */
/* ===================================================================== */

/**
 * window_focus - Focus a window
 * @win: Window to focus
 */
void window_focus(window_t *win);

/**
 * window_unfocus - Unfocus current window
 */
void window_unfocus(void);

/**
 * window_get_focused - Get currently focused window
 * 
 * Return: Focused window, or NULL
 */
window_t *window_get_focused(void);

/**
 * window_is_focused - Check if window is focused
 * @win: Window to check
 * 
 * Return: 1 if focused, 0 otherwise
 */
int window_is_focused(window_t *win);

/**
 * window_can_focus - Check if window can receive focus
 * @win: Window to check
 * 
 * Return: 1 if can focus, 0 otherwise
 */
int window_can_focus(window_t *win);

/* ===================================================================== */
/* Hit Testing */
/* ===================================================================== */

typedef enum {
    HIT_NONE = 0,
    HIT_CLIENT,         /* Inside window client area */
    HIT_TITLEBAR,       /* Titlebar */
    HIT_BORDER_N,       /* North border (resize) */
    HIT_BORDER_S,       /* South border (resize) */
    HIT_BORDER_E,       /* East border (resize) */
    HIT_BORDER_W,       /* West border (resize) */
    HIT_CORNER_NW,      /* Northwest corner (resize) */
    HIT_CORNER_NE,      /* Northeast corner (resize) */
    HIT_CORNER_SW,      /* Southwest corner (resize) */
    HIT_CORNER_SE,      /* Southeast corner (resize) */
    HIT_CLOSE,          /* Close button */
    HIT_MINIMIZE,       /* Minimize button */
    HIT_MAXIMIZE,       /* Maximize button */
} hit_test_result_t;

/**
 * window_hit_test - Test which part of window is at coordinates
 * @win: Window to test
 * @x, @y: Screen coordinates
 * 
 * Return: Hit test result
 */
hit_test_result_t window_hit_test(window_t *win, int x, int y);

/**
 * window_at_position - Find window at screen position
 * @x, @y: Screen coordinates
 * 
 * Return: Topmost window at position, or NULL
 */
window_t *window_at_position(int x, int y);

/**
 * window_contains_point - Check if window contains point
 * @win: Window to check
 * @x, @y: Screen coordinates
 * 
 * Return: 1 if contains, 0 otherwise
 */
int window_contains_point(window_t *win, int x, int y);

/* ===================================================================== */
/* Window Content */
/* ===================================================================== */

/**
 * window_get_client_rect - Get window client area
 * @win: Window
 * @x, @y: Output for client origin
 * @width, @height: Output for client size
 */
void window_get_client_rect(window_t *win, int *x, int *y, 
                            int *width, int *height);

/**
 * window_get_buffer - Get window backbuffer
 * @win: Window
 * 
 * Return: Pointer to backbuffer, or NULL
 */
void *window_get_buffer(window_t *win);

/**
 * window_get_pitch - Get window buffer pitch
 * @win: Window
 * 
 * Return: Bytes per line
 */
int window_get_pitch(window_t *win);

/**
 * window_invalidate - Mark window area as needing redraw
 * @win: Window
 * @x, @y: Position in client coordinates
 * @width, @height: Size of area
 */
void window_invalidate(window_t *win, int x, int y, int width, int height);

/**
 * window_invalidate_all - Mark entire window as needing redraw
 * @win: Window
 */
void window_invalidate_all(window_t *win);

/* ===================================================================== */
/* Event Routing */
/* ===================================================================== */

/**
 * window_send_event - Send event to window owner
 * @win: Target window
 * @event_type: Type of event
 * @data: Event data
 * 
 * Return: 0 on success, negative on error
 */
int window_send_event(window_t *win, int event_type, void *data);

/**
 * window_broadcast_event - Send event to all windows
 * @event_type: Type of event
 * @data: Event data
 */
void window_broadcast_event(int event_type, void *data);

/* ===================================================================== */
/* Queries */
/* ===================================================================== */

/**
 * window_get - Get window by ID
 * @id: Window ID
 * 
 * Return: Window pointer, or NULL
 */
window_t *window_get(int id);

/**
 * window_get_by_owner - Get window by owner process
 * @owner: Owner process
 * 
 * Return: Window pointer, or NULL
 */
window_t *window_get_by_owner(process_t *owner);

/**
 * window_count - Get total number of windows
 * 
 * Return: Window count
 */
int window_count(void);

/**
 * window_dump_state - Dump window manager state for debugging
 */
void window_dump_state(void);

/* ===================================================================== */
/* Drag/Resize State Machine */
/* ===================================================================== */

/**
 * window_begin_drag - Start dragging a window
 * @win: Window to drag
 * @mouse_x, @mouse_y: Initial mouse position
 */
void window_begin_drag(window_t *win, int mouse_x, int mouse_y);

/**
 * window_update_drag - Update window drag position
 * @win: Window being dragged
 * @mouse_x, @mouse_y: Current mouse position
 */
void window_update_drag(window_t *win, int mouse_x, int mouse_y);

/**
 * window_end_drag - End window drag operation
 * @win: Window being dragged
 */
void window_end_drag(window_t *win);

/**
 * window_begin_resize - Start resizing a window
 * @win: Window to resize
 * @edge: Which edge/corner (from hit test)
 * @mouse_x, @mouse_y: Initial mouse position
 */
void window_begin_resize(window_t *win, int edge, int mouse_x, int mouse_y);

/**
 * window_update_resize - Update window resize
 * @win: Window being resized
 * @mouse_x, @mouse_y: Current mouse position
 */
void window_update_resize(window_t *win, int mouse_x, int mouse_y);

/**
 * window_end_resize - End window resize operation
 * @win: Window being resized
 */
void window_end_resize(window_t *win);

#endif /* _WINDOW_MANAGER_H */
