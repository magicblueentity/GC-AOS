/*
 * GC-AOS Kernel - Driver Lifecycle Management Implementation
 * 
 * Provides safe driver loading, unloading, and resource management
 * with proper reference counting and hotplug support.
 */

#include "drivers/lifecycle.h"
#include "kernel/module.h"
#include "kernel/errno.h"
#include "kernel/printk.h"
#include "mm/slab.h"
#include "sched/sched.h"

/* ===================================================================== */
/* Missing Definitions */
/* ===================================================================== */

/* Current process pointer */
extern void *current;

/* Timestamp function */
uint64_t get_timestamp(void)
{
    static uint64_t counter = 0;
    return ++counter;
}

/* ===================================================================== */
/* Global Lifecycle Context */
/* ===================================================================== */

static struct driver_lifecycle_ctx lifecycle_ctx = {
    .drivers = LIST_HEAD_INIT(lifecycle_ctx.drivers),
    .driver_lock = SPIN_LOCK_UNLOCKED,
    .devices = LIST_HEAD_INIT(lifecycle_ctx.devices),
    .device_lock = SPIN_LOCK_UNLOCKED,
    .hotplug_devices = LIST_HEAD_INIT(lifecycle_ctx.hotplug_devices),
    .hotplug_lock = SPIN_LOCK_UNLOCKED,
    .power_devices = LIST_HEAD_INIT(lifecycle_ctx.power_devices),
    .power_lock = SPIN_LOCK_UNLOCKED,
    .error_devices = LIST_HEAD_INIT(lifecycle_ctx.error_devices),
    .error_lock = SPIN_LOCK_UNLOCKED,
    .driver_count = ATOMIC_INIT(0),
    .device_count = ATOMIC_INIT(0),
    .active_drivers = ATOMIC_INIT(0),
    .bound_devices = ATOMIC_INIT(0),
    .error_count = ATOMIC_INIT(0),
    .hotplug_enabled = true,
    .power_management_enabled = true,
    .error_recovery_enabled = true,
    .max_probe_retries = 3,
    .probe_retry_delay = 100,
    .initialized = false
};

/* ===================================================================== */
/* Driver Reference Management */
/* ===================================================================== */

int driver_get(struct driver *driver, const char *type, const char *file, int line)
{
    struct driver_reference *ref;
    unsigned long flags;
    
    if (!driver || !type) {
        return -EINVAL;
    }
    
    /* Allocate reference structure */
    ref = kmalloc(sizeof(struct driver_reference), GFP_KERNEL);
    if (!ref) {
        return -ENOMEM;
    }
    
    /* Initialize reference */
    ref->holder = current;
    ref->type = type;
    ref->file = file;
    ref->line = line;
    ref->timestamp = get_timestamp();
    INIT_LIST_HEAD(&ref->list);
    
    /* Add to driver's reference list */
    spin_lock_irqsave(&driver->refcount.ref_lock, flags);
    
    atomic_inc(&driver->refcount.refcount);
    list_add_tail(&ref->list, &driver->refcount.ref_list);
    
    /* Update type-specific counters */
    if (strcmp(type, "module") == 0) {
        atomic_inc(&driver->refcount.module_refs);
    } else if (strcmp(type, "device") == 0) {
        atomic_inc(&driver->refcount.device_refs);
    } else if (strcmp(type, "client") == 0) {
        atomic_inc(&driver->refcount.client_refs);
    } else if (strcmp(type, "sysfs") == 0) {
        atomic_inc(&driver->refcount.sysfs_refs);
    }
    
    /* Check reference limits */
    if (driver->refcount.ref_limit_enabled && 
        atomic_read(&driver->refcount.refcount) > driver->refcount.max_refs) {
        spin_unlock_irqrestore(&driver->refcount.ref_lock, flags);
        kfree(ref);
        return -EOVERFLOW;
    }
    
    spin_unlock_irqrestore(&driver->refcount.ref_lock, flags);
    
    return 0;
}

