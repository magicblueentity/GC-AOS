/*
 * GC-AOS - Global Event System Implementation
 */

#include "kernel/event_system.h"
#include "gui/window_manager.h"
#include "mm/kmalloc.h"
#include "printk.h"
#include <string.h>

/* ===================================================================== */
/* Missing Symbol Declarations */
/* ===================================================================== */

/* Timer function */
uint64_t get_time_ns(void)
{
    /* Simple timestamp implementation */
    static uint64_t counter = 0;
    return ++counter * 1000000; /* Return nanoseconds */
}

/* Key constants */
#define KEY_ESCAPE     27
#define KEY_ENTER      13
#define KEY_SPACE      ' '
#define KEY_BACKSPACE  8

/* ===================================================================== */
/* Global Event System */
/* ===================================================================== */

/* Global event system */
struct event_system g_event_system;

/* ===================================================================== */
/* Internal Helpers */
/* ===================================================================== */

static uint32_t alloc_event_id(void)
{
    return g_event_system.next_event_id++;
}

static event_t *alloc_event_from_pool(void)
{
    spin_lock(&g_event_system.queue.lock);
    
    event_t *event = NULL;
    if (!list_empty(&g_event_system.queue.free_list)) {
        event = list_first_entry(&g_event_system.queue.free_list, event_t, list);
        list_del(&event->list);
        g_event_system.queue.count++;
    }
    
    spin_unlock(&g_event_system.queue.lock);
    return event;
}

static void free_event_to_pool(event_t *event)
{
    spin_lock(&g_event_system.queue.lock);
    
    list_add(&event->list, &g_event_system.queue.free_list);
    if (g_event_system.queue.count > 0) {
        g_event_system.queue.count--;
    }
    
    spin_unlock(&g_event_system.queue.lock);
}

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

void event_system_init(void)
{
    printk(KERN_INFO "EVENT: Initializing event system\n");
    
    memset(&g_event_system, 0, sizeof(g_event_system));
    
    /* Initialize queue */
    spin_lock_init(&g_event_system.queue.lock);
    INIT_LIST_HEAD(&g_event_system.queue.free_list);
    INIT_LIST_HEAD(&g_event_system.queue.pending_list);
    
    for (int i = 0; i < EVENT_QUEUE_SIZE; i++) {
        list_add(&g_event_system.queue.events[i].list, &g_event_system.queue.free_list);
    }
    
    /* Initialize handlers */
    INIT_LIST_HEAD(&g_event_system.handlers);
    for (int i = 0; i < EVENT_USER; i++) {
        INIT_LIST_HEAD(&g_event_system.type_handlers[i]);
    }
    
    g_event_system.enabled = 1;
    
    printk(KERN_INFO "EVENT: Event system initialized (pool=%d)\n", EVENT_QUEUE_SIZE);
}

void event_system_shutdown(void)
{
    g_event_system.enabled = 0;
    
    /* Free all pending events */
    spin_lock(&g_event_system.queue.lock);
    
    event_t *event, *tmp;
    list_for_each_entry_safe(event, tmp, &g_event_system.queue.pending_list, list) {
        list_del(&event->list);
        free_event_to_pool(event);
    }
    
    spin_unlock(&g_event_system.queue.lock);
    
    printk(KERN_INFO "EVENT: Event system shutdown\n");
}

void event_system_enable(void)
{
    g_event_system.enabled = 1;
}

void event_system_disable(void)
{
    g_event_system.enabled = 0;
}

/* ===================================================================== */
/* Event Creation */
/* ===================================================================== */

event_t *event_create(event_type_t type)
{
    event_t *event = alloc_event_from_pool();
    if (!event) {
        g_event_system.events_dropped++;
        return NULL;
    }
    
    memset(event, 0, sizeof(event_t));
    event->type = type;
    event->id = alloc_event_id();
    event->timestamp = get_time_ns();
    event->target_window = -1;
    event->target_process = -1;
    INIT_LIST_HEAD(&event->list);
    
    return event;
}

void event_free(event_t *event)
{
    if (event) {
        free_event_to_pool(event);
    }
}

void event_set_target(event_t *event, int window_id, int process_id)
{
    if (!event) {
        return;
    }
    
    event->target_window = window_id;
    event->target_process = process_id;
}

/* ===================================================================== */
/* Event Posting */
/* ===================================================================== */

int event_post(event_t *event)
{
    if (!event || !g_event_system.enabled) {
        return -1;
    }
    
    spin_lock(&g_event_system.queue.lock);
    
    list_add_tail(&event->list, &g_event_system.queue.pending_list);
    g_event_system.events_posted++;
    
    spin_unlock(&g_event_system.queue.lock);
    
    return 0;
}

