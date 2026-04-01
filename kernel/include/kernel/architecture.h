/*
 * GC-AOS Kernel - Architecture Hardening Layer
 * 
 * Establishes clear layer separation and architectural boundaries
 * between hardware abstraction, kernel core, and user space interface.
 */

#ifndef _KERNEL_ARCHITECTURE_H
#define _KERNEL_ARCHITECTURE_H

#include "types.h"

/* ===================================================================== */
/* Architecture Layers */
/* ===================================================================== */

/* Layer definitions */
#define ARCH_LAYER_HAL              0       /* Hardware Abstraction Layer */
#define ARCH_LAYER_CORE             1       /* Kernel Core */
#define ARCH_LAYER_SUBSYSTEM        2       /* Subsystem Layer */
#define ARCH_LAYER_USERSPACE_IFACE  3       /* User Space Interface */

/* Layer boundaries */
#define ARCH_LAYER_MIN              ARCH_LAYER_HAL
#define ARCH_LAYER_MAX              ARCH_LAYER_USERSPACE_IFACE

/* ===================================================================== */
/* Hardware Abstraction Layer (HAL) */
/* ===================================================================== */

/* HAL components */
enum hal_component {
    HAL_COMPONENT_CPU,              /* CPU management */
    HAL_COMPONENT_MEMORY,           /* Memory management */
    HAL_COMPONENT_INTERRUPT,        /* Interrupt handling */
    HAL_COMPONENT_TIMER,            /* Timer management */
    HAL_COMPONENT_DMA,              /* DMA management */
    HAL_COMPONENT_POWER,            /* Power management */
    HAL_COMPONENT_CLOCK,            /* Clock management */
    HAL_COMPONENT_CACHE,            /* Cache management */
    HAL_COMPONENT_MMU,              /* Memory management unit */
    HAL_COMPONENT_DEBUG,            /* Debug interface */
    HAL_COMPONENT_PERIPHERAL,       /* Peripheral management */
    HAL_COMPONENT_MAX
};

/* HAL interface structure */
struct hal_interface {
    /* Component identification */
    enum hal_component component;   /* Component type */
    const char *name;               /* Component name */
    int version;                    /* Interface version */
    
    /* Component operations */
    int (*init)(void);               /* Initialize component */
    void (*shutdown)(void);          /* Shutdown component */
    int (*suspend)(void);            /* Suspend component */
    int (*resume)(void);             /* Resume component */
    
    /* Component state */
    bool initialized;                /* Component initialized */
    bool active;                     /* Component active */
    bool suspended;                  /* Component suspended */
    
    /* Component properties */
    uint32_t properties;            /* Component properties */
    void *private_data;              /* Private component data */
    
    /* Component statistics */
    uint64_t init_time;             /* Initialization time */
    uint64_t suspend_time;          /* Suspend time */
    uint64_t resume_time;           /* Resume time */
    uint32_t error_count;            /* Error count */
    
    /* Component lock */
    spinlock_t lock;                 /* Component lock */
};

/* HAL properties */
#define HAL_PROP_CRITICAL           (1 << 0)  /* Critical component */
#define HAL_PROP_ESSENTIAL           (1 << 1)  /* Essential component */
#define HAL_PROP_REMOVABLE           (1 << 2)  /* Removable component */
#define HAL_PROP_POWER_MANAGED      (1 << 3)  /* Power managed */
#define HAL_PROP_HOTPLUG            (1 << 4)  /* Hotplug capable */
#define HAL_PROP_DEBUG              (1 << 5)  /* Debug interface */
#define HAL_PROP_PERFORMANCE        (1 << 6)  /* Performance critical */
#define HAL_PROP_SECURITY           (1 << 7)  /* Security relevant */

/* ===================================================================== */
/* Kernel Core Layer */
/* ===================================================================== */

/* Core subsystems */
enum core_subsystem {
    CORE_SUBSYSTEM_SCHEDULER,       /* Process scheduler */
    CORE_SUBSYSTEM_MEMORY,          /* Memory management */
    CORE_SUBSYSTEM_VFS,             /* Virtual filesystem */
    CORE_SUBSYSTEM_NETWORK,         /* Network stack */
    CORE_SUBSYSTEM_IPC,             /* Inter-process communication */
    CORE_SUBSYSTEM_SYSCALL,         /* System call interface */
    CORE_SUBSYSTEM_SECURITY,        /* Security subsystem */
    CORE_SUBSYSTEM_MODULE,          /* Module system */
    CORE_SUBSYSTEM_DRIVER,          /* Driver system */
    CORE_SUBSYSTEM_MAX
};

