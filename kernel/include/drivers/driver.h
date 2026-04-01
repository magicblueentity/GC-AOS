/*
 * GC-AOS Kernel - Unified Driver Model
 * 
 * Provides a unified interface for device drivers with dynamic
 * loading/unloading capabilities and proper resource management.
 */

#ifndef _DRIVERS_DRIVER_H
#define _DRIVERS_DRIVER_H

#include "types.h"
#include "kernel/module.h"

/* ===================================================================== */
/* Driver Types and Classes */
/* ===================================================================== */

typedef enum {
    DRIVER_TYPE_UNKNOWN = 0,
    DRIVER_TYPE_CHAR,          /* Character device */
    DRIVER_TYPE_BLOCK,         /* Block device */
    DRIVER_TYPE_NETWORK,       /* Network device */
    DRIVER_TYPE_USB,           /* USB device */
    DRIVER_TYPE_PCI,           /* PCI device */
    DRIVER_TYPE_PLATFORM,      /* Platform device */
    DRIVER_TYPE_I2C,           /* I2C device */
    DRIVER_TYPE_SPI,           /* SPI device */
    DRIVER_TYPE_GPIO,          /* GPIO device */
    DRIVER_TYPE_INPUT,         /* Input device */
    DRIVER_TYPE_GRAPHICS,      /* Graphics device */
    DRIVER_TYPE_AUDIO,         /* Audio device */
    DRIVER_TYPE_STORAGE,       /* Storage device */
    DRIVER_TYPE_MAX
} driver_type_t;

/* Device classes */
typedef enum {
    DEVICE_CLASS_UNKNOWN = 0,
    DEVICE_CLASS_CONSOLE,      /* Console device */
    DEVICE_CLASS_SERIAL,       /* Serial port */
    DEVICE_CLASS_PARALLEL,     /* Parallel port */
    DEVICE_CLASS_DISK,         /* Disk device */
    DEVICE_CLASS_TAPE,         /* Tape device */
    DEVICE_CLASS_CDROM,        /* CD-ROM device */
    DEVICE_CLASS_SCANNER,      /* Scanner device */
    DEVICE_CLASS_ETH,          /* Ethernet device */
    DEVICE_CLASS_WLAN,         /* Wireless device */
    DEVICE_CLASS_KEYBOARD,     /* Keyboard device */
    DEVICE_CLASS_MOUSE,        /* Mouse device */
    DEVICE_CLASS_JOYSTICK,     /* Joystick device */
    DEVICE_CLASS_TOUCHSCREEN,  /* Touchscreen device */
    DEVICE_CLASS_DISPLAY,      /* Display device */
    DEVICE_CLASS_SOUND,        /* Sound device */
    DEVICE_CLASS_VIDEO,        /* Video device */
    DEVICE_CLASS_HID,          /* Human Interface Device */
    DEVICE_CLASS_POWER,        /* Power management */
    DEVICE_CLASS_THERMAL,      /* Thermal management */
    DEVICE_CLASS_MAX
} device_class_t;

/* ===================================================================== */
/* Device Structure */
/* ===================================================================== */

struct device {
    /* Device identification */
    char name[64];                    /* Device name */
    char modalias[128];               /* Modalias string */
    device_class_t class;             /* Device class */
    driver_type_t type;               /* Driver type */
    
    /* Device hierarchy */
    struct device *parent;            /* Parent device */
    struct list_head children;        /* Child devices */
    struct list_head sibling;         /* Sibling devices */
    
    /* Device resources */
    struct resource *resource;        /* Resource list */
    void *platform_data;              /* Platform-specific data */
    void *driver_data;                /* Driver-specific data */
    
    /* Device state */
    bool initialized;                 /* Device initialized */
    bool enabled;                     /* Device enabled */
    bool removable;                   /* Removable device */
    
    /* Device properties */
    struct device_property *properties; /* Device properties */
    
    /* Power management */
    bool can_wakeup;                  /* Can wake up system */
    bool runtime_pm;                  /* Runtime power management */
    
    /* Driver binding */
    struct driver *driver;            /* Bound driver */
    struct list_head driver_list;    /* List of bound drivers */
    
