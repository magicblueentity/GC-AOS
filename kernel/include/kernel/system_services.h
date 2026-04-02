/*
 * GC-AOS - System Services Layer
 * 
 * Clean API layer for all system services.
 * No direct cross-calls between subsystems.
 */

#ifndef _SYSTEM_SERVICES_H
#define _SYSTEM_SERVICES_H

#include "types.h"
#include "kernel/process_modern.h"
#include "gui/window_manager.h"
#include "gui/compositor.h"
#include "gui/app_framework.h"
#include "kernel/event_system.h"

/* ===================================================================== */
/* Forward Declarations */
/* ===================================================================== */

struct stat;

/* ===================================================================== */
/* Service IDs */
/* ===================================================================== */

typedef enum {
    SERVICE_NONE = 0,
    SERVICE_PROCESS,
    SERVICE_WINDOW,
    SERVICE_INPUT,
    SERVICE_FILESYSTEM,
    SERVICE_NETWORK,
    SERVICE_AUDIO,
    SERVICE_GRAPHICS,
    SERVICE_EVENT,
    SERVICE_TIME,
    SERVICE_MEMORY,
    SERVICE_MAX
} service_id_t;

/* ===================================================================== */
/* Service Status */
/* ===================================================================== */

typedef enum {
    SERVICE_STATUS_DOWN = 0,
    SERVICE_STATUS_STARTING,
    SERVICE_STATUS_UP,
    SERVICE_STATUS_ERROR,
    SERVICE_STATUS_STOPPING
} service_status_t;

/* ===================================================================== */
/* Service Structure */
/* ===================================================================== */

typedef struct {
    service_id_t id;
    const char *name;
    service_status_t status;
    
    /* Lifecycle */
    int (*init)(void);
    int (*shutdown)(void);
    
    /* Health check */
    int (*health_check)(void);
    
    /* Stats */
    void (*get_stats)(char *buf, int buf_size);
} service_t;

/* ===================================================================== */
/* System Services Manager */
/* ===================================================================== */

typedef struct {
    service_t services[SERVICE_MAX];
    int service_count;
    
    /* Status */
    int initialized;
    int running;
    
    /* Stats */
    uint64_t service_calls;
    uint64_t service_errors;
} services_manager_t;

extern services_manager_t g_services;

/* ===================================================================== */
/* Services Initialization */
/* ===================================================================== */

/**
 * services_init - Initialize all system services
 * 
 * Return: 0 on success, negative on failure
 */
int services_init(void);

/**
 * services_shutdown - Shut down all services
 */
void services_shutdown(void);

/**
 * services_get - Get service by ID
 * @id: Service ID
 * 
 * Return: Service pointer, or NULL
 */
service_t *services_get(service_id_t id);

/**
 * services_get_status - Get service status
 * @id: Service ID
 * 
 * Return: Service status
 */
service_status_t services_get_status(service_id_t id);

/**
 * services_is_ready - Check if service is ready
 * @id: Service ID
 * 
 * Return: 1 if ready, 0 otherwise
 */
int services_is_ready(service_id_t id);

/* ===================================================================== */
/* Process Service API */
/* ===================================================================== */

typedef struct {
    /* Process lifecycle */
    int (*create)(const char *name, void (*entry)(void *), void *arg, int priority);
    int (*start)(int pid);
    int (*kill)(int pid, int exit_code);
    void (*exit)(int code);
    
    /* Process queries */
    int (*count)(void);
    int (*count_ready)(void);
    int (*exists)(int pid);
    int (*get_info)(int pid, char *name, int name_size, int *state);
    
    /* Current process */
    int (*get_current_pid)(void);
    const char *(*get_current_name)(void);
    
    /* Scheduling */
    void (*yield)(void);
    void (*sleep)(uint64_t nanoseconds);
    int (*set_priority)(int pid, int priority);
} process_service_api_t;

extern process_service_api_t g_process_service;

/* ===================================================================== */
/* Window Service API */
/* ===================================================================== */

typedef struct {
    /* Window lifecycle */
    int (*create)(const char *title, int x, int y, int width, int height, 
                  uint32_t flags, int owner_pid);
    void (*destroy)(int window_id);
    void (*show)(int window_id);
    void (*hide)(int window_id);
    
    /* Window operations */
    void (*move)(int window_id, int x, int y);
    void (*resize)(int window_id, int width, int height);
    void (*set_title)(int window_id, const char *title);
    
    /* Z-order */
    void (*raise)(int window_id);
    void (*lower)(int window_id);
    
    /* Focus */
    void (*focus)(int window_id);
    void (*unfocus)(void);
    int (*get_focused)(void);
    int (*is_focused)(int window_id);
    
    /* Queries */
    int (*at_position)(int x, int y);
    int (*get_owner)(int window_id);
    void (*get_rect)(int window_id, int *x, int *y, int *w, int *h);
    
    /* Rendering */
    void *(*get_buffer)(int window_id);
    void (*invalidate)(int window_id);
} window_service_api_t;

