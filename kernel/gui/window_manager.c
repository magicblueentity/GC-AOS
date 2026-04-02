/*
 * GC-AOS - Modern Window Manager Implementation
 */

#include "gui/window_manager.h"
#include "printk.h"
#include <string.h>

/* Global window manager */
struct window_manager g_window_manager;

/* ===================================================================== */
/* Internal Helpers */
/* ===================================================================== */

static int alloc_window_id(void)
{
    spin_lock(&g_window_manager.lock);
    
    /* Find next available ID */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        int id = (g_window_manager.next_window_id + i) % MAX_WINDOWS;
        if (id == 0) continue; /* ID 0 is reserved */
        if (g_window_manager.windows[id] == NULL) {
            g_window_manager.next_window_id = (id + 1) % MAX_WINDOWS;
            spin_unlock(&g_window_manager.lock);
            return id;
        }
    }
    
    spin_unlock(&g_window_manager.lock);
    return -1; /* No ID available */
}

static window_t *alloc_window(void)
{
    window_t *win = kmalloc(sizeof(window_t));
    if (!win) {
        return NULL;
    }
    
    memset(win, 0, sizeof(window_t));
    INIT_LIST_HEAD(&win->list);
    INIT_LIST_HEAD(&win->z_list);
    
    return win;
}

static void free_window(window_t *win)
{
    if (win) {
        kfree(win);
    }
}

static void update_z_indices(void)
{
    int z = 0;
    window_t *win;
    
    /* Iterate from back to front */
    list_for_each_entry_reverse(win, &g_window_manager.z_ordered, z_list) {
        win->z_index = z++;
    }
    
    g_window_manager.max_z_index = z - 1;
}

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

void window_manager_init(int width, int height)
{
    printk(KERN_INFO "WM: Initializing window manager\n");
    
    memset(&g_window_manager, 0, sizeof(g_window_manager));
    spin_lock_init(&g_window_manager.lock);
    
    INIT_LIST_HEAD(&g_window_manager.all_windows);
    INIT_LIST_HEAD(&g_window_manager.z_ordered);
    INIT_LIST_HEAD(&g_window_manager.visible_windows);
    
    g_window_manager.desktop_width = width;
    g_window_manager.desktop_height = height;
    g_window_manager.next_window_id = 1;
    g_window_manager.focused_window = NULL;
    g_window_manager.window_count = 0;
    
    printk(KERN_INFO "WM: Window manager initialized (%dx%d)\n", width, height);
}

/* ===================================================================== */
/* Window Creation/Destruction */
/* ===================================================================== */