int driver_put(struct driver *driver, const char *type, const char *file, int line)
{
    struct driver_reference *ref, *tmp;
    unsigned long flags;
    bool found = false;
    
    if (!driver || !type) {
        return -EINVAL;
    }
    
    spin_lock_irqsave(&driver->refcount.ref_lock, flags);
    
    /* Find and remove reference */
    list_for_each_entry_safe(ref, tmp, &driver->refcount.ref_list, list) {
        if (strcmp(ref->type, type) == 0 && 
            (!file || strcmp(ref->file, file) == 0)) {
            list_del(&ref->list);
            kfree(ref);
            found = true;
            break;
        }
    }
    
    if (!found) {
        spin_unlock_irqrestore(&driver->refcount.ref_lock, flags);
        return -ENOENT;
    }
    
    /* Update counters */
    atomic_dec(&driver->refcount.refcount);
    
    if (strcmp(type, "module") == 0) {
        atomic_dec(&driver->refcount.module_refs);
    } else if (strcmp(type, "device") == 0) {
        atomic_dec(&driver->refcount.device_refs);
    } else if (strcmp(type, "client") == 0) {
        atomic_dec(&driver->refcount.client_refs);
    } else if (strcmp(type, "sysfs") == 0) {
        atomic_dec(&driver->refcount.sysfs_refs);
    }
    
    spin_unlock_irqrestore(&driver->refcount.ref_lock, flags);
    
    return 0;
}

int driver_try_get(struct driver *driver, const char *type)
{
    return driver_get(driver, type, __FILE__, __LINE__);
}

int driver_refcount_read(struct driver *driver)
{
    if (!driver) {
        return -EINVAL;
    }
    return atomic_read(&driver->refcount.refcount);
}

int driver_refcount_debug(struct driver *driver, char *buf, size_t size)
{
    struct driver_reference *ref;
    unsigned long flags;
    int len = 0;
    
    if (!driver || !buf) {
        return -EINVAL;
    }
    
    spin_lock_irqsave(&driver->refcount.ref_lock, flags);
    
    len += snprintf(buf + len, size - len,
                   "Driver Reference Count: %d\n"
                   "Module refs: %d\n"
                   "Device refs: %d\n"
                   "Client refs: %d\n"
                   "Sysfs refs: %d\n"
                   "References:\n",
                   atomic_read(&driver->refcount.refcount),
                   atomic_read(&driver->refcount.module_refs),
                   atomic_read(&driver->refcount.device_refs),
                   atomic_read(&driver->refcount.client_refs),
                   atomic_read(&driver->refcount.sysfs_refs));
    
    list_for_each_entry(ref, &driver->refcount.ref_list, list) {
        len += snprintf(buf + len, size - len,
                       "  %s: %s:%d (timestamp: %llu)\n",
                       ref->type, ref->file, ref->line, ref->timestamp);
    }
    
    spin_unlock_irqrestore(&driver->refcount.ref_lock, flags);
    
    return len;
}

/* ===================================================================== */
/* Driver Lifecycle Management */
/* ===================================================================== */

int driver_lifecycle_init(void)
{
    if (lifecycle_ctx.initialized) {
        return 0;
    }
    
    /* Initialize workqueue for hotplug */
    lifecycle_ctx.hotplug_wq = create_workqueue("driver_hotplug");
    if (!lifecycle_ctx.hotplug_wq) {
        return -ENOMEM;
    }
    
    lifecycle_ctx.initialized = true;
    
    printk(KERN_INFO "Driver lifecycle system initialized\n");
    return 0;
}

