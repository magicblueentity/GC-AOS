/*
 * GC-AOS Kernel - Slab Allocator Implementation
 */

#include "mm/slab.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "printk.h"
#include "kernel/string.h"
#include "kernel/errno.h"
#include "types.h"
#include "kernel/list.h"
#include "sync/spinlock.h"

/* Forward declarations for incomplete types */
struct task_struct;
struct inode;
struct dentry;
struct file;
struct socket;

/* Temporary PAGE_KERNEL definition */
#define PAGE_KERNEL 0

/* VMM function declarations */
extern void *vmm_alloc(size_t size, unsigned int flags);
extern void vmm_free(void *ptr, size_t size);

/* Slab allocator global state */
static struct list_head cache_list;
static spinlock_t cache_lock;
static atomic_t slab_caches = ATOMIC_INIT(0);

/* Common kernel caches */
struct kmem_cache *kmalloc_caches[SLAB_MAX_ORDER + 1];
struct kmem_cache *task_struct_cache;
struct kmem_cache *inode_cache;
struct kmem_cache *dentry_cache;
struct kmem_cache *file_cache;
struct kmem_cache *socket_cache;

/* Slab statistics */
static atomic_t total_slabs = ATOMIC_INIT(0);
static atomic_t total_objects = ATOMIC_INIT(0);
static atomic_t total_allocated = ATOMIC_INIT(0);

/* Forward declarations */
static struct slab *slab_alloc(struct kmem_cache *cache, unsigned int flags);
static void slab_destroy(struct slab *slab);
static void *slab_alloc_obj(struct slab *slab);
static void slab_free_obj(struct slab *slab, void *obj);
static bool kmem_cache_contains(struct kmem_cache *cache, void *obj);
static bool slab_contains_obj(struct slab *slab, void *obj);
static struct slab *slab_find_obj(struct kmem_cache *cache, void *obj);
static void slab_setup_freelist(struct slab *slab);

/**
 * slab_init - Initialize slab allocator system
 */
int slab_init(void)
{
    printk(KERN_INFO "Slab allocator: Initializing\n");
    
    INIT_LIST_HEAD(&cache_list);
    spin_lock_init(&cache_lock);
    
    /* Initialize statistics */
    atomic_set(&total_slabs, 0);
    atomic_set(&total_objects, 0);
    atomic_set(&total_allocated, 0);
    
    /* Create general purpose caches */
    for (int i = 0; i <= SLAB_MAX_ORDER; i++) {
        size_t size = 8 << i;  /* 8, 16, 32, 64, 128, 256 */
        char name[32];
        snprintf(name, sizeof(name), "kmalloc-%zu", size);
        
        kmalloc_caches[i] = kmem_cache_create(name, size, 0, 
                                              SLAB_HWCACHE_ALIGN, NULL, NULL);
        if (!kmalloc_caches[i]) {
            printk(KERN_ERR "Failed to create kmalloc cache %s\n", name);
            return -ENOMEM;
        }
    }
    
    printk(KERN_INFO "Slab allocator: Initialized with %d caches\n", 
           SLAB_MAX_ORDER + 1);
    return 0;
}

/**
 * slab_init_late - Late initialization of slab allocator
 */
void slab_init_late(void)
{
    printk(KERN_INFO "Slab allocator: Late initialization\n");
    
    /* Create caches for common kernel objects */
    /* TODO: Fix when these structs are properly defined */
    /*
    task_struct_cache = kmem_cache_create("task_struct", sizeof(struct task_struct),
                                          0, SLAB_HWCACHE_ALIGN, NULL, NULL);
    
    inode_cache = kmem_cache_create("inode", sizeof(struct inode),
                                    0, SLAB_HWCACHE_ALIGN, NULL, NULL);
    
    dentry_cache = kmem_cache_create("dentry", sizeof(struct dentry),
                                     0, SLAB_HWCACHE_ALIGN, NULL, NULL);
    
    file_cache = kmem_cache_create("file", sizeof(struct file),
                                   0, SLAB_HWCACHE_ALIGN, NULL, NULL);
    
    socket_cache = kmem_cache_create("socket", sizeof(struct socket),
                                     0, SLAB_HWCACHE_ALIGN, NULL, NULL);
    */
    
    printk(KERN_INFO "Slab allocator: Created kernel object caches\n");
}

