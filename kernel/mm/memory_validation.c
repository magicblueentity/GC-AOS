/*
 * GC-AOS Kernel - Memory Management Validation Implementation
 */

#include "mm/memory_validation.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "mm/slab.h"
#include "kernel/errno.h"
#include "types.h"
#include "kernel/process.h"
#include "arch/timer.h"
#include "kernel/string.h"
#include "mm/page.h"
#include "printk.h"
#include "sync/spinlock.h"

/* ===================================================================== */
/* Global Validation Context */
/* ===================================================================== */

static struct memval_context memval_ctx = {
    .initialized = false,
    .debug_level = MEMVAL_DEBUG_LEVEL,
    .guard_pages_enabled = MEMVAL_GUARD_PAGES,
    .canaries_enabled = MEMVAL_CANARIES,
    .tracking_enabled = MEMVAL_TRACK_ALLOCATIONS,
    .leak_detection_enabled = MEMVAL_DETECT_LEAKS,
    .sanitize_freed_enabled = MEMVAL_SANITIZE_FREED,
};

/* ===================================================================== */
/* Hash Table Configuration */
/* ===================================================================== */

#define MEMVAL_HASH_SIZE           1024    /* Hash table size */
#define MEMVAL_HASH_MASK          (MEMVAL_HASH_SIZE - 1)

/* Hash function for addresses */
static inline uint32_t memval_hash_addr(void *addr)
{
    uint64_t val = (uint64_t)addr;
    return (uint32_t)(val ^ (val >> 32)) & MEMVAL_HASH_MASK;
}

/* ===================================================================== */
/* Canary and Guard Page Functions */
/* ===================================================================== */

static uint32_t memval_generate_canary(void)
{
    /* Generate pseudo-random canary based on timestamp and address */
    static uint32_t canary_seed = 0xCAFEBABE;
    canary_seed = canary_seed * 1103515245 + 12345;
    return canary_seed ^ (uint32_t)(uint64_t)&canary_seed;
}

static int memval_setup_canaries(void *addr, size_t size, struct memval_allocation *alloc)
{
    if (!memval_ctx.canaries_enabled) {
        return 0;
    }
    
    /* Add canaries before and after allocation */
    uint32_t *canary_before = (uint32_t *)addr - 1;
    uint32_t *canary_after = (uint32_t *)((uintptr_t)addr + size);
    
    *canary_before = alloc->canary;
    *canary_after = alloc->canary;
    
    return 0;
}

static int memval_check_canaries_integrity(void *addr, size_t size, uint32_t expected_canary)
{
    if (!memval_ctx.canaries_enabled) {
        return 0;
    }
    
    uint32_t *canary_before = (uint32_t *)addr - 1;
    uint32_t *canary_after = (uint32_t *)((uintptr_t)addr + size);
    
    if (*canary_before != expected_canary) {
        printk(KERN_ERR "MEMVAL: Canary before %p corrupted (expected 0x%08x, got 0x%08x)\n",
               addr, expected_canary, *canary_before);
        return -MEMVAL_ERROR_CANARY;
    }
    
    if (*canary_after != expected_canary) {
        printk(KERN_ERR "MEMVAL: Canary after %p corrupted (expected 0x%08x, got 0x%08x)\n",
               addr, expected_canary, *canary_after);
        return -MEMVAL_ERROR_CANARY;
    }
    
    return 0;
}

static int memval_setup_guard_pages(void *addr, size_t size)
{
    if (!memval_ctx.guard_pages_enabled) {
        return 0;
    }
    
    /* Map guard pages before and after allocation */
    virt_addr_t alloc_start = (virt_addr_t)addr;
    virt_addr_t alloc_end = alloc_start + size;
    
    /* Guard page before allocation */
    virt_addr_t guard_before = alloc_start - PAGE_SIZE;
    if (vmm_map_page(guard_before, 0, PAGE_READONLY) < 0) {
        printk(KERN_WARNING "MEMVAL: Failed to setup guard page before %p\n", addr);
        return -ENOMEM;
    }
    
    /* Guard page after allocation */
    virt_addr_t guard_after = alloc_end;
    if (vmm_map_page(guard_after, 0, PAGE_READONLY) < 0) {
        printk(KERN_WARNING "MEMVAL: Failed to setup guard page after %p\n", addr);
        vmm_unmap_page(guard_before);
        return -ENOMEM;
    }
    
    return 0;
}