window_t *window_create(const char *title, int x, int y, 
                        int width, int height, window_flags_t flags,
                        process_t *owner)
{
    /* Validate parameters */
    if (!title) {
        title = "Untitled";
    }
    
    if (width < MIN_WINDOW_WIDTH) width = MIN_WINDOW_WIDTH;
    if (width > MAX_WINDOW_WIDTH) width = MAX_WINDOW_WIDTH;
    if (height < MIN_WINDOW_HEIGHT) height = MIN_WINDOW_HEIGHT;
    if (height > MAX_WINDOW_HEIGHT) height = MAX_WINDOW_HEIGHT;
    
    /* Clamp to desktop */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + width > g_window_manager.desktop_width) {
        x = g_window_manager.desktop_width - width;
    }
    if (y + height > g_window_manager.desktop_height) {
        y = g_window_manager.desktop_height - height;
    }
    
    /* Allocate window */
    window_t *win = alloc_window();
    if (!win) {
        printk(KERN_ERR "WM: Failed to allocate window\n");
        return NULL;
    }
    
    /* Allocate ID */
    int id = alloc_window_id();
    if (id < 0) {
        printk(KERN_ERR "WM: No window ID available\n");
        free_window(win);
        return NULL;
    }
    
    /* Initialize window */
    win->id = id;
    strncpy(win->title, title, WINDOW_TITLE_MAX - 1);
    win->title[WINDOW_TITLE_MAX - 1] = '\0';
    
    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->flags = flags;
    win->state = WINDOW_STATE_NORMAL;
    win->visible = 1;
    win->focused = 0;
    win->owner = owner;
    win->owner_pid = owner ? owner->pid : -1;
    
    /* Determine decorations */
    win->has_titlebar = !(flags & WINDOW_FLAG_BORDERLESS);
    win->has_border = !(flags & WINDOW_FLAG_BORDERLESS);
    
    /* Add to window manager */
    spin_lock(&g_window_manager.lock);
    
    g_window_manager.windows[id] = win;
    g_window_manager.window_count++;
    
    list_add(&win->list, &g_window_manager.all_windows);
    
    /* Add to z-order at front (top) */
    list_add(&win->z_list, &g_window_manager.z_ordered);
    update_z_indices();
    
    /* Add to visible list */
    list_add(&win->list, &g_window_manager.visible_windows);
    
    spin_unlock(&g_window_manager.lock);
    
    /* Focus new window (if not modal and can focus) */
    if (window_can_focus(win)) {
        window_focus(win);
    }
    
    printk(KERN_INFO "WM: Created window %d '%s' (%dx%d at %d,%d)\n",
           id, title, width, height, x, y);
    
    return win;
}

void window_destroy(window_t *win)
{
    if (!win) {
        return;
    }
    
    printk(KERN_INFO "WM: Destroying window %d '%s'\n", win->id, win->title);
    
    spin_lock(&g_window_manager.lock);
    
    /* Update focus if this window was focused */
    if (g_window_manager.focused_window == win) {
        g_window_manager.focused_window = NULL;
    }
    if (g_window_manager.previous_focus == win) {
        g_window_manager.previous_focus = NULL;
    }
    if (g_window_manager.modal_window == win) {
        g_window_manager.modal_window = NULL;
    }
    
    /* Remove from all lists */
    list_del(&win->list);
    list_del(&win->z_list);
    
    /* Remove from window table */
    g_window_manager.windows[win->id] = NULL;
    g_window_manager.window_count--;
    
    win->state = WINDOW_STATE_DESTROYED;
    
    spin_unlock(&g_window_manager.lock);
    
    /* Free window */
    free_window(win);
    
    /* Try to restore focus to previous window */
    if (g_window_manager.previous_focus && 
        g_window_manager.previous_focus->state != WINDOW_STATE_DESTROYED) {
        window_focus(g_window_manager.previous_focus);
    }
}

/* ===================================================================== */
/* Window State */
/* ===================================================================== */

void window_show(window_t *win)
{
    if (!win || win->state == WINDOW_STATE_DESTROYED) {
        return;
    }
    
    spin_lock(&g_window_manager.lock);
    win->visible = 1;
    if (win->state == WINDOW_STATE_HIDDEN) {
        win->state = WINDOW_STATE_NORMAL;
    }
    spin_unlock(&g_window_manager.lock);
    
    window_raise(win);
    window_invalidate_all(win);
}

void window_hide(window_t *win)
{
    if (!win || win->state == WINDOW_STATE_DESTROYED) {
        return;
    }
    
    spin_lock(&g_window_manager.lock);
    win->visible = 0;
    win->state = WINDOW_STATE_HIDDEN;
    
    /* Remove from visible list if present */
    list_del(&win->list);
    INIT_LIST_HEAD(&win->list);
    
    spin_unlock(&g_window_manager.lock);
    
    /* Unfocus if focused */
    if (window_is_focused(win)) {
        window_unfocus();
    }
}

