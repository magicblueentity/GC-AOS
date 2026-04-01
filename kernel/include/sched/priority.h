/*
 * GC-AOS Kernel - Enhanced Priority Scheduler
 * 
 * Implements a priority-based preemptive scheduler with SMP support
 * and load balancing across multiple CPUs.
 */

#ifndef _SCHED_PRIORITY_H
#define _SCHED_PRIORITY_H

#include "sched/sched.h"
#include "types.h"

/* ===================================================================== */
/* Scheduler Configuration */
/* ===================================================================== */

#define PRIO_LEVELS             140     /* Number of priority levels (0-139) */
#define MAX_PRIO                139     /* Maximum priority value */
#define MIN_PRIO                0       /* Minimum priority value */
#define NICE_TO_PRIO(nice)      (20 + (nice))  /* Convert nice to priority */
#define PRIO_TO_NICE(prio)      ((prio) - 20)  /* Convert priority to nice */

/* Priority ranges */
#define PRIO_RT_MIN             0       /* Minimum real-time priority */
#define PRIO_RT_MAX             99      /* Maximum real-time priority */
#define PRIO_NORMAL_MIN         100     /* Minimum normal priority */
#define PRIO_NORMAL_MAX         139     /* Maximum normal priority */

/* Real-time policy priorities */
#define MAX_RT_PRIO             100     /* Maximum real-time priority levels */
#define MAX_USER_RT_PRIO        99      /* Maximum user-settable RT priority */

/* Time slices */
#define DEFAULT_TIMESLICE       10      /* Default timeslice in ms */
#define MIN_TIMESLICE           1       /* Minimum timeslice in ms */
#define MAX_TIMESLICE           100     /* Maximum timeslice in ms */

/* Load balancing */
#define LOAD_BALANCE_INTERVAL   100     /* Load balance interval in ms */
#define MIGRATION_COST          50      /* Task migration cost */

/* ===================================================================== */
/* Scheduling Policies */
/* ===================================================================== */

typedef enum {
    SCHED_NORMAL,       /* Normal (SCHED_OTHER) - Completely fair scheduler */
    SCHED_FIFO,        /* Real-time FIFO - First-in, first-out */
    SCHED_RR,          /* Real-time Round Robin */
    SCHED_BATCH,       /* Batch processing - lower priority */
    SCHED_IDLE,        /* Idle tasks - lowest priority */
    SCHED_DEADLINE,    /* Deadline scheduler */
} sched_policy_t;

/* ===================================================================== */
/* Enhanced Task Structure */
/* ===================================================================== */

struct task_struct_ext {
    /* Base task structure */
    struct task_struct task;
    
    /* Scheduling policy */
    sched_policy_t policy;
    int prio;             /* Static priority */
    int normal_prio;      /* Normal (non-RT) priority */
    int rt_priority;     /* Real-time priority */
    int static_prio;      /* Static priority */
    int normal_prio;      /* Normal priority */
    
    /* Time management */
    uint64_t exec_start;  /* Task start time */
    uint64_t sum_exec_runtime;  /* Total runtime */
    uint64_t prev_sum_exec_runtime;  /* Previous runtime */
    uint64_t vruntime;   /* Virtual runtime for CFS */
    uint64_t min_vruntime;  /* Minimum vruntime in runqueue */
    
    /* Timeslice */
    int time_slice;       /* Remaining timeslice */
    int time_slice_count; /* Timeslice counter */
    int time_slice_remainder; /* Timeslice remainder */
    
    /* Load balancing */
    int cpu;              /* Current CPU */
    int migration_pending; /* Migration pending flag */
    uint64_t last_ran;    /* Last time task ran */
    
    /* Wait queue */
    struct list_head wait_list;  /* Wait queue list */
    
    /* Deadline scheduling */
    uint64_t deadline;    /* Absolute deadline */
    uint64_t period;      /* Period */
    uint64_t runtime;     /* Runtime per period */
    
    /* CPU affinity */
    cpumask_t cpus_allowed;  /* CPUs this task can run on */
    cpumask_t cpus_ptr;     /* Pointer to CPU mask */
    