extern window_service_api_t g_window_service;

/* ===================================================================== */
/* Input Service API */
/* ===================================================================== */

typedef struct {
    /* Event registration */
    int (*register_handler)(event_type_t type, event_handler_t handler, 
                           void *user_data, int priority);
    int (*unregister_handler)(int handler_id);
    
    /* Input state queries */
    int (*is_key_pressed)(int keycode);
    void (*get_mouse_pos)(int *x, int *y);
    int (*is_mouse_pressed)(int button);
    
    /* Input capture */
    void (*capture_keyboard)(int window_id);
    void (*capture_mouse)(int window_id);
    void (*release_capture)(int window_id);
} input_service_api_t;

extern input_service_api_t g_input_service;

/* ===================================================================== */
/* Filesystem Service API */
/* ===================================================================== */

typedef struct {
    /* File operations */
    int (*open)(const char *path, int flags);
    int (*close)(int fd);
    ssize_t (*read)(int fd, void *buf, size_t count);
    ssize_t (*write)(int fd, const void *buf, size_t count);
    off_t (*seek)(int fd, off_t offset, int whence);
    
    /* File info */
    int (*stat)(const char *path, struct stat *buf);
    int (*exists)(const char *path);
    
    /* Directory operations */
    int (*mkdir)(const char *path, int mode);
    int (*rmdir)(const char *path);
    int (*opendir)(const char *path);
    int (*readdir)(int dirfd, char *name, int name_size);
    int (*closedir)(int dirfd);
} filesystem_service_api_t;

extern filesystem_service_api_t g_filesystem_service;

/* ===================================================================== */
/* Event Service API */
/* ===================================================================== */

typedef struct {
    /* Event posting */
    int (*post_keyboard)(int type, int keycode, int scancode, 
                        uint32_t modifiers, char character);
    int (*post_mouse)(int type, int x, int y, int button, uint32_t modifiers);
    int (*post_window)(int type, int window_id, int x, int y, int w, int h);
    int (*post_system)(int type);
    
    /* Event dispatch */
    void (*dispatch_all)(void);
    event_t *(*poll)(int timeout_ms);
    
    /* Handler registration */
    int (*register_handler)(event_type_t type, event_handler_t handler,
                           void *user_data, int priority);
    int (*unregister_handler)(int handler_id);
} event_service_api_t;

extern event_service_api_t g_event_service;

/* ===================================================================== */
/* Time Service API */
/* ===================================================================== */

typedef struct {
    /* Time queries */
    uint64_t (*get_time_ns)(void);
    uint64_t (*get_time_ms)(void);
    uint64_t (*get_ticks)(void);
    
    /* Timing */
    void (*delay_ns)(uint64_t nanoseconds);
    void (*delay_ms)(uint64_t milliseconds);
    
    /* Timers */
    int (*create_timer)(uint64_t interval_ns, void (*callback)(void));
    void (*destroy_timer)(int timer_id);
} time_service_api_t;

extern time_service_api_t g_time_service;

/* ===================================================================== */
/* Memory Service API */
/* ===================================================================== */

typedef struct {
    /* Allocation */
    void *(*malloc)(size_t size);
    void *(*calloc)(size_t nmemb, size_t size);
    void *(*realloc)(void *ptr, size_t size);
    void (*free)(void *ptr);
    
    /* Aligned allocation */
    void *(*aligned_alloc)(size_t alignment, size_t size);
    
    /* Info */
    size_t (*get_free)(void);
    size_t (*get_total)(void);
} memory_service_api_t;

extern memory_service_api_t g_memory_service;

/* ===================================================================== */
/* Combined System API */
/* ===================================================================== */

typedef struct {
    process_service_api_t *process;
    window_service_api_t *window;
    input_service_api_t *input;
    filesystem_service_api_t *filesystem;
    event_service_api_t *event;
    time_service_api_t *time;
    memory_service_api_t *memory;
} system_api_t;

extern system_api_t g_system_api;

/**
 * system_get_api - Get pointer to system API
 * 
 * Return: System API pointer
 */
system_api_t *system_get_api(void);

/**
 * system_init_api - Initialize system API structure
 */
void system_init_api(void);

/* ===================================================================== */
/* Service Helpers */
/* ===================================================================== */

/**
 * service_call - Call a service function with error handling
 * @service_id: Service ID
 * @func: Function pointer
 * @...: Arguments
 * 
 * Return: Service function result
 */
#define service_call(service_id, func, ...) \
    ({ \
        int __result = -1; \
        if (services_is_ready(service_id)) { \
            __result = func(__VA_ARGS__); \
            g_services.service_calls++; \
        } else { \
            g_services.service_errors++; \
        } \
        __result; \
    })

/**
 * service_safe - Check if service is ready before calling
 * @service_id: Service ID
 * 
 * Return: 1 if safe to call, 0 otherwise
 */
#define service_safe(service_id) services_is_ready(service_id)

#endif /* _SYSTEM_SERVICES_H */