void window_minimize(window_t *win)
{
    if (!win || win->state == WINDOW_STATE_DESTROYED) {
        return;
    }
    
    if (!(win->flags & WINDOW_FLAG_MINIMIZABLE)) {
        return;
    }
    
    /* Save current state */
    win->prev_x = win->x;
    win->prev_y = win->y;
    win->prev_width = win->width;
    win->prev_height = win->height;
    
    win->state = WINDOW_STATE_MINIMIZED;
    win->visible = 0;
    
    /* Remove from visible list */
    spin_lock(&g_window_manager.lock);
    list_del(&win->list);
    INIT_LIST_HEAD(&win->list);
    spin_unlock(&g_window_manager.lock);
    
    if (window_is_focused(win)) {
        window_unfocus();
    }
    
    /* Send minimize event to owner */
    window_send_event(win, EVENT_WINDOW_MINIMIZE, NULL);
}

void window_maximize(window_t *win)
{
    if (!win || win->state == WINDOW_STATE_DESTROYED) {
        return;
    }
    
    if (!(win->flags & WINDOW_FLAG_MAXIMIZABLE)) {
        return;
    }
    
    if (win->state == WINDOW_STATE_MAXIMIZED) {
        return; /* Already maximized */
    }
    
    /* Save current position/size */
    win->prev_x = win->x;
    win->prev_y = win->y;
    win->prev_width = win->width;
    win->prev_height = win->height;
    
    /* Maximize to desktop (minus dock/menubar if present) */
    win->x = 0;
    win->y = 28; /* Below menu bar */
    win->width = g_window_manager.desktop_width;
    win->height = g_window_manager.desktop_height - 28 - 70; /* Minus menubar and dock */
    
    win->state = WINDOW_STATE_MAXIMIZED;
    
    window_invalidate_all(win);
    window_send_event(win, EVENT_WINDOW_MAXIMIZE, NULL);
}

void window_restore(window_t *win)
{
    if (!win || win->state == WINDOW_STATE_DESTROYED) {
        return;
    }
    
    if (win->state == WINDOW_STATE_MINIMIZED) {
        win->state = WINDOW_STATE_NORMAL;
        win->visible = 1;
        
        /* Add back to visible list */
        spin_lock(&g_window_manager.lock);
        list_add(&win->list, &g_window_manager.visible_windows);
        spin_unlock(&g_window_manager.lock);
        
        window_raise(win);
        window_focus(win);
        window_send_event(win, EVENT_WINDOW_RESTORE, NULL);
    }
    else if (win->state == WINDOW_STATE_MAXIMIZED) {
        /* Restore previous position/size */
        win->x = win->prev_x;
        win->y = win->prev_y;
        win->width = win->prev_width;
        win->height = win->prev_height;
        win->state = WINDOW_STATE_NORMAL;
        
        window_invalidate_all(win);
        window_send_event(win, EVENT_WINDOW_RESTORE, NULL);
    }
}

/* ===================================================================== */
/* Window Operations */
/* ===================================================================== */

void window_move(window_t *win, int x, int y)
{
    if (!win || !(win->flags & WINDOW_FLAG_MOVABLE)) {
        return;
    }
    
    /* Clamp to desktop */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + win->width > g_window_manager.desktop_width) {
        x = g_window_manager.desktop_width - win->width;
    }
    if (y + win->height > g_window_manager.desktop_height) {
        y = g_window_manager.desktop_height - win->height;
    }
    
    spin_lock(&g_window_manager.lock);
    win->x = x;
    win->y = y;
    spin_unlock(&g_window_manager.lock);
    
    window_invalidate_all(win);
}

void window_resize(window_t *win, int width, int height)
{
    if (!win || !(win->flags & WINDOW_FLAG_RESIZABLE)) {
        return;
    }
    
    /* Clamp size */
    if (width < MIN_WINDOW_WIDTH) width = MIN_WINDOW_WIDTH;
    if (width > MAX_WINDOW_WIDTH) width = MAX_WINDOW_WIDTH;
    if (height < MIN_WINDOW_HEIGHT) height = MIN_WINDOW_HEIGHT;
    if (height > MAX_WINDOW_HEIGHT) height = MAX_WINDOW_HEIGHT;
    
    spin_lock(&g_window_manager.lock);
    win->width = width;
    win->height = height;
    spin_unlock(&g_window_manager.lock);
    
    /* Notify compositor of resize */
    window_invalidate_all(win);
    window_send_event(win, EVENT_WINDOW_RESIZE, NULL);
}

