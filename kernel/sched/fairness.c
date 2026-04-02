/*
 * GC-AOS Kernel - Enhanced Fairness Scheduler Implementation
 * 
 * Implements starvation prevention, aging, and fair scheduling
 * with dynamic load balancing and optimized context switching.
 */

#include "sched/fairness.h"
#include "sched/priority.h"
#include "kernel/list.h"
#include "kernel/atomic.h"
#include "kernel/spinlock.h"
#include "kernel/rbtree.h"
#include "kernel/printk.h"
#include "mm/slab.h"
#include "sched/sched.h"

/* ===================================================================== */
/* Global Fairness Context */
/* ===================================================================== */

static struct fairness_context {
    /* Fairness configuration */
    int fairness_level;
    int aging_interval;
    uint64_t starvation_threshold;
    bool initialized;
    
    /* Per-CPU run queues */
    struct fairness_rq *runqueues;
    int nr_cpus;
    
    /* Fairness operations */
    struct fairness_ops ops;
    
    /* Statistics */
    atomic_t total_tasks;
    atomic_t aging_operations;
    atomic_t starvation_prevented;
    atomic_t migrations;
    atomic_t context_switches;
} fairness_ctx;

/* ===================================================================== */
/* Red-Black Tree Operations */
/* ===================================================================== */

static void rb_insert_color(struct rb_node *node, struct rb_root *root)
{
    struct rb_node *parent, *gparent;
    
    while ((parent = rb_parent(node)) && rb_is_red(parent)) {
        gparent = rb_parent(parent);
        
        if (parent == gparent->rb_left) {
            {
                struct rb_node *uncle = gparent->rb_right;
                if (uncle && rb_is_red(uncle)) {
                    rb_set_black(uncle);
                    rb_set_black(parent);
                    rb_set_red(gparent);
                    node = gparent;
                    continue;
                }
            }
            
            if (parent->rb_right == node) {
                struct rb_node *tmp;
                parent->rb_right = tmp = node->rb_left;
                node->rb_left = parent;
                if (tmp)
                    rb_set_parent(tmp, parent);
                rb_set_parent(node, gparent);
                node = parent;
                parent = tmp;
            }
            
            parent->rb_left = node->rb_right;
            node->rb_right = parent;
            if (parent->rb_right)
                rb_set_parent(parent->rb_right, parent);
            rb_set_parent(parent, node);
            parent = rb_parent(node);
            
            if (parent) {
                if (parent->rb_left == gparent)
                    parent->rb_left = node;
                else
                    parent->rb_right = node;
            } else {
                root->rb_node = node;
            }
            
            rb_set_red(gparent);
            node = parent;
        } else {
            {
                struct rb_node *uncle = gparent->rb_left;
                if (uncle && rb_is_red(uncle)) {
                    rb_set_black(uncle);
                    rb_set_black(parent);
                    rb_set_red(gparent);
                    node = gparent;
                    continue;
                }
            }
            
            if (parent->rb_left == node) {
                struct rb_node *tmp;
                parent->rb_left = tmp = node->rb_right;
                node->rb_right = parent;
                if (tmp)
                    rb_set_parent(tmp, parent);
                rb_set_parent(node, gparent);
                node = parent;
                parent = tmp;
            }
            
            parent->rb_right = node->rb_left;
            node->rb_left = parent;
            if (parent->rb_left)
                rb_set_parent(parent->rb_left, parent);
            rb_set_parent(parent, node);
            parent = rb_parent(node);
            
            if (parent) {
                if (parent->rb_right == gparent)
                    parent->rb_right = node;
                else
                    parent->rb_left = node;
            } else {
                root->rb_node = node;
            }
            
            rb_set_red(gparent);
            node = parent;
        }
    }
    
    rb_set_black(root->rb_node);
}

static void rb_erase_color(struct rb_node *node, struct rb_node *parent,
                          struct rb_root *root)
{
    struct rb_node *other;
    
