/*
 * GC-AOS Kernel - Driver Lifecycle Management
 * 
 * Provides safe driver loading, unloading, and resource management
 * with proper reference counting and hotplug support.
 */

#ifndef _DRIVERS_LIFECYCLE_H
#define _DRIVERS_LIFECYCLE_H

#include "types.h"
#include "kernel/list.h"

/* ===================================================================== */
/* Forward Declarations */
/* ===================================================================== */

struct driver;
struct device;
struct driver_refcount;

/* ===================================================================== */
/* Spinlock Definition */
/* ===================================================================== */

typedef struct {
    volatile int raw_lock;
} spinlock_t;

/* ===================================================================== */
/* Constants */
/* ===================================================================== */

#define SPIN_LOCK_UNLOCKED { 0 }

/* ===================================================================== */
/* Driver Lifecycle States */
/* ===================================================================== */

typedef enum {
    DRIVER_STATE_UNINITIALIZED = 0,  /* Driver not initialized */
    DRIVER_STATE_LOADING,            /* Driver is loading */
    DRIVER_STATE_LOADED,             /* Driver loaded but not probed */
    DRIVER_STATE_PROBING,            /* Driver is probing devices */
    DRIVER_STATE_ACTIVE,             /* Driver active and bound to devices */
    DRIVER_STATE_SUSPENDED,          /* Driver suspended */
    DRIVER_STATE_UNBINDING,          /* Driver unbinding from devices */
    DRIVER_STATE_UNLOADING,          /* Driver is unloading */
    DRIVER_STATE_ERROR,              /* Driver in error state */
    DRIVER_STATE_REMOVED,            /* Driver removed from system */
} driver_state_t;

/* Driver lifecycle flags */
#define DRIVER_FLAG_AUTOPROBE        (1 << 0)  /* Auto-probe devices */
#define DRIVER_FLAG_HOTPLUG          (1 << 1)  /* Support hotplug */
#define DRIVER_FLAG_REMOVABLE        (1 << 2)  /* Driver is removable */
#define DRIVER_FLAG_PERSISTENT        (1 << 3)  /* Driver cannot be removed */
#define DRIVER_FLAG_ASYNC_PROBE      (1 << 4)  /* Asynchronous probing */
#define DRIVER_FLAG_DEFERRED_PROBE   (1 << 5)  /* Deferred probing */
#define DRIVER_FLAG_IGNORE_ERRORS    (1 << 6)  /* Ignore probe errors */

/* ===================================================================== */
/* Driver Reference Management */
/* ===================================================================== */

struct driver_refcount {
    atomic_t refcount;               /* Reference count */
    atomic_t active_refs;            /* Active references */
    atomic_t pending_refs;           /* Pending references */
    
    /* Reference tracking */
    struct list_head ref_list;       /* List of references */
    spinlock_t ref_lock;             /* Reference lock */
    
    /* Reference types */
    atomic_t module_refs;            /* Module references */
    atomic_t device_refs;            /* Device references */
    atomic_t client_refs;            /* Client references */
    atomic_t sysfs_refs;             /* Sysfs references */
    
    /* Reference limits */
    int max_refs;                    /* Maximum references */
    bool ref_limit_enabled;          /* Reference limiting enabled */
};

/* Driver reference structure */
struct driver_reference {
    void *holder;                    /* Reference holder */
    const char *type;                /* Reference type */
    const char *file;                /* Source file */
    int line;                        /* Source line */
    uint64_t timestamp;             /* Reference time */
    
    struct list_head list;          /* Reference list */
};

/* ===================================================================== */
/* Device Lifecycle Management */
/* ===================================================================== */

struct device_lifecycle {
    /* Device state */
    driver_state_t state;            /* Device state */
    uint64_t state_time;             /* Last state change time */
    
    /* Binding management */
    struct driver *bound_driver;     /* Bound driver */
    atomic_t bind_count;             /* Bind count */
    spinlock_t bind_lock;            /* Bind lock */
    
    /* Probe management */
    int probe_status;                /* Probe status */
    uint64_t probe_time;             /* Probe time */
    int retry_count;                 /* Probe retry count */
    
    /* Remove management */
    bool removal_requested;          /* Removal requested */
    uint64_t removal_time;           /* Removal request time */
    bool force_remove;               /* Force removal */
    
    /* Hotplug management */
    bool hotplug_capable;            /* Hotplug capable */
    bool hotplug_enabled;            /* Hotplug enabled */
    uint64_t hotplug_time;           /* Last hotplug event */
    
    /* Power management */
    bool power_managed;              /* Power managed */
    bool runtime_pm_enabled;         /* Runtime PM enabled */
    bool suspended;                   /* Device suspended */
    
