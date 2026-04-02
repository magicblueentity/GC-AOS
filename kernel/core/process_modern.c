/*
 * GC-AOS - Modern Process System Implementation
 * 
 * Real process system with proper state management and scheduling.
 */

#include "kernel/process_modern.h"
#include "mm/kmalloc.h"
#include "mm/vmm.h"
#include "printk.h"
#include <string.h>

/* Global process manager */
struct process_manager g_process_manager;

/* Idle process function */
static void idle_process_func(void *arg)
{
    (void)arg;
    while (1) {
        /* Halt CPU until next interrupt */
        __asm__ volatile("wfi");
    }
}

/* ===================================================================== */
/* Internal Helpers */
/* ===================================================================== */

static int alloc_pid(void)
{
    spin_lock(&g_process_manager.lock);
    
    int pid = g_process_manager.next_pid;
    int start_pid = pid;
    
    /* Find next available PID */
    do {
        if (g_process_manager.processes[pid] == NULL) {
            g_process_manager.next_pid = (pid + 1) % MAX_PROCESSES;
            if (g_process_manager.next_pid < 1) {
                g_process_manager.next_pid = 1;
            }
            spin_unlock(&g_process_manager.lock);
            return pid;
        }
        pid = (pid + 1) % MAX_PROCESSES;
        if (pid < 1) pid = 1;
    } while (pid != start_pid);
    
    spin_unlock(&g_process_manager.lock);
    return -1; /* No PID available */
}

static process_t *alloc_process(void)
{
    process_t *proc = kmalloc(sizeof(process_t));
    if (!proc) {
        return NULL;
    }
    
    /* Zero initialize */
    memset(proc, 0, sizeof(process_t));
    
    /* Initialize lists */
    INIT_LIST_HEAD(&proc->children);
    INIT_LIST_HEAD(&proc->wait_list);
    INIT_LIST_HEAD(&proc->run_list);
    INIT_LIST_HEAD(&proc->all_list);
    
    return proc;
}

static void free_process(process_t *proc)
{
    if (proc->kernel_stack) {
        kfree_pages(proc->kernel_stack, PROCESS_STACK_SIZE / PAGE_SIZE);
    }
    if (proc->mm) {
        /* TODO: Clean up address space */
    }
    kfree(proc);
}

static void *alloc_stack(size_t size)
{
    void *stack = kmalloc_aligned(size, PAGE_SIZE);
    if (stack) {
        memset(stack, 0, size);
    }
    return stack;
}

/* ===================================================================== */
/* Process System Initialization */
/* ===================================================================== */

void process_system_init(void)
{
    printk(KERN_INFO "PROCESS: Initializing process system\n");
    
    memset(&g_process_manager, 0, sizeof(g_process_manager));
    spin_lock_init(&g_process_manager.lock);
    
    INIT_LIST_HEAD(&g_process_manager.all_processes);
    INIT_LIST_HEAD(&g_process_manager.ready_queue);
    INIT_LIST_HEAD(&g_process_manager.zombie_queue);
    
    g_process_manager.next_pid = 1;
    
    /* Create idle process (PID 0) */
    process_t *idle = alloc_process();
    if (!idle) {
        panic("Failed to create idle process");
    }
    
    idle->pid = 0;
    idle->tid = 0;
    strncpy(idle->name, "idle", PROCESS_NAME_MAX);
    idle->state = PROCESS_STATE_READY;
    idle->priority = 139; /* Lowest priority */
    idle->base_priority = 139;
    
    idle->kernel_stack = alloc_stack(PROCESS_STACK_SIZE);
    if (!idle->kernel_stack) {
        panic("Failed to allocate idle stack");
    }
    
    /* Set up initial context for idle process */
    idle->context.sp = (uint64_t)idle->kernel_stack + PROCESS_STACK_SIZE;
    idle->context.pc = (uint64_t)idle_process_func;
    idle->context.pstate = 0; /* EL1, no interrupts masked */
    
    g_process_manager.processes[0] = idle;
    g_process_manager.idle_process = idle;
    g_process_manager.current = idle;
    
    list_add(&idle->all_list, &g_process_manager.all_processes);
    
    printk(KERN_INFO "PROCESS: Process system initialized, idle process created\n");
}

