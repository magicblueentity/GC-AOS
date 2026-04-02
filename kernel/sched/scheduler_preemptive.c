/*
 * GC-AOS - Preemptive Priority Scheduler Implementation
 * 
 * Priority-based scheduling with time slicing and preemption.
 */

#include "sched/scheduler_preemptive.h"
#include "printk.h"
#include <string.h>

/* Global run queue */
runqueue_t g_runqueue;

/* Preemption counter (per-CPU) */
static __thread int preempt_cnt = 0;

/* ===================================================================== */
/* Bitmap Operations */
/* ===================================================================== */

static inline void set_bit(int nr, uint64_t *bitmap)
{
    bitmap[nr >> 6] |= (1ULL << (nr & 63));
}

static inline void clear_bit(int nr, uint64_t *bitmap)
{
    bitmap[nr >> 6] &= ~(1ULL << (nr & 63));
}

static inline int test_bit(int nr, const uint64_t *bitmap)
{
    return (bitmap[nr >> 6] >> (nr & 63)) & 1;
}

/* Find first set bit (highest priority = lowest index) */
static int find_first_bit(const uint64_t *bitmap, int size)
{
    for (int i = 0; i < (size + 63) / 64; i++) {
        if (bitmap[i]) {
            return __builtin_ctzll(bitmap[i]) + (i * 64);
        }
    }
    return -1;
}

/* ===================================================================== */
/* Priority Array Operations */
/* ===================================================================== */

static void prio_array_init(prio_array_t *array)
{
    memset(array->bitmap, 0, sizeof(array->bitmap));
    for (int i = 0; i < SCHED_PRIO_LEVELS; i++) {
        INIT_LIST_HEAD(&array->queues[i]);
    }
    array->count = 0;
}

static void prio_array_add(prio_array_t *array, process_t *proc)
{
    int prio = proc->priority;
    if (prio >= SCHED_PRIO_LEVELS) prio = SCHED_PRIO_LEVELS - 1;
    if (prio < 0) prio = 0;
    
    list_add_tail(&proc->run_list, &array->queues[prio]);
    set_bit(prio, array->bitmap);
    array->count++;
}

static void prio_array_remove(prio_array_t *array, process_t *proc)
{
    int prio = proc->priority;
    if (prio >= SCHED_PRIO_LEVELS) prio = SCHED_PRIO_LEVELS - 1;
    if (prio < 0) prio = 0;
    
    list_del(&proc->run_list);
    INIT_LIST_HEAD(&proc->run_list);
    
    /* Clear bit if queue is empty */
    if (list_empty(&array->queues[prio])) {
        clear_bit(prio, array->bitmap);
    }
    
    array->count--;
}

static process_t *prio_array_pick_next(prio_array_t *array)
{
    int prio = find_first_bit(array->bitmap, SCHED_PRIO_LEVELS);
    if (prio < 0) {
        return NULL; /* No runnable processes */
    }
    
    /* Take first process from this priority level */
    process_t *proc = list_first_entry(&array->queues[prio], process_t, run_list);
    prio_array_remove(array, proc);
    
    return proc;
}

/* ===================================================================== */
/* Scheduler Initialization */
/* ===================================================================== */

void scheduler_init(void)
{
    printk(KERN_INFO "SCHED: Initializing preemptive scheduler\n");
    
    memset(&g_runqueue, 0, sizeof(g_runqueue));
    spin_lock_init(&g_runqueue.lock);
    
    /* Initialize priority arrays */
    prio_array_init(&g_runqueue.array1);
    prio_array_init(&g_runqueue.array2);
    
    g_runqueue.active = &g_runqueue.array1;
    g_runqueue.expired = &g_runqueue.array2;
    
    /* Set up idle process pointer (will be set later) */
    g_runqueue.idle = NULL;
    g_runqueue.current = NULL;
    
    g_runqueue.need_resched = 0;
    
    printk(KERN_INFO "SCHED: Scheduler initialized (HZ=%d, levels=%d)\n",
           SCHED_HZ, SCHED_PRIO_LEVELS);
}

/* ===================================================================== */
/* Core Scheduling Functions */
/* ===================================================================== */