    /* Error handling */
    int error_count;                 /* Error count */
    int last_error;                  /* Last error */
    uint64_t last_error_time;        /* Last error time */
    bool error_recovery_enabled;      /* Error recovery enabled */
};

/* ===================================================================== */
/* Driver Lifecycle Operations */
/* ===================================================================== */

struct driver_lifecycle_ops {
    /* Lifecycle operations */
    int (*load)(struct driver *driver);           /* Load driver */
    int (*unload)(struct driver *driver);         /* Unload driver */
    int (*probe)(struct driver *driver, struct device *dev); /* Probe device */
    int (*remove)(struct driver *driver, struct device *dev); /* Remove device */
    
    /* Power management */
    int (*suspend)(struct driver *driver, struct device *dev); /* Suspend device */
    int (*resume)(struct driver *driver, struct device *dev);  /* Resume device */
    int (*runtime_suspend)(struct driver *driver, struct device *dev); /* Runtime suspend */
    int (*runtime_resume)(struct driver *driver, struct device *dev);  /* Runtime resume */
    
    /* Hotplug operations */
    int (*add)(struct driver *driver, struct device *dev); /* Add device */
    int (*remove_device)(struct driver *driver, struct device *dev); /* Remove device */
    
    /* Error handling */
    int (*error_handler)(struct driver *driver, struct device *dev, int error); /* Error handler */
    int (*recover)(struct driver *driver, struct device *dev); /* Recovery */
    
    /* Validation */
    int (*validate)(struct driver *driver); /* Validate driver */
    int (*sanitize)(struct driver *driver); /* Sanitize driver */
};

/* ===================================================================== */
/* Driver Lifecycle Context */
/* ===================================================================== */

struct driver_lifecycle_ctx {
    /* Driver tracking */
    struct list_head drivers;        /* All drivers */
    spinlock_t driver_lock;          /* Driver lock */
    
    /* Device tracking */
    struct list_head devices;        /* All devices */
    spinlock_t device_lock;          /* Device lock */
    
    /* Hotplug management */
    struct list_head hotplug_devices; /* Hotplug devices */
    spinlock_t hotplug_lock;         /* Hotplug lock */
    struct workqueue_struct *hotplug_wq; /* Hotplug workqueue */
    
    /* Power management */
    struct list_head power_devices;  /* Power managed devices */
    spinlock_t power_lock;           /* Power lock */
    
    /* Error handling */
    struct list_head error_devices;  /* Devices with errors */
    spinlock_t error_lock;           /* Error lock */
    
    /* Statistics */
    atomic_t driver_count;           /* Number of drivers */
    atomic_t device_count;           /* Number of devices */
    atomic_t active_drivers;         /* Number of active drivers */
    atomic_t bound_devices;          /* Number of bound devices */
    atomic_t error_count;            /* Number of errors */
    
    /* Configuration */
    bool hotplug_enabled;            /* Hotplug enabled */
    bool power_management_enabled;    /* Power management enabled */
    bool error_recovery_enabled;      /* Error recovery enabled */
    int max_probe_retries;           /* Maximum probe retries */
    int probe_retry_delay;           /* Probe retry delay (ms) */
    
    /* State */
    bool initialized;                 /* Context initialized */
};

/* ===================================================================== */
/* Driver Lifecycle Interface */
/* ===================================================================== */

/**
 * driver_lifecycle_init - Initialize driver lifecycle system
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_lifecycle_init(void);

/**
 * driver_lifecycle_shutdown - Shutdown driver lifecycle system
 */
void driver_lifecycle_shutdown(void);

/**
 * driver_lifecycle_load - Load driver with lifecycle management
 * @driver: Driver to load
 * @flags: Load flags
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_lifecycle_load(struct driver *driver, unsigned int flags);

/**
 * driver_lifecycle_unload - Unload driver with lifecycle management
 * @driver: Driver to unload
 * @force: Force unload
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_lifecycle_unload(struct driver *driver, bool force);

/**
 * driver_lifecycle_probe - Probe device with lifecycle management
 * @driver: Driver to probe with
 * @device: Device to probe
 * @async: Asynchronous probing
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_lifecycle_probe(struct driver *driver, struct device *device, bool async);

/**
 * driver_lifecycle_remove - Remove device with lifecycle management
 * @driver: Driver to remove from
 * @device: Device to remove
 * @force: Force removal
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_lifecycle_remove(struct driver *driver, struct device *device, bool force);

/* ===================================================================== */
/* Driver Reference Management */
/* ===================================================================== */