/* Core subsystem interface */
struct core_interface {
    /* Subsystem identification */
    enum core_subsystem subsystem;   /* Subsystem type */
    const char *name;               /* Subsystem name */
    int version;                    /* Interface version */
    
    /* Subsystem operations */
    int (*init)(void);               /* Initialize subsystem */
    void (*shutdown)(void);          /* Shutdown subsystem */
    int (*suspend)(void);            /* Suspend subsystem */
    int (*resume)(void);             /* Resume subsystem */
    int (*configure)(const char *config); /* Configure subsystem */
    
    /* Subsystem state */
    bool initialized;                /* Subsystem initialized */
    bool active;                     /* Subsystem active */
    bool suspended;                  /* Subsystem suspended */
    bool configured;                 /* Subsystem configured */
    
    /* Subsystem dependencies */
    enum core_subsystem *dependencies; /* Dependency list */
    int dependency_count;            /* Number of dependencies */
    
    /* Subsystem properties */
    uint32_t properties;            /* Subsystem properties */
    void *private_data;              /* Private subsystem data */
    
    /* Subsystem statistics */
    uint64_t init_time;             /* Initialization time */
    uint64_t config_time;            /* Configuration time */
    uint64_t suspend_time;          /* Suspend time */
    uint64_t resume_time;           /* Resume time */
    uint32_t error_count;            /* Error count */
    uint32_t request_count;          /* Request count */
    
    /* Subsystem lock */
    spinlock_t lock;                 /* Subsystem lock */
};

/* Core subsystem properties */
#define CORE_PROP_CRITICAL           (1 << 0)  /* Critical subsystem */
#define CORE_PROP_ESSENTIAL           (1 << 1)  /* Essential subsystem */
#define CORE_PROP_REMOVABLE           (1 << 2)  /* Removable subsystem */
#define CORE_PROP_POWER_MANAGED      (1 << 3)  /* Power managed */
#define CORE_PROP_SECURITY           (1 << 4)  /* Security relevant */
#define CORE_PROP_PERFORMANCE        (1 << 5)  /* Performance critical */
#define CORE_PROP_USER_VISIBLE       (1 << 6)  /* User visible */
#define CORE_PROP_CONFIGURABLE       (1 << 7)  /* Configurable */

/* ===================================================================== */
/* Subsystem Layer */
/* ===================================================================== */

/* Subsystem categories */
enum subsystem_category {
    SUBSYSTEM_CATEGORY_FS,           /* Filesystem subsystems */
    SUBSYSTEM_CATEGORY_NET,          /* Network subsystems */
    SUBSYSTEM_CATEGORY_DRIVER,       /* Driver subsystems */
    SUBSYSTEM_CATEGORY_PROTOCOL,     /* Protocol subsystems */
    SUBSYSTEM_CATEGORY_CRYPTO,       /* Cryptography subsystems */
    SUBSYSTEM_CATEGORY_MEDIA,        /* Media subsystems */
    SUBSYSTEM_CATEGORY_GRAPHICS,     /* Graphics subsystems */
    SUBSYSTEM_CATEGORY_AUDIO,        /* Audio subsystems */
    SUBSYSTEM_CATEGORY_INPUT,        /* Input subsystems */
    SUBSYSTEM_CATEGORY_STORAGE,      /* Storage subsystems */
    SUBSYSTEM_CATEGORY_MAX
};

/* Subsystem interface */
struct subsystem_interface {
    /* Subsystem identification */
    const char *name;               /* Subsystem name */
    const char *version;            /* Subsystem version */
    enum subsystem_category category; /* Subsystem category */
    int priority;                    /* Subsystem priority */
    
    /* Subsystem operations */
    int (*init)(void);               /* Initialize subsystem */
    void (*shutdown)(void);          /* Shutdown subsystem */
    int (*suspend)(void);            /* Suspend subsystem */
    int (*resume)(void);             /* Resume subsystem */
    int (*configure)(const char *config); /* Configure subsystem */
    
    /* Subsystem interfaces */
    void *interface_table;           /* Interface table */
    int interface_count;             /* Interface count */
    
