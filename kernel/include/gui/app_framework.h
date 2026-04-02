/*
 * GC-AOS - Application Framework
 * 
 * User-space style app abstraction with proper lifecycle management.
 * Apps run as scheduled units and interact via APIs only.
 */

#ifndef _APP_FRAMEWORK_H
#define _APP_FRAMEWORK_H

#include "types.h"
#include "kernel/process_modern.h"
#include "gui/window_manager.h"
#include "kernel/list.h"
#include "sync/spinlock.h"

/* ===================================================================== */
/* App Limits */
/* ===================================================================== */

#define MAX_APPS            32
#define APP_NAME_MAX        32
#define APP_ARGS_MAX        16
#define APP_ARG_LEN         256

/* ===================================================================== */
/* App States */
/* ===================================================================== */

typedef enum {
    APP_STATE_INACTIVE = 0,     /* Created but not started */
    APP_STATE_INITIALIZING,     /* Running init() */
    APP_STATE_RUNNING,        /* Active, receiving updates */
    APP_STATE_PAUSED,         /* Backgrounded, no updates */
    APP_STATE_SHUTTING_DOWN,  /* Running destroy() */
    APP_STATE_TERMINATED      /* Cleaned up */
} app_state_t;

/* ===================================================================== */
/* App Definition (VTable) */
/* ===================================================================== */

/* Forward declarations */
struct app;
struct app_context;

typedef struct {
    /* Lifecycle callbacks */
    int (*init)(struct app *app, int argc, char **argv);
    int (*update)(struct app *app);       /* Called every frame */
    int (*render)(struct app *app);       /* Render to backbuffer */
    int (*destroy)(struct app *app);
    
    /* Event handlers */
    int (*on_focus)(struct app *app);
    int (*on_blur)(struct app *app);
    int (*on_resize)(struct app *app, int width, int height);
    int (*on_key)(struct app *app, int key, int pressed);
    int (*on_mouse)(struct app *app, int x, int y, int button, int pressed);
} app_vtable_t;

/* ===================================================================== */
/* App Structure */
/* ===================================================================== */

typedef struct app {
    /* Identity */
    int app_id;
    char name[APP_NAME_MAX];
    
    /* State */
    app_state_t state;
    int exit_code;
    
    /* Lifecycle */
    const app_vtable_t *vtable;
    void *user_data;                    /* App-specific data */
    
    /* Process association */
    process_t *process;
    int pid;
    
    /* Window (if GUI app) */
    window_t *window;
    int window_id;
    
    /* Rendering */
    void *backbuffer;
    int buffer_width;
    int buffer_height;
    int needs_redraw;
    
    /* Timing */
    uint64_t last_update;
    uint64_t update_interval;           /* Target update rate */
    uint64_t frame_count;
    
    /* Arguments */
    int argc;
    char *argv[APP_ARGS_MAX];
    
    /* Links */
    struct list_head list;
    struct list_head active_list;
} app_t;

/* ===================================================================== */
/* App Context (API for apps) */
 * ===================================================================== */

typedef struct app_context {
    /* Identity */
    app_t *app;
    
    /* Window API */
    window_t *(*get_window)(app_t *app);
    int (*create_window)(app_t *app, const char *title, int x, int y, 
                         int width, int height);
    void (*destroy_window)(app_t *app);
    void (*invalidate)(app_t *app);
    
    /* Rendering API */
    void *(*get_backbuffer)(app_t *app);
    int (*get_buffer_width)(app_t *app);
    int (*get_buffer_height)(app_t *app);
    void (*present)(app_t *app);
    
    /* Process API */
    void (*yield)(app_t *app);
    void (*sleep)(app_t *app, uint64_t ns);
    process_t *(*get_process)(app_t *app);
    
    /* System API */
    uint64_t (*get_time)(void);
    void (*log)(const char *fmt, ...);
} app_context_t;

/* ===================================================================== */
/* App Manager */
/* ===================================================================== */

struct app_manager {
    spinlock_t lock;
    
    /* App table */
    app_t *apps[MAX_APPS];
    int app_count;
    int next_app_id;
    
    /* Lists */
    struct list_head all_apps;
    struct list_head active_apps;       /* Running or paused */
    struct list_head foreground_apps;   /* Currently focused */
    
    /* Active app */
    app_t *focused_app;
    
    /* Update loop */
    int running;
    uint64_t last_update;
    
    /* Stats */
    uint64_t total_frames;
    uint64_t apps_created;
    uint64_t apps_destroyed;
};

extern struct app_manager g_app_manager;
extern app_context_t g_app_context;