int event_post_keyboard(event_type_t type, int keycode, int scancode,
                        uint32_t modifiers, char character)
{
    event_t *event = event_create(type);
    if (!event) {
        return -1;
    }
    
    event->data.keyboard.keycode = keycode;
    event->data.keyboard.scancode = scancode;
    event->data.keyboard.modifiers = modifiers;
    event->data.keyboard.character = character;
    
    /* Route to focused window */
    window_t *focused = window_get_focused();
    if (focused) {
        event->target_window = focused->id;
        if (focused->owner) {
            event->target_process = focused->owner->pid;
        }
    }
    
    return event_post(event);
}

int event_post_mouse(event_type_t type, int x, int y, int button,
                     uint32_t modifiers)
{
    event_t *event = event_create(type);
    if (!event) {
        return -1;
    }
    
    event->data.mouse.x = x;
    event->data.mouse.y = y;
    event->data.mouse.button = button;
    event->data.mouse.modifiers = modifiers;
    
    /* Find window under cursor for routing */
    window_t *win = window_at_position(x, y);
    if (win) {
        event->target_window = win->id;
        if (win->owner) {
            event->target_process = win->owner->pid;
        }
    }
    
    return event_post(event);
}

int event_post_window(event_type_t type, int window_id, 
                      int x, int y, int width, int height)
{
    event_t *event = event_create(type);
    if (!event) {
        return -1;
    }
    
    event->target_window = window_id;
    event->data.window.window_id = window_id;
    event->data.window.x = x;
    event->data.window.y = y;
    event->data.window.width = width;
    event->data.window.height = height;
    
    /* Find owner process */
    window_t *win = window_get(window_id);
    if (win && win->owner) {
        event->target_process = win->owner->pid;
    }
    
    return event_post(event);
}

int event_post_system(event_type_t type)
{
    event_t *event = event_create(type);
    if (!event) {
        return -1;
    }
    
    event->data.system.timestamp = get_time_ns();
    
    return event_post(event);
}

/* ===================================================================== */
/* Event Dispatch */
/* ===================================================================== */

void event_dispatch(event_t *event)
{
    if (!event) {
        return;
    }
    
    int handled = 0;
    
    /* Call global handlers first */
    event_handler_entry_t *handler;
    list_for_each_entry(handler, &g_event_system.handlers, list) {
        if (handler->type == (event_type_t)-1 || handler->type == event->type) {
            int ret = handler->handler(event, handler->user_data);
            if (ret == 0) {
                handled = 1;
                break; /* Handler consumed event */
            }
        }
    }
    
    /* Call type-specific handlers */
    if (!handled && event->type < EVENT_USER) {
        list_for_each_entry(handler, &g_event_system.type_handlers[event->type], list) {
            int ret = handler->handler(event, handler->user_data);
            if (ret == 0) {
                handled = 1;
                break;
            }
        }
    }
    
    /* Call custom router if set */
    if (!handled && g_event_system.router) {
        handled = g_event_system.router(event);
    }
    
    g_event_system.events_dispatched++;
    
    /* Free event */
    event_free(event);
}

void event_dispatch_all(void)
{
    while (1) {
        spin_lock(&g_event_system.queue.lock);
        
        if (list_empty(&g_event_system.queue.pending_list)) {
            spin_unlock(&g_event_system.queue.lock);
            break;
        }
        
        event_t *event = list_first_entry(&g_event_system.queue.pending_list, 
                                          event_t, list);
        list_del(&event->list);
        
        spin_unlock(&g_event_system.queue.lock);
        
        /* Dispatch outside of lock */
        event_dispatch(event);
    }
}

event_t *event_poll(int timeout_ms)
{
    (void)timeout_ms; /* TODO: Implement timeout */
    
    spin_lock(&g_event_system.queue.lock);
    
    if (list_empty(&g_event_system.queue.pending_list)) {
        spin_unlock(&g_event_system.queue.lock);
        return NULL;
    }
    
    event_t *event = list_first_entry(&g_event_system.queue.pending_list, 
                                      event_t, list);
    list_del(&event->list);
    
    spin_unlock(&g_event_system.queue.lock);
    
    return event;
}

/* ===================================================================== */
/* Event Handlers */
/* ===================================================================== */

int event_register_handler(event_type_t type, event_handler_t handler,
                           void *user_data, int priority)
{
    if (!handler) {
        return -1;
    }
    
    event_handler_entry_t *entry = kmalloc(sizeof(event_handler_entry_t));
    if (!entry) {
        return -2;
    }
    
    entry->type = type;
    entry->handler = handler;
    entry->user_data = user_data;
    entry->priority = priority;
    INIT_LIST_HEAD(&entry->list);
    
    /* Add to appropriate list (sorted by priority) */
    struct list_head *head;
    if (type == (event_type_t)-1) {
        head = &g_event_system.handlers;
    } else if (type < EVENT_USER) {
        head = &g_event_system.type_handlers[type];
    } else {
        kfree(entry);
        return -3; /* Invalid type */
    }
    
    /* Insert in priority order */
    event_handler_entry_t *pos;
    list_for_each_entry(pos, head, list) {
        if (pos->priority < priority) {
            list_add_tail(&entry->list, &pos->list);
            return (int)(uintptr_t)entry; /* Use pointer as ID */
        }
    }
    
    /* Add to end if lowest priority */
    list_add_tail(&entry->list, head);
    
    return (int)(uintptr_t)entry;
}