    /* Reference counting */
    atomic_t refcount;                /* Reference count */
    
    /* Device lock */
    spinlock_t lock;                  /* Device lock */
    
    /* Device nodes */
    struct list_head devnodes;        /* Device nodes */
    
    /* Statistics */
    uint64_t open_count;              /* Number of opens */
    uint64_t read_count;              /* Number of reads */
    uint64_t write_count;             /* Number of writes */
    uint64_t error_count;             /* Number of errors */
};

/* ===================================================================== */
/* Driver Structure */
/* ===================================================================== */

struct driver {
    /* Driver identification */
    char name[64];                    /* Driver name */
    char version[32];                 /* Driver version */
    char author[64];                  /* Driver author */
    char description[256];             /* Driver description */
    
    /* Driver type and class */
    driver_type_t type;               /* Driver type */
    device_class_t class;             /* Device class */
    
    /* Module information */
    struct module *owner;             /* Owner module */
    
    /* Device matching */
    struct device_id *id_table;       /* Device ID table */
    int (*probe)(struct device *dev);  /* Probe function */
    void (*remove)(struct device *dev); /* Remove function */
    
    /* Device operations */
    struct device_ops *ops;           /* Device operations */
    
    /* Power management */
    int (*suspend)(struct device *dev); /* Suspend function */
    int (*resume)(struct device *dev);  /* Resume function */
    int (*runtime_suspend)(struct device *dev); /* Runtime suspend */
    int (*runtime_resume)(struct device *dev);  /* Runtime resume */
    
    /* Driver state */
    bool loaded;                      /* Driver loaded */
    bool initialized;                 /* Driver initialized */
    
    /* Device list */
    struct list_head device_list;     /* List of bound devices */
    
    /* Driver statistics */
    int num_devices;                  /* Number of bound devices */
    uint64_t probe_count;             /* Number of probes */
    uint64_t remove_count;            /* Number of removes */
    
    /* Driver lock */
    spinlock_t lock;                  /* Driver lock */
    
    /* List linkage */
    struct list_head list;            /* Driver list */
};

/* ===================================================================== */
/* Device ID Structure */
/* ===================================================================== */

struct device_id {
    /* Device identification */
    uint32_t vendor;                  /* Vendor ID */
    uint32_t device;                  /* Device ID */
    uint32_t subvendor;               /* Sub-vendor ID */
    uint32_t subdevice;               /* Sub-device ID */
    uint32_t class;                   /* Device class */
    uint32_t class_mask;              /* Class mask */
    
    /* Matching flags */
    uint32_t flags;                   /* Matching flags */
    
    /* Driver data */
    void *driver_data;                /* Driver-specific data */
    
    /* Modalias */
    char modalias[128];               /* Modalias string */
    
    /* Terminator */
    uint32_t terminator;              /* Must be zero */
};

/* Device ID matching flags */
#define DEVICE_ID_MATCH_VENDOR       (1 << 0)
#define DEVICE_ID_MATCH_DEVICE       (1 << 1)
#define DEVICE_ID_MATCH_SUBVENDOR    (1 << 2)
#define DEVICE_ID_MATCH_SUBDEVICE    (1 << 3)
#define DEVICE_ID_MATCH_CLASS        (1 << 4)
#define DEVICE_ID_MATCH_CLASS_MASK   (1 << 5)
#define DEVICE_ID_MATCH_MODALIAS     (1 << 6)

/* ===================================================================== */
/* Device Operations */
/* ===================================================================== */

struct device_ops {
    /* File operations */
    int (*open)(struct device *dev, struct file *file);
    int (*release)(struct device *dev, struct file *file);
    ssize_t (*read)(struct device *dev, struct file *file, char *buf, size_t count, loff_t *pos);
    ssize_t (*write)(struct device *dev, struct file *file, const char *buf, size_t count, loff_t *pos);
    loff_t (*llseek)(struct device *dev, struct file *file, loff_t offset, int whence);
    
    /* I/O control */
    int (*ioctl)(struct device *dev, struct file *file, unsigned int cmd, unsigned long arg);
    long (*unlocked_ioctl)(struct device *dev, struct file *file, unsigned int cmd, unsigned long arg);
    long (*compat_ioctl)(struct device *dev, struct file *file, unsigned int cmd, unsigned long arg);
    
