/*
 * GC-AOS - Global Event System
 * 
 * Central event bus for all input and system events.
 * All input MUST go through this system.
 */

#ifndef _EVENT_SYSTEM_H
#define _EVENT_SYSTEM_H

#include "types.h"
#include "kernel/list.h"
#include "sync/spinlock.h"

/* ===================================================================== */
/* Event Types */
/* ===================================================================== */

typedef enum {
    /* Keyboard events */
    EVENT_KEY_DOWN = 0,
    EVENT_KEY_UP,
    EVENT_KEY_REPEAT,
    
    /* Mouse events */
    EVENT_MOUSE_MOVE,
    EVENT_MOUSE_DOWN,
    EVENT_MOUSE_UP,
    EVENT_MOUSE_SCROLL,
    EVENT_MOUSE_ENTER,
    EVENT_MOUSE_LEAVE,
    
    /* Window events */
    EVENT_WINDOW_CREATE,
    EVENT_WINDOW_DESTROY,
    EVENT_WINDOW_SHOW,
    EVENT_WINDOW_HIDE,
    EVENT_WINDOW_FOCUS,
    EVENT_WINDOW_UNFOCUS,
    EVENT_WINDOW_MOVE,
    EVENT_WINDOW_RESIZE,
    EVENT_WINDOW_MINIMIZE,
    EVENT_WINDOW_MAXIMIZE,
    EVENT_WINDOW_RESTORE,
    EVENT_WINDOW_CLOSE,
    
    /* System events */
    EVENT_SYSTEM_STARTUP,
    EVENT_SYSTEM_SHUTDOWN,
    EVENT_SYSTEM_SUSPEND,
    EVENT_SYSTEM_RESUME,
    EVENT_SYSTEM_TIMER,
    EVENT_SYSTEM_IDLE,
    
    /* Process events */
    EVENT_PROCESS_CREATE,
    EVENT_PROCESS_EXIT,
    EVENT_PROCESS_CRASH,
    
    /* User-defined events start here */
    EVENT_USER = 1000
} event_type_t;

/* ===================================================================== */
/* Event Modifiers */
/* ===================================================================== */

#define MOD_SHIFT       (1 << 0)
#define MOD_CTRL        (1 << 1)
#define MOD_ALT         (1 << 2)
#define MOD_META        (1 << 3)
#define MOD_CAPS_LOCK   (1 << 4)
#define MOD_NUM_LOCK    (1 << 5)

/* ===================================================================== */
/* Mouse Buttons */
/* ===================================================================== */

#define MOUSE_BUTTON_LEFT       0
#define MOUSE_BUTTON_RIGHT      1
#define MOUSE_BUTTON_MIDDLE     2
#define MOUSE_BUTTON_X1         3
#define MOUSE_BUTTON_X2         4

/* ===================================================================== */
/* Event Data Structures */
/* ===================================================================== */

typedef struct {
    int keycode;            /* Platform keycode */
    int scancode;           /* Physical scancode */
    uint32_t modifiers;
    char character;         /* ASCII character (if printable) */
} event_keyboard_data_t;

typedef struct {
    int x, y;               /* Screen coordinates */
    int delta_x, delta_y;   /* Movement delta (for MOVE) */
    int button;             /* Button index (for DOWN/UP) */
    int pressed;            /* 1 = pressed, 0 = released */
    int click_count;        /* For double-click detection */
    int scroll_delta;       /* For SCROLL event */
    uint32_t modifiers;
} event_mouse_data_t;

typedef struct {
    int window_id;
    int x, y;
    int width, height;
    int prev_x, prev_y;
    int prev_width, prev_height;
} event_window_data_t;

typedef struct {
    int pid;
    int exit_code;
    const char *name;
} event_process_data_t;

typedef struct {
    uint64_t timestamp;
    uint64_t delta_time;
} event_system_data_t;

/* ===================================================================== */
/* Event Structure */
/* ===================================================================== */

#define EVENT_MAX_SIZE 128

typedef struct event {
    /* Header */
    event_type_t type;
    uint32_t id;
    uint64_t timestamp;
    
    /* Source */
    int source_type;        /* 0 = kernel, 1 = driver, 2 = process */
    int source_id;
    
    /* Target (for routing) */
    int target_window;
    int target_process;
    
    /* Data (type-specific) */
    union {
        event_keyboard_data_t keyboard;
        event_mouse_data_t mouse;
        event_window_data_t window;
        event_process_data_t process;
        event_system_data_t system;
        uint8_t raw[64];
    } data;
    
    /* Link for queue */
    struct list_head list;
} event_t;

/* ===================================================================== */
/* Event Queue */
/* ===================================================================== */

#define EVENT_QUEUE_SIZE    1024

typedef struct {
    event_t events[EVENT_QUEUE_SIZE];
    struct list_head free_list;
    struct list_head pending_list;
    spinlock_t lock;
    int count;
} event_queue_t;

/* ===================================================================== */
/* Event Handler */
/* ===================================================================== */

typedef int (*event_handler_t)(event_t *event, void *user_data);

typedef struct event_handler {
    event_type_t type;          /* Event type to handle (or -1 for all) */
    event_handler_t handler;
    void *user_data;
    int priority;               /* Higher = earlier invocation */
    struct list_head list;
} event_handler_entry_t;

/* ===================================================================== */
/* Event System State */
/* ===================================================================== */

struct event_system {
    event_queue_t queue;
    
    /* Handlers */
    struct list_head handlers;          /* All handlers */
    struct list_head type_handlers[EVENT_USER]; /* Per-type handlers */
    