    while ((!node || rb_is_black(node)) && node != root->rb_node) {
        if (parent->rb_left == node) {
            other = parent->rb_right;
            if (rb_is_red(other)) {
                rb_set_black(other);
                rb_set_red(parent);
                rb_left_rotate(parent, root);
                other = parent->rb_right;
            }
            if ((!other->rb_left || rb_is_black(other->rb_left)) &&
                (!other->rb_right || rb_is_black(other->rb_right))) {
                rb_set_red(other);
                node = parent;
                parent = rb_parent(node);
            } else {
                if (!other->rb_right || rb_is_black(other->rb_right)) {
                    rb_set_black(other->rb_left);
                    rb_set_red(other);
                    rb_right_rotate(other, root);
                    other = parent->rb_right;
                }
                rb_set_color(other, rb_color(parent));
                rb_set_black(parent);
                rb_set_black(other->rb_right);
                rb_left_rotate(parent, root);
                node = root->rb_node;
                break;
            }
        } else {
            other = parent->rb_left;
            if (rb_is_red(other)) {
                rb_set_black(other);
                rb_set_red(parent);
                rb_right_rotate(parent, root);
                other = parent->rb_left;
            }
            if ((!other->rb_left || rb_is_black(other->rb_left)) &&
                (!other->rb_right || rb_is_black(other->rb_right))) {
                rb_set_red(other);
                node = parent;
                parent = rb_parent(node);
            } else {
                if (!other->rb_left || rb_is_black(other->rb_left)) {
                    rb_set_black(other->rb_right);
                    rb_set_red(other);
                    rb_left_rotate(other, root);
                    other = parent->rb_left;
                }
                rb_set_color(other, rb_color(parent));
                rb_set_black(parent);
                rb_set_black(other->rb_left);
                rb_right_rotate(parent, root);
                node = root->rb_node;
                break;
            }
        }
    }
    if (node)
        rb_set_black(node);
}

static void rb_erase(struct rb_node *node, struct rb_root *root)
{
    struct rb_node *child, *parent;
    int color;
    
    if (!node->rb_left)
        child = node->rb_right;
    else if (!node->rb_right)
        child = node->rb_left;
    else {
        struct rb_node *old = node, *left;
        
        node = node->rb_right;
        while ((left = node->rb_left) != NULL)
            node = left;
        
        if (rb_parent(old)) {
            if (rb_parent(old)->rb_left == old)
                rb_parent(old)->rb_left = node;
            else
                rb_parent(old)->rb_right = node;
        } else
            root->rb_node = node;
        
        child = node->rb_right;
        parent = rb_parent(node);
        color = rb_color(node);
        
        if (parent == old) {
            parent = node;
        } else {
            if (child)
                rb_set_parent(child, parent);
            parent->rb_left = child;
            node->rb_right = old->rb_right;
            rb_set_parent(old->rb_right, node);
        }
        
        node->rb_left = old->rb_left;
        rb_set_parent(old->rb_left, node);
        
        if (rb_parent(old)) {
            if (rb_parent(old)->rb_left == old)
                rb_parent(old)->rb_left = node;
            else
                rb_parent(old)->rb_right = node;
        } else
            root->rb_node = node;
        
        rb_set_parent(node, rb_parent(old));
        rb_set_color(node, rb_color(old));
        goto color;
    }
    
    parent = rb_parent(node);
    color = rb_color(node);
    
    if (child)
        rb_set_parent(child, parent);
    
    if (parent) {
        if (parent->rb_left == node)
            parent->rb_left = child;
        else
            parent->rb_right = child;
    } else
        root->rb_node = child;
    
color:
    if (color == RB_BLACK)
        rb_erase_color(child, parent, root);
}

/* ===================================================================== */
/* Fairness Calculations */
/* ===================================================================== */

uint64_t fairness_calc_vruntime(struct task_struct_ext *task)
{
    if (!task) {
        return 0;
    }
    
    uint64_t delta = task->fairness.sum_exec_runtime - task->fairness.prev_sum_exec_runtime;
    int weight = task->fairness.weight;
    
    if (weight == 0) {
        weight = 1024; /* Default weight */
    }
    
    return task->fairness.vruntime + (delta * 1024) / weight;
}

