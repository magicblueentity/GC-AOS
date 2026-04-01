/*
 * GC-AOS Kernel - Slab Allocator
 * 
 * Efficient memory allocator for small, frequently allocated objects
 * Based on the original slab allocator design by Sun Microsystems
 */

#ifndef _MM_SLAB_H
#define _MM_SLAB_H

#include "types.h"
#include "mm/pmm.h"
#include "kernel/list.h"
#include "sync/spinlock.h"

/* ===================================================================== */
/* Slab Cache Configuration */
/* ===================================================================== */

#define SLAB_MIN_ORDER         0       /* Minimum allocation order */
#define SLAB_MAX_ORDER         5       /* Maximum allocation order */
#define SLAB_HWCACHE_ALIGN     1       /* Align to cache line */
#define SLAB_CACHE_DMA         2       /* DMA-capable cache */
#define SLAB_CACHE_REAP        4       /* Auto-reap enabled */
#define SLAB_CACHE_DESTROY     8       /* Cache being destroyed */

/* Slab object limits */
#define SLAB_OBJ_MIN_SIZE      8       /* Minimum object size */
#define SLAB_OBJ_MAX_SIZE      (PAGE_SIZE * 8)  /* Maximum object size */
#define SLAB_OBJ_PER_PAGE_MIN  2       /* Minimum objects per page */

/* Cache line size */
#define L1_CACHE_BYTES         64      /* Typical L1 cache line size */

/* ===================================================================== */
/* Slab Cache Structure */
/* ===================================================================== */

struct kmem_cache {
    /* Cache metadata */
    char name[32];                    /* Cache name */
    size_t object_size;               /* Size of each object */
    size_t align;                     /* Alignment requirement */
    unsigned int flags;               /* Cache flags */
    
    /* Slab management */
    struct list_head slabs_full;     /* Full slabs */
    struct list_head slabs_partial;  /* Partially full slabs */
    struct list_head slabs_free;     /* Empty slabs */
    
    unsigned int num;                 /* Number of objects per slab */
    unsigned int batchcount;          /* Objects to allocate/free in batch */
    
    /* Constructor/Destructor */
    void (*ctor)(void *obj);         /* Object constructor */
    void (*dtor)(void *obj);         /* Object destructor */
    
    /* Statistics */
    atomic_t num_active;              /* Active objects */
    atomic_t num_allocated;           /* Total allocated objects */
    
    /* Memory management */
    size_t colour;                    /* Colour offset for cache alignment */
    size_t colour_next;               /* Next colour to use */
    size_t colour_off;                /* Maximum colour offset */
    
    /* Cache management */
    struct list_head next;            /* List of all caches */
    spinlock_t lock;                  /* Cache lock */
    
    /* Reaping */
    unsigned long reap_time;          /* Last reap time */
    unsigned long reap_timer;         /* Reap interval */
};

/* ===================================================================== */
/* Slab Structure */
/* ===================================================================== */

struct slab {
    struct list_head list;            /* List linkage */
    struct kmem_cache *cache;         /* Parent cache */
    void *s_mem;                      /* First object in slab */
    unsigned int inuse;               /* Objects in use */
    unsigned int colouroff;           /* Colour offset */
    
    /* Bitmap for tracking free objects */
    void *freelist;                   /* Free object list */
    unsigned long *bitmap;            /* Allocation bitmap */
    
    /* Page management */
    struct page *pages;               /* Pages backing this slab */
    unsigned int nr_pages;             /* Number of pages */
};

/* ===================================================================== */
/* Common Kernel Caches */
/* ===================================================================== */

/* Predefined caches for common kernel objects */
extern struct kmem_cache *kmalloc_caches[SLAB_MAX_ORDER + 1];
extern struct kmem_cache *task_struct_cache;
extern struct kmem_cache *inode_cache;
extern struct kmem_cache *dentry_cache;
extern struct kmem_cache *file_cache;
extern struct kmem_cache *socket_cache;

/* ===================================================================== */
/* Slab Allocator Interface */
/* ===================================================================== */

/**
 * kmem_cache_create - Create a new slab cache
 * @name: Cache name
 * @size: Object size
 * @align: Alignment requirement
 * @flags: Cache flags
 * @ctor: Object constructor (optional)
 * @dtor: Object destructor (optional)
 * 
 * Return: Cache pointer or NULL on failure
 */
struct kmem_cache *kmem_cache_create(const char *name, size_t size, size_t align,
                                     unsigned int flags,
                                     void (*ctor)(void *), void (*dtor)(void *));

/**
 * kmem_cache_destroy - Destroy a slab cache
 * @cache: Cache to destroy
 * 
 * Return: 0 on success, negative error on failure
 */
int kmem_cache_destroy(struct kmem_cache *cache);