/**
 * kmem_cache_create - Create a new slab cache
 */
struct kmem_cache *kmem_cache_create(const char *name, size_t size, size_t align,
                                     unsigned int flags,
                                     void (*ctor)(void *), void (*dtor)(void *))
{
    if (!name || size < SLAB_OBJ_MIN_SIZE || size > SLAB_OBJ_MAX_SIZE) {
        return NULL;
    }
    
    printk(KERN_INFO "Creating slab cache %s (size: %zu)\n", name, size);
    
    struct kmem_cache *cache = kmalloc(sizeof(struct kmem_cache), GFP_KERNEL);
    if (!cache) {
        return NULL;
    }
    
    memset(cache, 0, sizeof(struct kmem_cache));
    strncpy(cache->name, name, sizeof(cache->name) - 1);
    cache->object_size = size;
    cache->align = align;
    cache->flags = flags;
    cache->ctor = ctor;
    cache->dtor = dtor;
    
    /* Initialize lists */
    INIT_LIST_HEAD(&cache->slabs_full);
    INIT_LIST_HEAD(&cache->slabs_partial);
    INIT_LIST_HEAD(&cache->slabs_free);
    spin_lock_init(&cache->lock);
    
    /* Calculate objects per page */
    size_t object_size = ALIGN(size, align);
    size_t header_size = sizeof(struct slab);
    size_t available = PAGE_SIZE - header_size;
    
    cache->num = available / object_size;
    if (cache->num < SLAB_OBJ_PER_PAGE_MIN) {
        cache->num = SLAB_OBJ_PER_PAGE_MIN;
    }
    
    /* Setup colouring for cache alignment */
    if (flags & SLAB_HWCACHE_ALIGN) {
        cache->colour_off = L1_CACHE_BYTES;
        cache->colour_next = 0;
        cache->colour = cache->colour_off;
    }
    
    /* Initialize statistics */
    atomic_set(&cache->num_active, 0);
    atomic_set(&cache->num_allocated, 0);
    
    /* Add to global cache list */
    spin_lock(&cache_lock);
    list_add(&cache->next, &cache_list);
    atomic_inc(&slab_caches);
    spin_unlock(&cache_lock);
    
    printk(KERN_INFO "Slab cache %s created (%d objects per slab)\n", 
           name, cache->num);
    return cache;
}

/**
 * kmem_cache_destroy - Destroy a slab cache
 */
int kmem_cache_destroy(struct kmem_cache *cache)
{
    if (!cache) {
        return -EINVAL;
    }
    
    printk(KERN_INFO "Destroying slab cache %s\n", cache->name);
    
    /* Check if cache has active objects */
    if (atomic_read(&cache->num_active) > 0) {
        printk(KERN_WARNING "Cache %s has %d active objects\n", 
               cache->name, atomic_read(&cache->num_active));
        return -EBUSY;
    }
    
    /* Free all slabs */
    spin_lock(&cache->lock);
    
    /* Free full slabs */
    while (!list_empty(&cache->slabs_full)) {
        struct slab *slab = list_first_entry(&cache->slabs_full, struct slab, list);
        list_del(&slab->list);
        slab_destroy(slab);
    }
    
    /* Free partial slabs */
    while (!list_empty(&cache->slabs_partial)) {
        struct slab *slab = list_first_entry(&cache->slabs_partial, struct slab, list);
        list_del(&slab->list);
        slab_destroy(slab);
    }
    
    /* Free empty slabs */
    while (!list_empty(&cache->slabs_free)) {
        struct slab *slab = list_first_entry(&cache->slabs_free, struct slab, list);
        list_del(&slab->list);
        slab_destroy(slab);
    }
    
    spin_unlock(&cache->lock);
    
    /* Remove from global list */
    spin_lock(&cache_lock);
    list_del(&cache->next);
    atomic_dec(&slab_caches);
    spin_unlock(&cache_lock);
    
    kfree(cache);
    
    printk(KERN_INFO "Slab cache %s destroyed\n", cache->name);
    return 0;
}