    /* Routing */
    int (*router)(event_t *event);     /* Custom router function */
    
    /* Stats */
    uint64_t events_posted;
    uint64_t events_dispatched;
    uint64_t events_dropped;
    
    /* State */
    uint32_t next_event_id;
    int enabled;
};

extern struct event_system g_event_system;

/* ===================================================================== */
/* Event System API */
/* ===================================================================== */

/**
 * event_system_init - Initialize event system
 */
void event_system_init(void);

/**
 * event_system_shutdown - Shut down event system
 */
void event_system_shutdown(void);

/**
 * event_system_enable - Enable event processing
 */
void event_system_enable(void);

/**
 * event_system_disable - Disable event processing
 */
void event_system_disable(void);

/* ===================================================================== */
/* Event Creation */
/* ===================================================================== */

/**
 * event_create - Allocate and initialize event
 * @type: Event type
 * 
 * Return: Event pointer, or NULL on failure
 */
event_t *event_create(event_type_t type);

/**
 * event_free - Free event back to pool
 * @event: Event to free
 */
void event_free(event_t *event);

/**
 * event_set_target - Set event routing target
 * @event: Event to modify
 * @window_id: Target window (-1 for none)
 * @process_id: Target process (-1 for none)
 */
void event_set_target(event_t *event, int window_id, int process_id);

/* ===================================================================== */
/* Event Posting */
/* ===================================================================== */

/**
 * event_post - Post event to queue
 * @event: Event to post
 * 
 * Return: 0 on success, negative on failure (queue full)
 */
int event_post(event_t *event);

/**
 * event_post_keyboard - Create and post keyboard event
 * @type: KEY_DOWN, KEY_UP, or KEY_REPEAT
 * @keycode: Virtual keycode
 * @scancode: Physical scancode
 * @modifiers: Modifier flags
 * @character: ASCII character (if printable)
 * 
 * Return: 0 on success, negative on failure
 */
int event_post_keyboard(event_type_t type, int keycode, int scancode,
                        uint32_t modifiers, char character);

/**
 * event_post_mouse - Create and post mouse event
 * @type: MOUSE_MOVE, MOUSE_DOWN, MOUSE_UP, etc.
 * @x, @y: Screen coordinates
 * @button: Button index (for DOWN/UP)
 * @modifiers: Modifier flags
 * 
 * Return: 0 on success, negative on failure
 */
int event_post_mouse(event_type_t type, int x, int y, int button,
                     uint32_t modifiers);

/**
 * event_post_window - Create and post window event
 * @type: Window event type
 * @window_id: Window ID
 * @x, @y: Position (if applicable)
 * @width, @height: Size (if applicable)
 * 
 * Return: 0 on success, negative on failure
 */
int event_post_window(event_type_t type, int window_id, 
                      int x, int y, int width, int height);

/**
 * event_post_system - Create and post system event
 * @type: System event type
 * 
 * Return: 0 on success, negative on failure
 */
int event_post_system(event_type_t type);

/* ===================================================================== */
/* Event Dispatch */
/* ===================================================================== */

/**
 * event_dispatch - Dispatch single event
 * @event: Event to dispatch
 * 
 * Routes event to appropriate handlers.
 */
void event_dispatch(event_t *event);

/**
 * event_dispatch_all - Dispatch all pending events
 * 
 * Processes entire event queue.
 */
void event_dispatch_all(void);

/**
 * event_poll - Poll for single event
 * @timeout_ms: Timeout in milliseconds (-1 = infinite)
 * 
 * Return: Event pointer, or NULL if no event
 */
event_t *event_poll(int timeout_ms);

/* ===================================================================== */
/* Event Handlers */
/* ===================================================================== */

/**
 * event_register_handler - Register event handler
 * @type: Event type to handle (-1 for all types)
 * @handler: Handler function
 * @user_data: User data passed to handler
 * @priority: Handler priority (higher = earlier)
 * 
 * Return: Handler ID on success, negative on failure
 */
int event_register_handler(event_type_t type, event_handler_t handler,
                           void *user_data, int priority);

/**
 * event_unregister_handler - Unregister handler
 * @handler_id: Handler ID from register
 * 
 * Return: 0 on success, negative on failure
 */
int event_unregister_handler(int handler_id);

/**
 * event_set_router - Set custom event router
 * @router: Router function (NULL for default)
 */
void event_set_router(int (*router)(event_t *event));

/* ===================================================================== */
/* Input Integration */
/* ===================================================================== */

/**
 * event_input_keyboard - Process raw keyboard input
 * @scancode: Hardware scancode
 * @pressed: 1 if pressed, 0 if released
 * 
 * Translates scancode and posts appropriate event.
 */
void event_input_keyboard(int scancode, int pressed);

/**
 * event_input_mouse - Process raw mouse input
 * @x, @y: Screen coordinates
 * @button_mask: Bitmask of pressed buttons
 * @delta_z: Scroll delta
 * 
 * Posts appropriate mouse events.
 */
void event_input_mouse(int x, int y, uint32_t button_mask, int delta_z);

/* ===================================================================== */
/* Stats */
/* ===================================================================== */

/**
 * event_get_stats - Get event system statistics
 * @posted: Output for total events posted
 * @dispatched: Output for events dispatched
 * @dropped: Output for events dropped
 */
void event_get_stats(uint64_t *posted, uint64_t *dispatched, uint64_t *dropped);

/**
 * event_dump_stats - Print event statistics
 */
void event_dump_stats(void);

#endif /* _EVENT_SYSTEM_H */
