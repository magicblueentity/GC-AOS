/*
 * GC-AOS Kernel - Enhanced Fairness Scheduler
 * 
 * Implements starvation prevention, aging, and fair scheduling
 * with dynamic load balancing and optimized context switching.
 */

#ifndef _SCHED_FAIRNESS_H
#define _SCHED_FAIRNESS_H

#include "sched/priority.h"
#include "types.h"

/* ===================================================================== */
/* Fairness Scheduler Configuration */
/* ===================================================================== */

#define FAIRNESS_AGING_INTERVAL      100     /* Aging interval in ms */
#define FAIRNESS_AGE_BOOST          1       /* Age boost per interval */
#define FAIRNESS_MAX_AGE            10      /* Maximum age boost */
#define FAIRNESS_STARVATION_THRESHOLD 5000  /* Starvation threshold in ms */
#define FAIRNESS_MIN_TIMESLICE      1       /* Minimum timeslice in ms */
#define FAIRNESS_MAX_TIMESLICE      100     /* Maximum timeslice in ms */
#define FAIRNESS_TARGET_LATENCY     20      /* Target latency in ms */

/* Fairness levels */
#define FAIRNESS_LEVEL_NONE         0       /* No fairness */
#define FAIRNESS_LEVEL_BASIC        1       /* Basic fairness */
#define FAIRNESS_LEVEL_AGGRESSIVE   2       /* Aggressive fairness */
#define FAIRNESS_LEVEL_REALTIME     3       /* Real-time fairness */

/* ===================================================================== */
/* Enhanced Task Structure */
/* ===================================================================== */

struct task_fairness {
    /* Fairness metrics */
    uint64_t vruntime;              /* Virtual runtime */
    uint64_t min_vruntime;          /* Minimum vruntime in runqueue */
    uint64_t exec_start;            /* Execution start time */
    uint64_t sum_exec_runtime;      /* Total execution time */
    uint64_t prev_sum_exec_runtime;  /* Previous execution time */
    uint64_t wait_start;            /* Wait start time */
    uint64_t sleep_start;           /* Sleep start time */
    
    /* Aging and starvation prevention */
    int age;                        /* Current age */
    uint64_t last_age_time;         /* Last aging time */
    uint64_t last_run_time;         /* Last time task ran */
    int starvation_count;            /* Starvation counter */
    
    /* Fairness parameters */
    int weight;                     /* Task weight (nice-based) */
    int timeslice;                  /* Current timeslice */
    int dynamic_timeslice;           /* Dynamic timeslice */
    uint64_t time_slice_remaining;   /* Remaining time slice */
    
    /* Load balancing */
    int preferred_cpu;               /* Preferred CPU */
    int migration_penalty;          /* Migration penalty */
    uint64_t last_migration;         /* Last migration time */
    
    /* Fairness statistics */
    uint64_t wait_sum;              /* Total wait time */
    uint64_t sleep_sum;             /* Total sleep time */
    uint64_t iowait_sum;            /* Total I/O wait time */
    uint64_t sched_switch_count;    /* Number of scheduler switches */
    uint64_t voluntary_switches;     /* Voluntary context switches */
    uint64_t involuntary_switches;  /* Involuntary context switches */
    
    /* Fairness flags */
    bool aged;                       /* Task has been aged */
    bool starving;                  /* Task is starving */
    bool boosted;                   /* Task has been boosted */
    bool migrated;                   /* Task has been migrated */
};

/* ===================================================================== */
/* Fairness Run Queue */
/* ===================================================================== */

struct fairness_rq {
    /* Base run queue */
    struct rq_ext rq;
    
    /* Fairness tree (red-black tree for vruntime) */
    struct rb_root rb_root;         /* RB tree root */
    struct rb_node *rb_leftmost;     /* Leftmost node (min vruntime) */
    
    /* Fairness metrics */
    uint64_t min_vruntime;          /* Minimum vruntime in queue */
    uint64_t exec_clock;             /* Execution clock */
    uint64_t idle_clock;             /* Idle clock */
    
    /* Load tracking */
    unsigned long load;              /* Current load */
    unsigned long load_weight;       /* Load weight */
    unsigned long runnable_load;     /* Runnable load */
    