void window_set_title(window_t *win, const char *title)
{
    if (!win || !title) {
        return;
    }
    
    spin_lock(&g_window_manager.lock);
    strncpy(win->title, title, WINDOW_TITLE_MAX - 1);
    win->title[WINDOW_TITLE_MAX - 1] = '\0';
    spin_unlock(&g_window_manager.lock);
    
    /* Invalidate titlebar area */
    if (win->has_titlebar) {
        window_invalidate(win, 0, 0, win->width, WINDOW_TITLEBAR_HEIGHT);
    }
}

void window_set_flags(window_t *win, window_flags_t flags)
{
    if (!win) {
        return;
    }
    
    spin_lock(&g_window_manager.lock);
    win->flags = flags;
    win->has_titlebar = !(flags & WINDOW_FLAG_BORDERLESS);
    win->has_border = !(flags & WINDOW_FLAG_BORDERLESS);
    spin_unlock(&g_window_manager.lock);
    
    window_invalidate_all(win);
}

/* ===================================================================== */
/* Z-Order Management */
/* ===================================================================== */

void window_raise(window_t *win)
{
    if (!win) {
        return;
    }
    
    spin_lock(&g_window_manager.lock);
    
    /* Remove from current position */
    list_del(&win->z_list);
    
    /* Add to front (top) */
    list_add(&win->z_list, &g_window_manager.z_ordered);
    
    update_z_indices();
    
    spin_unlock(&g_window_manager.lock);
    
    /* Focus raised window */
    window_focus(win);
    
    /* Notify compositor */
    window_z_order_changed(win);
}

void window_lower(window_t *win)
{
    if (!win) {
        return;
    }
    
    spin_lock(&g_window_manager.lock);
    
    /* Remove from current position */
    list_del(&win->z_list);
    
    /* Add to back (bottom) */
    list_add_tail(&win->z_list, &g_window_manager.z_ordered);
    
    update_z_indices();
    
    spin_unlock(&g_window_manager.lock);
    
    window_z_order_changed(win);
}

void window_raise_above(window_t *win, window_t *above)
{
    if (!win || !above) {
        return;
    }
    
    spin_lock(&g_window_manager.lock);
    
    /* Remove from current position */
    list_del(&win->z_list);
    
    /* Add after 'above' window */
    list_add(&win->z_list, &above->z_list);
    
    update_z_indices();
    
    spin_unlock(&g_window_manager.lock);
    
    window_z_order_changed(win);
}

void window_z_order_changed(window_t *win)
{
    (void)win;
    /* Compositor will handle this - mark all windows as needing redraw */
    /* This is a notification, not an action */
}

/* ===================================================================== */
/* Focus Management */
/* ===================================================================== */

void window_focus(window_t *win)
{
    if (!win || win->state == WINDOW_STATE_DESTROYED) {
        return;
    }
    
    if (!window_can_focus(win)) {
        return;
    }
    
    /* If modal window exists, only allow focusing it */
    if (g_window_manager.modal_window && win != g_window_manager.modal_window) {
        return;
    }
    
    spin_lock(&g_window_manager.lock);
    
    /* Save previous focus */
    if (g_window_manager.focused_window && 
        g_window_manager.focused_window != win) {
        g_window_manager.previous_focus = g_window_manager.focused_window;
        g_window_manager.focused_window->focused = 0;
    }
    
    /* Set new focus */
    g_window_manager.focused_window = win;
    win->focused = 1;
    
    spin_unlock(&g_window_manager.lock);
    
    /* Raise focused window */
    window_raise(win);
    
    /* Send focus event */
    window_send_event(win, EVENT_WINDOW_FOCUS, NULL);
}