/* ===================================================================== */
/* App Manager API */
/* ===================================================================== */

/**
 * app_manager_init - Initialize app framework
 */
void app_manager_init(void);

/**
 * app_manager_shutdown - Shut down app framework
 */
void app_manager_shutdown(void);

/**
 * app_manager_update - Update all active apps
 * 
 * Called by system main loop. Runs update() on all apps,
 * handles scheduling, and triggers rendering.
 */
void app_manager_update(void);

/**
 * app_manager_render - Render all apps
 * 
 * Called by compositor before presenting frame.
 */
void app_manager_render(void);

/* ===================================================================== */
/* App Lifecycle */
/* ===================================================================== */

/**
 * app_create - Create an app instance
 * @name: App name
 * @vtable: App vtable with callbacks
 * @argc: Argument count
 * @argv: Argument vector
 * 
 * Return: App pointer, or NULL on failure
 */
app_t *app_create(const char *name, const app_vtable_t *vtable, 
                  int argc, char **argv);

/**
 * app_start - Start an app
 * @app: App to start
 * 
 * Return: 0 on success, negative on error
 */
int app_start(app_t *app);

/**
 * app_pause - Pause an app (background)
 * @app: App to pause
 */
void app_pause(app_t *app);

/**
 * app_resume - Resume a paused app
 * @app: App to resume
 */
void app_resume(app_t *app);

/**
 * app_stop - Stop and destroy an app
 * @app: App to stop
 * @exit_code: Exit code
 */
void app_stop(app_t *app, int exit_code);

/**
 * app_destroy - Clean up app resources
 * @app: App to destroy
 */
void app_destroy(app_t *app);

/* ===================================================================== */
/* App Queries */
/* ===================================================================== */

/**
 * app_get - Get app by ID
 * @app_id: App ID
 * 
 * Return: App pointer, or NULL
 */
app_t *app_get(int app_id);

/**
 * app_get_by_window - Get app by window
 * @window: Window pointer
 * 
 * Return: App pointer, or NULL
 */
app_t *app_get_by_window(window_t *window);

/**
 * app_get_focused - Get currently focused app
 * 
 * Return: Focused app, or NULL
 */
app_t *app_get_focused(void);

/**
 * app_count - Get number of running apps
 * 
 * Return: Active app count
 */
int app_count(void);

/* ===================================================================== */
/* App Context API (for apps to use) */
 * ===================================================================== */

/**
 * app_get_context - Get app context for calling app
 * 
 * Return: App context pointer
 */
app_context_t *app_get_context(void);

/**
 * app_log - Log message from app
 * @app: Calling app
 * @fmt: Format string
 * @...: Arguments
 */
void app_log(app_t *app, const char *fmt, ...);

/**
 * app_request_redraw - Request window redraw
 * @app: Calling app
 */
void app_request_redraw(app_t *app);

/**
 * app_set_update_rate - Set target update rate
 * @app: Calling app
 * @hz: Updates per second (0 = every frame)
 */
void app_set_update_rate(app_t *app, int hz);

/* ===================================================================== */
/* Built-in App Helpers */
 * ===================================================================== */

/**
 * app_create_simple - Create simple app with minimal vtable
 * @name: App name
 * @init_func: Init function (can be NULL)
 * @update_func: Update function
 * @render_func: Render function
 * @destroy_func: Destroy function (can be NULL)
 * 
 * Return: App pointer, or NULL
 */
app_t *app_create_simple(const char *name,
                         int (*init_func)(app_t *, int, char **),
                         int (*update_func)(app_t *),
                         int (*render_func)(app_t *),
                         int (*destroy_func)(app_t *));

/* ===================================================================== */
/* App Registration */
 * ===================================================================== */

/**
 * app_register_builtin - Register a built-in app
 * @name: App name
 * @vtable: App vtable
 * @icon: Icon data (can be NULL)
 * 
 * Return: 0 on success, negative on error
 */
int app_register_builtin(const char *name, const app_vtable_t *vtable, void *icon);

/**
 * app_launch_builtin - Launch a registered built-in app
 * @name: Registered app name
 * @argc: Argument count
 * @argv: Arguments
 * 
 * Return: App ID on success, negative on error
 */
int app_launch_builtin(const char *name, int argc, char **argv);

/* ===================================================================== */
/* Debugging */
 * ===================================================================== */

/**
 * app_dump_state - Dump app manager state
 */
void app_dump_state(void);

/**
 * app_dump_app - Dump single app state
 * @app: App to dump
 */
void app_dump_app(app_t *app);

#endif /* _APP_FRAMEWORK_H */
