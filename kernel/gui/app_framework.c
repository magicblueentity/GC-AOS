/*
 * GC-AOS - Application Framework Implementation
 */

#include "gui/app_framework.h"
#include "gui/compositor.h"
#include "mm/kmalloc.h"
#include "printk.h"
#include <string.h>
#include <stdarg.h>

/* Global app manager */
struct app_manager g_app_manager;
app_context_t g_app_context;

/* ===================================================================== */
/* App Context Implementation */
/* ===================================================================== */

static window_t *ctx_get_window(app_t *app)
{
    return app ? app->window : NULL;
}

static int ctx_create_window(app_t *app, const char *title, int x, int y, 
                             int width, int height)
{
    if (!app) {
        return -1;
    }
    
    /* Create window */
    window_t *win = window_create(title, x, y, width, height, 
                                  WINDOW_FLAG_DEFAULT, app->process);
    if (!win) {
        return -2;
    }
    
    app->window = win;
    app->window_id = win->id;
    
    /* Create backbuffer for window */
    compositor_buffer_t *buf = compositor_create_buffer(width, height, win);
    if (!buf) {
        window_destroy(win);
        app->window = NULL;
        return -3;
    }
    
    compositor_attach_buffer(buf, win);
    app->backbuffer = buf->pixels;
    app->buffer_width = width;
    app->buffer_height = height;
    
    return 0;
}

static void ctx_destroy_window(app_t *app)
{
    if (!app || !app->window) {
        return;
    }
    
    window_destroy(app->window);
    app->window = NULL;
    app->backbuffer = NULL;
}

static void ctx_invalidate(app_t *app)
{
    if (!app || !app->window) {
        return;
    }
    
    window_invalidate_all(app->window);
    app->needs_redraw = 1;
}

static void *ctx_get_backbuffer(app_t *app)
{
    return app ? app->backbuffer : NULL;
}

static int ctx_get_buffer_width(app_t *app)
{
    return app ? app->buffer_width : 0;
}

static int ctx_get_buffer_height(app_t *app)
{
    return app ? app->buffer_height : 0;
}

static void ctx_present(app_t *app)
{
    if (!app || !app->window) {
        return;
    }
    
    /* Mark window dirty for compositor */
    compositor_mark_window_dirty(app->window);
    app->needs_redraw = 0;
}

static void ctx_yield(app_t *app)
{
    (void)app;
    scheduler_yield();
}

static void ctx_sleep(app_t *app, uint64_t ns)
{
    if (app && app->process) {
        process_sleep(app->process, ns);
    }
}

static process_t *ctx_get_process(app_t *app)
{
    return app ? app->process : NULL;
}

static uint64_t ctx_get_time(void)
{
    return get_time_ns();
}

static void ctx_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);
}

/* ===================================================================== */
/* Internal Helpers */
/* ===================================================================== */

static int alloc_app_id(void)
{
    spin_lock(&g_app_manager.lock);
    
    for (int i = 0; i < MAX_APPS; i++) {
        int id = (g_app_manager.next_app_id + i) % MAX_APPS;
        if (id == 0) continue;
        if (g_app_manager.apps[id] == NULL) {
            g_app_manager.next_app_id = (id + 1) % MAX_APPS;
            spin_unlock(&g_app_manager.lock);
            return id;
        }
    }
    
    spin_unlock(&g_app_manager.lock);
    return -1;
}

static app_t *alloc_app(void)
{
    app_t *app = kmalloc(sizeof(app_t));
    if (!app) {
        return NULL;
    }
    
    memset(app, 0, sizeof(app_t));
    INIT_LIST_HEAD(&app->list);
    INIT_LIST_HEAD(&app->active_list);
    
    return app;
}

static void free_app(app_t *app)
{
    if (app) {
        /* Free arguments */
        for (int i = 0; i < app->argc && i < APP_ARGS_MAX; i++) {
            if (app->argv[i]) {
                kfree(app->argv[i]);
            }
        }
        kfree(app);
    }
}

static int copy_args(app_t *app, int argc, char **argv)
{
    app->argc = (argc > APP_ARGS_MAX) ? APP_ARGS_MAX : argc;
    
    for (int i = 0; i < app->argc; i++) {
        if (argv[i]) {
            app->argv[i] = kmalloc(APP_ARG_LEN);
            if (!app->argv[i]) {
                return -1;
            }
            strncpy(app->argv[i], argv[i], APP_ARG_LEN - 1);
            app->argv[i][APP_ARG_LEN - 1] = '\0';
        }
    }
    
    return 0;
}

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

