/*
 * GC-AOS - Preemptive Priority Scheduler
 * 
 * Priority-based scheduling with time slicing and round-robin fallback.
 * Ensures no process can block the system.
 */

#ifndef _SCHEDULER_PREEMPTIVE_H
#define _SCHEDULER_PREEMPTIVE_H

#include "kernel/process_modern.h"
#include "types.h"
#include "sync/spinlock.h"

/* ===================================================================== */
/* Scheduler Configuration */
/* ===================================================================== */

#define SCHED_PRIO_LEVELS       140     /* 0-139, 0 = highest */
#define SCHED_PRIO_REALTIME     0       /* Real-time range start */
#define SCHED_PRIO_NORMAL       100     /* Normal process range start */
#define SCHED_PRIO_IDLE         139     /* Idle priority */

#define SCHED_TIMESLICE_MIN     1       /* 1ms */
#define SCHED_TIMESLICE_DEFAULT 10      /* 10ms */
#define SCHED_TIMESLICE_MAX     100     /* 100ms */

#define SCHED_HZ                100     /* 100Hz timer tick */
#define SCHED_TICK_NS           (1000000000ULL / SCHED_HZ)  /* 10ms per tick */

#define SCHED_LOAD_BALANCE_MS   100     /* Load balance interval */

/* ===================================================================== */
/* Scheduling Policies */
/* ===================================================================== */

typedef enum {
    SCHED_POLICY_FIFO = 0,      /* First-in-first-out (real-time) */
    SCHED_POLICY_RR,            /* Round-robin (real-time) */
    SCHED_POLICY_NORMAL,        /* Normal fair scheduling */
    SCHED_POLICY_BATCH,         /* Batch (low priority) */
    SCHED_POLICY_IDLE,          /* Idle (lowest priority) */
} sched_policy_t;

/* ===================================================================== */
/* Run Queue Structure */
/* ===================================================================== */

/* Priority queue bitmap for O(1) scheduling */
#define BITMAP_SIZE ((SCHED_PRIO_LEVELS + 63) / 64)

typedef struct {
    uint64_t bitmap[BITMAP_SIZE];   /* Priority bitmap */
    struct list_head queues[SCHED_PRIO_LEVELS]; /* Per-priority queues */
    int count;                       /* Total runnable tasks */
} prio_array_t;

/* Per-CPU run queue */
typedef struct runqueue {
    spinlock_t lock;
    
    /* Current and idle process */
    process_t *current;
    process_t *idle;
    
    /* Priority arrays (active and expired) */
    prio_array_t *active;
    prio_array_t *expired;
    prio_array_t array1;
    prio_array_t array2;
    
    /* Statistics */
    uint64_t nr_switches;           /* Context switches */
    uint64_t nr_running;            /* Runnable processes */
    uint64_t nr_blocked;            /* Blocked processes */
    uint64_t load;                  /* CPU load (0-100) */
    
    /* Timing */
    uint64_t tick_count;
    uint64_t last_balance;
    
    /* Preemption */
    int need_resched;               /* Reschedule flag */
    uint64_t next_tick;             /* Next timer tick */
} runqueue_t;

/* Global scheduler state */
extern runqueue_t g_runqueue;

/* ===================================================================== */
/* Scheduler API */
/* ===================================================================== */

/**
 * scheduler_init - Initialize the scheduler
 */
void scheduler_init(void);

/**
 * scheduler_tick - Timer tick handler
 * 
 * Called from timer interrupt. Handles preemption,
 * time slice accounting, and priority decay.
 */
void scheduler_tick(void);

/**
 * scheduler_add_process - Add process to run queue
 * @proc: Process to add
 * 
 * Return: 0 on success, negative on error
 */
int scheduler_add_process(process_t *proc);

/**
 * scheduler_remove_process - Remove process from run queue
 * @proc: Process to remove
 * 
 * Return: 0 on success, negative on error
 */
int scheduler_remove_process(process_t *proc);

/**
 * scheduler_reschedule - Main scheduling function
 * 
 * Selects the next process to run based on priority and
 * performs context switch. Called from timer interrupt
 * or when process yields/blocks.
 */
void scheduler_reschedule(void);

/**
 * scheduler_yield - Voluntarily yield CPU
 */
void scheduler_yield(void);

/**
 * scheduler_block - Block current process
 * @reason: Block reason (for debugging)
 */
void scheduler_block(const char *reason);

/**
 * scheduler_wake - Wake up a blocked/sleeping process
 * @proc: Process to wake
 * 
 * Return: 0 if woken, 1 if already running, negative on error
 */
int scheduler_wake(process_t *proc);

/* ===================================================================== */
/* Priority Management */
/* ===================================================================== */

/**
 * scheduler_set_policy - Set scheduling policy for process
 * @proc: Process to modify
 * @policy: New scheduling policy
 * @priority: New priority (0-139)
 * 
 * Return: 0 on success, negative on error
 */
int scheduler_set_policy(process_t *proc, sched_policy_t policy, int priority);

/**
 * scheduler_boost_priority - Temporarily boost process priority
 * @proc: Process to boost
 * @boost_amount: Priority boost (negative = higher priority)
 * 
 * Return: 0 on success, negative on error
 */
int scheduler_boost_priority(process_t *proc, int boost_amount);

/**
 * scheduler_unboost_priority - Remove priority boost
 * @proc: Process to unboost
 * 
 * Return: 0 on success, negative on error
 */
int scheduler_unboost_priority(process_t *proc);

/**
 * scheduler_get_timeslice - Calculate timeslice for priority
 * @priority: Process priority
 * 
 * Return: Timeslice in milliseconds
 */
int scheduler_get_timeslice(int priority);

/* ===================================================================== */
/* Preemption Control */
/* ===================================================================== */

/**
 * scheduler_need_resched - Mark that reschedule is needed
 */
static inline void scheduler_need_resched(void)
{
    g_runqueue.need_resched = 1;
}

/**
 * scheduler_clear_resched - Clear reschedule flag
 */
static inline void scheduler_clear_resched(void)
{
    g_runqueue.need_resched = 0;
}

/**
 * scheduler_should_resched - Check if reschedule is needed
 * 
 * Return: 1 if reschedule needed, 0 otherwise
 */
static inline int scheduler_should_resched(void)
{
    return g_runqueue.need_resched;
}

/**
 * preempt_enable - Enable preemption
 */
void preempt_enable(void);

/**
 * preempt_disable - Disable preemption
 */
void preempt_disable(void);

/**
 * preempt_count - Get current preemption depth
 * 
 * Return: Preemption disable count
 */
int preempt_count(void);

/* ===================================================================== */
/* Fair Scheduling (CFS-like) */
/* ===================================================================== */

/**
 * scheduler_calc_vruntime - Calculate virtual runtime
 * @proc: Process
 * 
 * Return: Virtual runtime value
 */
uint64_t scheduler_calc_vruntime(process_t *proc);

/**
 * scheduler_update_vruntime - Update process virtual runtime
 * @proc: Process
 * @delta_ns: Time spent running
 */
void scheduler_update_vruntime(process_t *proc, uint64_t delta_ns);

/* ===================================================================== */
/* Statistics */
/* ===================================================================== */

/**
 * scheduler_get_stats - Get scheduler statistics
 * @switches: Output for context switch count
 * @load: Output for CPU load
 */
void scheduler_get_stats(uint64_t *switches, int *load);

/**
 * scheduler_dump_state - Dump scheduler state for debugging
 */
void scheduler_dump_state(void);

#endif /* _SCHEDULER_PREEMPTIVE_H */