static void memval_remove_guard_pages_int(void *addr, size_t size)
{
    if (!memval_ctx.guard_pages_enabled) {
        return;
    }
    
    virt_addr_t alloc_start = (virt_addr_t)addr;
    virt_addr_t alloc_end = alloc_start + size;
    
    /* Unmap guard pages */
    vmm_unmap_page(alloc_start - PAGE_SIZE);
    vmm_unmap_page(alloc_end);
}

/* ===================================================================== */
/* Memory Validation Functions */
/* ===================================================================== */

/**
 * memval_init - Initialize memory validation system
 */
int memval_init(int debug_level)
{
    if (memval_ctx.initialized) {
        printk(KERN_WARNING "MEMVAL: Already initialized\n");
        return 0;
    }
    
    printk(KERN_INFO "MEMVAL: Initializing memory validation (level %d)\n", debug_level);
    
    memval_ctx.debug_level = debug_level;
    
    /* Initialize hash table */
    size_t hash_size = MEMVAL_HASH_SIZE * sizeof(struct list_head);
    memval_ctx.alloc_hash = kmalloc(hash_size, GFP_KERNEL);
    if (!memval_ctx.alloc_hash) {
        printk(KERN_ERR "MEMVAL: Failed to allocate hash table\n");
        return -ENOMEM;
    }
    
    for (int i = 0; i < MEMVAL_HASH_SIZE; i++) {
        INIT_LIST_HEAD(&memval_ctx.alloc_hash[i]);
    }
    
    /* Initialize locks */
    spin_lock_init(&memval_ctx.hash_lock);
    spin_lock_init(&memval_ctx.alloc_lock);
    spin_lock_init(&memval_ctx.region_lock);
    
    /* Initialize lists */
    INIT_LIST_HEAD(&memval_ctx.allocations);
    INIT_LIST_HEAD(&memval_ctx.regions);
    
    /* Create allocation cache */
    memval_ctx.alloc_cache = kmem_cache_create("memval_alloc",
                                              sizeof(struct memval_allocation),
                                              0, SLAB_HWCACHE_ALIGN,
                                              NULL, NULL);
    if (!memval_ctx.alloc_cache) {
        printk(KERN_ERR "MEMVAL: Failed to create allocation cache\n");
        kfree(memval_ctx.alloc_hash);
        return -ENOMEM;
    }
    
    /* Initialize statistics */
    memset(&memval_ctx.stats, 0, sizeof(struct memval_stats));
    
    memval_ctx.initialized = true;
    printk(KERN_INFO "MEMVAL: Memory validation initialized\n");
    return 0;
}

/**
 * memval_shutdown - Shutdown memory validation system
 */
void memval_shutdown(void)
{
    if (!memval_ctx.initialized) {
        return;
    }
    
    printk(KERN_INFO "MEMVAL: Shutting down memory validation\n");
    
    /* Check for leaks before shutdown */
    if (memval_ctx.leak_detection_enabled) {
        char leak_buf[4096];
        int leak_count = memval_detect_leaks(leak_buf, sizeof(leak_buf));
        if (leak_count > 0) {
            printk(KERN_WARNING "MEMVAL: Detected %d memory leaks\n", leak_count);
            printk(KERN_WARNING "%s\n", leak_buf);
        }
    }
    
    /* Destroy allocation cache */
    if (memval_ctx.alloc_cache) {
        kmem_cache_destroy(memval_ctx.alloc_cache);
    }
    
    /* Free hash table */
    if (memval_ctx.alloc_hash) {
        kfree(memval_ctx.alloc_hash);
    }
    
    memval_ctx.initialized = false;
    printk(KERN_INFO "MEMVAL: Shutdown complete\n");
}