/**
 * driver_get - Get driver reference
 * @driver: Driver to reference
 * @type: Reference type
 * @file: Source file
 * @line: Source line
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_get(struct driver *driver, const char *type, const char *file, int line);

/**
 * driver_put - Put driver reference
 * @driver: Driver to unreference
 * @type: Reference type
 * @file: Source file
 * @line: Source line
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_put(struct driver *driver, const char *type, const char *file, int line);

/**
 * driver_try_get - Try to get driver reference without blocking
 * @driver: Driver to reference
 * @type: Reference type
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_try_get(struct driver *driver, const char *type);

/**
 * driver_refcount_read - Read driver reference count
 * @driver: Driver to read
 * 
 * Return: Reference count
 */
int driver_refcount_read(struct driver *driver);

/**
 * driver_refcount_debug - Debug driver reference count
 * @driver: Driver to debug
 * @buf: Buffer to write debug info to
 * @size: Buffer size
 * 
 * Return: Number of bytes written
 */
int driver_refcount_debug(struct driver *driver, char *buf, size_t size);

/* ===================================================================== */
/* Device Lifecycle Management */
/* ===================================================================== */

/**
 * device_lifecycle_add - Add device with lifecycle management
 * @device: Device to add
 * @hotplug: Hotplug addition
 * 
 * Return: 0 on success, negative error on failure
 */
int device_lifecycle_add(struct device *device, bool hotplug);

/**
 * device_lifecycle_remove - Remove device with lifecycle management
 * @device: Device to remove
 * @force: Force removal
 * 
 * Return: 0 on success, negative error on failure
 */
int device_lifecycle_remove(struct device *device, bool force);

/**
 * device_lifecycle_bind - Bind device to driver
 * @device: Device to bind
 * @driver: Driver to bind to
 * 
 * Return: 0 on success, negative error on failure
 */
int device_lifecycle_bind(struct device *device, struct driver *driver);

/**
 * device_lifecycle_unbind - Unbind device from driver
 * @device: Device to unbind
 * @force: Force unbind
 * 
 * Return: 0 on success, negative error on failure
 */
int device_lifecycle_unbind(struct device *device, bool force);

/**
 * device_lifecycle_suspend - Suspend device
 * @device: Device to suspend
 * 
 * Return: 0 on success, negative error on failure
 */
int device_lifecycle_suspend(struct device *device);

/**
 * device_lifecycle_resume - Resume device
 * @device: Device to resume
 * 
 * Return: 0 on success, negative error on failure
 */
int device_lifecycle_resume(struct device *device);

/* ===================================================================== */
/* Hotplug Management */
/* ===================================================================== */

/**
 * driver_lifecycle_hotplug_add - Handle hotplug device addition
 * @device: Device being added
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_lifecycle_hotplug_add(struct device *device);

/**
 * driver_lifecycle_hotplug_remove - Handle hotplug device removal
 * @device: Device being removed
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_lifecycle_hotplug_remove(struct device *device);

/**
 * driver_lifecycle_hotplug_enable - Enable hotplug for driver
 * @driver: Driver to enable hotplug for
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_lifecycle_hotplug_enable(struct driver *driver);

/**
 * driver_lifecycle_hotplug_disable - Disable hotplug for driver
 * @driver: Driver to disable hotplug for
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_lifecycle_hotplug_disable(struct driver *driver);

/* ===================================================================== */
/* Error Handling and Recovery */
/* ===================================================================== */

/**
 * driver_lifecycle_error_report - Report driver error
 * @driver: Driver with error
 * @device: Device with error
 * @error: Error code
 * @message: Error message
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_lifecycle_error_report(struct driver *driver, struct device *device,
                                 int error, const char *message);

/**
 * driver_lifecycle_error_recover - Attempt error recovery
 * @driver: Driver with error
 * @device: Device with error
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_lifecycle_error_recover(struct driver *driver, struct device *device);

/**
 * driver_lifecycle_error_reset - Reset error state
 * @driver: Driver to reset
 * @device: Device to reset
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_lifecycle_error_reset(struct driver *driver, struct device *device);

/* ===================================================================== */
/* Validation and Sanitization */
/* ===================================================================== */

/**
 * driver_lifecycle_validate - Validate driver lifecycle
 * @driver: Driver to validate
 * 
 * Return: 0 if valid, negative error if invalid
 */
int driver_lifecycle_validate(struct driver *driver);

/**
 * driver_lifecycle_sanitize - Sanitize driver lifecycle
 * @driver: Driver to sanitize
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_lifecycle_sanitize(struct driver *driver);

/**
 * driver_lifecycle_validate_all - Validate all drivers
 * 
 * Return: 0 if all valid, negative error if any invalid
 */
int driver_lifecycle_validate_all(void);

/* ===================================================================== */
/* Statistics and Debugging */
/* ===================================================================== */

/**
 * driver_lifecycle_get_stats - Get lifecycle statistics
 * @buf: Buffer to write stats to
 * @size: Buffer size
 * 
 * Return: Number of bytes written
 */
