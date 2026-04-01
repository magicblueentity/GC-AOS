/*
 * GC-AOS Kernel - Kernel Module System
 * 
 * This provides a Unix-like kernel module interface for dynamic
 * loading/unloading of kernel modules.
 */

#ifndef _KERNEL_MODULE_H
#define _KERNEL_MODULE_H

#include "types.h"

/* ===================================================================== */
/* Module Information Structure */
/* ===================================================================== */

struct module {
    char name[64];                    /* Module name */
    void *module_init;                /* Init function pointer */
    void *module_exit;                /* Exit function pointer */
    size_t size;                      /* Module size in bytes */
    void *module_base;                /* Base address in memory */
    struct module *next;              /* Linked list */
    
    /* Module metadata */
    const char *author;               /* Module author */
    const char *description;          /* Module description */
    const char *license;              /* Module license */
    const char *version;              /* Module version */
    
    /* Module state */
    uint32_t flags;                   /* Module flags */
    atomic_t refcount;                /* Reference count */
    
    /* Symbol table for module */
    struct {
        void *addr;                   /* Symbol address */
        const char *name;              /* Symbol name */
    } *symbols;
    int num_symbols;                  /* Number of symbols */
};

/* Module flags */
#define MODULE_FLAG_LOADED        (1 << 0)
#define MODULE_FLAG_INITIALIZED   (1 << 1)
#define MODULE_FLAG_EXITING       (1 << 2)

/* ===================================================================== */
/* Module Operations Structure */
/* ===================================================================== */

struct module_operations {
    /* Required operations */
    int (*init)(void);               /* Module initialization */
    void (*exit)(void);              /* Module cleanup */
    
    /* Optional operations */
    int (*suspend)(void);            /* Suspend module */
    int (*resume)(void);             /* Resume module */
    int (*ioctl)(unsigned int cmd, unsigned long arg); /* Module ioctl */
};

/* ===================================================================== */
/* Module Export/Import Macros */
/* ===================================================================== */

#define MODULE_AUTHOR(name)          const char __module_author[] = name
#define MODULE_DESCRIPTION(desc)     const char __module_description[] = desc
#define MODULE_LICENSE(license)      const char __module_license[] = license
#define MODULE_VERSION(version)      const char __module_version[] = version

#define MODULE_INIT(fn)              static void (*__module_init)(void) = fn
#define MODULE_EXIT(fn)              static void (*__module_exit)(void) = fn

/* Export symbol for use by other modules */
#define EXPORT_SYMBOL(sym)            \
    extern typeof(sym) sym;           \
    static const struct __export_symbol __export_##sym = { \
        .addr = &sym,                 \
        .name = #sym                  \
    }

/* Import symbol from another module */
#define IMPORT_SYMBOL(sym)            \
    extern typeof(sym) sym

/* ===================================================================== */
/* Function Declarations */
/* ===================================================================== */

/**
 * module_init - Initialize kernel module system
 */
int module_init(void);

/**
 * module_exit - Shutdown kernel module system
 */
void module_exit(void);

/**
 * module_load - Load a kernel module
 * @name: Module name
 * @data: Module binary data
 * @size: Size of module data
 * 
 * Return: 0 on success, negative error on failure
 */
int module_load(const char *name, const void *data, size_t size);

/**
 * module_unload - Unload a kernel module
 * @name: Module name
 * 
 * Return: 0 on success, negative error on failure
 */
int module_unload(const char *name);

/**
 * module_find - Find a loaded module by name
 * @name: Module name
 * 
 * Return: Module pointer or NULL if not found
 */
struct module *module_find(const char *name);

/**
 * module_get_symbol - Resolve symbol in module or kernel
 * @name: Symbol name
 * 
 * Return: Symbol address or NULL if not found
 */
void *module_get_symbol(const char *name);

/**
 * module_list - List all loaded modules
 * @buf: Buffer to write module names to
 * @size: Buffer size
 * 
 * Return: Number of bytes written or negative error
 */
int module_list(char *buf, size_t size);

/* Module parameter support */
#define module_param(name, type, perm) \
    static type __module_param_##name; \
    module_param_named(name, __module_param_##name, type, perm)

#define module_param_named(name, var, type, perm) \
    static const struct __module_param __module_param_##name = { \
        .name = #name, \
        .addr = &var, \
        .type = #type, \
        .perm = perm \
    }

/* Module parameter permissions */
#define S_IRUSR         00400   /* Read by owner */
#define S_IWUSR         00200   /* Write by owner */
#define S_IRGRP         00040   /* Read by group */
#define S_IWGRP         00020   /* Write by group */
#define S_IROTH         00004   /* Read by others */
#define S_IWOTH         00002   /* Write by others */

#endif /* _KERNEL_MODULE_H */