void window_unfocus(void)
{
    spin_lock(&g_window_manager.lock);
    
    if (g_window_manager.focused_window) {
        g_window_manager.focused_window->focused = 0;
        g_window_manager.previous_focus = g_window_manager.focused_window;
        window_send_event(g_window_manager.focused_window, EVENT_WINDOW_UNFOCUS, NULL);
    }
    
    g_window_manager.focused_window = NULL;
    
    spin_unlock(&g_window_manager.lock);
}

window_t *window_get_focused(void)
{
    return g_window_manager.focused_window;
}

int window_is_focused(window_t *win)
{
    if (!win) {
        return 0;
    }
    return win->focused;
}

int window_can_focus(window_t *win)
{
    if (!win) {
        return 0;
    }
    
    return win->visible && 
           win->state != WINDOW_STATE_DESTROYED &&
           win->state != WINDOW_STATE_MINIMIZED;
}

/* ===================================================================== */
/* Hit Testing */
/* ===================================================================== */

hit_test_result_t window_hit_test(window_t *win, int x, int y)
{
    if (!win || !win->visible) {
        return HIT_NONE;
    }
    
    /* Convert to window coordinates */
    int wx = x - win->x;
    int wy = y - win->y;
    
    /* Check if inside window bounds */
    int total_width = win->width + (win->has_border ? WINDOW_BORDER_WIDTH * 2 : 0);
    int total_height = win->height + (win->has_titlebar ? WINDOW_TITLEBAR_HEIGHT : 0) +
                      (win->has_border ? WINDOW_BORDER_WIDTH : 0);
    
    if (wx < 0 || wx >= total_width || wy < 0 || wy >= total_height) {
        return HIT_NONE;
    }
    
    /* Titlebar area */
    if (win->has_titlebar && wy < WINDOW_TITLEBAR_HEIGHT) {
        /* Traffic light buttons (macOS style) */
        int btn_y = 6;
        int btn_size = 12;
        int close_x = 12;
        int min_x = 32;
        int max_x = 52;
        
        if (wy >= btn_y && wy < btn_y + btn_size) {
            if (wx >= close_x && wx < close_x + btn_size) {
                return HIT_CLOSE;
            }
            if ((win->flags & WINDOW_FLAG_MINIMIZABLE) &&
                wx >= min_x && wx < min_x + btn_size) {
                return HIT_MINIMIZE;
            }
            if ((win->flags & WINDOW_FLAG_MAXIMIZABLE) &&
                wx >= max_x && wx < max_x + btn_size) {
                return HIT_MAXIMIZE;
            }
        }
        
        return HIT_TITLEBAR;
    }
    
    /* Client area */
    int client_y = win->has_titlebar ? WINDOW_TITLEBAR_HEIGHT : 0;
    
    if (!win->has_border) {
        return HIT_CLIENT;
    }
    
    /* Border areas (for resizing) */
    int border = WINDOW_BORDER_WIDTH;
    int resize = WINDOW_RESIZE_HANDLE;
    int right = win->width + border;
    int bottom = win->height + client_y + border;
    
    /* Corners */
    if (wx < resize && wy < client_y + resize) {
        return HIT_CORNER_NW;
    }
    if (wx >= win->width + border - resize && wy < client_y + resize) {
        return HIT_CORNER_NE;
    }
    if (wx < resize && wy >= bottom - resize) {
        return HIT_CORNER_SW;
    }
    if (wx >= right - resize && wy >= bottom - resize) {
        return HIT_CORNER_SE;
    }
    
    /* Edges */
    if (wy < client_y + border) {
        return HIT_BORDER_N;
    }
    if (wy >= bottom - border) {
        return HIT_BORDER_S;
    }
    if (wx < border) {
        return HIT_BORDER_W;
    }
    if (wx >= right - border) {
        return HIT_BORDER_E;
    }
    
    return HIT_CLIENT;
}

