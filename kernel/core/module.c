/*
 * GC-AOS Kernel - Module System Implementation
 */

#include "kernel/module.h"
#include "printk.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "mm/slab.h"
#include "string.h"
#include "sync/spinlock.h"

/* Error codes */
#define EINVAL 22
#define EEXIST 17
#define ENOMEM 12
#define ENOENT 2
#define EBUSY 16

/* Page flags */
#define PAGE_KERNEL 0

/* Spin lock initialization */
#define SPIN_LOCK_UNLOCKED { 0 }

/* Atomic operations */
#define ATOMIC_INIT(i) { (i) }

/* Forward declarations */
static int module_parse_elf(struct module *mod);
static void *kernel_get_symbol(const char *name);
extern void vmm_free(void *addr, size_t size);
extern void *vmm_alloc(size_t size, unsigned int flags);
extern int snprintf(char *str, size_t size, const char *format, ...);

/* Module system state */
static struct module *module_list_head = NULL;
static spinlock_t module_lock = SPIN_LOCK_UNLOCKED;

/* Module system statistics */
static atomic_t module_count = ATOMIC_INIT(0);
static size_t total_module_memory = 0;

/**
 * module_init - Initialize kernel module system
 */
int module_init(void)
{
    printk(KERN_INFO "Module system: Initializing\n");
    
    spin_lock_init(&module_lock);
    
    /* Initialize module subsystem */
    atomic_set(&module_count, 0);
    total_module_memory = 0;
    
    printk(KERN_INFO "Module system: Ready\n");
    return 0;
}

/**
 * module_exit - Shutdown kernel module system
 */
void module_exit(void)
{
    printk(KERN_INFO "Module system: Shutting down\n");
    
    /* Unload all modules */
    spin_lock(&module_lock);
    
    struct module *mod = module_list_head;
    while (mod) {
        struct module *next = mod->next;
        
        if (mod->flags & MODULE_FLAG_LOADED) {
            printk(KERN_WARNING "Module %s still loaded, forcing unload\n", mod->name);
            
            /* Call module exit if available */
            if (mod->module_exit && (mod->flags & MODULE_FLAG_INITIALIZED)) {
                ((void (*)(void))mod->module_exit)();
            }
            
            /* Free module memory */
            if (mod->module_base) {
                vmm_free(mod->module_base, mod->size);
                total_module_memory -= mod->size;
            }
        }
        
        mod = next;
    }
    
    module_list_head = NULL;
    spin_unlock(&module_lock);
    
    printk(KERN_INFO "Module system: Shutdown complete\n");
}

/**
 * module_load - Load a kernel module
 */
int module_load(const char *name, const void *data, size_t size)
{
    if (!name || !data || size == 0) {
        return -EINVAL;
    }
    
    /* Check if module already loaded */
    if (module_find(name) != NULL) {
        printk(KERN_ERR "Module %s already loaded\n", name);
        return -EEXIST;
    }
    
    printk(KERN_INFO "Loading module %s (size: %zu bytes)\n", name, size);
    
    /* Allocate module structure */
    struct module *mod = kmalloc(sizeof(struct module), GFP_KERNEL);
    if (!mod) {
        return -ENOMEM;
    }
    
    memset(mod, 0, sizeof(struct module));
    strncpy(mod->name, name, sizeof(mod->name) - 1);
    mod->size = size;
    mod->flags = MODULE_FLAG_LOADED;
    atomic_set(&mod->refcount, 1);
    
    /* Allocate memory for module code */
    mod->module_base = vmm_alloc(size, PAGE_KERNEL);
    if (!mod->module_base) {
        kfree(mod);
        return -ENOMEM;
    }
    
    /* Copy module code */
    memcpy(mod->module_base, data, size);
    
    /* Parse module metadata and symbols */
    int ret = module_parse_elf(mod);
    if (ret < 0) {
        printk(KERN_ERR "Failed to parse module %s: %d\n", name, ret);
        vmm_free(mod->module_base, size);
        kfree(mod);
        return ret;
    }
    
    /* Add to module list */
    spin_lock(&module_lock);
    mod->next = module_list_head;
    module_list_head = mod;
    atomic_inc(&module_count);
    total_module_memory += size;
    spin_unlock(&module_lock);
    
    /* Call module init function */
    if (mod->module_init) {
        printk(KERN_INFO "Calling module init for %s\n", name);
        ret = ((int (*)(void))mod->module_init)();
        if (ret < 0) {
            printk(KERN_ERR "Module %s init failed: %d\n", name, ret);
            
            /* Remove from list */
            spin_lock(&module_lock);
            if (module_list_head == mod) {
                module_list_head = mod->next;
            } else {
                struct module *m = module_list_head;
                while (m && m->next != mod) {
                    m = m->next;
                }
                if (m) {
                    m->next = mod->next;
                }
            }
            atomic_dec(&module_count);
            total_module_memory -= size;
            spin_unlock(&module_lock);
            
            vmm_free(mod->module_base, size);
            kfree(mod);
            return ret;
        }
        mod->flags |= MODULE_FLAG_INITIALIZED;
    }
    
    printk(KERN_INFO "Module %s loaded successfully\n", name);
    return 0;
}