void driver_lifecycle_shutdown(void)
{
    struct driver *driver, *tmp;
    struct device *device, *dev_tmp;
    unsigned long flags;
    
    if (!lifecycle_ctx.initialized) {
        return;
    }
    
    /* Remove all devices */
    spin_lock_irqsave(&lifecycle_ctx.device_lock, flags);
    list_for_each_entry_safe(device, dev_tmp, &lifecycle_ctx.devices, list) {
        list_del(&device->list);
        /* Device cleanup would go here */
    }
    spin_unlock_irqrestore(&lifecycle_ctx.device_lock, flags);
    
    /* Unload all drivers */
    spin_lock_irqsave(&lifecycle_ctx.driver_lock, flags);
    list_for_each_entry_safe(driver, tmp, &lifecycle_ctx.drivers, list) {
        list_del(&driver->list);
        /* Driver cleanup would go here */
    }
    spin_unlock_irqrestore(&lifecycle_ctx.driver_lock, flags);
    
    /* Destroy workqueue */
    if (lifecycle_ctx.hotplug_wq) {
        destroy_workqueue(lifecycle_ctx.hotplug_wq);
    }
    
    lifecycle_ctx.initialized = false;
    
    printk(KERN_INFO "Driver lifecycle system shutdown\n");
}

int driver_lifecycle_load(struct driver *driver, unsigned int flags)
{
    unsigned long drv_flags;
    int ret = 0;
    
    if (!driver) {
        return -EINVAL;
    }
    
    spin_lock_irqsave(&lifecycle_ctx.driver_lock, drv_flags);
    
    /* Check if driver already loaded */
    if (driver->state == DRIVER_STATE_LOADED || 
        driver->state == DRIVER_STATE_ACTIVE) {
        spin_unlock_irqrestore(&lifecycle_ctx.driver_lock, drv_flags);
        return -EEXIST;
    }
    
    driver->state = DRIVER_STATE_LOADING;
    
    /* Add to driver list */
    list_add_tail(&driver->list, &lifecycle_ctx.drivers);
    atomic_inc(&lifecycle_ctx.driver_count);
    
    spin_unlock_irqrestore(&lifecycle_ctx.driver_lock, drv_flags);
    
    /* Call driver load function */
    if (driver->lifecycle_ops && driver->lifecycle_ops->load) {
        ret = driver->lifecycle_ops->load(driver);
        if (ret) {
            spin_lock_irqsave(&lifecycle_ctx.driver_lock, drv_flags);
            driver->state = DRIVER_STATE_ERROR;
            list_del(&driver->list);
            atomic_dec(&lifecycle_ctx.driver_count);
            spin_unlock_irqrestore(&lifecycle_ctx.driver_lock, drv_flags);
            return ret;
        }
    }
    
    /* Auto-probe if requested */
    if (flags & DRIVER_FLAG_AUTOPROBE) {
        ret = driver_lifecycle_probe(driver, NULL, true);
        if (ret && !(flags & DRIVER_FLAG_IGNORE_ERRORS)) {
            driver_lifecycle_unload(driver, false);
            return ret;
        }
    }
    
    spin_lock_irqsave(&lifecycle_ctx.driver_lock, drv_flags);
    driver->state = DRIVER_STATE_LOADED;
    spin_unlock_irqrestore(&lifecycle_ctx.driver_lock, drv_flags);
    
    printk(KERN_INFO "Driver %s loaded successfully\n", driver->name);
    return 0;
}

