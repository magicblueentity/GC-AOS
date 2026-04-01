/*
 * GC-AOS Kernel - Memory Management Validation Layer
 * 
 * Provides validation, debugging, and hardening for memory management.
 * Detects race conditions, memory corruption, and fragmentation.
 */

#ifndef _MM_MEMORY_VALIDATION_H
#define _MM_MEMORY_VALIDATION_H

#include "types.h"
#include "mm/slab.h"
#include "mm/pmm.h"

/* ===================================================================== */
/* Memory Validation Configuration */
/* ===================================================================== */

#define MEMVAL_ENABLED            1       /* Enable validation */
#define MEMVAL_DEBUG_LEVEL        2       /* Debug level (0-3) */
#define MEMVAL_GUARD_PAGES        1       /* Enable guard pages */
#define MEMVAL_CANARIES           1       /* Enable canaries */
#define MEMVAL_TRACK_ALLOCATIONS  1       /* Track allocations */
#define MEMVAL_DETECT_LEAKS       1       /* Leak detection */
#define MEMVAL_SANITIZE_FREED     1       /* Sanitize freed memory */

/* Validation levels */
#define MEMVAL_LEVEL_NONE         0       /* No validation */
#define MEMVAL_LEVEL_BASIC        1       /* Basic checks */
#define MEMVAL_LEVEL_DETAILED     2       /* Detailed validation */
#define MEMVAL_LEVEL_PARANOID     3       /* Paranoid validation */

/* ===================================================================== */
/* Memory Allocation Tracking */
/* ===================================================================== */

struct memval_allocation {
    /* Allocation info */
    void *addr;                     /* Allocation address */
    size_t size;                    /* Allocation size */
    size_t requested_size;          /* Originally requested size */
    
    /* Allocation metadata */
    const char *file;               /* Source file */
    int line;                       /* Source line */
    const char *function;           /* Source function */
    uint64_t timestamp;             /* Allocation time */
    pid_t pid;                      /* Allocating process */
    uint64_t call_stack[8];         /* Call stack trace */
    
    /* Validation data */
    uint32_t magic;                 /* Magic number */
    uint32_t canary;                /* Canary value */
    uint32_t checksum;              /* Checksum */
    
    /* State tracking */
    bool active;                    /* Allocation active */
    bool corrupted;                 /* Corruption detected */
    bool double_free;               /* Double free detected */
    
    /* List linkage */
    struct list_head list;          /* Allocation list */
    struct list_head hash_list;     /* Hash list */
};

/* Magic numbers */
#define MEMVAL_MAGIC_ALLOC         0xDEADBEEF  /* Allocation magic */
#define MEMVAL_MAGIC_FREE          0xFEEDFACE  /* Free magic */
#define MEMVAL_MAGIC_CORRUPTED     0xBADC0FFE  /* Corrupted magic */

/* Canary values */
#define MEMVAL_CANARY_VALUE        0xCAFEBABE  /* Canary value */
#define MEMVAL_CANARY_FREED        0xDEADC0DE  /* Freed canary */

/* ===================================================================== */
/* Memory Statistics */
/* ===================================================================== */

struct memval_stats {
    /* Allocation statistics */
    atomic_t total_allocations;     /* Total allocations */
    atomic_t total_frees;           /* Total frees */
    atomic_t active_allocations;    /* Active allocations */
    atomic_t peak_allocations;      /* Peak allocations */
    
    /* Size statistics */
    atomic_t total_allocated;       /* Total bytes allocated */
    atomic_t total_freed;          /* Total bytes freed */
    atomic_t peak_allocated;        /* Peak bytes allocated */
    
    /* Error statistics */
    atomic_t corruption_detected;   /* Corruption detected */
    atomic_t double_frees;          /* Double frees */
    atomic_t invalid_frees;          /* Invalid frees */
    atomic_t buffer_overflows;       /* Buffer overflows */
    atomic_t buffer_underflows;     /* Buffer underflows */
    atomic_t use_after_free;        /* Use after free */
    