    /* Fairness statistics */
    unsigned long nr_running;       /* Number of running tasks */
    unsigned long nr_uninterruptible; /* Number of uninterruptible tasks */
    unsigned long nr_sleeping;       /* Number of sleeping tasks */
    
    /* Aging management */
    uint64_t last_aging_time;       /* Last aging time */
    int aging_interval;              /* Aging interval */
    
    /* Load balancing */
    int cpu;                         /* CPU ID */
    bool overloaded;                 /* CPU is overloaded */
    uint64_t last_balance_time;      /* Last load balance time */
    int balance_interval;            /* Balance interval */
    
    /* Fairness lock */
    spinlock_t lock;
};

/* ===================================================================== */
/* Fairness Scheduler Operations */
/* ===================================================================== */

struct fairness_ops {
    /* Task operations */
    void (*enqueue_task)(struct task_struct_ext *task, struct fairness_rq *rq, int flags);
    void (*dequeue_task)(struct task_struct_ext *task, struct fairness_rq *rq);
    void (*yield_task)(struct task_struct_ext *task, struct fairness_rq *rq);
    void (*check_preempt)(struct task_struct_ext *curr, struct task_struct_ext *p);
    
    /* Aging operations */
    void (*age_task)(struct task_struct_ext *task);
    void (*boost_task)(struct task_struct_ext *task);
    bool (*is_starving)(struct task_struct_ext *task);
    
    /* Load balancing */
    int (*migrate_task)(struct task_struct_ext *task, int dest_cpu);
    bool (*should_migrate)(struct task_struct_ext *task, int dest_cpu);
    
    /* Fairness calculations */
    uint64_t (*calc_vruntime)(struct task_struct_ext *task);
    int (*calc_timeslice)(struct task_struct_ext *task);
    int (*calc_weight)(int nice);
    
    /* Statistics */
    void (*update_stats)(struct task_struct_ext *task);
    void (*reset_stats)(struct task_struct_ext *task);
};

/* ===================================================================== */
/* Fairness Scheduler Interface */
/* ===================================================================== */

/**
 * fairness_init - Initialize fairness scheduler
 * @level: Fairness level (0-3)
 * 
 * Return: 0 on success, negative error on failure
 */
int fairness_init(int level);

/**
 * fairness_shutdown - Shutdown fairness scheduler
 */
void fairness_shutdown(void);

/**
 * fairness_enqueue_task - Enqueue task in fairness run queue
 * @task: Task to enqueue
 * @rq: Run queue
 * @flags: Enqueue flags
 */
void fairness_enqueue_task(struct task_struct_ext *task, struct fairness_rq *rq, int flags);

/**
 * fairness_dequeue_task - Dequeue task from fairness run queue
 * @task: Task to dequeue
 * @rq: Run queue
 */
void fairness_dequeue_task(struct task_struct_ext *task, struct fairness_rq *rq);

/**
 * fairness_pick_next_task - Pick next task to run
 * @rq: Run queue
 * 
 * Return: Next task to run or NULL
 */
struct task_struct_ext *fairness_pick_next_task(struct fairness_rq *rq);

/**
 * fairness_task_tick - Handle scheduler tick for task
 * @task: Currently running task
 * @rq: Current run queue
 */
void fairness_task_tick(struct task_struct_ext *task, struct fairness_rq *rq);

/**
 * fairness_check_preempt - Check if task should preempt current
 * @curr: Current task
 * @p: New task
 * 
 * Return: True if preemption should occur
 */
bool fairness_check_preempt(struct task_struct_ext *curr, struct task_struct_ext *p);

/**
 * fairness_yield_task - Yield CPU voluntarily
 * @task: Task yielding CPU
 * @rq: Current run queue
 */
void fairness_yield_task(struct task_struct_ext *task, struct fairness_rq *rq);

/* ===================================================================== */
/* Aging and Starvation Prevention */
/* ===================================================================== */

/**
 * fairness_age_tasks - Age all tasks in run queue
 * @rq: Run queue
 */
void fairness_age_tasks(struct fairness_rq *rq);

/**
 * fairness_boost_starving_tasks - Boost starving tasks
 * @rq: Run queue
 */
void fairness_boost_starving_tasks(struct fairness_rq *rq);

/**
 * fairness_check_starvation - Check for task starvation
 * @rq: Run queue
 * 
 * Return: Number of starving tasks
 */