int driver_lifecycle_unload(struct driver *driver, bool force)
{
    unsigned long flags;
    int ret = 0;
    
    if (!driver) {
        return -EINVAL;
    }
    
    spin_lock_irqsave(&lifecycle_ctx.driver_lock, flags);
    
    /* Check if driver can be unloaded */
    if (!force && driver->state == DRIVER_STATE_ACTIVE) {
        if (atomic_read(&driver->refcount.refcount) > 0) {
            spin_unlock_irqrestore(&lifecycle_ctx.driver_lock, flags);
            return -EBUSY;
        }
    }
    
    driver->state = DRIVER_STATE_UNLOADING;
    
    spin_unlock_irqrestore(&lifecycle_ctx.driver_lock, flags);
    
    /* Call driver unload function */
    if (driver->lifecycle_ops && driver->lifecycle_ops->unload) {
        ret = driver->lifecycle_ops->unload(driver);
        if (ret && !force) {
            spin_lock_irqsave(&lifecycle_ctx.driver_lock, flags);
            driver->state = DRIVER_STATE_ERROR;
            spin_unlock_irqrestore(&lifecycle_ctx.driver_lock, flags);
            return ret;
        }
    }
    
    /* Remove from driver list */
    spin_lock_irqsave(&lifecycle_ctx.driver_lock, flags);
    list_del(&driver->list);
    atomic_dec(&lifecycle_ctx.driver_count);
    driver->state = DRIVER_STATE_REMOVED;
    spin_unlock_irqrestore(&lifecycle_ctx.driver_lock, flags);
    
    printk(KERN_INFO "Driver %s unloaded successfully\n", driver->name);
    return 0;
}

int driver_lifecycle_probe(struct driver *driver, struct device *device, bool async)
{
    unsigned long flags;
    int ret = 0;
    
    if (!driver) {
        return -EINVAL;
    }
    
    spin_lock_irqsave(&lifecycle_ctx.driver_lock, flags);
    
    if (driver->state != DRIVER_STATE_LOADED) {
        spin_unlock_irqrestore(&lifecycle_ctx.driver_lock, flags);
        return -EINVAL;
    }
    
    driver->state = DRIVER_STATE_PROBING;
    
    spin_unlock_irqrestore(&lifecycle_ctx.driver_lock, flags);
    
    /* Call driver probe function */
    if (driver->lifecycle_ops && driver->lifecycle_ops->probe) {
        if (async && (driver->flags & DRIVER_FLAG_ASYNC_PROBE)) {
            /* Async probing would go here */
            ret = driver->lifecycle_ops->probe(driver, device);
        } else {
            ret = driver->lifecycle_ops->probe(driver, device);
        }
    }
    
    spin_lock_irqsave(&lifecycle_ctx.driver_lock, flags);
    
    if (ret == 0) {
        driver->state = DRIVER_STATE_ACTIVE;
        atomic_inc(&lifecycle_ctx.active_drivers);
        if (device) {
            device->lifecycle.bound_driver = driver;
            atomic_inc(&lifecycle_ctx.bound_devices);
        }
    } else {
        driver->state = DRIVER_STATE_ERROR;
        atomic_inc(&lifecycle_ctx.error_count);
    }
    
    spin_unlock_irqrestore(&lifecycle_ctx.driver_lock, flags);
    
    return ret;
}

int driver_lifecycle_remove(struct driver *driver, struct device *device, bool force)
{
    unsigned long flags;
    int ret = 0;
    
    if (!driver) {
        return -EINVAL;
    }
    
    spin_lock_irqsave(&lifecycle_ctx.driver_lock, flags);
    
    if (driver->state != DRIVER_STATE_ACTIVE && !force) {
        spin_unlock_irqrestore(&lifecycle_ctx.driver_lock, flags);
        return -EINVAL;
    }
    
    driver->state = DRIVER_STATE_UNBINDING;
    
    spin_unlock_irqrestore(&lifecycle_ctx.driver_lock, flags);
    
    /* Call driver remove function */
    if (driver->lifecycle_ops && driver->lifecycle_ops->remove) {
        ret = driver->lifecycle_ops->remove(driver, device);
        if (ret && !force) {
            spin_lock_irqsave(&lifecycle_ctx.driver_lock, flags);
            driver->state = DRIVER_STATE_ERROR;
            spin_unlock_irqrestore(&lifecycle_ctx.driver_lock, flags);
            return ret;
        }
    }
    
    spin_lock_irqsave(&lifecycle_ctx.driver_lock, flags);
    
    if (device) {
        device->lifecycle.bound_driver = NULL;
        atomic_dec(&lifecycle_ctx.bound_devices);
    }
    
    driver->state = DRIVER_STATE_LOADED;
    atomic_dec(&lifecycle_ctx.active_drivers);
    
    spin_unlock_irqrestore(&lifecycle_ctx.driver_lock, flags);
    
    return ret;
}