/* ===================================================================== */
/* Process Creation */
/* ===================================================================== */

process_t *process_create(const char *name, void (*entry)(void *), 
                          void *arg, int priority)
{
    if (!name || !entry) {
        return NULL;
    }
    
    /* Clamp priority */
    if (priority < 0) priority = 0;
    if (priority > 139) priority = 139;
    
    /* Allocate process structure */
    process_t *proc = alloc_process();
    if (!proc) {
        printk(KERN_ERR "PROCESS: Failed to allocate process structure\n");
        return NULL;
    }
    
    /* Allocate PID */
    int pid = alloc_pid();
    if (pid < 0) {
        printk(KERN_ERR "PROCESS: No PID available\n");
        free_process(proc);
        return NULL;
    }
    
    /* Allocate kernel stack */
    proc->kernel_stack = alloc_stack(PROCESS_STACK_SIZE);
    if (!proc->kernel_stack) {
        printk(KERN_ERR "PROCESS: Failed to allocate kernel stack\n");
        free_process(proc);
        return NULL;
    }
    
    /* Initialize process */
    proc->pid = pid;
    proc->tid = pid;
    strncpy(proc->name, name, PROCESS_NAME_MAX - 1);
    proc->name[PROCESS_NAME_MAX - 1] = '\0';
    
    proc->state = PROCESS_STATE_NEW;
    proc->priority = priority;
    proc->base_priority = priority;
    
    /* Set up execution context */
    proc->entry_point = (uint64_t)entry;
    proc->context.sp = (uint64_t)proc->kernel_stack + PROCESS_STACK_SIZE;
    proc->context.pc = (uint64_t)entry;
    proc->context.x0 = (uint64_t)arg;  /* Pass arg in x0 */
    proc->context.pstate = 0;
    
    proc->time_slice = 10 * 1000000; /* 10ms in nanoseconds */
    proc->created_at = get_time_ns();
    proc->cpu = -1; /* No CPU affinity yet */
    
    /* Link to parent */
    process_t *parent = process_current();
    if (parent && parent != g_process_manager.idle_process) {
        proc->parent = parent;
        list_add(&proc->sibling, &parent->children);
    }
    
    /* Register in process table */
    spin_lock(&g_process_manager.lock);
    g_process_manager.processes[pid] = proc;
    g_process_manager.process_count++;
    list_add(&proc->all_list, &g_process_manager.all_processes);
    spin_unlock(&g_process_manager.lock);
    
    printk(KERN_INFO "PROCESS: Created process %d '%s' (priority=%d)\n", 
           pid, name, priority);
    
    return proc;
}

process_t *process_create_user(const char *path, int argc, char **argv)
{
    /* TODO: Load ELF and create user process */
    (void)path;
    (void)argc;
    (void)argv;
    return NULL;
}

int process_start(process_t *proc)
{
    if (!proc) {
        return -1;
    }
    
    spin_lock(&g_process_manager.lock);
    
    if (proc->state != PROCESS_STATE_NEW) {
        spin_unlock(&g_process_manager.lock);
        return -2; /* Already started or invalid state */
    }
    
    proc->state = PROCESS_STATE_READY;
    list_add_tail(&proc->run_list, &g_process_manager.ready_queue);
    
    spin_unlock(&g_process_manager.lock);
    
    printk(KERN_INFO "PROCESS: Started process %d '%s'\n", proc->pid, proc->name);
    
    return 0;
}

/* ===================================================================== */
/* Process Termination */
/* ===================================================================== */