    /* Memory mapping */
    int (*mmap)(struct device *dev, struct file *file, void *vma);
    
    /* Polling */
    unsigned int (*poll)(struct device *dev, struct file *file, struct poll_table_struct *wait);
    
    /* Asynchronous I/O */
    ssize_t (*readv)(struct device *dev, struct file *file, const struct iovec *vec, unsigned long count, loff_t *pos);
    ssize_t (*writev)(struct device *dev, struct file *file, const struct iovec *vec, unsigned long count, loff_t *pos);
    
    /* Device-specific operations */
    int (*suspend)(struct device *dev);
    int (*resume)(struct device *dev);
    int (*reset)(struct device *dev);
    int (*flush)(struct device *dev);
    
    /* Power management */
    int (*pm_suspend)(struct device *dev);
    int (*pm_resume)(struct device *dev);
    int (*pm_runtime_suspend)(struct device *dev);
    int (*pm_runtime_resume)(struct device *dev);
    
    /* Event handling */
    void (*event)(struct device *dev, enum device_event event, void *data);
};

/* Device events */
typedef enum {
    DEVICE_EVENT_ADD,                /* Device added */
    DEVICE_EVENT_REMOVE,             /* Device removed */
    DEVICE_EVENT_BIND,               /* Device bound to driver */
    DEVICE_EVENT_UNBIND,             /* Device unbound from driver */
    DEVICE_EVENT_ONLINE,             /* Device online */
    DEVICE_EVENT_OFFLINE,            /* Device offline */
    DEVICE_EVENT_SUSPEND,            /* Device suspended */
    DEVICE_EVENT_RESUME,             /* Device resumed */
    DEVICE_EVENT_RESET,              /* Device reset */
    DEVICE_EVENT_ERROR,              /* Device error */
} device_event_t;

/* ===================================================================== */
/* Resource Management */
/* ===================================================================== */

struct resource {
    resource_size_t start;           /* Resource start */
    resource_size_t end;             /* Resource end */
    const char *name;                /* Resource name */
    unsigned long flags;             /* Resource flags */
    
    /* Resource hierarchy */
    struct resource *parent;         /* Parent resource */
    struct resource *sibling;        /* Sibling resource */
    struct resource *child;          /* Child resource */
    
    /* Resource management */
    void *data;                      /* Resource-specific data */
    
    /* Reference counting */
    atomic_t refcount;                /* Reference count */
};

/* Resource types */
#define IORESOURCE_IO        0x00000100  /* I/O port resource */
#define IORESOURCE_MEM       0x00000200  /* Memory resource */
#define IORESOURCE_IRQ       0x00000400  /* IRQ resource */
#define IORESOURCE_DMA       0x00000800  /* DMA resource */
#define IORESOURCE_BUS       0x00001000  /* Bus resource */
#define IORESOURCE_PREFETCH  0x00002000  /* Prefetchable */
#define IORESOURCE_READONLY  0x00004000  /* Read-only */
#define IORESOURCE_CACHEABLE 0x00008000  /* Cacheable */
#define IORESOURCE_RANGELESS 0x00010000  /* No range */
#define IORESOURCE_SHADOWABLE 0x00020000 /* Shadowable */
#define IORESOURCE_SIZEALIGN 0x00040000  /* Size aligned */
#define IORESOURCE_STARTALIGN 0x00080000 /* Start aligned */
#define IORESOURCE_MEM_64BIT 0x00100000 /* 64-bit memory */
#define IORESOURCE_WINDOW    0x00200000 /* Window */
#define IORESOURCE_MUXED     0x00400000 /* Muxed */

/* ===================================================================== */
/* Device Property Structure */
/* ===================================================================== */

struct device_property {
    char name[64];                    /* Property name */
    enum {
        PROPERTY_TYPE_STRING,         /* String property */
        PROPERTY_TYPE_INTEGER,        /* Integer property */
        PROPERTY_TYPE_BOOLEAN,        /* Boolean property */
        PROPERTY_TYPE_BINARY,         /* Binary property */
        PROPERTY_TYPE_ARRAY,          /* Array property */
    } type;
    