int event_unregister_handler(int handler_id)
{
    if (handler_id == 0) {
        return -1;
    }
    
    event_handler_entry_t *entry = (event_handler_entry_t *)(uintptr_t)handler_id;
    
    list_del(&entry->list);
    kfree(entry);
    
    return 0;
}

void event_set_router(int (*router)(event_t *event))
{
    g_event_system.router = router;
}

/* ===================================================================== */
/* Input Integration */
/* ===================================================================== */

/* Simple scancode to keycode mapping (subset) */
static int scancode_to_keycode(int scancode)
{
    /* This would be a full mapping table in production */
    switch (scancode) {
        case 0x01: return KEY_ESCAPE;
        case 0x02: return '1';
        case 0x03: return '2';
        case 0x1E: return 'a';
        case 0x30: return 'b';
        case 0x2E: return 'c';
        case 0x20: return 'd';
        /* ... more mappings ... */
        case 0x1C: return KEY_ENTER;
        case 0x39: return KEY_SPACE;
        case 0x0E: return KEY_BACKSPACE;
        default: return scancode;
    }
}

void event_input_keyboard(int scancode, int pressed)
{
    int keycode = scancode_to_keycode(scancode);
    
    event_type_t type = pressed ? EVENT_KEY_DOWN : EVENT_KEY_UP;
    
    /* Get modifiers */
    uint32_t modifiers = 0;
    /* TODO: Track modifier state */
    
    /* Determine character */
    char character = 0;
    if (pressed && keycode < 128) {
        character = (char)keycode;
        /* TODO: Apply shift state for uppercase */
    }
    
    event_post_keyboard(type, keycode, scancode, modifiers, character);
}

/* Previous mouse state for delta calculation */
static int prev_mouse_x = 0;
static int prev_mouse_y = 0;
static uint32_t prev_button_mask = 0;

void event_input_mouse(int x, int y, uint32_t button_mask, int delta_z)
{
    /* Post move event if position changed */
    if (x != prev_mouse_x || y != prev_mouse_y) {
        event_t *event = event_create(EVENT_MOUSE_MOVE);
        if (event) {
            event->data.mouse.x = x;
            event->data.mouse.y = y;
            event->data.mouse.delta_x = x - prev_mouse_x;
            event->data.mouse.delta_y = y - prev_mouse_y;
            event_post(event);
        }
    }
    
    /* Check button changes */
    for (int i = 0; i < 5; i++) {
        int was_pressed = (prev_button_mask >> i) & 1;
        int is_pressed = (button_mask >> i) & 1;
        
        if (is_pressed && !was_pressed) {
            event_post_mouse(EVENT_MOUSE_DOWN, x, y, i, 0);
        } else if (!is_pressed && was_pressed) {
            event_post_mouse(EVENT_MOUSE_UP, x, y, i, 0);
        }
    }
    
    /* Post scroll event */
    if (delta_z != 0) {
        event_t *event = event_create(EVENT_MOUSE_SCROLL);
        if (event) {
            event->data.mouse.x = x;
            event->data.mouse.y = y;
            event->data.mouse.scroll_delta = delta_z;
            event_post(event);
        }
    }
    
    prev_mouse_x = x;
    prev_mouse_y = y;
    prev_button_mask = button_mask;
}

/* ===================================================================== */
/* Stats */
/* ===================================================================== */

void event_get_stats(uint64_t *posted, uint64_t *dispatched, uint64_t *dropped)
{
    if (posted) *posted = g_event_system.events_posted;
    if (dispatched) *dispatched = g_event_system.events_dispatched;
    if (dropped) *dropped = g_event_system.events_dropped;
}

void event_dump_stats(void)
{
    printk(KERN_INFO "=== Event System Stats ===\n");
    printk(KERN_INFO "Posted: %llu, Dispatched: %llu, Dropped: %llu\n",
           g_event_system.events_posted,
           g_event_system.events_dispatched,
           g_event_system.events_dropped);
    printk(KERN_INFO "Queue: %d/%d\n",
           g_event_system.queue.count, EVENT_QUEUE_SIZE);
    printk(KERN_INFO "Enabled: %d\n", g_event_system.enabled);
}