    /* Fragmentation statistics */
    atomic_t fragmentation_count;   /* Fragmentation events */
    atomic_t compaction_count;      /* Compaction events */
    atomic_t defragmentation_count; /* Defragmentation events */
    
    /* Performance statistics */
    uint64_t validation_time;       /* Time spent in validation */
    uint64_t overhead_time;        /* Overhead time */
    uint64_t last_compaction;       /* Last compaction time */
};

/* ===================================================================== */
/* Memory Validation Context */
/* ===================================================================== */

struct memval_context {
    /* Hash table for fast lookup */
    struct list_head *alloc_hash;    /* Allocation hash table */
    size_t hash_size;               /* Hash table size */
    spinlock_t hash_lock;           /* Hash table lock */
    
    /* Allocation tracking */
    struct list_head allocations;    /* All allocations */
    spinlock_t alloc_lock;          /* Allocation lock */
    
    /* Slab cache for tracking */
    struct kmem_cache *alloc_cache; /* Allocation cache */
    
    /* Statistics */
    struct memval_stats stats;      /* Validation statistics */
    
    /* Configuration */
    int debug_level;                /* Debug level */
    bool guard_pages_enabled;       /* Guard pages enabled */
    bool canaries_enabled;          /* Canaries enabled */
    bool tracking_enabled;          /* Tracking enabled */
    bool leak_detection_enabled;    /* Leak detection enabled */
    bool sanitize_freed_enabled;     /* Sanitize freed memory */
    
    /* State */
    bool initialized;               /* Context initialized */
    bool validation_running;        /* Validation running */
    
    /* Memory regions */
    struct list_head regions;       /* Memory regions */
    spinlock_t region_lock;         /* Region lock */
};

/* ===================================================================== */
/* Memory Region Structure */
/* ===================================================================== */

struct memval_region {
    /* Region info */
    virt_addr_t start;              /* Region start */
    virt_addr_t end;                /* Region end */
    size_t size;                    /* Region size */
    
    /* Region type */
    enum {
        MEMVAL_REGION_KERNEL,       /* Kernel memory */
        MEMVAL_REGION_USER,         /* User memory */
        MEMVAL_REGION_DEVICE,       /* Device memory */
        MEMVAL_REGION_DMA,          /* DMA memory */
        MEMVAL_REGION_RESERVED,     /* Reserved memory */
    } type;
    
    /* Region properties */
    bool read_only;                 /* Read-only region */
    bool execute_disable;           /* Execute disable */
    bool cache_disabled;            /* Cache disabled */
    bool guard_pages;               /* Guard pages enabled */
    
    /* Region metadata */
    const char *name;               /* Region name */
    pid_t owner_pid;                /* Owner process */
    uint64_t created_time;          /* Creation time */
    
    /* List linkage */
    struct list_head list;          /* Region list */
};

/* ===================================================================== */
/* Memory Validation Functions */
/* ===================================================================== */

/**
 * memval_init - Initialize memory validation system
 * @debug_level: Debug level (0-3)
 * 
 * Return: 0 on success, negative error on failure
 */
int memval_init(int debug_level);

/**
 * memval_shutdown - Shutdown memory validation system
 */
void memval_shutdown(void);

/**
 * memval_alloc_track - Track memory allocation
 * @addr: Allocation address
 * @size: Allocation size
 * @requested_size: Originally requested size
 * @file: Source file
 * @line: Source line
 * @function: Source function
 * 
 * Return: 0 on success, negative error on failure
 */
int memval_alloc_track(void *addr, size_t size, size_t requested_size,
                      const char *file, int line, const char *function);

/**
 * memval_free_track - Track memory deallocation
 * @addr: Allocation address
 * @file: Source file
 * @line: Source line
 * @function: Source function
 * 
 * Return: 0 on success, negative error on failure
 */
int memval_free_track(void *addr, const char *file, int line, const char *function);

/**
 * memval_validate_allocation - Validate memory allocation
 * @addr: Allocation address
 * @size: Expected size
 * 
 * Return: 0 if valid, negative error if corrupted
 */