/* ===================================================================== */
/* Device Lifecycle Management */
/* ===================================================================== */

int device_lifecycle_add(struct device *device, bool hotplug)
{
    unsigned long flags;
    
    if (!device) {
        return -EINVAL;
    }
    
    spin_lock_irqsave(&lifecycle_ctx.device_lock, flags);
    
    /* Initialize device lifecycle */
    device->lifecycle.state = DRIVER_STATE_LOADED;
    device->lifecycle.state_time = get_timestamp();
    device->lifecycle.bound_driver = NULL;
    atomic_set(&device->lifecycle.bind_count, 0);
    device->lifecycle.probe_status = 0;
    device->lifecycle.retry_count = 0;
    device->lifecycle.removal_requested = false;
    device->lifecycle.hotplug_capable = hotplug;
    device->lifecycle.hotplug_enabled = hotplug;
    device->lifecycle.power_managed = false;
    device->lifecycle.runtime_pm_enabled = false;
    device->lifecycle.suspended = false;
    device->lifecycle.error_count = 0;
    device->lifecycle.error_recovery_enabled = true;
    
    /* Add to device list */
    list_add_tail(&device->list, &lifecycle_ctx.devices);
    atomic_inc(&lifecycle_ctx.device_count);
    
    if (hotplug) {
        list_add_tail(&device->list, &lifecycle_ctx.hotplug_devices);
    }
    
    spin_unlock_irqrestore(&lifecycle_ctx.device_lock, flags);
    
    /* Trigger hotplug handling if enabled */
    if (hotplug && lifecycle_ctx.hotplug_enabled) {
        driver_lifecycle_hotplug_add(device);
    }
    
    return 0;
}

int device_lifecycle_remove(struct device *device, bool force)
{
    unsigned long flags;
    
    if (!device) {
        return -EINVAL;
    }
    
    spin_lock_irqsave(&lifecycle_ctx.device_lock, flags);
    
    device->lifecycle.removal_requested = true;
    device->lifecycle.removal_time = get_timestamp();
    device->lifecycle.force_remove = force;
    
    /* Unbind from driver if bound */
    if (device->lifecycle.bound_driver) {
        spin_unlock_irqrestore(&lifecycle_ctx.device_lock, flags);
        driver_lifecycle_remove(device->lifecycle.bound_driver, device, force);
        spin_lock_irqsave(&lifecycle_ctx.device_lock, flags);
    }
    
    /* Remove from lists */
    list_del(&device->list);
    atomic_dec(&lifecycle_ctx.device_count);
    
    if (device->lifecycle.hotplug_capable) {
        list_del(&device->list);
    }
    
    spin_unlock_irqrestore(&lifecycle_ctx.device_lock, flags);
    
    return 0;
}

/* ===================================================================== */
/* Hotplug Management */
/* ===================================================================== */

int driver_lifecycle_hotplug_add(struct device *device)
{
    if (!device) {
        return -EINVAL;
    }
    
    printk(KERN_INFO "Hotplug: device %s added\n", device->name);
    
    /* Find suitable driver and probe */
    /* This would scan driver list and match device to driver */
    
    return 0;
}

int driver_lifecycle_hotplug_remove(struct device *device)
{
    if (!device) {
        return -EINVAL;
    }
    
    printk(KERN_INFO "Hotplug: device %s removed\n", device->name);
    
    return device_lifecycle_remove(device, false);
}

/* ===================================================================== */
/* Error Handling and Recovery */
/* ===================================================================== */