int process_kill(int pid, int exit_code)
{
    process_t *proc = process_get(pid);
    if (!proc) {
        return -1; /* No such process */
    }
    
    /* Can't kill idle process */
    if (pid == 0) {
        return -2; /* Permission denied */
    }
    
    spin_lock(&g_process_manager.lock);
    
    /* Mark for termination */
    proc->exit_code = exit_code;
    proc->state = PROCESS_STATE_ZOMBIE;
    
    /* Remove from run queue if present */
    if (!list_empty(&proc->run_list)) {
        list_del(&proc->run_list);
        INIT_LIST_HEAD(&proc->run_list);
    }
    
    /* Wake up parent if waiting */
    if (proc->waiting_parent) {
        process_wake(proc->waiting_parent);
        proc->waiting_parent = NULL;
    }
    
    /* Reparent children to init */
    process_t *child;
    list_for_each_entry(child, &proc->children, sibling) {
        child->parent = NULL; /* Or init process */
    }
    
    /* Add to zombie queue for cleanup */
    list_add(&proc->run_list, &g_process_manager.zombie_queue);
    
    spin_unlock(&g_process_manager.lock);
    
    printk(KERN_INFO "PROCESS: Killed process %d '%s' (code=%d)\n", 
           pid, proc->name, exit_code);
    
    /* If killing current process, reschedule immediately */
    if (proc == process_current()) {
        process_schedule();
    }
    
    return 0;
}

void process_exit(int code)
{
    process_t *current = process_current();
    
    printk(KERN_INFO "PROCESS: Process %d '%s' exiting with code %d\n",
           current->pid, current->name, code);
    
    process_kill(current->pid, code);
    
    /* Should not reach here - process_kill reschedules if killing self */
    while (1) {
        __asm__ volatile("wfi");
    }
}

void process_destroy(process_t *proc)
{
    if (!proc) {
        return;
    }
    
    spin_lock(&g_process_manager.lock);
    
    /* Remove from all lists */
    list_del(&proc->all_list);
    list_del(&proc->sibling);
    
    if (!list_empty(&proc->run_list)) {
        list_del(&proc->run_list);
    }
    
    /* Remove from process table */
    g_process_manager.processes[proc->pid] = NULL;
    g_process_manager.process_count--;
    
    spin_unlock(&g_process_manager.lock);
    
    /* Free resources */
    free_process(proc);
    
    printk(KERN_INFO "PROCESS: Destroyed process %d\n", proc->pid);
}

/* ===================================================================== */
/* Context Switching */
/* ===================================================================== */

void process_switch_context(process_t *from, process_t *to)
{
    if (!from || !to || from == to) {
        return;
    }
    
    /* Save current context */
    arch_context_save(&from->context);
    
    /* Switch address space if needed */
    if (to->mm && to->mm != from->mm) {
        vmm_switch_address_space(to->mm);
    }
    
    /* Update current process */
    g_process_manager.current = to;
    
    /* Update CPU tracking */
    from->cpu = -1;
    to->cpu = 0; /* TODO: Get actual CPU ID for SMP */
    
    /* Update statistics */
    from->switch_count++;
    to->exec_start = get_time_ns();
    
    /* Restore new context */
    arch_context_restore(&to->context);
}

void process_schedule(void)
{
    spin_lock(&g_process_manager.lock);
    
    process_t *current = g_process_manager.current;
    process_t *next = NULL;
    
    /* Find next ready process */
    if (!list_empty(&g_process_manager.ready_queue)) {
        next = list_first_entry(&g_process_manager.ready_queue, process_t, run_list);
    }
    
    /* Fall back to idle process */
    if (!next) {
        next = g_process_manager.idle_process;
    }
    
    /* If current is still running, move to end of queue */
    if (current->state == PROCESS_STATE_RUNNING && 
        current != g_process_manager.idle_process) {
        current->state = PROCESS_STATE_READY;
        if (!list_empty(&current->run_list)) {
            list_del(&current->run_list);
        }
        list_add_tail(&current->run_list, &g_process_manager.ready_queue);
    }
    
    /* Mark next as running */
    next->state = PROCESS_STATE_RUNNING;
    if (!list_empty(&next->run_list)) {
        list_del(&next->run_list);
        INIT_LIST_HEAD(&next->run_list);
    }
    
    spin_unlock(&g_process_manager.lock);
    
    /* Perform context switch */
    if (next != current) {
        process_switch_context(current, next);
    }
}

void process_yield(void)
{
    process_schedule();
}

/* ===================================================================== */
/* Process State Management */
/* ===================================================================== */