int scheduler_add_process(process_t *proc)
{
    if (!proc) {
        return -1;
    }
    
    spin_lock(&g_runqueue.lock);
    
    if (proc->state != PROCESS_STATE_READY && 
        proc->state != PROCESS_STATE_NEW) {
        spin_unlock(&g_runqueue.lock);
        return -2; /* Invalid state */
    }
    
    proc->state = PROCESS_STATE_READY;
    prio_array_add(g_runqueue.active, proc);
    g_runqueue.nr_running++;
    
    /* Check if new process should preempt current */
    if (g_runqueue.current && proc->priority < g_runqueue.current->priority) {
        g_runqueue.need_resched = 1;
    }
    
    spin_unlock(&g_runqueue.lock);
    
    return 0;
}

int scheduler_remove_process(process_t *proc)
{
    if (!proc) {
        return -1;
    }
    
    spin_lock(&g_runqueue.lock);
    
    /* Remove from whichever array it's in */
    if (!list_empty(&proc->run_list)) {
        prio_array_remove(g_runqueue.active, proc);
        if (g_runqueue.nr_running > 0) {
            g_runqueue.nr_running--;
        }
    }
    
    spin_unlock(&g_runqueue.lock);
    
    return 0;
}

void scheduler_reschedule(void)
{
    spin_lock(&g_runqueue.lock);
    
    process_t *prev = g_runqueue.current;
    process_t *next = NULL;
    
    /* Pick next process from active array */
    next = prio_array_pick_next(g_runqueue.active);
    
    /* If active array is empty, swap arrays */
    if (!next && g_runqueue.expired->count > 0) {
        prio_array_t *temp = g_runqueue.active;
        g_runqueue.active = g_runqueue.expired;
        g_runqueue.expired = temp;
        
        next = prio_array_pick_next(g_runqueue.active);
    }
    
    /* Fall back to idle process */
    if (!next) {
        next = g_runqueue.idle;
    }
    
    /* Nothing to do if same process */
    if (next == prev) {
        g_runqueue.need_resched = 0;
        spin_unlock(&g_runqueue.lock);
        return;
    }
    
    /* Handle previous process */
    if (prev) {
        if (prev->state == PROCESS_STATE_RUNNING) {
            /* Still runnable - move to expired if timeslice exhausted */
            if (prev->time_slice <= 0) {
                prev->state = PROCESS_STATE_READY;
                prev->time_slice = scheduler_get_timeslice(prev->priority) * 1000000ULL;
                prio_array_add(g_runqueue.expired, prev);
            } else {
                /* Put back in active array */
                prev->state = PROCESS_STATE_READY;
                prio_array_add(g_runqueue.active, prev);
            }
        }
    }
    
    /* Switch to new process */
    next->state = PROCESS_STATE_RUNNING;
    g_runqueue.current = next;
    g_runqueue.nr_switches++;
    g_runqueue.need_resched = 0;
    
    /* Update statistics */
    next->exec_start = get_time_ns();
    
    spin_unlock(&g_runqueue.lock);
    
    /* Perform context switch */
    if (prev && next && prev != next) {
        process_switch_context(prev, next);
    }
}

void scheduler_tick(void)
{
    spin_lock(&g_runqueue.lock);
    
    process_t *current = g_runqueue.current;
    if (!current) {
        spin_unlock(&g_runqueue.lock);
        return;
    }
    
    g_runqueue.tick_count++;
    
    /* Update timeslice */
    current->time_slice -= SCHED_TICK_NS;
    current->total_runtime += SCHED_TICK_NS;
    
    /* Update virtual runtime for fair scheduling */
    scheduler_update_vruntime(current, SCHED_TICK_NS);
    
    /* Check for preemption */
    int need_resched = 0;
    
    /* Timeslice exhausted */
    if (current->time_slice <= 0) {
        need_resched = 1;
    }
    
    /* Check if higher priority process is waiting */
    int next_prio = find_first_bit(g_runqueue.active->bitmap, SCHED_PRIO_LEVELS);
    if (next_prio >= 0 && next_prio < current->priority) {
        need_resched = 1;
    }
    
    /* GUI/System processes get priority boost if starved */
    if (current->priority < SCHED_PRIO_NORMAL && 
        current->total_runtime > 1000000000ULL) { /* 1 second */
        /* Boost interactive processes */
        if (current->priority > 10) {
            current->priority -= 5; /* Boost by 5 levels */
            if (current->priority < 10) current->priority = 10;
        }
    }
    
    if (need_resched) {
        g_runqueue.need_resched = 1;
    }
    
    spin_unlock(&g_runqueue.lock);
    
    /* If reschedule needed, do it now (from interrupt context) */
    if (g_runqueue.need_resched) {
        scheduler_reschedule();
    }
}