/**
 * kmem_cache_alloc - Allocate object from cache
 * @cache: Cache to allocate from
 * @flags: Allocation flags
 * 
 * Return: Object pointer or NULL on failure
 */
void *kmem_cache_alloc(struct kmem_cache *cache, unsigned int flags);

/**
 * kmem_cache_free - Free object to cache
 * @cache: Cache to free to
 * @obj: Object to free
 */
void kmem_cache_free(struct kmem_cache *cache, void *obj);

/**
 * kmem_cache_alloc_node - Allocate object from specific node (NUMA)
 * @cache: Cache to allocate from
 * @flags: Allocation flags
 * @node: NUMA node ID
 * 
 * Return: Object pointer or NULL on failure
 */
void *kmem_cache_alloc_node(struct kmem_cache *cache, unsigned int flags, int node);

/**
 * kmem_cache_reap - Reap empty slabs from cache
 * @cache: Cache to reap
 * 
 * Return: Number of slabs reclaimed
 */
int kmem_cache_reap(struct kmem_cache *cache);

/**
 * kmem_cache_shrink - Shrink cache by freeing empty slabs
 * @cache: Cache to shrink
 */
void kmem_cache_shrink(struct kmem_cache *cache);

/**
 * kmem_cache_size - Get object size from cache
 * @cache: Cache to query
 * 
 * Return: Object size
 */
static inline size_t kmem_cache_size(struct kmem_cache *cache)
{
    return cache->object_size;
}

/* ===================================================================== */
/* General Purpose Memory Allocation */
/* ===================================================================== */

/**
 * kmalloc - Allocate memory
 * @size: Size to allocate
 * @flags: Allocation flags
 * 
 * Return: Pointer to allocated memory or NULL
 */
void *kmalloc(size_t size, unsigned int flags);

/**
 * kzalloc - Allocate and zero memory
 * @size: Size to allocate
 * @flags: Allocation flags
 * 
 * Return: Pointer to allocated memory or NULL
 */
void *kzalloc(size_t size, unsigned int flags);

/**
 * krealloc - Reallocate memory
 * @ptr: Original pointer
 * @size: New size
 * @flags: Allocation flags
 * 
 * Return: Pointer to reallocated memory or NULL
 */
void *krealloc(const void *ptr, size_t size, unsigned int flags);

/**
 * kfree - Free memory
 * @ptr: Pointer to free
 */
void kfree(const void *ptr);

/**
 * ksize - Get actual size of allocation
 * @ptr: Pointer to allocation
 * 
 * Return: Actual allocation size
 */
size_t ksize(const void *ptr);

/* ===================================================================== */
/* Slab Allocator Initialization */
/* ===================================================================== */

/**
 * slab_init - Initialize slab allocator system
 * 
 * Return: 0 on success, negative error on failure
 */
int slab_init(void);

/**
 * slab_init_late - Late initialization of slab allocator
 * 
 * Called after basic memory management is up
 */
void slab_init_late(void);

/**
 * slab_shutdown - Shutdown slab allocator system
 */
void slab_shutdown(void);

/* ===================================================================== */
/* Debugging and Statistics */
/* ===================================================================== */

/**
 * slab_get_stats - Get slab allocator statistics
 * @buf: Buffer to write stats to
 * @size: Buffer size
 * 
 * Return: Number of bytes written
 */
int slab_get_stats(char *buf, size_t size);

/**
 * slab_cache_info - Get information about specific cache
 * @cache: Cache to query
 * @buf: Buffer to write info to
 * @size: Buffer size
 * 
 * Return: Number of bytes written
 */
int slab_cache_info(struct kmem_cache *cache, char *buf, size_t size);

/**
 * slab_validate - Validate slab allocator state
 * 
 * Return: 0 if valid, negative error if corruption detected
 */
int slab_validate(void);

/* ===================================================================== */
/* Allocation Flags */
/* ===================================================================== */

#define GFP_KERNEL        0x01    /* Normal kernel allocation */
#define GFP_ATOMIC        0x02    /* Atomic allocation (cannot sleep) */
#define GFP_DMA           0x04    /* DMA-capable allocation */
#define GFP_HIGHMEM       0x08    /* High memory allocation */
#define GFP_ZERO          0x10    /* Zero the allocation */
#define GFP_NOWAIT        0x20    /* Do not wait for memory */
#define GFP_RECLAIM       0x40    /* Can reclaim memory */
#define GFP_IO            0x80    /* Can perform I/O */
#define GFP_FS            0x100   /* Can perform filesystem operations */

/* Common flag combinations */
#define GFP_KERNEL_IO    (GFP_KERNEL | GFP_IO | GFP_FS)
#define GFP_ATOMIC_IO    (GFP_ATOMIC | GFP_IO)
#define GFP_DMA_KERNEL   (GFP_DMA | GFP_KERNEL)

#endif /* _MM_SLAB_H */