int driver_lifecycle_get_stats(char *buf, size_t size);

/**
 * driver_lifecycle_dump_drivers - Dump all drivers
 * @buf: Buffer to write dump to
 * @size: Buffer size
 * 
 * Return: Number of bytes written
 */
int driver_lifecycle_dump_drivers(char *buf, size_t size);

/**
 * driver_lifecycle_dump_devices - Dump all devices
 * @buf: Buffer to write dump to
 * @size: Buffer size
 * 
 * Return: Number of bytes written
 */
int driver_lifecycle_dump_devices(char *buf, size_t size);

/**
 * driver_lifecycle_debug_info - Get debug information
 * @driver: Driver to debug
 * @buf: Buffer to write info to
 * @size: Buffer size
 * 
 * Return: Number of bytes written
 */
int driver_lifecycle_debug_info(struct driver *driver, char *buf, size_t size);

/* ===================================================================== */
/* Configuration Management */
/* ===================================================================== */

/**
 * driver_lifecycle_set_config - Set lifecycle configuration
 * @hotplug_enabled: Enable hotplug
 * @power_management_enabled: Enable power management
 * @error_recovery_enabled: Enable error recovery
 * @max_probe_retries: Maximum probe retries
 * @probe_retry_delay: Probe retry delay in ms
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_lifecycle_set_config(bool hotplug_enabled, bool power_management_enabled,
                               bool error_recovery_enabled, int max_probe_retries,
                               int probe_retry_delay);

/**
 * driver_lifecycle_get_config - Get lifecycle configuration
 * @hotplug_enabled: Output for hotplug enabled
 * @power_management_enabled: Output for power management enabled
 * @error_recovery_enabled: Output for error recovery enabled
 * @max_probe_retries: Output for maximum probe retries
 * @probe_retry_delay: Output for probe retry delay
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_lifecycle_get_config(bool *hotplug_enabled, bool *power_management_enabled,
                               bool *error_recovery_enabled, int *max_probe_retries,
                               int *probe_retry_delay);

/* ===================================================================== */
/* Helper Macros */
/* ===================================================================== */

#define driver_get_ref(driver) \
    driver_get(driver, "generic", __FILE__, __LINE__)

#define driver_put_ref(driver) \
    driver_put(driver, "generic", __FILE__, __LINE__)

#define driver_get_module(driver) \
    driver_get(driver, "module", __FILE__, __LINE__)

#define driver_put_module(driver) \
    driver_put(driver, "module", __FILE__, __LINE__)

#define driver_get_device(driver) \
    driver_get(driver, "device", __FILE__, __LINE__)

#define driver_put_device(driver) \
    driver_put(driver, "device", __FILE__, __LINE__)

#define driver_get_client(driver) \
    driver_get(driver, "client", __FILE__, __LINE__)

#define driver_put_client(driver) \
    driver_put(driver, "client", __FILE__, __LINE__)

/* ===================================================================== */
/* Error Codes */
/* ===================================================================== */

#define DRIVER_LIFECYCLE_SUCCESS        0       /* Success */
#define DRIVER_LIFECYCLE_ERROR_INVALID  -1      /* Invalid parameter */
#define DRIVER_LIFECYCLE_ERROR_NOMEM    -2      /* Out of memory */
#define DRIVER_LIFECYCLE_ERROR_BUSY     -3      /* Driver busy */
#define DRIVER_LIFECYCLE_ERROR_INUSE    -4      /* Driver in use */
#define DRIVER_LIFECYCLE_ERROR_NOTFOUND -5      /* Driver not found */
#define DRIVER_LIFECYCLE_ERROR_PROBE    -6      /* Probe failed */
#define DRIVER_LIFECYCLE_ERROR_REMOVE   -7      /* Remove failed */
#define DRIVER_LIFECYCLE_ERROR_SUSPEND  -8      /* Suspend failed */
#define DRIVER_LIFECYCLE_ERROR_RESUME   -9      /* Resume failed */
#define DRIVER_LIFECYCLE_ERROR_HOTPLUG  -10     /* Hotplug failed */
#define DRIVER_LIFECYCLE_ERROR_RECOVER  -11     /* Recovery failed */
#define DRIVER_LIFECYCLE_ERROR_VALIDATE -12     /* Validation failed */
#define DRIVER_LIFECYCLE_ERROR_CORRUPT -13     /* Corruption detected */
#define DRIVER_LIFECYCLE_ERROR_TIMEOUT  -14     /* Operation timeout */
#define DRIVER_LIFECYCLE_ERROR_FORBIDDEN -15    /* Operation forbidden */

#endif /* _DRIVERS_LIFECYCLE_H */