    union {
        char *string;                 /* String value */
        uint64_t integer;             /* Integer value */
        bool boolean;                 /* Boolean value */
        struct {
            void *data;               /* Binary data */
            size_t size;              /* Data size */
        } binary;
        struct {
            void *elements;           /* Array elements */
            size_t count;             /* Number of elements */
            size_t element_size;      /* Element size */
        } array;
    } value;
    
    struct list_head list;            /* Property list */
};

/* ===================================================================== */
/* Driver Model Interface */
/* ===================================================================== */

/**
 * driver_register - Register a driver
 * @driver: Driver to register
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_register(struct driver *driver);

/**
 * driver_unregister - Unregister a driver
 * @driver: Driver to unregister
 */
void driver_unregister(struct driver *driver);

/**
 * driver_find - Find a driver by name
 * @name: Driver name
 * 
 * Return: Driver pointer or NULL if not found
 */
struct driver *driver_find(const char *name);

/**
 * driver_probe_device - Probe a device with a driver
 * @driver: Driver to probe with
 * @device: Device to probe
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_probe_device(struct driver *driver, struct device *device);

/**
 * driver_remove_device - Remove a device from a driver
 * @driver: Driver to remove from
 * @device: Device to remove
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_remove_device(struct driver *driver, struct device *device);

/**
 * device_register - Register a device
 * @device: Device to register
 * 
 * Return: 0 on success, negative error on failure
 */
int device_register(struct device *device);

/**
 * device_unregister - Unregister a device
 * @device: Device to unregister
 */
void device_unregister(struct device *device);

/**
 * device_find - Find a device by name
 * @name: Device name
 * 
 * Return: Device pointer or NULL if not found
 */
struct device *device_find(const char *name);

/**
 * device_create - Create and register a device
 * @parent: Parent device
 * @class: Device class
 * @devt: Device number
 * @drvdata: Driver data
 * @fmt: Format string for device name
 * 
 * Return: Device pointer or NULL on failure
 */
struct device *device_create(struct device *parent, device_class_t class, 
                            dev_t devt, void *drvdata, const char *fmt, ...);

/**
 * device_destroy - Destroy and unregister a device
 * @parent: Parent device
 * @devt: Device number
 */
void device_destroy(struct device *parent, dev_t devt);

/**
 * device_bind_driver - Bind a driver to a device
 * @device: Device to bind
 * @driver: Driver to bind
 * 
 * Return: 0 on success, negative error on failure
 */
int device_bind_driver(struct device *device, struct driver *driver);

/**
 * device_unbind_driver - Unbind a driver from a device
 * @device: Device to unbind
 * 
 * Return: 0 on success, negative error on failure
 */
int device_unbind_driver(struct device *device);

/* ===================================================================== */
/* Resource Management Functions */
/* ===================================================================== */

/**
 * request_resource - Request a resource
 * @new: Resource to request
 * 
 * Return: 0 on success, negative error on failure
 */
int request_resource(struct resource *new);

/**
 * release_resource - Release a resource
 * @old: Resource to release
 * 
 * Return: 0 on success, negative error on failure
 */
int release_resource(struct resource *old);

/**
 * allocate_resource - Allocate a resource from a pool
 * @root: Root resource
 * @new: New resource
 * @size: Resource size
 * @min: Minimum address
 * @max: Maximum address
 * @align: Alignment requirement
 * 
 * Return: 0 on success, negative error on failure
 */
int allocate_resource(struct resource *root, struct resource *new,
                     resource_size_t size, resource_size_t min,
                     resource_size_t max, resource_size_t align);

/**
 * insert_resource - Insert a resource
 * @parent: Parent resource
 * @new: New resource
 * 
 * Return: 0 on success, negative error on failure
 */
int insert_resource(struct resource *parent, struct resource *new);

/* ===================================================================== */
/* Device Property Functions */
/* ===================================================================== */

/**
 * device_property_read_string - Read string property
 * @dev: Device
 * @propname: Property name
 * @val: Output value
 * 
 * Return: 0 on success, negative error on failure
 */
