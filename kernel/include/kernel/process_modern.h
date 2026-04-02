/*
 * GC-AOS - Modern Process System
 * 
 * Real process system with proper state management,
 * context switching, and lifecycle control.
 */

#ifndef _PROCESS_MODERN_H
#define _PROCESS_MODERN_H

#include "types.h"
#include "kernel/list.h"
#include "sync/spinlock.h"

/* ===================================================================== */
/* Process Limits */
/* ===================================================================== */

#define MAX_PROCESSES       256
#define MAX_THREADS         1024
#define PROCESS_NAME_MAX    32
#define PROCESS_STACK_SIZE  (64 * 1024)     /* 64KB kernel stack */
#define USER_STACK_SIZE     (2 * 1024 * 1024) /* 2MB user stack */

/* ===================================================================== */
/* Process States */
/* ===================================================================== */

typedef enum {
    PROCESS_STATE_FREE = 0,         /* Slot available */
    PROCESS_STATE_NEW,              /* Created, not yet started */
    PROCESS_STATE_READY,            /* Ready to run */
    PROCESS_STATE_RUNNING,          /* Currently executing */
    PROCESS_STATE_BLOCKED,          /* Waiting for resource/event */
    PROCESS_STATE_SLEEPING,         /* Sleeping (timed wait) */
    PROCESS_STATE_ZOMBIE,           /* Exited, waiting cleanup */
    PROCESS_STATE_DEAD              /* Being destroyed */
} process_state_t;

/* ===================================================================== */
/* Process Context (Architecture-agnostic) */
/* ===================================================================== */

struct cpu_context {
    uint64_t x0;     /* Parameter/return value */
    uint64_t x1;     /* Parameter */
    uint64_t x2;     /* Parameter */
    uint64_t x3;     /* Parameter */
    uint64_t x4;
    uint64_t x5;
    uint64_t x6;
    uint64_t x7;
    uint64_t x8;
    uint64_t x9;
    uint64_t x10;
    uint64_t x11;
    uint64_t x12;
    uint64_t x13;
    uint64_t x14;
    uint64_t x15;
    uint64_t x16;
    uint64_t x17;
    uint64_t x18;
    uint64_t x19;    /* Callee-saved */
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t fp;     /* Frame pointer (x29) */
    uint64_t sp;     /* Stack pointer */
    uint64_t pc;     /* Program counter */
    uint64_t pstate; /* Processor state (flags) */
};

/* ===================================================================== */
/* Process Structure */
/* ===================================================================== */

typedef struct process {
    /* Identity */
    int pid;
    int tid;                        /* Thread ID (same as pid for main thread) */
    char name[PROCESS_NAME_MAX];
    
    /* State */
    process_state_t state;
    int priority;
    int base_priority;              /* Base priority (can be boosted) */
    
    /* Execution Context */
    struct cpu_context context;
    void *kernel_stack;             /* Kernel stack base */
    void *user_stack;               /* User stack base */
    uint64_t entry_point;
    
    /* Memory */
    struct mm_struct *mm;           /* Memory management */
    uint64_t code_base;
    uint64_t code_size;
    uint64_t heap_base;
    uint64_t heap_size;
    
    /* Scheduling */
    uint64_t time_slice;            /* Remaining time slice (ns) */
    uint64_t vruntime;              /* Virtual runtime for fair scheduling */
    uint64_t exec_start;            /* When process started running */
    uint64_t total_runtime;         /* Total CPU time used */
    int cpu;                        /* CPU affinity */
    
    /* Timers */
    uint64_t sleep_until;           /* Wake time if sleeping */
    
    /* Hierarchy */
    struct process *parent;
    struct list_head children;
    struct list_head sibling;
    
    /* Synchronization */
    struct list_head wait_list;     /* List of processes waiting on this */
    void *wait_obj;                 /* Object being waited on */
    
    /* Statistics */
    uint64_t created_at;
    uint64_t wake_count;
    uint64_t switch_count;
    
    /* Exit */
    int exit_code;
    struct process *waiting_parent; /* Parent waiting for this process */
    
    /* Links */
    struct list_head run_list;      /* Run queue linkage */
    struct list_head all_list;      /* Global process list */
} process_t;

/* ===================================================================== */
/* Process Manager State */
/* ===================================================================== */

struct process_manager {
    spinlock_t lock;
    
    /* Process table */
    process_t *processes[MAX_PROCESSES];
    int next_pid;
    int process_count;
    
    /* Lists */
    struct list_head all_processes;     /* All processes */
    struct list_head ready_queue;       /* Ready to run */
    struct list_head zombie_queue;      /* Waiting for cleanup */
    
    /* Current process */
    process_t *current;
    process_t *idle_process;
};