/**
 * memval_alloc_track - Track memory allocation
 */
int memval_alloc_track(void *addr, size_t size, size_t requested_size,
                      const char *file, int line, const char *function)
{
    if (!memval_ctx.initialized || !memval_ctx.tracking_enabled) {
        return 0;
    }
    
    if (!addr || size == 0) {
        return -MEMVAL_ERROR_INVALID;
    }
    
    /* Allocate tracking structure */
    struct memval_allocation *alloc = kmem_cache_alloc(memval_ctx.alloc_cache, GFP_KERNEL);
    if (!alloc) {
        return -MEMVAL_ERROR_NOMEM;
    }
    
    /* Initialize allocation tracking */
    memset(alloc, 0, sizeof(struct memval_allocation));
    alloc->addr = addr;
    alloc->size = size;
    alloc->requested_size = requested_size;
    alloc->file = file;
    alloc->line = line;
    alloc->function = function;
    alloc->timestamp = arch_timer_get_ms();
    alloc->pid = current->pid;
    alloc->magic = MEMVAL_MAGIC_ALLOC;
    alloc->canary = memval_generate_canary();
    alloc->active = true;
    alloc->corrupted = false;
    alloc->double_free = false;
    
    /* Calculate checksum */
    alloc->checksum = (uint32_t)((uint64_t)alloc ^ size ^ alloc->timestamp);
    
    /* Setup canaries and guard pages */
    memval_setup_canaries(addr, size, alloc);
    memval_setup_guard_pages(addr, size);
    
    /* Add to hash table */
    uint32_t hash = memval_hash_addr(addr);
    spin_lock(&memval_ctx.hash_lock);
    list_add(&alloc->hash_list, &memval_ctx.alloc_hash[hash]);
    spin_unlock(&memval_ctx.hash_lock);
    
    /* Add to allocation list */
    spin_lock(&memval_ctx.alloc_lock);
    list_add(&alloc->list, &memval_ctx.allocations);
    spin_unlock(&memval_ctx.alloc_lock);
    
    /* Update statistics */
    atomic_inc(&memval_ctx.stats.total_allocations);
    atomic_inc(&memval_ctx.stats.active_allocations);
    atomic_add(size, &memval_ctx.stats.total_allocated);
    
    /* Update peak allocations */
    int active = atomic_read(&memval_ctx.stats.active_allocations);
    if (active > atomic_read(&memval_ctx.stats.peak_allocations)) {
        atomic_set(&memval_ctx.stats.peak_allocations, active);
    }
    
    /* Update peak allocated memory */
    int allocated = atomic_read(&memval_ctx.stats.total_allocated) - 
                    atomic_read(&memval_ctx.stats.total_freed);
    if (allocated > atomic_read(&memval_ctx.stats.peak_allocated)) {
        atomic_set(&memval_ctx.stats.peak_allocated, allocated);
    }
    
    if (memval_ctx.debug_level >= MEMVAL_LEVEL_DETAILED) {
        printk(KERN_DEBUG "MEMVAL: Allocated %p (%zu bytes) at %s:%d in %s()\n",
               addr, size, file, line, function);
    }
    
    return 0;
}

/**
 * memval_free_track - Track memory deallocation
 */