void scheduler_yield(void)
{
    spin_lock(&g_runqueue.lock);
    
    process_t *current = g_runqueue.current;
    if (current && current->state == PROCESS_STATE_RUNNING) {
        /* Reset timeslice for yielded process */
        current->time_slice = scheduler_get_timeslice(current->priority) * 1000000ULL;
        g_runqueue.need_resched = 1;
    }
    
    spin_unlock(&g_runqueue.lock);
    
    scheduler_reschedule();
}

void scheduler_block(const char *reason)
{
    (void)reason; /* For debugging */
    
    spin_lock(&g_runqueue.lock);
    
    process_t *current = g_runqueue.current;
    if (current) {
        current->state = PROCESS_STATE_BLOCKED;
        g_runqueue.nr_blocked++;
        g_runqueue.need_resched = 1;
    }
    
    spin_unlock(&g_runqueue.lock);
    
    scheduler_reschedule();
}

int scheduler_wake(process_t *proc)
{
    if (!proc) {
        return -1;
    }
    
    spin_lock(&g_runqueue.lock);
    
    if (proc->state == PROCESS_STATE_READY || 
        proc->state == PROCESS_STATE_RUNNING) {
        spin_unlock(&g_runqueue.lock);
        return 1; /* Already running */
    }
    
    proc->state = PROCESS_STATE_READY;
    proc->time_slice = scheduler_get_timeslice(proc->priority) * 1000000ULL;
    prio_array_add(g_runqueue.active, proc);
    g_runqueue.nr_running++;
    g_runqueue.nr_blocked--;
    
    /* Check if we should preempt current */
    if (g_runqueue.current && proc->priority < g_runqueue.current->priority) {
        g_runqueue.need_resched = 1;
    }
    
    spin_unlock(&g_runqueue.lock);
    
    return 0;
}

/* ===================================================================== */
/* Priority and Policy Management */
/* ===================================================================== */

int scheduler_set_policy(process_t *proc, sched_policy_t policy, int priority)
{
    if (!proc || priority < 0 || priority >= SCHED_PRIO_LEVELS) {
        return -1;
    }
    
    /* Validate policy/priority combination */
    switch (policy) {
        case SCHED_POLICY_FIFO:
        case SCHED_POLICY_RR:
            /* Real-time must be in RT range */
            if (priority >= SCHED_PRIO_NORMAL) {
                return -2;
            }
            break;
        case SCHED_POLICY_NORMAL:
        case SCHED_POLICY_BATCH:
            /* Normal must be in normal range */
            if (priority < SCHED_PRIO_NORMAL) {
                return -2;
            }
            break;
        case SCHED_POLICY_IDLE:
            priority = SCHED_PRIO_IDLE;
            break;
    }
    
    spin_lock(&g_runqueue.lock);
    
    /* Remove from current array if present */
    int was_running = (proc->state == PROCESS_STATE_RUNNING);
    int in_active = 0;
    
    if (!list_empty(&proc->run_list)) {
        prio_array_remove(g_runqueue.active, proc);
        in_active = 1;
    }
    
    /* Update priority */
    proc->base_priority = priority;
    proc->priority = priority;
    proc->time_slice = scheduler_get_timeslice(priority) * 1000000ULL;
    
    /* Re-add to array */
    if (in_active) {
        prio_array_add(g_runqueue.active, proc);
    }
    
    /* If we lowered priority of running process, may need to reschedule */
    if (was_running && g_runqueue.current == proc) {
        int next_prio = find_first_bit(g_runqueue.active->bitmap, SCHED_PRIO_LEVELS);
        if (next_prio >= 0 && next_prio < priority) {
            g_runqueue.need_resched = 1;
        }
    }
    
    spin_unlock(&g_runqueue.lock);
    
    return 0;
}