    /* Scheduler statistics */
    unsigned long sched_info;  /* Scheduler information */
    unsigned long pcount;      /* Preemption count */
    unsigned long gtime;       /* Guest time */
    unsigned long stime;       /* System time */
    unsigned long utime;       /* User time */
};

/* ===================================================================== */
/* Per-CPU Run Queue */
/* ===================================================================== */

struct rq_ext {
    /* Base run queue */
    struct rq rq;
    
    /* Priority arrays */
    struct list_head active[PRIO_LEVELS];     /* Active tasks */
    struct list_head expired[PRIO_LEVELS];    /* Expired tasks */
    DECLARE_BITMAP(active_bitmap, PRIO_LEVELS); /* Active priority bitmap */
    DECLARE_BITMAP(expired_bitmap, PRIO_LEVELS); /* Expired priority bitmap */
    
    /* Current priority arrays */
    struct list_head *active_array;
    struct list_head *expired_array;
    DECLARE_BITMAP(active_bitmap_ptr, PRIO_LEVELS);
    DECLARE_BITMAP(expired_bitmap_ptr, PRIO_LEVELS);
    
    /* Load tracking */
    unsigned long nr_running;          /* Number of running tasks */
    unsigned long nr_uninterruptible;  /* Number of uninterruptible tasks */
    unsigned long load;                /* CPU load */
    unsigned long load_weight;         /* Load weight */
    
    /* Load balancing */
    uint64_t last_load_balance;        /* Last load balance time */
    int nr_migratory;                  /* Number of migratory tasks */
    int nr_running_migratory;          /* Number of running migratory tasks */
    
    /* CPU idle tracking */
    int idle_stamp;                    /* Idle timestamp */
    int idle_stamp_window;             /* Idle window */
    int idle_balance;                  /* Idle balance flag */
    
    /* Scheduler statistics */
    unsigned long sched_switches;      /* Context switches */
    unsigned long sched_goidle;        /* Times CPU went idle */
    unsigned long sched_cpu_load;      /* CPU load */
    unsigned long sched_load_avg;      /* Average load */
    
    /* Lock for run queue */
    spinlock_t lock;
    
    /* CPU information */
    int cpu;                           /* CPU ID */
    bool online;                       /* CPU online status */
};

/* ===================================================================== */
/* Load Balancing Structure */
/* ===================================================================== */

struct lb_env {
    struct rq_ext *src_rq;            /* Source run queue */
    struct rq_ext *dst_rq;            /* Destination run queue */
    struct task_struct_ext *task;      /* Task to migrate */
    
    int src_cpu;                       /* Source CPU */
    int dst_cpu;                       /* Destination CPU */
    
    int busiest_cpu;                   /* Busiest CPU */
    int this_cpu;                      /* This CPU */
    
    unsigned long busiest_load;        /* Busiest CPU load */
    unsigned long this_load;           /* This CPU load */
    
    unsigned long imbalance;           /* Load imbalance */
    unsigned long moved;               /* Number of tasks moved */
    
    bool idle;                         /* Load balancing on idle */
    bool forkexec;                     /* Load balancing on fork/exec */
    bool newidle_cpu;                  /* New idle CPU */
};

/* ===================================================================== */
/* Scheduler Function Declarations */
/* ===================================================================== */

/**
 * sched_priority_init - Initialize priority scheduler
 * 
 * Return: 0 on success, negative error on failure
 */
int sched_priority_init(void);

/**
 * sched_priority_add_task - Add task to priority run queue
 * @task: Task to add
 * @rq: Run queue to add to
 * @wakeup: True if this is a wakeup
 */
void sched_priority_add_task(struct task_struct_ext *task, struct rq_ext *rq, bool wakeup);

/**
 * sched_priority_del_task - Remove task from priority run queue
 * @task: Task to remove
 * @rq: Run queue to remove from
 */
void sched_priority_del_task(struct task_struct_ext *task, struct rq_ext *rq);

/**
 * sched_priority_pick_next_task - Pick next task to run
 * @rq: Run queue to pick from
 * 
 * Return: Next task to run or NULL
 */
struct task_struct_ext *sched_priority_pick_next_task(struct rq_ext *rq);