int fairness_check_starvation(struct fairness_rq *rq);

/**
 * fairness_prevent_starvation - Prevent task starvation
 * @task: Task to check
 * @rq: Run queue
 */
void fairness_prevent_starvation(struct task_struct_ext *task, struct fairness_rq *rq);

/* ===================================================================== */
/* Load Balancing */
/* ===================================================================== */

/**
 * fairness_balance_load - Balance load across CPUs
 * @cpu: CPU to balance
 * @idle: True if balancing on idle
 */
void fairness_balance_load(int cpu, bool idle);

/**
 * fairness_migrate_task - Migrate task to different CPU
 * @task: Task to migrate
 * @dest_cpu: Destination CPU
 * 
 * Return: 0 on success, negative error on failure
 */
int fairness_migrate_task(struct task_struct_ext *task, int dest_cpu);

/**
 * fairness_should_migrate - Check if task should migrate
 * @task: Task to check
 * @dest_cpu: Destination CPU
 * 
 * Return: True if task should migrate
 */
bool fairness_should_migrate(struct task_struct_ext *task, int dest_cpu);

/**
 * fairness_find_busiest_cpu - Find busiest CPU
 * @this_cpu: Current CPU
 * @load: Output for busiest load
 * 
 * Return: Busiest CPU ID
 */
int fairness_find_busiest_cpu(int this_cpu, unsigned long *load);

/**
 * fairness_find_idlest_cpu - Find idlest CPU
 * @this_cpu: Current CPU
 * @load: Output for idlest load
 * 
 * Return: Idlest CPU ID
 */
int fairness_find_idlest_cpu(int this_cpu, unsigned long *load);

/* ===================================================================== */
/* Fairness Calculations */
/* ===================================================================== */

/**
 * fairness_calc_vruntime - Calculate virtual runtime
 * @task: Task to calculate for
 * 
 * Return: Virtual runtime
 */
uint64_t fairness_calc_vruntime(struct task_struct_ext *task);

/**
 * fairness_calc_timeslice - Calculate time slice
 * @task: Task to calculate for
 * 
 * Return: Time slice in milliseconds
 */
int fairness_calc_timeslice(struct task_struct_ext *task);

/**
 * fairness_calc_weight - Calculate task weight from nice value
 * @nice: Nice value
 * 
 * Return: Task weight
 */
int fairness_calc_weight(int nice);

/**
 * fairness_update_vruntime - Update virtual runtime
 * @task: Task to update
 * @delta: Time delta
 */
void fairness_update_vruntime(struct task_struct_ext *task, uint64_t delta);

/**
 * fairness_place_entity - Place task in fairness tree
 * @rq: Run queue
 * @task: Task to place
 */
void fairness_place_entity(struct fairness_rq *rq, struct task_struct_ext *task);

/* ===================================================================== */
/* Context Switch Optimization */
/* ===================================================================== */

/**
 * fairness_optimize_switch - Optimize context switch
 * @prev: Previous task
 * @next: Next task
 */
void fairness_optimize_switch(struct task_struct_ext *prev, struct task_struct_ext *next);

/**
 * fairness_cache_friendly_switch - Cache-friendly context switch
 * @prev: Previous task
 * @next: Next task
 */
void fairness_cache_friendly_switch(struct task_struct_ext *prev, struct task_struct_ext *next);

/**
 * fairness_minimize_switch_overhead - Minimize switch overhead
 * @prev: Previous task
 * @next: Next task
 */
void fairness_minimize_switch_overhead(struct task_struct_ext *prev, struct task_struct_ext *next);

/* ===================================================================== */
/* Fairness Statistics and Debugging */
/* ===================================================================== */

/**
 * fairness_get_stats - Get fairness statistics
 * @cpu: CPU ID
 * @buf: Buffer to write stats to
 * @size: Buffer size
 * 
 * Return: Number of bytes written
 */
int fairness_get_stats(int cpu, char *buf, size_t size);

/**
 * fairness_dump_runqueue - Dump run queue information
 * @cpu: CPU ID
 * @buf: Buffer to write dump to
 * @size: Buffer size
 * 
 * Return: Number of bytes written
 */
int fairness_dump_runqueue(int cpu, char *buf, size_t size);

/**
 * fairness_validate - Validate fairness scheduler state
 * @cpu: CPU ID
 * 
 * Return: 0 if valid, negative error if corruption detected
 */