int scheduler_boost_priority(process_t *proc, int boost_amount)
{
    if (!proc) {
        return -1;
    }
    
    spin_lock(&g_runqueue.lock);
    
    int new_prio = proc->base_priority - boost_amount;
    if (new_prio < 0) new_prio = 0;
    
    proc->priority = new_prio;
    
    spin_unlock(&g_runqueue.lock);
    
    return 0;
}

int scheduler_unboost_priority(process_t *proc)
{
    if (!proc) {
        return -1;
    }
    
    spin_lock(&g_runqueue.lock);
    proc->priority = proc->base_priority;
    spin_unlock(&g_runqueue.lock);
    
    return 0;
}

int scheduler_get_timeslice(int priority)
{
    if (priority < SCHED_PRIO_NORMAL) {
        /* Real-time: larger timeslices */
        return SCHED_TIMESLICE_MAX;
    } else if (priority >= SCHED_PRIO_IDLE) {
        /* Idle: minimum */
        return SCHED_TIMESLICE_MIN;
    } else {
        /* Normal: proportional to priority */
        int nice = priority - 120; /* -20 to 19 range */
        return SCHED_TIMESLICE_DEFAULT + (20 - nice);
    }
}

/* ===================================================================== */
/* Fair Scheduling */
/* ===================================================================== */

uint64_t scheduler_calc_vruntime(process_t *proc)
{
    /* Higher priority = lower weight = faster vruntime growth */
    int weight = SCHED_PRIO_LEVELS - proc->priority;
    if (weight < 1) weight = 1;
    
    return proc->total_runtime / weight;
}

void scheduler_update_vruntime(process_t *proc, uint64_t delta_ns)
{
    /* Weight vruntime by priority */
    int weight = SCHED_PRIO_LEVELS - proc->priority;
    if (weight < 1) weight = 1;
    
    proc->vruntime += (delta_ns * 1024) / weight;
}

/* ===================================================================== */
/* Preemption Control */
/* ===================================================================== */

void preempt_enable(void)
{
    preempt_cnt--;
    if (preempt_cnt <= 0 && g_runqueue.need_resched) {
        scheduler_reschedule();
    }
}

void preempt_disable(void)
{
    preempt_cnt++;
}

int preempt_count(void)
{
    return preempt_cnt;
}

/* ===================================================================== */
/* Statistics */
/* ===================================================================== */

void scheduler_get_stats(uint64_t *switches, int *load)
{
    spin_lock(&g_runqueue.lock);
    if (switches) {
        *switches = g_runqueue.nr_switches;
    }
    if (load) {
        *load = (int)g_runqueue.load;
    }
    spin_unlock(&g_runqueue.lock);
}

void scheduler_dump_state(void)
{
    spin_lock(&g_runqueue.lock);
    
    printk(KERN_INFO "=== Scheduler State ===\n");
    printk(KERN_INFO "Running: %llu, Switches: %llu\n", 
           g_runqueue.nr_running, g_runqueue.nr_switches);
    printk(KERN_INFO "Active queue: %d processes\n", g_runqueue.active->count);
    printk(KERN_INFO "Expired queue: %d processes\n", g_runqueue.expired->count);
    printk(KERN_INFO "Current: %s (PID %d, Prio %d)\n",
           g_runqueue.current ? g_runqueue.current->name : "none",
           g_runqueue.current ? g_runqueue.current->pid : -1,
           g_runqueue.current ? g_runqueue.current->priority : -1);
    
    /* Show priority distribution */
    for (int i = 0; i < 10 && i < SCHED_PRIO_LEVELS; i++) {
        if (!list_empty(&g_runqueue.active->queues[i])) {
            int count = 0;
            process_t *p;
            list_for_each_entry(p, &g_runqueue.active->queues[i], run_list) {
                count++;
            }
            printk(KERN_INFO "  Priority %d: %d processes\n", i, count);
        }
    }
    
    spin_unlock(&g_runqueue.lock);
}