void app_manager_init(void)
{
    printk(KERN_INFO "APP: Initializing app framework\n");
    
    memset(&g_app_manager, 0, sizeof(g_app_manager));
    spin_lock_init(&g_app_manager.lock);
    
    INIT_LIST_HEAD(&g_app_manager.all_apps);
    INIT_LIST_HEAD(&g_app_manager.active_apps);
    INIT_LIST_HEAD(&g_app_manager.foreground_apps);
    
    g_app_manager.next_app_id = 1;
    g_app_manager.running = 1;
    
    /* Initialize app context */
    memset(&g_app_context, 0, sizeof(g_app_context));
    g_app_context.get_window = ctx_get_window;
    g_app_context.create_window = ctx_create_window;
    g_app_context.destroy_window = ctx_destroy_window;
    g_app_context.invalidate = ctx_invalidate;
    g_app_context.get_backbuffer = ctx_get_backbuffer;
    g_app_context.get_buffer_width = ctx_get_buffer_width;
    g_app_context.get_buffer_height = ctx_get_buffer_height;
    g_app_context.present = ctx_present;
    g_app_context.yield = ctx_yield;
    g_app_context.sleep = ctx_sleep;
    g_app_context.get_process = ctx_get_process;
    g_app_context.get_time = ctx_get_time;
    g_app_context.log = ctx_log;
    
    printk(KERN_INFO "APP: App framework initialized\n");
}

void app_manager_shutdown(void)
{
    printk(KERN_INFO "APP: Shutting down app framework\n");
    
    g_app_manager.running = 0;
    
    /* Stop all apps */
    app_t *app, *tmp;
    list_for_each_entry_safe(app, tmp, &g_app_manager.active_apps, active_list) {
        app_stop(app, 0);
    }
    
    printk(KERN_INFO "APP: App framework shutdown\n");
}

void app_manager_update(void)
{
    if (!g_app_manager.running) {
        return;
    }
    
    uint64_t now = get_time_ns();
    app_t *app;
    
    list_for_each_entry(app, &g_app_manager.active_apps, active_list) {
        /* Skip paused apps */
        if (app->state != APP_STATE_RUNNING) {
            continue;
        }
        
        /* Check update interval */
        if (app->update_interval > 0) {
            if (now - app->last_update < app->update_interval) {
                continue;
            }
        }
        
        /* Call update */
        if (app->vtable && app->vtable->update) {
            preempt_disable();
            int ret = app->vtable->update(app);
            preempt_enable();
            
            if (ret != 0) {
                /* App signaled error/stop */
                app_stop(app, ret);
                continue;
            }
        }
        
        app->last_update = now;
        app->frame_count++;
    }
    
    g_app_manager.total_frames++;
    g_app_manager.last_update = now;
}

void app_manager_render(void)
{
    app_t *app;
    
    list_for_each_entry(app, &g_app_manager.active_apps, active_list) {
        if (app->state != APP_STATE_RUNNING && app->state != APP_STATE_PAUSED) {
            continue;
        }
        
        /* Call render if app needs redraw or always render */
        if (app->vtable && app->vtable->render && app->needs_redraw) {
            preempt_disable();
            app->vtable->render(app);
            preempt_enable();
            
            /* Present to compositor */
            ctx_present(app);
        }
    }
}

/* ===================================================================== */
/* App Lifecycle */
/* ===================================================================== */