int memval_validate_allocation(void *addr, size_t size);

/**
 * memval_validate_range - Validate memory range
 * @addr: Start address
 * @size: Range size
 * @writable: True if range should be writable
 * 
 * Return: 0 if valid, negative error if corrupted
 */
int memval_validate_range(void *addr, size_t size, bool writable);

/**
 * memval_check_corruption - Check for memory corruption
 * 
 * Return: 0 if no corruption, negative error if corruption detected
 */
int memval_check_corruption(void);

/**
 * memval_detect_leaks - Detect memory leaks
 * @buf: Buffer to write leak report to
 * @size: Buffer size
 * 
 * Return: Number of bytes written or negative error
 */
int memval_detect_leaks(char *buf, size_t size);

/**
 * memval_compact_memory - Compact memory to reduce fragmentation
 * 
 * Return: 0 on success, negative error on failure
 */
int memval_compact_memory(void);

/**
 * memval_defragment - Defragment memory
 * 
 * Return: 0 on success, negative error on failure
 */
int memval_defragment(void);

/* ===================================================================== */
/* Memory Region Management */
/* ===================================================================== */

/**
 * memval_add_region - Add memory region
 * @start: Region start
 * @end: Region end
 * @type: Region type
 * @name: Region name
 * @read_only: Read-only flag
 * @execute_disable: Execute disable flag
 * 
 * Return: 0 on success, negative error on failure
 */
int memval_add_region(virt_addr_t start, virt_addr_t end, int type,
                     const char *name, bool read_only, bool execute_disable);

/**
 * memval_remove_region - Remove memory region
 * @start: Region start
 * @end: Region end
 * 
 * Return: 0 on success, negative error on failure
 */
int memval_remove_region(virt_addr_t start, virt_addr_t end);

/**
 * memval_protect_region - Protect memory region
 * @start: Region start
 * @end: Region end
 * @read_only: Make read-only
 * @execute_disable: Disable execution
 * 
 * Return: 0 on success, negative error on failure
 */
int memval_protect_region(virt_addr_t start, virt_addr_t end,
                         bool read_only, bool execute_disable);

/* ===================================================================== */
/* Memory Sanitization */
/* ===================================================================== */

/**
 * memval_sanitize_memory - Sanitize memory region
 * @addr: Memory address
 * @size: Memory size
 * @pattern: Sanitization pattern
 */
void memval_sanitize_memory(void *addr, size_t size, uint8_t pattern);

/**
 * memval_sanitize_on_free - Sanitize memory on free
 * @addr: Memory address
 * @size: Memory size
 */
void memval_sanitize_on_free(void *addr, size_t size);

/* ===================================================================== */
/* Debugging and Diagnostics */
/* ===================================================================== */

/**
 * memval_dump_allocations - Dump all allocations
 * @buf: Buffer to write dump to
 * @size: Buffer size
 * 
 * Return: Number of bytes written or negative error
 */
int memval_dump_allocations(char *buf, size_t size);

/**
 * memval_dump_regions - Dump memory regions
 * @buf: Buffer to write dump to
 * @size: Buffer size
 * 
 * Return: Number of bytes written or negative error
 */
int memval_dump_regions(char *buf, size_t size);

/**
 * memval_get_stats - Get validation statistics
 * @stats: Output statistics
 * 
 * Return: 0 on success, negative error on failure
 */
int memval_get_stats(struct memval_stats *stats);

/**
 * memval_reset_stats - Reset validation statistics
 */
void memval_reset_stats(void);

/**
 * memval_validate_all - Validate all memory
 * 
 * Return: 0 if all valid, negative error if corruption detected
 */
int memval_validate_all(void);

/* ===================================================================== */
/* Guard Page Management */
/* ===================================================================== */

/**
 * memval_add_guard_pages - Add guard pages around allocation
 * @addr: Allocation address
 * @size: Allocation size
 * 
 * Return: 0 on success, negative error on failure
 */