int memval_free_track(void *addr, const char *file, int line, const char *function)
{
    if (!memval_ctx.initialized || !memval_ctx.tracking_enabled) {
        return 0;
    }
    
    if (!addr) {
        return -MEMVAL_ERROR_INVALID;
    }
    
    /* Find allocation in hash table */
    uint32_t hash = memval_hash_addr(addr);
    struct memval_allocation *alloc = NULL;
    
    spin_lock(&memval_ctx.hash_lock);
    list_for_each_entry(alloc, &memval_ctx.alloc_hash[hash], hash_list) {
        if (alloc->addr == addr) {
            break;
        }
    }
    
    if (!alloc || &alloc->hash_list == &memval_ctx.alloc_hash[hash]) {
        spin_unlock(&memval_ctx.hash_lock);
        atomic_inc(&memval_ctx.stats.invalid_frees);
        printk(KERN_ERR "MEMVAL: Invalid free of %p at %s:%d in %s()\n",
               addr, file, line, function);
        return -MEMVAL_ERROR_INVALID_FREE;
    }
    
    /* Check for double free */
    if (!alloc->active) {
        spin_unlock(&memval_ctx.hash_lock);
        alloc->double_free = true;
        atomic_inc(&memval_ctx.stats.double_frees);
        printk(KERN_ERR "MEMVAL: Double free of %p at %s:%d in %s() (original at %s:%d in %s())\n",
               addr, file, line, function, alloc->file, alloc->line, alloc->function);
        return -MEMVAL_ERROR_DOUBLE_FREE;
    }
    
    /* Check magic number */
    if (alloc->magic != MEMVAL_MAGIC_ALLOC) {
        spin_unlock(&memval_ctx.hash_lock);
        alloc->corrupted = true;
        atomic_inc(&memval_ctx.stats.corruption_detected);
        printk(KERN_ERR "MEMVAL: Corrupted allocation header for %p\n", addr);
        return -MEMVAL_ERROR_CORRUPTION;
    }
    
    /* Check canaries */
    int canary_result = memval_check_canaries_integrity(addr, alloc->size, alloc->canary);
    if (canary_result < 0) {
        alloc->corrupted = true;
        atomic_inc(&memval_ctx.stats.corruption_detected);
        if (canary_result == -MEMVAL_ERROR_CANARY) {
            atomic_inc(&memval_ctx.stats.buffer_overflows);
        }
    }
    
    /* Remove from hash table */
    list_del(&alloc->hash_list);
    spin_unlock(&memval_ctx.hash_lock);
    
    /* Remove from allocation list */
    spin_lock(&memval_ctx.alloc_lock);
    list_del(&alloc->list);
    spin_unlock(&memval_ctx.alloc_lock);
    
    /* Remove guard pages */
    memval_remove_guard_pages_int(addr, alloc->size);
    
    /* Sanitize memory if enabled */
    if (memval_ctx.sanitize_freed_enabled) {
        memval_sanitize_on_free(addr, alloc->size);
    }
    
    /* Update statistics */
    alloc->active = false;
    atomic_dec(&memval_ctx.stats.active_allocations);
    atomic_inc(&memval_ctx.stats.total_frees);
    atomic_add(alloc->size, &memval_ctx.stats.total_freed);
    
    /* Free tracking structure */
    kmem_cache_free(memval_ctx.alloc_cache, alloc);
    
    if (memval_ctx.debug_level >= MEMVAL_LEVEL_DETAILED) {
        printk(KERN_DEBUG "MEMVAL: Freed %p at %s:%d in %s()\n",
               addr, file, line, function);
    }
    
    return 0;
}

/**
 * memval_validate_allocation - Validate memory allocation
 */
int memval_validate_allocation(void *addr, __attribute__((unused)) size_t size)
{
    if (!memval_ctx.initialized) {
        return 0;
    }
    
    if (!addr) {
        return -MEMVAL_ERROR_INVALID;
    }
    
    /* Find allocation */
    uint32_t hash = memval_hash_addr(addr);
    struct memval_allocation *alloc = NULL;
    
    spin_lock(&memval_ctx.hash_lock);
    list_for_each_entry(alloc, &memval_ctx.alloc_hash[hash], hash_list) {
        if (alloc->addr == addr) {
            break;
        }
    }
    spin_unlock(&memval_ctx.hash_lock);
    
    if (!alloc || &alloc->hash_list == &memval_ctx.alloc_hash[hash]) {
        return -MEMVAL_ERROR_INVALID_FREE;
    }
    
    /* Check magic */
    if (alloc->magic != MEMVAL_MAGIC_ALLOC) {
        return -MEMVAL_ERROR_CORRUPTION;
    }
    
    /* Check canaries */
    return memval_check_canaries_integrity(addr, alloc->size, alloc->canary);
}