    /* Subsystem state */
    bool initialized;                /* Subsystem initialized */
    bool active;                     /* Subsystem active */
    bool suspended;                  /* Subsystem suspended */
    bool configured;                 /* Subsystem configured */
    
    /* Subsystem dependencies */
    char **dependencies;             /* Dependency names */
    int dependency_count;            /* Number of dependencies */
    
    /* Subsystem properties */
    uint32_t properties;            /* Subsystem properties */
    void *private_data;              /* Private subsystem data */
    
    /* Subsystem statistics */
    uint64_t init_time;             /* Initialization time */
    uint64_t config_time;            /* Configuration time */
    uint64_t suspend_time;          /* Suspend time */
    uint64_t resume_time;           /* Resume time */
    uint32_t error_count;            /* Error count */
    uint32_t request_count;          /* Request count */
    
    /* Subsystem lock */
    spinlock_t lock;                 /* Subsystem lock */
};

/* Subsystem properties */
#define SUBSYSTEM_PROP_CRITICAL      (1 << 0)  /* Critical subsystem */
#define SUBSYSTEM_PROP_ESSENTIAL      (1 << 1)  /* Essential subsystem */
#define SUBSYSTEM_PROP_REMOVABLE      (1 << 2)  /* Removable subsystem */
#define SUBSYSTEM_PROP_POWER_MANAGED (1 << 3)  /* Power managed */
#define SUBSYSTEM_PROP_SECURITY       (1 << 4)  /* Security relevant */
#define SUBSYSTEM_PROP_PERFORMANCE    (1 << 5)  /* Performance critical */
#define SUBSYSTEM_PROP_USER_VISIBLE   (1 << 6)  /* User visible */
#define SUBSYSTEM_PROP_CONFIGURABLE   (1 << 7)  /* Configurable */
#define SUBSYSTEM_PROP_MODULE         (1 << 8)  /* Module subsystem */
#define SUBSYSTEM_PROP_USERSPACE      (1 << 9)  /* Userspace accessible */

/* ===================================================================== */
/* User Space Interface Layer */
/* ===================================================================== */

/* User space interface types */
enum userspace_interface_type {
    USERSPACE_INTERFACE_SYSCALL,     /* System call interface */
    USERSPACE_INTERFACE_PROC,        /* Proc filesystem */
    USERSPACE_INTERFACE_SYSFS,       /* Sys filesystem */
    USERSPACE_INTERFACE_DEVFS,       /* Device filesystem */
    USERSPACE_INTERFACE_DEBUGFS,     /* Debug filesystem */
    USERSPACE_INTERFACE_CONFIGFS,    /* Config filesystem */
    USERSPACE_INTERFACE_SOCKET,      /* Socket interface */
    USERSPACE_INTERFACE_SIGNAL,      /* Signal interface */
    USERSPACE_INTERFACE_TIMER,       /* Timer interface */
    USERSPACE_INTERFACE_MAX
};

/* User space interface structure */
struct userspace_interface {
    /* Interface identification */
    enum userspace_interface_type type; /* Interface type */
    const char *name;               /* Interface name */
    const char *path;                /* Interface path */
    int version;                    /* Interface version */
    
    /* Interface operations */
    int (*init)(void);               /* Initialize interface */
    void (*shutdown)(void);          /* Shutdown interface */
    int (*open)(const char *path, int flags); /* Open interface */
    int (*close)(int fd);            /* Close interface */
    ssize_t (*read)(int fd, void *buf, size_t count); /* Read from interface */
    ssize_t (*write)(int fd, const void *buf, size_t count); /* Write to interface */
    int (*ioctl)(int fd, unsigned int cmd, unsigned long arg); /* IOCTL interface */
    
    /* Interface state */
    bool initialized;                /* Interface initialized */
    bool active;                     /* Interface active */
    bool mounted;                    /* Interface mounted */
    
    /* Interface properties */
    uint32_t properties;            /* Interface properties */
    void *private_data;              /* Private interface data */
    
    /* Interface statistics */
    uint64_t init_time;             /* Initialization time */
    uint64_t mount_time;            /* Mount time */
    uint32_t open_count;             /* Open count */
    uint32_t read_count;             /* Read count */
    uint32_t write_count;            /* Write count */
    uint32_t error_count;            /* Error count */
    
    /* Interface lock */
    spinlock_t lock;                 /* Interface lock */
};