int fairness_validate(int cpu);

/**
 * fairness_reset_stats - Reset fairness statistics
 * @cpu: CPU ID
 */
void fairness_reset_stats(int cpu);

/* ===================================================================== */
/* Fairness Configuration */
/* ===================================================================== */

/**
 * fairness_set_level - Set fairness level
 * @level: Fairness level (0-3)
 * 
 * Return: 0 on success, negative error on failure
 */
int fairness_set_level(int level);

/**
 * fairness_get_level - Get current fairness level
 * 
 * Return: Current fairness level
 */
int fairness_get_level(void);

/**
 * fairness_set_aging_interval - Set aging interval
 * @interval: Aging interval in milliseconds
 * 
 * Return: 0 on success, negative error on failure
 */
int fairness_set_aging_interval(int interval);

/**
 * fairness_set_starvation_threshold - Set starvation threshold
 * @threshold: Starvation threshold in milliseconds
 * 
 * Return: 0 on success, negative error on failure
 */
int fairness_set_starvation_threshold(uint64_t threshold);

/* ===================================================================== */
/* Helper Functions */
/* ===================================================================== */

/**
 * fairness_task_by_pid - Find task by PID
 * @pid: Process ID
 * 
 * Return: Task pointer or NULL if not found
 */
struct task_struct_ext *fairness_task_by_pid(pid_t pid);

/**
 * fairness_cpu_of_task - Get CPU of task
 * @task: Task to query
 * 
 * Return: CPU ID
 */
int fairness_cpu_of_task(struct task_struct_ext *task);

/**
 * fairness_rq_of_cpu - Get run queue of CPU
 * @cpu: CPU ID
 * 
 * Return: Run queue pointer
 */
struct fairness_rq *fairness_rq_of_cpu(int cpu);

/**
 * fairness_is_task_running - Check if task is running
 * @task: Task to check
 * 
 * Return: True if task is running
 */
bool fairness_is_task_running(struct task_struct_ext *task);

/**
 * fairness_is_task_on_rq - Check if task is on run queue
 * @task: Task to check
 * 
 * Return: True if task is on run queue
 */
bool fairness_is_task_on_rq(struct task_struct_ext *task);

/* ===================================================================== */
/* Fairness Constants and Macros */
/* ===================================================================== */

/* Nice to weight conversion */
#define NICE_TO_LOAD(nice)          (1024 / nice_to_weight[nice + 20])
#define LOAD_TO_NICE(load)           (weight_to_nice[1024 / load])

/* Default nice to weight table */
static const int nice_to_weight[40] = {
   /* -20 */     88761,     71755,     56483,     46273,     36291,
   /* -15 */     29154,     23254,     18705,     14952,     11916,
   /* -10 */      9548,      7620,      6100,      4904,      3951,
   /*  -5  */      3181,      2567,      2087,      1699,      1385,
   /*   0  */      1120,       910,       740,       602,       492,
   /*   5  */       402,       327,       265,       217,       178,
   /*  10  */       147,       120,        98,        81,        66,
   /*  15  */        54,        45,        37,        31,        26,
   /*  19  */        22
};

/* Weight to nice conversion (inverse of above) */
static const int weight_to_nice[128] = {
    /* 0-31 */  0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,  7,
    /* 32-63 */  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15,
    /* 64-95 */ 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23,
    /* 96-127 */24, 24, 25, 25, 26, 26, 27, 27, 28, 28, 29, 29, 30, 30, 31, 31
};

/* Fairness macros */
#define FAIRNESS_DELTA(vruntime, weight) \
    ((vruntime) * (1024 / (weight)))

#define FAIRNESS_TIMESLICE(weight) \
    (FAIRNESS_TARGET_LATENCY * (weight) / 1024)

#define FAIRNESS_MAX_SLEEP(ms) \
    ((ms) * 1000)  /* Convert to microseconds */

/* Debug macros */
#ifdef CONFIG_SCHED_FAIRNESS_DEBUG
#define fairness_debug(fmt, ...) \
    printk(KERN_DEBUG "FAIRNESS: " fmt, ##__VA_ARGS__)
#else
#define fairness_debug(fmt, ...) do {} while(0)
#endif

#endif /* _SCHED_FAIRNESS_H */