int memval_add_guard_pages(void *addr, size_t size);

/**
 * memval_remove_guard_pages - Remove guard pages
 * @addr: Allocation address
 * @size: Allocation size
 * 
 * Return: 0 on success, negative error on failure
 */
int memval_remove_guard_pages(void *addr, size_t size);

/**
 * memval_check_guard_pages - Check guard pages
 * @addr: Allocation address
 * @size: Allocation size
 * 
 * Return: 0 if guard pages intact, negative error if corrupted
 */
int memval_check_guard_pages(void *addr, size_t size);

/* ===================================================================== */
/* Canary Management */
/* ===================================================================== */

/**
 * memval_add_canaries - Add canaries around allocation
 * @addr: Allocation address
 * @size: Allocation size
 * 
 * Return: 0 on success, negative error on failure
 */
int memval_add_canaries(void *addr, size_t size);

/**
 * memval_check_canaries - Check canaries
 * @addr: Allocation address
 * @size: Allocation size
 * 
 * Return: 0 if canaries intact, negative error if corrupted
 */
int memval_check_canaries(void *addr, size_t size);

/**
 * memval_update_canaries - Update canaries
 * @addr: Allocation address
 * @size: Allocation size
 * 
 * Return: 0 on success, negative error on failure
 */
int memval_update_canaries(void *addr, size_t size);

/* ===================================================================== */
/* Helper Macros */
/* ===================================================================== */

#ifdef MEMVAL_ENABLED
#define memval_track_alloc(addr, size) \
    memval_alloc_track(addr, size, size, __FILE__, __LINE__, __func__)

#define memval_track_free(addr) \
    memval_free_track(addr, __FILE__, __LINE__, __func__)

#define memval_validate(addr, size) \
    memval_validate_allocation(addr, size)

#define memval_kmalloc(size) \
    ({ \
        void *ptr = kmalloc(size, GFP_KERNEL); \
        if (ptr) memval_track_alloc(ptr, size); \
        ptr; \
    })

#define memval_kfree(ptr) \
    do { \
        if (ptr) { \
            memval_track_free(ptr); \
            kfree(ptr); \
        } \
    } while(0)

#define memval_kzalloc(size) \
    ({ \
        void *ptr = kzalloc(size, GFP_KERNEL); \
        if (ptr) memval_track_alloc(ptr, size); \
        ptr; \
    })

#else
#define memval_track_alloc(addr, size) do {} while(0)
#define memval_track_free(addr) do {} while(0)
#define memval_validate(addr, size) (0)
#define memval_kmalloc(size) kmalloc(size, GFP_KERNEL)
#define memval_kfree(ptr) kfree(ptr)
#define memval_kzalloc(size) kzalloc(size, GFP_KERNEL)
#endif

/* ===================================================================== */
/* Error Codes */
/* ===================================================================== */

#define MEMVAL_SUCCESS              0       /* Success */
#define MEMVAL_ERROR_INVALID        -1      /* Invalid parameter */
#define MEMVAL_ERROR_NOMEM          -2      /* Out of memory */
#define MEMVAL_ERROR_CORRUPTION     -3      /* Memory corruption */
#define MEMVAL_ERROR_DOUBLE_FREE    -4      /* Double free */
#define MEMVAL_ERROR_INVALID_FREE   -5      /* Invalid free */
#define MEMVAL_ERROR_OVERFLOW       -6      /* Buffer overflow */
#define MEMVAL_ERROR_UNDERFLOW      -7      /* Buffer underflow */
#define MEMVAL_ERROR_USE_AFTER_FREE -8      /* Use after free */
#define MEMVAL_ERROR_LEAK           -9      /* Memory leak */
#define MEMVAL_ERROR_FRAGMENTATION  -10     /* Memory fragmentation */
#define MEMVAL_ERROR_GUARD_PAGE    -11     /* Guard page violation */
#define MEMVAL_ERROR_CANARY         -12     /* Canary violation */

#endif /* _MM_MEMORY_VALIDATION_H */