int fairness_calc_timeslice(struct task_struct_ext *task)
{
    if (!task) {
        return FAIRNESS_MIN_TIMESLICE;
    }
    
    int weight = task->fairness.weight;
    if (weight == 0) {
        weight = 1024;
    }
    
    int timeslice = (FAIRNESS_TARGET_LATENCY * weight) / 1024;
    
    /* Clamp to min/max values */
    if (timeslice < FAIRNESS_MIN_TIMESLICE) {
        timeslice = FAIRNESS_MIN_TIMESLICE;
    } else if (timeslice > FAIRNESS_MAX_TIMESLICE) {
        timeslice = FAIRNESS_MAX_TIMESLICE;
    }
    
    return timeslice;
}

int fairness_calc_weight(int nice)
{
    if (nice < -20) {
        nice = -20;
    } else if (nice > 19) {
        nice = 19;
    }
    
    return nice_to_weight[nice + 20];
}

void fairness_update_vruntime(struct task_struct_ext *task, uint64_t delta)
{
    if (!task) {
        return;
    }
    
    int weight = task->fairness.weight;
    if (weight == 0) {
        weight = 1024;
    }
    
    task->fairness.vruntime += (delta * 1024) / weight;
    task->fairness.sum_exec_runtime += delta;
}

/* ===================================================================== */
/* Task Operations */
/* ===================================================================== */

void fairness_enqueue_task(struct task_struct_ext *task, struct fairness_rq *rq, int flags)
{
    unsigned long rq_flags;
    
    if (!task || !rq) {
        return;
    }
    
    spin_lock_irqsave(&rq->lock, rq_flags);
    
    /* Initialize task fairness if not already done */
    if (task->fairness.vruntime == 0) {
        task->fairness.vruntime = rq->min_vruntime;
        task->fairness.weight = fairness_calc_weight(task->nice);
        task->fairness.timeslice = fairness_calc_timeslice(task);
        task->fairness.time_slice_remaining = task->fairness.timeslice;
    }
    
    /* Add to red-black tree */
    struct rb_node **link = &rq->rb_root.rb_node;
    struct rb_node *parent = NULL;
    struct task_struct_ext *entry;
    
    while (*link) {
        parent = *link;
        entry = rb_entry(parent, struct task_struct_ext, rb_node);
        
        if (task->fairness.vruntime < entry->fairness.vruntime) {
            link = &parent->rb_left;
        } else {
            link = &parent->rb_right;
        }
    }
    
    rb_link_node(&task->rb_node, parent, link);
    rb_insert_color(&task->rb_node, &rq->rb_root);
    
    /* Update leftmost pointer if needed */
    if (rq->rb_leftmost == NULL || 
        task->fairness.vruntime < rq->rb_leftmost->fairness.vruntime) {
        rq->rb_leftmost = &task->rb_node;
    }
    
    /* Update statistics */
    rq->nr_running++;
    rq->runnable_load += task->fairness.weight;
    
    spin_unlock_irqrestore(&rq->lock, rq_flags);
    
    atomic_inc(&fairness_ctx.total_tasks);
}

void fairness_dequeue_task(struct task_struct_ext *task, struct fairness_rq *rq)
{
    unsigned long rq_flags;
    
    if (!task || !rq) {
        return;
    }
    
    spin_lock_irqsave(&rq->lock, rq_flags);
    
    /* Remove from red-black tree */
    rb_erase(&task->rb_node, &rq->rb_root);
    
    /* Update leftmost pointer */
    if (rq->rb_leftmost == &task->rb_node) {
        rq->rb_leftmost = rb_first(&rq->rb_root);
    }
    
    /* Update statistics */
    rq->nr_running--;
    rq->runnable_load -= task->fairness.weight;
    
    spin_unlock_irqrestore(&rq->lock, rq_flags);
    
    atomic_dec(&fairness_ctx.total_tasks);
}

struct task_struct_ext *fairness_pick_next_task(struct fairness_rq *rq)
{
    unsigned long rq_flags;
    struct task_struct_ext *task = NULL;
    