window_t *window_at_position(int x, int y)
{
    window_t *win;
    
    /* Search from front to back */
    list_for_each_entry(win, &g_window_manager.z_ordered, z_list) {
        if (window_contains_point(win, x, y)) {
            return win;
        }
    }
    
    return NULL;
}

int window_contains_point(window_t *win, int x, int y)
{
    if (!win || !win->visible) {
        return 0;
    }
    
    int total_width = win->width + (win->has_border ? WINDOW_BORDER_WIDTH * 2 : 0);
    int total_height = win->height + (win->has_titlebar ? WINDOW_TITLEBAR_HEIGHT : 0) +
                      (win->has_border ? WINDOW_BORDER_WIDTH : 0);
    
    return (x >= win->x && x < win->x + total_width &&
            y >= win->y && y < win->y + total_height);
}

/* ===================================================================== */
/* Window Content */
/* ===================================================================== */

void window_get_client_rect(window_t *win, int *x, int *y, int *width, int *height)
{
    if (!win) {
        return;
    }
    
    if (x) *x = win->x + (win->has_border ? WINDOW_BORDER_WIDTH : 0);
    if (y) *y = win->y + (win->has_titlebar ? WINDOW_TITLEBAR_HEIGHT : 0);
    if (width) *width = win->width;
    if (height) *height = win->height;
}

void *window_get_buffer(window_t *win)
{
    if (!win) {
        return NULL;
    }
    return win->backbuffer;
}

int window_get_pitch(window_t *win)
{
    if (!win) {
        return 0;
    }
    return win->buffer_pitch;
}

void window_invalidate(window_t *win, int x, int y, int width, int height)
{
    (void)win;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    /* Delegated to compositor - mark region as dirty */
    /* This will be implemented in the compositor module */
}

void window_invalidate_all(window_t *win)
{
    if (!win) {
        return;
    }
    window_invalidate(win, 0, 0, win->width, win->height);
}

/* ===================================================================== */
/* Event Routing */
/* ===================================================================== */

int window_send_event(window_t *win, int event_type, void *data)
{
    if (!win || !win->owner) {
        return -1;
    }
    
    /* TODO: Send event to process via IPC/event system */
    (void)event_type;
    (void)data;
    
    return 0;
}

void window_broadcast_event(int event_type, void *data)
{
    window_t *win;
    
    list_for_each_entry(win, &g_window_manager.all_windows, list) {
        if (win->state != WINDOW_STATE_DESTROYED && win->owner) {
            window_send_event(win, event_type, data);
        }
    }
}

/* ===================================================================== */
/* Queries */
/* ===================================================================== */

window_t *window_get(int id)
{
    if (id < 0 || id >= MAX_WINDOWS) {
        return NULL;
    }
    return g_window_manager.windows[id];
}

window_t *window_get_by_owner(process_t *owner)
{
    if (!owner) {
        return NULL;
    }
    
    window_t *win;
    list_for_each_entry(win, &g_window_manager.all_windows, list) {
        if (win->owner == owner) {
            return win;
        }
    }
    
    return NULL;
}

int window_count(void)
{
    return g_window_manager.window_count;
}

void window_dump_state(void)
{
    printk(KERN_INFO "=== Window Manager State ===\n");
    printk(KERN_INFO "Windows: %d, Desktop: %dx%d\n",
           g_window_manager.window_count,
           g_window_manager.desktop_width,
           g_window_manager.desktop_height);
    printk(KERN_INFO "Focused: %s (ID %d)\n",
           g_window_manager.focused_window ? 
               g_window_manager.focused_window->title : "none",
           g_window_manager.focused_window ? 
               g_window_manager.focused_window->id : -1);
    
    window_t *win;
    list_for_each_entry(win, &g_window_manager.z_ordered, z_list) {
        printk(KERN_INFO "  [%d] '%s' at (%d,%d) %dx%d, z=%d, state=%d\n",
               win->id, win->title, win->x, win->y,
               win->width, win->height, win->z_index, win->state);
    }
}