/**
 * memval_check_corruption - Check for memory corruption
 */
int memval_check_corruption(void)
{
    if (!memval_ctx.initialized) {
        return 0;
    }
    
    int corruption_count = 0;
    
    spin_lock(&memval_ctx.alloc_lock);
    struct memval_allocation *alloc;
    list_for_each_entry(alloc, &memval_ctx.allocations, list) {
        if (alloc->active) {
            int result = memval_validate_allocation(alloc->addr, alloc->size);
            if (result < 0) {
                corruption_count++;
                alloc->corrupted = true;
                printk(KERN_ERR "MEMVAL: Corruption detected in allocation %p (%zu bytes) from %s:%d in %s()\n",
                       alloc->addr, alloc->size, alloc->file, alloc->line, alloc->function);
            }
        }
    }
    spin_unlock(&memval_ctx.alloc_lock);
    
    if (corruption_count > 0) {
        printk(KERN_ERR "MEMVAL: Total corruption count: %d\n", corruption_count);
        return -MEMVAL_ERROR_CORRUPTION;
    }
    
    return 0;
}

/**
 * memval_detect_leaks - Detect memory leaks
 */
int memval_detect_leaks(char *buf, size_t size)
{
    if (!memval_ctx.initialized || !memval_ctx.leak_detection_enabled) {
        return 0;
    }
    
    if (!buf || size == 0) {
        return -MEMVAL_ERROR_INVALID;
    }
    
    int offset = 0;
    int leak_count = 0;
    
    spin_lock(&memval_ctx.alloc_lock);
    struct memval_allocation *alloc;
    list_for_each_entry(alloc, &memval_ctx.allocations, list) {
        if (alloc->active) {
            leak_count++;
            offset += snprintf(buf + offset, size - offset,
                             "Leak: %p (%zu bytes) allocated at %s:%d in %s() by PID %d\n",
                             alloc->addr, alloc->size, alloc->file, alloc->line, 
                             alloc->function, alloc->pid);
            
            if (offset >= (int)size - 100) {
                offset += snprintf(buf + offset, size - offset, "... (truncated)\n");
                break;
            }
        }
    }
    spin_unlock(&memval_ctx.alloc_lock);
    
    return leak_count;
}

/**
 * memval_sanitize_on_free - Sanitize memory on free
 */
void memval_sanitize_on_free(void *addr, size_t size)
{
    if (!memval_ctx.sanitize_freed_enabled) {
        return;
    }
    
    /* Fill with pattern to detect use-after-free */
    memset(addr, 0xFE, size);
}

/**
 * memval_get_stats - Get validation statistics
 */
int memval_get_stats(struct memval_stats *stats)
{
    if (!memval_ctx.initialized || !stats) {
        return -MEMVAL_ERROR_INVALID;
    }
    
    *stats = memval_ctx.stats;
    return 0;
}

/**
 * memval_dump_allocations - Dump all allocations
 */
int memval_dump_allocations(char *buf, size_t size)
{
    if (!memval_ctx.initialized || !buf || size == 0) {
        return -MEMVAL_ERROR_INVALID;
    }
    
    int offset = 0;
    
    spin_lock(&memval_ctx.alloc_lock);
    struct memval_allocation *alloc;
    list_for_each_entry(alloc, &memval_ctx.allocations, list) {
        if (alloc->active) {
            offset += snprintf(buf + offset, size - offset,
                             "%p: %zu bytes (requested %zu) from %s:%d in %s() by PID %d\n",
                             alloc->addr, alloc->size, alloc->requested_size,
                             alloc->file, alloc->line, alloc->function, alloc->pid);
            
            if (offset >= (int)size - 100) {
                offset += snprintf(buf + offset, size - offset, "... (truncated)\n");
                break;
            }
        }
    }
    spin_unlock(&memval_ctx.alloc_lock);
    
    return offset;
}