    if (!rq) {
        return NULL;
    }
    
    spin_lock_irqsave(&rq->lock, rq_flags);
    
    /* Get leftmost node (minimum vruntime) */
    if (rq->rb_leftmost) {
        task = rb_entry(rq->rb_leftmost, struct task_struct_ext, rb_node);
        task->fairness.exec_start = get_timestamp();
        task->fairness.time_slice_remaining = task->fairness.timeslice;
    }
    
    spin_unlock_irqrestore(&rq->lock, rq_flags);
    
    return task;
}

void fairness_task_tick(struct task_struct_ext *task, struct fairness_rq *rq)
{
    unsigned long rq_flags;
    
    if (!task || !rq) {
        return;
    }
    
    spin_lock_irqsave(&rq->lock, rq_flags);
    
    /* Update execution time */
    uint64_t now = get_timestamp();
    uint64_t delta = now - task->fairness.exec_start;
    task->fairness.exec_start = now;
    
    /* Update virtual runtime */
    fairness_update_vruntime(task, delta);
    
    /* Update time slice */
    task->fairness.time_slice_remaining--;
    
    /* Check if timeslice expired */
    if (task->fairness.time_slice_remaining <= 0) {
        /* Requeue task */
        fairness_dequeue_task(task, rq);
        fairness_enqueue_task(task, rq, 0);
        
        /* Force reschedule */
        set_need_resched();
    }
    
    /* Update run queue min vruntime */
    if (rq->rb_leftmost) {
        struct task_struct_ext *leftmost = rb_entry(rq->rb_leftmost, 
                                                     struct task_struct_ext, rb_node);
        rq->min_vruntime = leftmost->fairness.vruntime;
    }
    
    spin_unlock_irqrestore(&rq->lock, rq_flags);
    
    atomic_inc(&fairness_ctx.context_switches);
}

bool fairness_check_preempt(struct task_struct_ext *curr, struct task_struct_ext *p)
{
    if (!curr || !p) {
        return false;
    }
    
    /* Check if new task has smaller vruntime */
    if (p->fairness.vruntime < curr->fairness.vruntime) {
        return true;
    }
    
    /* Check for starvation */
    uint64_t now = get_timestamp();
    if (now - p->fairness.last_run_time > fairness_ctx.starvation_threshold) {
        return true;
    }
    
    return false;
}

/* ===================================================================== */
/* Aging and Starvation Prevention */
/* ===================================================================== */

void fairness_age_tasks(struct fairness_rq *rq)
{
    unsigned long rq_flags;
    struct task_struct_ext *task;
    struct rb_node *node;
    
    if (!rq) {
        return;
    }
    
    spin_lock_irqsave(&rq->lock, rq_flags);
    
    uint64_t now = get_timestamp();
    
    /* Check if it's time to age */
    if (now - rq->last_aging_time < fairness_ctx.aging_interval) {
        spin_unlock_irqrestore(&rq->lock, rq_flags);
        return;
    }
    
    rq->last_aging_time = now;
    
    /* Age all tasks in run queue */
    for (node = rb_first(&rq->rb_root); node; node = rb_next(node)) {
        task = rb_entry(node, struct task_struct_ext, rb_node);
        
        /* Increase age */
        if (task->fairness.age < FAIRNESS_MAX_AGE) {
            task->fairness.age++;
            task->fairness.last_age_time = now;
            
            /* Boost priority slightly */
            task->fairness.vruntime -= FAIRNESS_AGE_BOOST;
            task->fairness.aged = true;
        }
    }
    
    spin_unlock_irqrestore(&rq->lock, rq_flags);
    
    atomic_inc(&fairness_ctx.aging_operations);
}