int driver_lifecycle_error_report(struct driver *driver, struct device *device,
                                 int error, const char *message)
{
    unsigned long flags;
    
    if (!driver) {
        return -EINVAL;
    }
    
    spin_lock_irqsave(&lifecycle_ctx.error_lock, flags);
    
    if (device) {
        device->lifecycle.error_count++;
        device->lifecycle.last_error = error;
        device->lifecycle.last_error_time = get_timestamp();
        
        /* Add to error list if not already there */
        if (device->lifecycle.error_count == 1) {
            list_add_tail(&device->list, &lifecycle_ctx.error_devices);
        }
    }
    
    atomic_inc(&lifecycle_ctx.error_count);
    
    spin_unlock_irqrestore(&lifecycle_ctx.error_lock, flags);
    
    printk(KERN_ERR "Driver error: %s - %s (error: %d)\n",
           driver->name, message ? message : "Unknown error", error);
    
    /* Attempt recovery if enabled */
    if (lifecycle_ctx.error_recovery_enabled) {
        driver_lifecycle_error_recover(driver, device);
    }
    
    return 0;
}

int driver_lifecycle_error_recover(struct driver *driver, struct device *device)
{
    int ret = 0;
    
    if (!driver) {
        return -EINVAL;
    }
    
    printk(KERN_INFO "Attempting error recovery for driver %s\n", driver->name);
    
    /* Call driver recovery function */
    if (driver->lifecycle_ops && driver->lifecycle_ops->recover) {
        ret = driver->lifecycle_ops->recover(driver, device);
    }
    
    if (ret == 0) {
        printk(KERN_INFO "Error recovery successful for driver %s\n", driver->name);
        if (device) {
            device->lifecycle.error_count = 0;
        }
    } else {
        printk(KERN_ERR "Error recovery failed for driver %s\n", driver->name);
    }
    
    return ret;
}

/* ===================================================================== */
/* Statistics and Debugging */
/* ===================================================================== */

int driver_lifecycle_get_stats(char *buf, size_t size)
{
    int len = 0;
    
    if (!buf) {
        return -EINVAL;
    }
    
    len += snprintf(buf + len, size - len,
                   "Driver Lifecycle Statistics:\n"
                   "Total drivers: %d\n"
                   "Active drivers: %d\n"
                   "Total devices: %d\n"
                   "Bound devices: %d\n"
                   "Error count: %d\n"
                   "Hotplug enabled: %s\n"
                   "Power management enabled: %s\n"
                   "Error recovery enabled: %s\n",
                   atomic_read(&lifecycle_ctx.driver_count),
                   atomic_read(&lifecycle_ctx.active_drivers),
                   atomic_read(&lifecycle_ctx.device_count),
                   atomic_read(&lifecycle_ctx.bound_devices),
                   atomic_read(&lifecycle_ctx.error_count),
                   lifecycle_ctx.hotplug_enabled ? "Yes" : "No",
                   lifecycle_ctx.power_management_enabled ? "Yes" : "No",
                   lifecycle_ctx.error_recovery_enabled ? "Yes" : "No");
    
    return len;
}

int driver_lifecycle_validate(struct driver *driver)
{
    if (!driver) {
        return -EINVAL;
    }
    
    /* Validate driver state */
    if (driver->state < DRIVER_STATE_UNINITIALIZED || 
        driver->state > DRIVER_STATE_REMOVED) {
        return -EINVAL;
    }
    
    /* Validate reference count */
    if (atomic_read(&driver->refcount.refcount) < 0) {
        return -EINVAL;
    }
    
    return 0;
}

int driver_lifecycle_validate_all(void)
{
    struct driver *driver;
    unsigned long flags;
    int ret = 0;
    
    spin_lock_irqsave(&lifecycle_ctx.driver_lock, flags);
    
    list_for_each_entry(driver, &lifecycle_ctx.drivers, list) {
        if (driver_lifecycle_validate(driver) != 0) {
            ret = -EINVAL;
            break;
        }
    }
    
    spin_unlock_irqrestore(&lifecycle_ctx.driver_lock, flags);
    
    return ret;
}