/**
 * module_unload - Unload a kernel module
 */
int module_unload(const char *name)
{
    if (!name) {
        return -EINVAL;
    }
    
    printk(KERN_INFO "Unloading module %s\n", name);
    
    spin_lock(&module_lock);
    
    /* Find module in list */
    struct module *mod = module_list_head;
    struct module *prev = NULL;
    
    while (mod) {
        if (strcmp(mod->name, name) == 0) {
            break;
        }
        prev = mod;
        mod = mod->next;
    }
    
    if (!mod) {
        spin_unlock(&module_lock);
        return -ENOENT;
    }
    
    /* Check reference count */
    if (atomic_read(&mod->refcount) > 1) {
        spin_unlock(&module_lock);
        printk(KERN_WARNING "Module %s still in use (refcount: %d)\n", 
               name, atomic_read(&mod->refcount));
        return -EBUSY;
    }
    
    /* Mark as exiting */
    mod->flags |= MODULE_FLAG_EXITING;
    spin_unlock(&module_lock);
    
    /* Call module exit function */
    if (mod->module_exit && (mod->flags & MODULE_FLAG_INITIALIZED)) {
        printk(KERN_INFO "Calling module exit for %s\n", name);
        ((void (*)(void))mod->module_exit)();
    }
    
    /* Remove from list and free memory */
    spin_lock(&module_lock);
    
    if (prev) {
        prev->next = mod->next;
    } else {
        module_list_head = mod->next;
    }
    
    atomic_dec(&module_count);
    total_module_memory -= mod->size;
    
    spin_unlock(&module_lock);
    
    /* Free module memory */
    if (mod->module_base) {
        vmm_free(mod->module_base, mod->size);
    }
    
    kfree(mod);
    
    printk(KERN_INFO "Module %s unloaded successfully\n", name);
    return 0;
}

/**
 * module_find - Find a loaded module by name
 */
struct module *module_find(const char *name)
{
    if (!name) {
        return NULL;
    }
    
    spin_lock(&module_lock);
    
    struct module *mod = module_list_head;
    while (mod) {
        if (strcmp(mod->name, name) == 0) {
            atomic_inc(&mod->refcount);
            spin_unlock(&module_lock);
            return mod;
        }
        mod = mod->next;
    }
    
    spin_unlock(&module_lock);
    return NULL;
}

/**
 * module_get_symbol - Resolve symbol in module or kernel
 */
void *module_get_symbol(const char *name)
{
    if (!name) {
        return NULL;
    }
    
    /* First check kernel symbols */
    void *addr = kernel_get_symbol(name);
    if (addr) {
        return addr;
    }
    
    /* Then check loaded modules */
    spin_lock(&module_lock);
    
    struct module *mod = module_list_head;
    while (mod) {
        if (mod->symbols) {
            for (int i = 0; i < mod->num_symbols; i++) {
                if (strcmp(mod->symbols[i].name, name) == 0) {
                    spin_unlock(&module_lock);
                    return mod->symbols[i].addr;
                }
            }
        }
        mod = mod->next;
    }
    
    spin_unlock(&module_lock);
    return NULL;
}

/**
 * module_list - List all loaded modules
 */
int module_list(char *buf, size_t size)
{
    if (!buf || size == 0) {
        return -EINVAL;
    }
    
    spin_lock(&module_lock);
    
    int offset = 0;
    struct module *mod = module_list_head;
    
    while (mod && (size_t)offset < size) {
        int len = snprintf(buf + offset, size - offset,
                         "%s: %s (refs: %d)\n",
                         mod->name,
                         (mod->flags & MODULE_FLAG_INITIALIZED) ? "initialized" : "loaded",
                         atomic_read(&mod->refcount));
        
        if (len > 0 && (size_t)(offset + len) < size) {
            offset += len;
        } else {
            break;
        }
        
        mod = mod->next;
    }
    
    spin_unlock(&module_lock);
    return offset;
}

/**
 * module_parse_elf - Parse ELF module and extract symbols
 */
static int module_parse_elf(struct module *mod)
{
    /* This is a simplified ELF parser for kernel modules
     * In a real implementation, this would be more comprehensive
     */
    
    /* For now, we'll just set up basic module structure */
    /* TODO: Implement proper ELF parsing with symbol table extraction */
    
    (void)mod; /* Suppress unused parameter warning */
    
    return 0;
}

/**
 * kernel_get_symbol - Get kernel symbol (simplified)
 */
static void *kernel_get_symbol(const char *name)
{
    /* This would normally use the kernel's symbol table
     * For now, we'll provide a few essential symbols
     */
    
    if (strcmp(name, "printk") == 0) {
        return (void*)printk;
    }
    
    if (strcmp(name, "kmalloc") == 0) {
        return (void*)kmalloc;
    }
    
    if (strcmp(name, "kfree") == 0) {
        return (void*)kfree;
    }
    
    return NULL;
}