void fairness_boost_starving_tasks(struct fairness_rq *rq)
{
    unsigned long rq_flags;
    struct task_struct_ext *task;
    struct rb_node *node;
    uint64_t now;
    
    if (!rq) {
        return;
    }
    
    spin_lock_irqsave(&rq->lock, rq_flags);
    
    now = get_timestamp();
    
    /* Find and boost starving tasks */
    for (node = rb_first(&rq->rb_root); node; node = rb_next(node)) {
        task = rb_entry(node, struct task_struct_ext, rb_node);
        
        /* Check if task is starving */
        if (now - task->fairness.last_run_time > fairness_ctx.starvation_threshold) {
            task->fairness.starvation_count++;
            task->fairness.starving = true;
            
            /* Significant boost */
            task->fairness.vruntime -= fairness_ctx.starvation_threshold / 10;
            task->fairness.boosted = true;
            
            atomic_inc(&fairness_ctx.starvation_prevented);
        }
    }
    
    spin_unlock_irqrestore(&rq->lock, rq_flags);
}

int fairness_check_starvation(struct fairness_rq *rq)
{
    unsigned long rq_flags;
    struct task_struct_ext *task;
    struct rb_node *node;
    int starving_count = 0;
    uint64_t now;
    
    if (!rq) {
        return 0;
    }
    
    spin_lock_irqsave(&rq->lock, rq_flags);
    
    now = get_timestamp();
    
    /* Count starving tasks */
    for (node = rb_first(&rq->rb_root); node; node = rb_next(node)) {
        task = rb_entry(node, struct task_struct_ext, rb_node);
        
        if (now - task->fairness.last_run_time > fairness_ctx.starvation_threshold) {
            starving_count++;
        }
    }
    
    spin_unlock_irqrestore(&rq->lock, rq_flags);
    
    return starving_count;
}

/* ===================================================================== */
/* Load Balancing */
/* ===================================================================== */

void fairness_balance_load(int cpu, bool idle)
{
    struct fairness_rq *src_rq, *dst_rq;
    struct task_struct_ext *task;
    int busiest_cpu, idlest_cpu;
    unsigned long busiest_load, idlest_load;
    
    if (cpu >= fairness_ctx.nr_cpus) {
        return;
    }
    
    src_rq = &fairness_ctx.runqueues[cpu];
    
    /* Find busiest CPU */
    busiest_cpu = fairness_find_busiest_cpu(cpu, &busiest_load);
    if (busiest_cpu < 0) {
        return;
    }
    
    /* Find idlest CPU */
    idlest_cpu = fairness_find_idlest_cpu(cpu, &idlest_load);
    if (idlest_cpu < 0) {
        return;
    }
    
    /* Check if balancing is needed */
    if (busiest_load - idlest_load < 1024) { /* Threshold */
        return;
    }
    
    src_rq = &fairness_ctx.runqueues[busiest_cpu];
    dst_rq = &fairness_ctx.runqueues[idlest_cpu];
    
    /* Find task to migrate */
    unsigned long rq_flags;
    spin_lock_irqsave(&src_rq->lock, rq_flags);
    
    if (src_rq->rb_leftmost) {
        task = rb_entry(src_rq->rb_leftmost, struct task_struct_ext, rb_node);
        
        if (fairness_should_migrate(task, idlest_cpu)) {
            fairness_dequeue_task(task, src_rq);
            spin_unlock_irqrestore(&src_rq->lock, rq_flags);
            
            fairness_enqueue_task(task, dst_rq, 0);
            
            task->fairness.migrated = true;
            task->fairness.last_migration = get_timestamp();
            task->fairness.preferred_cpu = idlest_cpu;
            
            atomic_inc(&fairness_ctx.migrations);
        } else {
            spin_unlock_irqrestore(&src_rq->lock, rq_flags);
        }
    } else {
        spin_unlock_irqrestore(&src_rq->lock, rq_flags);
    }
}

int fairness_migrate_task(struct task_struct_ext *task, int dest_cpu)
{
    struct fairness_rq *src_rq, *dst_rq;
    
    if (!task || dest_cpu >= fairness_ctx.nr_cpus) {
        return -EINVAL;
    }
    
    src_rq = task->rq;
    dst_rq = &fairness_ctx.runqueues[dest_cpu];
    
    /* Check migration penalty */
    uint64_t now = get_timestamp();
    if (now - task->fairness.last_migration < task->fairness.migration_penalty) {
        return -EBUSY;
    }
    
    /* Move task */
    fairness_dequeue_task(task, src_rq);
    fairness_enqueue_task(task, dst_rq, 0);
    
    task->fairness.migrated = true;
    task->fairness.last_migration = now;
    task->fairness.preferred_cpu = dest_cpu;
    
    atomic_inc(&fairness_ctx.migrations);
    
    return 0;
}