app_t *app_create(const char *name, const app_vtable_t *vtable, 
                  int argc, char **argv)
{
    if (!name || !vtable) {
        return NULL;
    }
    
    /* Allocate app */
    app_t *app = alloc_app();
    if (!app) {
        printk(KERN_ERR "APP: Failed to allocate app\n");
        return NULL;
    }
    
    /* Allocate ID */
    int app_id = alloc_app_id();
    if (app_id < 0) {
        printk(KERN_ERR "APP: No app ID available\n");
        free_app(app);
        return NULL;
    }
    
    /* Copy arguments */
    if (copy_args(app, argc, argv) < 0) {
        printk(KERN_ERR "APP: Failed to copy args\n");
        free_app(app);
        return NULL;
    }
    
    /* Create process for app */
    char proc_name[64];
    snprintf(proc_name, sizeof(proc_name), "app:%s", name);
    
    process_t *proc = process_create(proc_name, NULL, NULL, 120); /* Normal priority */
    if (!proc) {
        printk(KERN_ERR "APP: Failed to create process\n");
        free_app(app);
        return NULL;
    }
    
    /* Initialize app */
    app->app_id = app_id;
    strncpy(app->name, name, APP_NAME_MAX - 1);
    app->name[APP_NAME_MAX - 1] = '\0';
    app->vtable = vtable;
    app->state = APP_STATE_INACTIVE;
    app->process = proc;
    app->pid = proc->pid;
    app->update_interval = 0; /* Every frame */
    app->needs_redraw = 1;
    
    /* Register in manager */
    spin_lock(&g_app_manager.lock);
    g_app_manager.apps[app_id] = app;
    g_app_manager.app_count++;
    g_app_manager.apps_created++;
    list_add(&app->list, &g_app_manager.all_apps);
    spin_unlock(&g_app_manager.lock);
    
    printk(KERN_INFO "APP: Created app %d '%s'\n", app_id, name);
    
    return app;
}

int app_start(app_t *app)
{
    if (!app || app->state != APP_STATE_INACTIVE) {
        return -1;
    }
    
    printk(KERN_INFO "APP: Starting app %d '%s'\n", app->app_id, app->name);
    
    /* Call init */
    app->state = APP_STATE_INITIALIZING;
    
    if (app->vtable && app->vtable->init) {
        int ret = app->vtable->init(app, app->argc, app->argv);
        if (ret != 0) {
            printk(KERN_ERR "APP: App %d init failed with %d\n", app->app_id, ret);
            app->state = APP_STATE_TERMINATED;
            return ret;
        }
    }
    
    /* Start running */
    app->state = APP_STATE_RUNNING;
    app->last_update = get_time_ns();
    
    spin_lock(&g_app_manager.lock);
    list_add(&app->active_list, &g_app_manager.active_apps);
    spin_unlock(&g_app_manager.lock);
    
    /* Start process */
    process_start(app->process);
    
    return 0;
}

void app_pause(app_t *app)
{
    if (!app || app->state != APP_STATE_RUNNING) {
        return;
    }
    
    app->state = APP_STATE_PAUSED;
    
    if (app->vtable && app->vtable->on_blur) {
        app->vtable->on_blur(app);
    }
    
    printk(KERN_INFO "APP: Paused app %d '%s'\n", app->app_id, app->name);
}

void app_resume(app_t *app)
{
    if (!app || app->state != APP_STATE_PAUSED) {
        return;
    }
    
    app->state = APP_STATE_RUNNING;
    app->last_update = get_time_ns();
    
    if (app->vtable && app->vtable->on_focus) {
        app->vtable->on_focus(app);
    }
    
    printk(KERN_INFO "APP: Resumed app %d '%s'\n", app->app_id, app->name);
}

void app_stop(app_t *app, int exit_code)
{
    if (!app || app->state == APP_STATE_TERMINATED) {
        return;
    }
    
    printk(KERN_INFO "APP: Stopping app %d '%s' (code=%d)\n", 
           app->app_id, app->name, exit_code);
    
    app->state = APP_STATE_SHUTTING_DOWN;
    app->exit_code = exit_code;
    
    /* Call destroy */
    if (app->vtable && app->vtable->destroy) {
        app->vtable->destroy(app);
    }
    
    /* Remove from lists */
    spin_lock(&g_app_manager.lock);
    list_del(&app->active_list);
    INIT_LIST_HEAD(&app->active_list);
    
    if (g_app_manager.focused_app == app) {
        g_app_manager.focused_app = NULL;
    }
    spin_unlock(&g_app_manager.lock);
    
    /* Kill process */
    if (app->process) {
        process_kill(app->process->pid, exit_code);
    }
    
    app->state = APP_STATE_TERMINATED;
    
    /* Schedule cleanup */
    app_destroy(app);
}