/* User space interface properties */
#define USERSPACE_PROP_CRITICAL      (1 << 0)  /* Critical interface */
#define USERSPACE_PROP_ESSENTIAL      (1 << 1)  /* Essential interface */
#define USERSPACE_PROP_REMOVABLE      (1 << 2)  /* Removable interface */
#define USERSPACE_PROP_SECURITY       (1 << 3)  /* Security relevant */
#define USERSPACE_PROP_PERFORMANCE    (1 << 4)  /* Performance critical */
#define USERSPACE_PROP_CONFIGURABLE   (1 << 5)  /* Configurable */
#define USERSPACE_PROP_PRIVILEGED     (1 << 6)  /* Privileged interface */
#define USERSPACE_PROP_GLOBAL         (1 << 7)  /* Global interface */

/* ===================================================================== */
/* Architecture Management */
/* ===================================================================== */

/* Architecture context */
struct architecture_context {
    /* Layer management */
    struct hal_interface *hal_components[HAL_COMPONENT_MAX];
    struct core_interface *core_subsystems[CORE_SUBSYSTEM_MAX];
    struct list_head subsystems;      /* All subsystems */
    struct userspace_interface *userspace_interfaces[USERSPACE_INTERFACE_MAX];
    
    /* Layer state */
    bool hal_initialized;            /* HAL initialized */
    bool core_initialized;           /* Core initialized */
    bool subsystems_initialized;     /* Subsystems initialized */
    bool userspace_initialized;       /* Userspace initialized */
    
    /* Architecture properties */
    uint32_t arch_properties;        /* Architecture properties */
    int arch_version;                /* Architecture version */
    char arch_name[64];              /* Architecture name */
    
    /* Architecture statistics */
    uint64_t boot_time;              /* Boot time */
    uint64_t init_time;              /* Initialization time */
    uint32_t component_count;         /* Component count */
    uint32_t subsystem_count;        /* Subsystem count */
    uint32_t interface_count;         /* Interface count */
    uint32_t error_count;             /* Error count */
    
    /* Architecture locks */
    spinlock_t hal_lock;             /* HAL lock */
    spinlock_t core_lock;            /* Core lock */
    spinlock_t subsystem_lock;       /* Subsystem lock */
    spinlock_t userspace_lock;       /* Userspace lock */
    
    /* Architecture state */
    bool initialized;                 /* Architecture initialized */
    bool shutdown;                   /* Architecture shutdown */
};

/* Architecture properties */
#define ARCH_PROP_CRITICAL           (1 << 0)  /* Critical architecture */
#define ARCH_PROP_ESSENTIAL           (1 << 1)  /* Essential architecture */
#define ARCH_PROP_MODULAR             (1 << 2)  /* Modular architecture */
#define ARCH_PROP_SECURITY            (1 << 3)  /* Security focused */
#define ARCH_PROP_PERFORMANCE        (1 << 4)  /* Performance focused */
#define ARCH_PROP_POWER_MANAGED      (1 << 5)  /* Power managed */
#define ARCH_PROP_DEBUG              (1 << 6)  /* Debug enabled */
#define ARCH_PROP_USERSPACE          (1 << 7)  /* Userspace focused */

/* ===================================================================== */
/* Architecture Interface */
/* ===================================================================== */

/**
 * architecture_init - Initialize architecture layer
 * @arch_name: Architecture name
 * @arch_version: Architecture version
 * @properties: Architecture properties
 * 
 * Return: 0 on success, negative error on failure
 */
int architecture_init(const char *arch_name, int arch_version, uint32_t properties);

/**
 * architecture_shutdown - Shutdown architecture layer
 */
void architecture_shutdown(void);

/**
 * architecture_register_hal - Register HAL component
 * @component: Component type
 * @interface: Component interface
 * 
 * Return: 0 on success, negative error on failure
 */
int architecture_register_hal(enum hal_component component, struct hal_interface *interface);

/**
 * architecture_register_core - Register core subsystem
 * @subsystem: Subsystem type
 * @interface: Subsystem interface
 * 
 * Return: 0 on success, negative error on failure
 */
int architecture_register_core(enum core_subsystem subsystem, struct core_interface *interface);

/**
 * architecture_register_subsystem - Register subsystem
 * @interface: Subsystem interface
 * 
 * Return: 0 on success, negative error on failure
 */
int architecture_register_subsystem(struct subsystem_interface *interface);

/**
 * architecture_register_userspace - Register userspace interface
 * @type: Interface type
 * @interface: Interface interface
 * 
 * Return: 0 on success, negative error on failure
 */