bool fairness_should_migrate(struct task_struct_ext *task, int dest_cpu)
{
    if (!task || dest_cpu >= fairness_ctx.nr_cpus) {
        return false;
    }
    
    /* Don't migrate if task has affinity */
    if (task->cpu_allowed && !(task->cpu_allowed & (1 << dest_cpu))) {
        return false;
    }
    
    /* Don't migrate if recently migrated */
    uint64_t now = get_timestamp();
    if (now - task->fairness.last_migration < task->fairness.migration_penalty) {
        return false;
    }
    
    /* Check load difference */
    struct fairness_rq *src_rq = task->rq;
    struct fairness_rq *dst_rq = &fairness_ctx.runqueues[dest_cpu];
    
    if (src_rq->runnable_load - dst_rq->runnable_load < 512) {
        return false;
    }
    
    return true;
}

int fairness_find_busiest_cpu(int this_cpu, unsigned long *load)
{
    int busiest_cpu = -1;
    unsigned long max_load = 0;
    int i;
    
    for (i = 0; i < fairness_ctx.nr_cpus; i++) {
        if (i == this_cpu) {
            continue;
        }
        
        struct fairness_rq *rq = &fairness_ctx.runqueues[i];
        if (rq->runnable_load > max_load) {
            max_load = rq->runnable_load;
            busiest_cpu = i;
        }
    }
    
    if (load) {
        *load = max_load;
    }
    
    return busiest_cpu;
}

int fairness_find_idlest_cpu(int this_cpu, unsigned long *load)
{
    int idlest_cpu = -1;
    unsigned long min_load = ULONG_MAX;
    int i;
    
    for (i = 0; i < fairness_ctx.nr_cpus; i++) {
        if (i == this_cpu) {
            continue;
        }
        
        struct fairness_rq *rq = &fairness_ctx.runqueues[i];
        if (rq->runnable_load < min_load) {
            min_load = rq->runnable_load;
            idlest_cpu = i;
        }
    }
    
    if (load) {
        *load = min_load;
    }
    
    return idlest_cpu;
}

/* ===================================================================== */
/* Fairness Scheduler Interface */
/* ===================================================================== */

int fairness_init(int level)
{
    int i;
    
    if (fairness_ctx.initialized) {
        return 0;
    }
    
    /* Validate fairness level */
    if (level < FAIRNESS_LEVEL_NONE || level > FAIRNESS_LEVEL_REALTIME) {
        return -EINVAL;
    }
    
    fairness_ctx.fairness_level = level;
    fairness_ctx.aging_interval = FAIRNESS_AGING_INTERVAL;
    fairness_ctx.starvation_threshold = FAIRNESS_STARVATION_THRESHOLD;
    
    /* Get number of CPUs */
    fairness_ctx.nr_cpus = num_online_cpus();
    
    /* Allocate run queues */
    fairness_ctx.runqueues = kmalloc(sizeof(struct fairness_rq) * fairness_ctx.nr_cpus, 
                                     GFP_KERNEL);
    if (!fairness_ctx.runqueues) {
        return -ENOMEM;
    }
    
    /* Initialize run queues */
    for (i = 0; i < fairness_ctx.nr_cpus; i++) {
        struct fairness_rq *rq = &fairness_ctx.runqueues[i];
        
        rq->rb_root = RB_ROOT;
        rq->rb_leftmost = NULL;
        rq->min_vruntime = 0;
        rq->exec_clock = 0;
        rq->idle_clock = 0;
        rq->load = 0;
        rq->load_weight = 0;
        rq->runnable_load = 0;
        rq->nr_running = 0;
        rq->nr_uninterruptible = 0;
        rq->nr_sleeping = 0;
        rq->last_aging_time = get_timestamp();
        rq->aging_interval = fairness_ctx.aging_interval;
        rq->cpu = i;
        rq->overloaded = false;
        rq->last_balance_time = get_timestamp();
        rq->balance_interval = 100; /* ms */
        spin_lock_init(&rq->lock);
    }
    
    /* Initialize statistics */
    atomic_set(&fairness_ctx.total_tasks, 0);
    atomic_set(&fairness_ctx.aging_operations, 0);
    atomic_set(&fairness_ctx.starvation_prevented, 0);
    atomic_set(&fairness_ctx.migrations, 0);
    atomic_set(&fairness_ctx.context_switches, 0);
    
    fairness_ctx.initialized = true;
    
    printk(KERN_INFO "Fairness scheduler initialized (level %d, %d CPUs)\n", 
           level, fairness_ctx.nr_cpus);
    
    return 0;
}