void app_destroy(app_t *app)
{
    if (!app) {
        return;
    }
    
    printk(KERN_INFO "APP: Destroying app %d '%s'\n", app->app_id, app->name);
    
    spin_lock(&g_app_manager.lock);
    
    /* Remove from all lists */
    list_del(&app->list);
    INIT_LIST_HEAD(&app->list);
    
    /* Remove from table */
    g_app_manager.apps[app->app_id] = NULL;
    g_app_manager.app_count--;
    g_app_manager.apps_destroyed++;
    
    spin_unlock(&g_app_manager.lock);
    
    /* Clean up resources */
    if (app->window) {
        ctx_destroy_window(app);
    }
    
    free_app(app);
}

/* ===================================================================== */
/* App Queries */
/* ===================================================================== */

app_t *app_get(int app_id)
{
    if (app_id < 0 || app_id >= MAX_APPS) {
        return NULL;
    }
    return g_app_manager.apps[app_id];
}

app_t *app_get_by_window(window_t *window)
{
    if (!window) {
        return NULL;
    }
    
    app_t *app;
    list_for_each_entry(app, &g_app_manager.active_apps, active_list) {
        if (app->window == window) {
            return app;
        }
    }
    
    return NULL;
}

app_t *app_get_focused(void)
{
    return g_app_manager.focused_app;
}

int app_count(void)
{
    spin_lock(&g_app_manager.lock);
    int count = g_app_manager.app_count;
    spin_unlock(&g_app_manager.lock);
    return count;
}

/* ===================================================================== */
/* App Context API */
/* ===================================================================== */

app_context_t *app_get_context(void)
{
    return &g_app_context;
}

void app_log(app_t *app, const char *fmt, ...)
{
    if (!app) {
        return;
    }
    
    printk(KERN_INFO "[%s]: ", app->name);
    
    va_list args;
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);
    
    printk("\n");
}

void app_request_redraw(app_t *app)
{
    if (!app) {
        return;
    }
    
    app->needs_redraw = 1;
}

void app_set_update_rate(app_t *app, int hz)
{
    if (!app) {
        return;
    }
    
    if (hz <= 0) {
        app->update_interval = 0;
    } else {
        app->update_interval = 1000000000ULL / hz;
    }
}

/* ===================================================================== */
/* Built-in App Helpers */
/* ===================================================================== */

app_t *app_create_simple(const char *name,
                         int (*init_func)(app_t *, int, char **),
                         int (*update_func)(app_t *),
                         int (*render_func)(app_t *),
                         int (*destroy_func)(app_t *))
{
    /* Allocate static vtable */
    static app_vtable_t vtable;
    
    memset(&vtable, 0, sizeof(vtable));
    vtable.init = init_func;
    vtable.update = update_func;
    vtable.render = render_func;
    vtable.destroy = destroy_func;
    
    return app_create(name, &vtable, 0, NULL);
}

/* ===================================================================== */
/* Debugging */
/* ===================================================================== */

void app_dump_state(void)
{
    printk(KERN_INFO "=== App Manager State ===\n");
    printk(KERN_INFO "Apps: %d active, %llu created, %llu destroyed\n",
           g_app_manager.app_count,
           g_app_manager.apps_created,
           g_app_manager.apps_destroyed);
    printk(KERN_INFO "Total frames: %llu\n", g_app_manager.total_frames);
    printk(KERN_INFO "Focused: %s (ID %d)\n",
           g_app_manager.focused_app ? g_app_manager.focused_app->name : "none",
           g_app_manager.focused_app ? g_app_manager.focused_app->app_id : -1);
    
    app_t *app;
    list_for_each_entry(app, &g_app_manager.active_apps, active_list) {
        app_dump_app(app);
    }
}

void app_dump_app(app_t *app)
{
    if (!app) {
        return;
    }
    
    const char *state_str;
    switch (app->state) {
        case APP_STATE_INACTIVE: state_str = "inactive"; break;
        case APP_STATE_INITIALIZING: state_str = "init"; break;
        case APP_STATE_RUNNING: state_str = "running"; break;
        case APP_STATE_PAUSED: state_str = "paused"; break;
        case APP_STATE_SHUTTING_DOWN: state_str = "shutdown"; break;
        case APP_STATE_TERMINATED: state_str = "terminated"; break;
        default: state_str = "unknown"; break;
    }
    
    printk(KERN_INFO "  [%d] '%s' state=%s pid=%d frames=%llu\n",
           app->app_id, app->name, state_str, app->pid, app->frame_count);
}