int device_property_read_string(struct device *dev, const char *propname, const char **val);

/**
 * device_property_read_u32 - Read 32-bit integer property
 * @dev: Device
 * @propname: Property name
 * @val: Output value
 * 
 * Return: 0 on success, negative error on failure
 */
int device_property_read_u32(struct device *dev, const char *propname, uint32_t *val);

/**
 * device_property_read_u64 - Read 64-bit integer property
 * @dev: Device
 * @propname: Property name
 * @val: Output value
 * 
 * Return: 0 on success, negative error on failure
 */
int device_property_read_u64(struct device *dev, const char *propname, uint64_t *val);

/**
 * device_property_read_bool - Read boolean property
 * @dev: Device
 * @propname: Property name
 * @val: Output value
 * 
 * Return: 0 on success, negative error on failure
 */
int device_property_read_bool(struct device *dev, const char *propname, bool *val);

/**
 * device_property_set_string - Set string property
 * @dev: Device
 * @propname: Property name
 * @val: Property value
 * 
 * Return: 0 on success, negative error on failure
 */
int device_property_set_string(struct device *dev, const char *propname, const char *val);

/**
 * device_property_set_u32 - Set 32-bit integer property
 * @dev: Device
 * @propname: Property name
 * @val: Property value
 * 
 * Return: 0 on success, negative error on failure
 */
int device_property_set_u32(struct device *dev, const char *propname, uint32_t val);

/**
 * device_property_set_u64 - Set 64-bit integer property
 * @dev: Device
 * @propname: Property name
 * @val: Property value
 * 
 * Return: 0 on success, negative error on failure
 */
int device_property_set_u64(struct device *dev, const char *propname, uint64_t val);

/**
 * device_property_set_bool - Set boolean property
 * @dev: Device
 * @propname: Property name
 * @val: Property value
 * 
 * Return: 0 on success, negative error on failure
 */
int device_property_set_bool(struct device *dev, const char *propname, bool val);

/* ===================================================================== */
/* Driver Model Initialization */
/* ===================================================================== */

/**
 * driver_model_init - Initialize driver model
 * 
 * Return: 0 on success, negative error on failure
 */
int driver_model_init(void);

/**
 * driver_model_exit - Shutdown driver model
 */
void driver_model_exit(void);

/* ===================================================================== */
/* Debugging and Statistics */
/* ===================================================================== */

/**
 * driver_model_get_stats - Get driver model statistics
 * @buf: Buffer to write stats to
 * @size: Buffer size
 * 
 * Return: Number of bytes written
 */
int driver_model_get_stats(char *buf, size_t size);

/**
 * driver_list_devices - List all devices
 * @buf: Buffer to write device list to
 * @size: Buffer size
 * 
 * Return: Number of bytes written
 */
int driver_list_devices(char *buf, size_t size);

/**
 * driver_list_drivers - List all drivers
 * @buf: Buffer to write driver list to
 * @size: Buffer size
 * 
 * Return: Number of bytes written
 */
int driver_list_drivers(char *buf, size_t size);

/* ===================================================================== */
/* Helper Macros */
/* ===================================================================== */

#define MODULE_DEVICE_TABLE(type, name) \
    extern const struct device_id name[]; \
    EXPORT_SYMBOL(name)

#define DRIVER_VERSION(major, minor, patch) \
    #major "." #minor "." #patch

#define DRIVER_AUTHOR(name) \
    MODULE_AUTHOR(name)

#define DRIVER_DESCRIPTION(desc) \
    MODULE_DESCRIPTION(desc)

#define DECLARE_DRIVER(name, type, class) \
    struct driver name##_driver = { \
        .name = #name, \
        .type = DRIVER_TYPE_##type, \
        .class = DEVICE_CLASS_##class, \
        .probe = name##_probe, \
        .remove = name##_remove, \
        .ops = &name##_ops, \
    }

#define REGISTER_DRIVER(driver) \
    driver_register(&driver)

#define UNREGISTER_DRIVER(driver) \
    driver_unregister(&driver)

#endif /* _DRIVERS_DRIVER_H */