/* ===================================================================== */
/* Drag/Resize State Machine */
/* ===================================================================== */

void window_begin_drag(window_t *win, int mouse_x, int mouse_y)
{
    if (!win || !(win->flags & WINDOW_FLAG_MOVABLE)) {
        return;
    }
    
    win->is_dragging = 1;
    win->drag_start_x = mouse_x - win->x;
    win->drag_start_y = mouse_y - win->y;
}

void window_update_drag(window_t *win, int mouse_x, int mouse_y)
{
    if (!win || !win->is_dragging) {
        return;
    }
    
    int new_x = mouse_x - win->drag_start_x;
    int new_y = mouse_y - win->drag_start_y;
    
    window_move(win, new_x, new_y);
}

void window_end_drag(window_t *win)
{
    if (!win) {
        return;
    }
    
    win->is_dragging = 0;
}

void window_begin_resize(window_t *win, int edge, int mouse_x, int mouse_y)
{
    if (!win || !(win->flags & WINDOW_FLAG_RESIZABLE)) {
        return;
    }
    
    win->is_resizing = 1;
    win->resize_edge = edge;
    win->drag_start_x = mouse_x;
    win->drag_start_y = mouse_y;
    win->resize_start_w = win->width;
    win->resize_start_h = win->height;
}

void window_update_resize(window_t *win, int mouse_x, int mouse_y)
{
    if (!win || !win->is_resizing) {
        return;
    }
    
    int dx = mouse_x - win->drag_start_x;
    int dy = mouse_y - win->drag_start_y;
    
    int new_x = win->x;
    int new_y = win->y;
    int new_w = win->width;
    int new_h = win->height;
    
    switch (win->resize_edge) {
        case HIT_BORDER_E:
        case HIT_CORNER_NE:
        case HIT_CORNER_SE:
            new_w = win->resize_start_w + dx;
            break;
        case HIT_BORDER_W:
        case HIT_CORNER_NW:
        case HIT_CORNER_SW:
            new_w = win->resize_start_w - dx;
            new_x = win->x + dx;
            break;
    }
    
    switch (win->resize_edge) {
        case HIT_BORDER_S:
        case HIT_CORNER_SW:
        case HIT_CORNER_SE:
            new_h = win->resize_start_h + dy;
            break;
        case HIT_BORDER_N:
        case HIT_CORNER_NW:
        case HIT_CORNER_NE:
            new_h = win->resize_start_h - dy;
            new_y = win->y + dy;
            break;
    }
    
    /* Clamp position and size */
    if (new_w < MIN_WINDOW_WIDTH) {
        new_w = MIN_WINDOW_WIDTH;
        if (win->resize_edge == HIT_BORDER_W || 
            win->resize_edge == HIT_CORNER_NW ||
            win->resize_edge == HIT_CORNER_SW) {
            new_x = win->x + win->width - MIN_WINDOW_WIDTH;
        }
    }
    
    if (new_h < MIN_WINDOW_HEIGHT) {
        new_h = MIN_WINDOW_HEIGHT;
        if (win->resize_edge == HIT_BORDER_N || 
            win->resize_edge == HIT_CORNER_NW ||
            win->resize_edge == HIT_CORNER_NE) {
            new_y = win->y + win->height - MIN_WINDOW_HEIGHT;
        }
    }
    
    win->x = new_x;
    win->y = new_y;
    window_resize(win, new_w, new_h);
}

void window_end_resize(window_t *win)
{
    if (!win) {
        return;
    }
    
    win->is_resizing = 0;
    win->resize_edge = HIT_NONE;
}