extern struct process_manager g_process_manager;

/* ===================================================================== */
/* Process Lifecycle API */
/* ===================================================================== */

/**
 * process_system_init - Initialize the process management system
 */
void process_system_init(void);

/**
 * process_create - Create a new process
 * @name: Process name
 * @entry: Entry point function
 * @arg: Argument to pass to entry
 * @priority: Initial priority (0-139, lower is higher priority)
 * 
 * Return: New process pointer, or NULL on failure
 */
process_t *process_create(const char *name, void (*entry)(void *), 
                          void *arg, int priority);

/**
 * process_create_user - Create a user-space process from ELF
 * @path: Path to ELF executable
 * @argc: Argument count
 * @argv: Argument vector
 * 
 * Return: New process pointer, or NULL on failure
 */
process_t *process_create_user(const char *path, int argc, char **argv);

/**
 * process_start - Start a created process
 * @proc: Process to start
 * 
 * Return: 0 on success, negative on error
 */
int process_start(process_t *proc);

/**
 * process_kill - Terminate a process
 * @pid: Process ID to kill
 * @exit_code: Exit code
 * 
 * Return: 0 on success, negative on error
 */
int process_kill(int pid, int exit_code);

/**
 * process_exit - Exit current process
 * @code: Exit code
 */
void process_exit(int code) __noreturn;

/**
 * process_destroy - Clean up and free a process
 * @proc: Process to destroy
 */
void process_destroy(process_t *proc);

/* ===================================================================== */
/* Context Switching */
/* ===================================================================== */

/**
 * process_switch_context - Switch from one process to another
 * @from: Current process (save context)
 * @to: Next process (restore context)
 * 
 * This is the core context switch function.
 */
void process_switch_context(process_t *from, process_t *to);

/**
 * process_schedule - Select next process and switch to it
 */
void process_schedule(void);

/**
 * process_yield - Voluntarily give up CPU
 */
void process_yield(void);

/* ===================================================================== */
/* Process State Management */
/* ===================================================================== */

/**
 * process_sleep - Put process to sleep for a duration
 * @proc: Process to sleep (NULL = current)
 * @nanoseconds: Sleep duration in nanoseconds
 */
void process_sleep(process_t *proc, uint64_t nanoseconds);

/**
 * process_wake - Wake up a sleeping/blocked process
 * @proc: Process to wake
 * 
 * Return: 0 if woken, 1 if already running, negative on error
 */
int process_wake(process_t *proc);

/**
 * process_block - Block process on a wait object
 * @proc: Process to block
 * @obj: Object being waited on
 */
void process_block(process_t *proc, void *obj);

/**
 * process_unblock_all - Unblock all processes waiting on an object
 * @obj: Wait object
 */
void process_unblock_all(void *obj);

/* ===================================================================== */
/* Process Queries */
/* ===================================================================== */

/**
 * process_get - Get process by PID
 * @pid: Process ID
 * 
 * Return: Process pointer, or NULL if not found
 */
process_t *process_get(int pid);

/**
 * process_current - Get currently running process
 * 
 * Return: Current process pointer
 */
process_t *process_current(void);

/**
 * process_count - Get number of active processes
 * 
 * Return: Active process count
 */
int process_count(void);

/**
 * process_count_ready - Get number of ready processes
 * 
 * Return: Ready process count
 */
int process_count_ready(void);

/**
 * process_set_priority - Change process priority
 * @proc: Process to modify
 * @priority: New priority (0-139)
 * 
 * Return: 0 on success, negative on error
 */
int process_set_priority(process_t *proc, int priority);

/**
 * process_get_info - Get process information
 * @pid: Process ID
 * @name: Buffer for name (can be NULL)
 * @name_size: Name buffer size
 * @state: Output for state (can be NULL)
 * 
 * Return: 0 if found, negative if not found
 */
int process_get_info(int pid, char *name, int name_size, int *state);

/* ===================================================================== */
/* Assembly Helpers */
/* ===================================================================== */

/**
 * arch_context_save - Architecture-specific context save
 * @ctx: Context structure to save to
 */
void arch_context_save(struct cpu_context *ctx);

/**
 * arch_context_restore - Architecture-specific context restore
 * @ctx: Context structure to restore from
 */
void arch_context_restore(struct cpu_context *ctx);

/**
 * arch_get_stack_pointer - Get current stack pointer
 * 
 * Return: Current SP value
 */
uint64_t arch_get_stack_pointer(void);

/**
 * arch_get_program_counter - Get current program counter
 * 
 * Return: Current PC value
 */
uint64_t arch_get_program_counter(void);

#endif /* _PROCESS_MODERN_H */