/**
 * sched_priority_tick - Scheduler tick handler
 * @rq: Current run queue
 */
void sched_priority_tick(struct rq_ext *rq);

/**
 * sched_priority_task_tick - Task tick handler
 * @task: Task that is running
 * @rq: Current run queue
 */
void sched_priority_task_tick(struct task_struct_ext *task, struct rq_ext *rq);

/**
 * sched_priority_enqueue_task - Enqueue task on run queue
 * @task: Task to enqueue
 * @rq: Run queue
 * @flags: Enqueue flags
 */
void sched_priority_enqueue_task(struct task_struct_ext *task, struct rq_ext *rq, int flags);

/**
 * sched_priority_dequeue_task - Dequeue task from run queue
 * @task: Task to dequeue
 * @rq: Run queue
 * @flags: Dequeue flags
 */
void sched_priority_dequeue_task(struct task_struct_ext *task, struct rq_ext *rq, int flags);

/**
 * sched_priority_yield_task - Yield CPU voluntarily
 * @task: Task yielding CPU
 * @rq: Current run queue
 */
void sched_priority_yield_task(struct task_struct_ext *task, struct rq_ext *rq);

/**
 * sched_priority_check_preempt - Check if task should preempt current
 * @curr: Current task
 * @p: New task
 * 
 * Return: True if preemption should occur
 */
bool sched_priority_check_preempt(struct task_struct_ext *curr, struct task_struct_ext *p);

/**
 * sched_priority_set_task_policy - Set task scheduling policy
 * @task: Task to modify
 * @policy: New scheduling policy
 * @prio: New priority
 * 
 * Return: 0 on success, negative error on failure
 */
int sched_priority_set_task_policy(struct task_struct_ext *task, sched_policy_t policy, int prio);

/**
 * sched_priority_get_task_policy - Get task scheduling policy
 * @task: Task to query
 * @policy: Output for policy
 * @prio: Output for priority
 * 
 * Return: 0 on success, negative error on failure
 */
int sched_priority_get_task_policy(struct task_struct_ext *task, sched_policy_t *policy, int *prio);

/* ===================================================================== */
/* Load Balancing Functions */
/* ===================================================================== */

/**
 * sched_balance_load - Load balance across CPUs
 * @cpu: CPU to balance
 * @idle: True if balancing on idle
 */
void sched_balance_load(int cpu, bool idle);

/**
 * sched_move_task - Move task to different CPU
 * @task: Task to move
 * @dest_cpu: Destination CPU
 * 
 * Return: 0 on success, negative error on failure
 */
int sched_move_task(struct task_struct_ext *task, int dest_cpu);

/**
 * sched_can_migrate_task - Check if task can be migrated
 * @task: Task to check
 * @rq: Source run queue
 * @dst_cpu: Destination CPU
 * 
 * Return: True if task can be migrated
 */
bool sched_can_migrate_task(struct task_struct_ext *task, struct rq_ext *rq, int dst_cpu);

/**
 * sched_migrate_task - Migrate task to different CPU
 * @task: Task to migrate
 * @dest_cpu: Destination CPU
 * 
 * Return: 0 on success, negative error on failure
 */
int sched_migrate_task(struct task_struct_ext *task, int dest_cpu);

/* ===================================================================== */
/* SMP Support Functions */
/* ===================================================================== */

/**
 * sched_smp_init - Initialize SMP scheduler support
 * 
 * Return: 0 on success, negative error on failure
 */
int sched_smp_init(void);

/**
 * sched_cpu_online - Mark CPU as online
 * @cpu: CPU ID
 */
void sched_cpu_online(int cpu);

/**
 * sched_cpu_offline - Mark CPU as offline
 * @cpu: CPU ID
 */
void sched_cpu_offline(int cpu);

/**
 * sched_cpu_idle - Handle CPU idle state
 * @cpu: CPU ID
 */
void sched_cpu_idle(int cpu);

/**
 * sched_ipi_handler - Handle inter-processor interrupt
 * @cpu: CPU ID
 */
void sched_ipi_handler(int cpu);

/* ===================================================================== */
/* Real-time Scheduling Functions */
/* ===================================================================== */