void fairness_shutdown(void)
{
    if (!fairness_ctx.initialized) {
        return;
    }
    
    /* Free run queues */
    if (fairness_ctx.runqueues) {
        kfree(fairness_ctx.runqueues);
        fairness_ctx.runqueues = NULL;
    }
    
    fairness_ctx.initialized = false;
    
    printk(KERN_INFO "Fairness scheduler shutdown\n");
}

/* ===================================================================== */
/* Statistics and Debugging */
/* ===================================================================== */

int fairness_get_stats(int cpu, char *buf, size_t size)
{
    struct fairness_rq *rq;
    int len = 0;
    
    if (!buf || cpu >= fairness_ctx.nr_cpus) {
        return -EINVAL;
    }
    
    rq = &fairness_ctx.runqueues[cpu];
    
    len += snprintf(buf + len, size - len,
                   "Fairness Statistics CPU %d:\n"
                   "Running tasks: %lu\n"
                   "Runnable load: %lu\n"
                   "Min vruntime: %llu\n"
                   "Exec clock: %llu\n"
                   "Idle clock: %llu\n"
                   "Overloaded: %s\n"
                   "Last aging: %llu\n"
                   "Last balance: %llu\n",
                   cpu,
                   rq->nr_running,
                   rq->runnable_load,
                   rq->min_vruntime,
                   rq->exec_clock,
                   rq->idle_clock,
                   rq->overloaded ? "Yes" : "No",
                   rq->last_aging_time,
                   rq->last_balance_time);
    
    return len;
}

int fairness_validate(int cpu)
{
    struct fairness_rq *rq;
    
    if (cpu >= fairness_ctx.nr_cpus) {
        return -EINVAL;
    }
    
    rq = &fairness_ctx.runqueues[cpu];
    
    /* Validate red-black tree */
    if (!rb_tree_validate(&rq->rb_root)) {
        return -EINVAL;
    }
    
    /* Validate statistics */
    if (rq->nr_running < 0 || rq->runnable_load < 0) {
        return -EINVAL;
    }
    
    return 0;
}

/* ===================================================================== */
/* Configuration */
/* ===================================================================== */

int fairness_set_level(int level)
{
    if (level < FAIRNESS_LEVEL_NONE || level > FAIRNESS_LEVEL_REALTIME) {
        return -EINVAL;
    }
    
    fairness_ctx.fairness_level = level;
    
    printk(KERN_INFO "Fairness level set to %d\n", level);
    return 0;
}

int fairness_get_level(void)
{
    return fairness_ctx.fairness_level;
}

int fairness_set_aging_interval(int interval)
{
    if (interval < 10 || interval > 10000) {
        return -EINVAL;
    }
    
    fairness_ctx.aging_interval = interval;
    
    /* Update all run queues */
    int i;
    for (i = 0; i < fairness_ctx.nr_cpus; i++) {
        fairness_ctx.runqueues[i].aging_interval = interval;
    }
    
    return 0;
}

int fairness_set_starvation_threshold(uint64_t threshold)
{
    if (threshold < 100 || threshold > 60000) {
        return -EINVAL;
    }
    
    fairness_ctx.starvation_threshold = threshold;
    return 0;
}