int architecture_register_userspace(enum userspace_interface_type type, 
                                   struct userspace_interface *interface);

/**
 * architecture_get_hal - Get HAL component
 * @component: Component type
 * 
 * Return: Component interface or NULL if not found
 */
struct hal_interface *architecture_get_hal(enum hal_component component);

/**
 * architecture_get_core - Get core subsystem
 * @subsystem: Subsystem type
 * 
 * Return: Subsystem interface or NULL if not found
 */
struct core_interface *architecture_get_core(enum core_subsystem subsystem);

/**
 * architecture_get_subsystem - Get subsystem
 * @name: Subsystem name
 * 
 * Return: Subsystem interface or NULL if not found
 */
struct subsystem_interface *architecture_get_subsystem(const char *name);

/**
 * architecture_get_userspace - Get userspace interface
 * @type: Interface type
 * 
 * Return: Interface interface or NULL if not found
 */
struct userspace_interface *architecture_get_userspace(enum userspace_interface_type type);

/**
 * architecture_validate - Validate architecture state
 * 
 * Return: 0 if valid, negative error if invalid
 */
int architecture_validate(void);

/**
 * architecture_get_stats - Get architecture statistics
 * @buf: Buffer to write stats to
 * @size: Buffer size
 * 
 * Return: Number of bytes written
 */
int architecture_get_stats(char *buf, size_t size);

/* ===================================================================== */
/* Layer Validation */
/* ===================================================================== */

/**
 * architecture_validate_hal - Validate HAL layer
 * 
 * Return: 0 if valid, negative error if invalid
 */
int architecture_validate_hal(void);

/**
 * architecture_validate_core - Validate core layer
 * 
 * Return: 0 if valid, negative error if invalid
 */
int architecture_validate_core(void);

/**
 * architecture_validate_subsystems - Validate subsystem layer
 * 
 * Return: 0 if valid, negative error if invalid
 */
int architecture_validate_subsystems(void);

/**
 * architecture_validate_userspace - Validate userspace layer
 * 
 * Return: 0 if valid, negative error if invalid
 */
int architecture_validate_userspace(void);

/* ===================================================================== */
/* Layer Dependencies */
/* ===================================================================== */

/**
 * architecture_check_dependencies - Check layer dependencies
 * 
 * Return: 0 if dependencies are valid, negative error if invalid
 */
int architecture_check_dependencies(void);

/**
 * architecture_resolve_dependencies - Resolve layer dependencies
 * 
 * Return: 0 if dependencies resolved, negative error if not
 */
int architecture_resolve_dependencies(void);

/* ===================================================================== */
/* Helper Macros */
/* ===================================================================== */

#define architecture_get_component(component) \
    architecture_get_hal(component)

#define architecture_get_subsystem(subsystem) \
    architecture_get_core(subsystem)

#define architecture_get_interface(type) \
    architecture_get_userspace(type)

#define architecture_validate_layer(layer) \
    architecture_validate_##layer()

/* ===================================================================== */
/* Error Codes */
/* ===================================================================== */

#define ARCH_SUCCESS                  0       /* Success */
#define ARCH_ERROR_INVALID            -1      /* Invalid parameter */
#define ARCH_ERROR_NOT_FOUND          -2      /* Component not found */
#define ARCH_ERROR_ALREADY_EXISTS     -3      /* Component already exists */
#define ARCH_ERROR_DEPENDENCY        -4      /* Dependency error */
#define ARCH_ERROR_INIT_FAILED        -5      /* Initialization failed */
#define ARCH_ERROR_SHUTDOWN_FAILED    -6      /* Shutdown failed */
#define ARCH_ERROR_SUSPEND_FAILED     -7      /* Suspend failed */
#define ARCH_ERROR_RESUME_FAILED      -8      /* Resume failed */
#define ARCH_ERROR_CONFIG_FAILED     -9      /* Configuration failed */
#define ARCH_ERROR_VALIDATION        -10     /* Validation failed */
#define ARCH_ERROR_CORRUPTION        -11     /* Corruption detected */
#define ARCH_ERROR_TIMEOUT           -12     /* Operation timeout */
#define ARCH_ERROR_PERMISSION         -13     /* Permission denied */
#define ARCH_ERROR_RESOURCE          -14     /* Resource error */

#endif /* _KERNEL_ARCHITECTURE_H */