/**
 * sched_rt_enqueue - Enqueue real-time task
 * @task: Real-time task
 * @rq: Run queue
 */
void sched_rt_enqueue(struct task_struct_ext *task, struct rq_ext *rq);

/**
 * sched_rt_dequeue - Dequeue real-time task
 * @task: Real-time task
 * @rq: Run queue
 */
void sched_rt_dequeue(struct task_struct_ext *task, struct rq_ext *rq);

/**
 * sched_rt_pick_next - Pick next real-time task
 * @rq: Run queue
 * 
 * Return: Next real-time task or NULL
 */
struct task_struct_ext *sched_rt_pick_next(struct rq_ext *rq);

/* ===================================================================== */
/* Helper Functions */
/* ===================================================================== */

/**
 * sched_prio_to_timeslice - Convert priority to timeslice
 * @prio: Priority value
 * 
 * Return: Timeslice in milliseconds
 */
static inline int sched_prio_to_timeslice(int prio)
{
    if (prio < PRIO_NORMAL_MIN) {
        /* Real-time tasks get larger timeslices */
        return MAX_TIMESLICE;
    }
    
    /* Normal tasks: higher priority = larger timeslice */
    int nice = PRIO_TO_NICE(prio);
    if (nice <= -20) {
        return MAX_TIMESLICE;
    } else if (nice >= 19) {
        return MIN_TIMESLICE;
    } else {
        return DEFAULT_TIMESLICE + (20 - nice) * 2;
    }
}

/**
 * sched_timeslice_to_prio - Convert timeslice to priority
 * @timeslice: Timeslice in milliseconds
 * 
 * Return: Priority value
 */
static inline int sched_timeslice_to_prio(int timeslice)
{
    if (timeslice >= MAX_TIMESLICE) {
        return PRIO_NORMAL_MIN;
    } else if (timeslice <= MIN_TIMESLICE) {
        return PRIO_NORMAL_MAX;
    } else {
        return NICE_TO_PRIO((DEFAULT_TIMESLICE - timeslice) / 2);
    }
}

/**
 * sched_task_is_rt - Check if task is real-time
 * @task: Task to check
 * 
 * Return: True if task is real-time
 */
static inline bool sched_task_is_rt(struct task_struct_ext *task)
{
    return task->policy == SCHED_FIFO || task->policy == SCHED_RR;
}

/**
 * sched_task_is_idle - Check if task is idle
 * @task: Task to check
 * 
 * Return: True if task is idle
 */
static inline bool sched_task_is_idle(struct task_struct_ext *task)
{
    return task->policy == SCHED_IDLE;
}

/**
 * sched_task_is_batch - Check if task is batch
 * @task: Task to check
 * 
 * Return: True if task is batch
 */
static inline bool sched_task_is_batch(struct task_struct_ext *task)
{
    return task->policy == SCHED_BATCH;
}

/* ===================================================================== */
/* CPU Mask Operations */
/* ===================================================================== */

#define CPU_BITS_NONE   0UL
#define CPU_BITS_ALL    (~0UL)

typedef struct {
    unsigned long bits[1];
} cpumask_t;

#define cpumask_set_cpu(cpu, mask)    ((mask)->bits[0] |= (1UL << (cpu)))
#define cpumask_clear_cpu(cpu, mask) ((mask)->bits[0] &= ~(1UL << (cpu)))
#define cpumask_test_cpu(cpu, mask)  (!!((mask)->bits[0] & (1UL << (cpu))))
#define cpumask_empty(mask)          (!((mask)->bits[0]))
#define cpumask_full(mask)           (!((~(mask)->bits[0])))
#define cpumask_weight(mask)         (__builtin_popcountl((mask)->bits[0]))

#define for_each_cpu(cpu, mask) \
    for ((cpu) = 0; (cpu) < NR_CPUS; (cpu)++) \
        if (cpumask_test_cpu((cpu), (mask)))

#define for_each_online_cpu(cpu) \
    for_each_cpu((cpu), &cpu_online_mask)

/* Global CPU masks */
extern cpumask_t cpu_online_mask;
extern cpumask_t cpu_present_mask;
extern cpumask_t cpu_possible_mask;

#endif /* _SCHED_PRIORITY_H */