void process_sleep(process_t *proc, uint64_t nanoseconds)
{
    if (!proc) {
        proc = process_current();
    }
    
    spin_lock(&g_process_manager.lock);
    
    proc->state = PROCESS_STATE_SLEEPING;
    proc->sleep_until = get_time_ns() + nanoseconds;
    
    /* Remove from run queue */
    if (!list_empty(&proc->run_list)) {
        list_del(&proc->run_list);
        INIT_LIST_HEAD(&proc->run_list);
    }
    
    spin_unlock(&g_process_manager.lock);
    
    /* Reschedule */
    if (proc == process_current()) {
        process_schedule();
    }
}

int process_wake(process_t *proc)
{
    if (!proc) {
        return -1;
    }
    
    spin_lock(&g_process_manager.lock);
    
    if (proc->state == PROCESS_STATE_READY || 
        proc->state == PROCESS_STATE_RUNNING) {
        spin_unlock(&g_process_manager.lock);
        return 1; /* Already running */
    }
    
    proc->state = PROCESS_STATE_READY;
    proc->sleep_until = 0;
    proc->wake_count++;
    
    /* Add to run queue */
    if (list_empty(&proc->run_list)) {
        list_add_tail(&proc->run_list, &g_process_manager.ready_queue);
    }
    
    spin_unlock(&g_process_manager.lock);
    
    return 0;
}

void process_block(process_t *proc, void *obj)
{
    if (!proc) {
        proc = process_current();
    }
    
    spin_lock(&g_process_manager.lock);
    
    proc->state = PROCESS_STATE_BLOCKED;
    proc->wait_obj = obj;
    
    /* Remove from run queue */
    if (!list_empty(&proc->run_list)) {
        list_del(&proc->run_list);
        INIT_LIST_HEAD(&proc->run_list);
    }
    
    spin_unlock(&g_process_manager.lock);
    
    /* Reschedule */
    if (proc == process_current()) {
        process_schedule();
    }
}

void process_unblock_all(void *obj)
{
    process_t *proc;
    
    spin_lock(&g_process_manager.lock);
    
    list_for_each_entry(proc, &g_process_manager.all_processes, all_list) {
        if (proc->state == PROCESS_STATE_BLOCKED && proc->wait_obj == obj) {
            proc->wait_obj = NULL;
            proc->state = PROCESS_STATE_READY;
            if (list_empty(&proc->run_list)) {
                list_add_tail(&proc->run_list, &g_process_manager.ready_queue);
            }
        }
    }
    
    spin_unlock(&g_process_manager.lock);
}

/* ===================================================================== */
/* Process Queries */
/* ===================================================================== */

process_t *process_get(int pid)
{
    if (pid < 0 || pid >= MAX_PROCESSES) {
        return NULL;
    }
    
    spin_lock(&g_process_manager.lock);
    process_t *proc = g_process_manager.processes[pid];
    spin_unlock(&g_process_manager.lock);
    
    return proc;
}

process_t *process_current(void)
{
    return g_process_manager.current;
}

int process_count(void)
{
    spin_lock(&g_process_manager.lock);
    int count = g_process_manager.process_count;
    spin_unlock(&g_process_manager.lock);
    return count;
}

int process_count_ready(void)
{
    int count = 0;
    process_t *proc;
    
    spin_lock(&g_process_manager.lock);
    list_for_each_entry(proc, &g_process_manager.ready_queue, run_list) {
        count++;
    }
    spin_unlock(&g_process_manager.lock);
    
    return count;
}

int process_set_priority(process_t *proc, int priority)
{
    if (!proc || priority < 0 || priority > 139) {
        return -1;
    }
    
    spin_lock(&g_process_manager.lock);
    proc->priority = priority;
    proc->base_priority = priority;
    spin_unlock(&g_process_manager.lock);
    
    return 0;
}

int process_get_info(int pid, char *name, int name_size, int *state)
{
    process_t *proc = process_get(pid);
    if (!proc) {
        return -1;
    }
    
    if (name && name_size > 0) {
        strncpy(name, proc->name, name_size - 1);
        name[name_size - 1] = '\0';
    }
    
    if (state) {
        *state = (int)proc->state;
    }
    
    return 0;
}