/**
 * kmem_cache_alloc - Allocate object from cache
 */
void *kmem_cache_alloc(struct kmem_cache *cache, unsigned int flags)
{
    if (!cache) {
        return NULL;
    }
    
    spin_lock(&cache->lock);
    
    struct slab *slab;
    void *obj = NULL;
    
    /* Try partial slabs first */
    if (!list_empty(&cache->slabs_partial)) {
        slab = list_first_entry(&cache->slabs_partial, struct slab, list);
        obj = slab_alloc_obj(slab);
        if (slab->inuse == cache->num) {
            list_move(&slab->list, &cache->slabs_full);
        }
    } else if (!list_empty(&cache->slabs_free)) {
        /* Use empty slab */
        slab = list_first_entry(&cache->slabs_free, struct slab, list);
        obj = slab_alloc_obj(slab);
        list_move(&slab->list, &cache->slabs_partial);
    } else {
        /* Need to allocate new slab */
        slab = slab_alloc(cache, flags);
        if (slab) {
            obj = slab_alloc_obj(slab);
            list_add(&slab->list, &cache->slabs_partial);
            atomic_inc(&total_slabs);
            atomic_add(cache->num, &total_objects);
        }
    }
    
    if (obj) {
        atomic_inc(&cache->num_active);
        atomic_inc(&cache->num_allocated);
        atomic_inc(&total_allocated);
        
        /* Call constructor if available */
        if (cache->ctor) {
            cache->ctor(obj);
        }
    }
    
    spin_unlock(&cache->lock);
    
    return obj;
}

/**
 * kmem_cache_free - Free object to cache
 */
void kmem_cache_free(struct kmem_cache *cache, void *obj)
{
    if (!cache || !obj) {
        return;
    }
    
    spin_lock(&cache->lock);
    
    /* Find slab containing this object */
    struct slab *slab = slab_find_obj(cache, obj);
    if (!slab) {
        printk(KERN_ERR "Object %p not found in cache %s\n", obj, cache->name);
        spin_unlock(&cache->lock);
        return;
    }
    
    /* Call destructor if available */
    if (cache->dtor) {
        cache->dtor(obj);
    }
    
    /* Free object */
    slab_free_obj(slab, obj);
    atomic_dec(&cache->num_active);
    atomic_dec(&total_allocated);
    
    /* Move slab to appropriate list */
    if (slab->inuse == 0) {
        list_move(&slab->list, &cache->slabs_free);
    } else if (slab->inuse == cache->num - 1) {
        list_move(&slab->list, &cache->slabs_partial);
    }
    
    spin_unlock(&cache->lock);
}

/**
 * kmalloc - Allocate memory
 */
void *kmalloc(size_t size, unsigned int flags)
{
    if (size == 0) {
        return NULL;
    }
    
    /* Find appropriate cache */
    for (int i = 0; i <= SLAB_MAX_ORDER; i++) {
        if (size <= (8 << i)) {
            return kmem_cache_alloc(kmalloc_caches[i], flags);
        }
    }
    
    /* For large allocations, fall back to page allocator */
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    void *ptr = vmm_alloc(pages * PAGE_SIZE, PAGE_KERNEL);
    if (ptr && (flags & GFP_ZERO)) {
        memset(ptr, 0, pages * PAGE_SIZE);
    }
    return ptr;
}

/* Memory allocation functions are defined in kmalloc.c */

/**
 * ksize - Get actual size of allocation
 */
