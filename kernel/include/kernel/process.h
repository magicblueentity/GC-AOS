/*
 * GC-AOS Kernel - Process Management
 * 
 * Process context and task management
 */

#ifndef _KERNEL_PROCESS_H
#define _KERNEL_PROCESS_H

#include "types.h"

/* Process structure */
struct process {
    int pid;                    /* Process ID */
    uid_t uid;                  /* User ID */
    gid_t gid;                  /* Group ID */
    char comm[16];              /* Process name */
    struct mm_struct *mm;         /* Memory management */
    struct task_struct *task;       /* Main task */
    struct list_head tasks;         /* Task list */
    struct list_head children;       /* Child processes */
    struct process *parent;         /* Parent process */
    int state;                   /* Process state */
    int exit_code;                /* Exit code */
};

/* Current process pointer */
extern struct process *current;

/* Process management functions */
struct process *get_process_by_pid(int pid);
struct process *get_current_process(void);

#endif /* _KERNEL_PROCESS_H */