size_t ksize(const void *ptr)
{
    if (!ptr) {
        return 0;
    }
    
    /* Check if this is in a cache */
    spin_lock(&cache_lock);
    
    struct kmem_cache *cache;
    list_for_each_entry(cache, &cache_list, next) {
        if (kmem_cache_contains(cache, ptr)) {
            spin_unlock(&cache_lock);
            return cache->object_size;
        }
    }
    
    spin_unlock(&cache_lock);
    
    /* Not in cache, return page-aligned size */
    /* For now, assume page size - this should be improved */
    return PAGE_SIZE;
}

/* Helper functions for slab management */
static struct slab *slab_alloc(struct kmem_cache *cache, unsigned int flags)
{
    /* Allocate pages for slab */
    size_t slab_size = PAGE_SIZE;  /* Single page for now */
    void *mem = vmm_alloc(slab_size, PAGE_KERNEL);
    if (!mem) {
        return NULL;
    }
    
    /* Initialize slab structure */
    struct slab *slab = (struct slab *)mem;
    memset(slab, 0, sizeof(struct slab));
    
    slab->cache = cache;
    slab->s_mem = (void *)((uintptr_t)mem + sizeof(struct slab));
    slab->inuse = 0;
    slab->nr_pages = 1;
    
    /* Setup freelist */
    slab_setup_freelist(slab);
    
    return slab;
}

static void slab_destroy(struct slab *slab)
{
    if (slab) {
        vmm_free(slab, slab->nr_pages * PAGE_SIZE);
        atomic_dec(&total_slabs);
        atomic_sub(slab->cache->num, &total_objects);
    }
}

static void *slab_alloc_obj(struct slab *slab)
{
    if (slab->inuse >= slab->cache->num) {
        return NULL;
    }
    
    void *obj = slab->freelist;
    if (obj) {
        slab->freelist = *(void **)obj;
        slab->inuse++;
    }
    
    return obj;
}

static void slab_free_obj(struct slab *slab, void *obj)
{
    *(void **)obj = slab->freelist;
    slab->freelist = obj;
    slab->inuse--;
}

static void slab_setup_freelist(struct slab *slab)
{
    void *obj = slab->s_mem;
    void **freelist = &slab->freelist;
    
    for (unsigned int i = 0; i < slab->cache->num; i++) {
        *freelist = obj;
        freelist = (void **)obj;
        obj = (void *)((uintptr_t)obj + slab->cache->object_size);
    }
    
    *freelist = NULL;
}

static struct slab *slab_find_obj(struct kmem_cache *cache, void *obj)
{
    /* Search all slabs in this cache */
    struct slab *slab;
    
    list_for_each_entry(slab, &cache->slabs_full, list) {
        if (slab_contains_obj(slab, obj)) {
            return slab;
        }
    }
    
    list_for_each_entry(slab, &cache->slabs_partial, list) {
        if (slab_contains_obj(slab, obj)) {
            return slab;
        }
    }
    
    list_for_each_entry(slab, &cache->slabs_free, list) {
        if (slab_contains_obj(slab, obj)) {
            return slab;
        }
    }
    
    return NULL;
}

static bool slab_contains_obj(struct slab *slab, void *obj)
{
    void *start = slab->s_mem;
    void *end = (void *)((uintptr_t)start + slab->cache->num * slab->cache->object_size);
    
    return obj >= start && obj < end;
}

static bool kmem_cache_contains(struct kmem_cache *cache, void *obj)
{
    struct slab *slab;
    
    list_for_each_entry(slab, &cache->slabs_full, list) {
        if (slab_contains_obj(slab, obj)) {
            return true;
        }
    }
    
    list_for_each_entry(slab, &cache->slabs_partial, list) {
        if (slab_contains_obj(slab, obj)) {
            return true;
        }
    }
    
    list_for_each_entry(slab, &cache->slabs_free, list) {
        if (slab_contains_obj(slab, obj)) {
            return true;
        }
    }
    
    return false;
}
