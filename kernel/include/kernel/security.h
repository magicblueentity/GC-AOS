/*
 * GC-AOS Kernel - Security Hardening Framework
 * 
 * Provides capability-based security, module signing, syscall validation,
 * and memory protection for production-grade kernel security.
 */

#ifndef _KERNEL_SECURITY_H
#define _KERNEL_SECURITY_H

#include "types.h"
#include "kernel/module.h"

/* ===================================================================== */
/* Security Configuration */
/* ===================================================================== */

#define SECURITY_ENABLED              1       /* Enable security features */
#define SECURITY_STRICT_MODE          1       /* Strict security mode */
#define SECURITY_AUDIT_ENABLED       1       /* Security auditing */
#define SECURITY_ENFORCE_CAPS        1       /* Enforce capabilities */
#define SECURITY_MODULE_SIGNING      1       /* Module signing required */
#define SECURITY_MEMORY_PROTECTION   1       /* Memory protection enabled */
#define SECURITY_SYSCALL_VALIDATION  1       /* Syscall validation enabled */

/* Security levels */
#define SECURITY_LEVEL_NONE          0       /* No security */
#define SECURITY_LEVEL_BASIC         1       /* Basic security */
#define SECURITY_LEVEL_STANDARD      2       /* Standard security */
#define SECURITY_LEVEL_HIGH          3       /* High security */
#define SECURITY_LEVEL_PARANOID      4       /* Paranoid security */

/* ===================================================================== */
/* Capability System */
/* ===================================================================== */

/* Capability flags */
#define CAP_NONE                     0x00000000ULL  /* No capabilities */
#define CAP_SYS_ADMIN               (1ULL << 0)   /* System administration */
#define CAP_SYS_MODULE              (1ULL << 1)   /* Load/unload kernel modules */
#define CAP_SYS_RAWIO              (1ULL << 2)   /* Raw I/O operations */
#define CAP_SYS_CHROOT              (1ULL << 3)   /* Change root */
#define CAP_SYS_PTRACE              (1ULL << 4)   /* Trace processes */
#define CAP_SYS_PACCT               (1ULL << 5)   /* Process accounting */
#define CAP_SYS_ADMIN               (1ULL << 6)   /* System administration */
#define CAP_SYS_BOOT                (1ULL << 7)   /* Reboot system */
#define CAP_SYS_NICE                (1ULL << 8)   /* Change priority */
#define CAP_SYS_RESOURCE            (1ULL << 9)   /* Resource limits */
#define CAP_SYS_TIME                (1ULL << 10)  /* Set system time */
#define CAP_SYS_TTY_CONFIG          (1ULL << 11)  /* Configure TTY */
#define CAP_MKNOD                    (1ULL << 12)  /* Create device nodes */
#define CAP_LEASE                    (1ULL << 13)  /* File leases */
#define CAP_AUDIT_WRITE              (1ULL << 14)  /* Write audit logs */
#define CAP_AUDIT_CONTROL            (1ULL << 15)  /* Control audit subsystem */
#define CAP_SETFCAP                  (1ULL << 16)  /* Set file capabilities */
#define CAP_SETPCAP                  (1ULL << 17)  /* Set process capabilities */
#define CAP_MAC_OVERRIDE             (1ULL << 18)  /* Override MAC */
#define CAP_MAC_ADMIN                (1ULL << 19)  /* MAC administration */
#define CAP_SYSLOG                  (1ULL << 20)  /* Syslog operations */
#define CAP_CHOWN                    (1ULL << 21)  /* Change file ownership */
#define CAP_NET_ADMIN                (1ULL << 22)  /* Network administration */
#define CAP_NET_BIND_SERVICE        (1ULL << 23)  /* Bind to privileged ports */
#define CAP_NET_BROADCAST           (1ULL << 24)  /* Network broadcast */
#define CAP_NET_RAW                 (1ULL << 25)  /* Raw sockets */
#define CAP_IPC_LOCK                (1ULL << 26)  /* Lock shared memory */
#define CAP_IPC_OWNER               (1ULL << 27)  /* IPC ownership */
#define CAP_SYS_MODULE              (1ULL << 28)  /* Load kernel modules */
#define CAP_SYS_RAWIO               (1ULL << 29)  /* Raw I/O */
#define CAP_SYS_CHROOT              (1ULL << 30)  /* Change root */
#define CAP_SYS_PTRACE              (1ULL << 31)  /* Trace processes */
#define CAP_SYS_PACCT               (1ULL << 32)  /* Process accounting */
#define CAP_SYS_ADMIN               (1ULL << 33)  /* System administration */
#define CAP_SYS_BOOT                (1ULL << 34)  /* Reboot system */
#define CAP_SYS_NICE                (1ULL << 35)  /* Change priority */
#define CAP_SYS_RESOURCE            (1ULL << 36)  /* Resource limits */
#define CAP_SYS_TIME                (1ULL << 37)  /* Set system time */
#define CAP_SYS_TTY_CONFIG          (1ULL << 38)  /* Configure TTY */
#define CAP_MKNOD                    (1ULL << 39)  /* Create device nodes */
#define CAP_LEASE                    (1ULL << 40)  /* File leases */
#define CAP_AUDIT_WRITE              (1ULL << 41)  /* Write audit logs */
#define CAP_AUDIT_CONTROL            (1ULL << 42)  /* Control audit subsystem */
#define CAP_SETFCAP                  (1ULL << 43)  /* Set file capabilities */
#define CAP_SETPCAP                  (1ULL << 44)  /* Set process capabilities */
#define CAP_LINUX_IMMUTABLE         (1ULL << 45)  /* Immutable files */
#define CAP_NET_BIND_SERVICE        (1ULL << 46)  /* Bind to privileged ports */
#define CAP_NET_BROADCAST           (1ULL << 47)  /* Network broadcast */
#define CAP_NET_ADMIN                (1ULL << 48)  /* Network administration */
#define CAP_NET_RAW                 (1ULL << 49)  /* Raw sockets */
#define CAP_IPC_LOCK                (1ULL << 50)  /* Lock shared memory */
#define CAP_IPC_OWNER               (1ULL << 51)  /* IPC ownership */
#define CAP_SYS_MODULE              (1ULL << 52)  /* Load kernel modules */
#define CAP_SYS_RAWIO               (1ULL << 53)  /* Raw I/O */

/* Capability sets */
#define CAP_FULL_SET                0xFFFFFFFFFFFFFFFFULL  /* All capabilities */
#define CAP_DEFAULT_SET             (CAP_SYS_ADMIN | CAP_SYS_MODULE | CAP_SYS_RAWIO)
#define CAP_USER_SET                (CAP_NET_BIND_SERVICE | CAP_NET_RAW)
#define CAP_ROOT_SET                CAP_FULL_SET

/* ===================================================================== */
/* Process Security Structure */
/* ===================================================================== */

struct process_security {
    /* Capabilities */
    uint64_t cap_effective;          /* Effective capabilities */
    uint64_t cap_permitted;          /* Permitted capabilities */
    uint64_t cap_inheritable;        /* Inheritable capabilities */
    uint64_t cap_bounding;           /* Bounding set */
    
    /* Security context */
    uid_t uid;                       /* User ID */
    uid_t euid;                      /* Effective user ID */
    uid_t suid;                      /* Saved user ID */
    gid_t gid;                       /* Group ID */
    gid_t egid;                      /* Effective group ID */
    gid_t sgid;                      /* Saved group ID */
    
    /* Security flags */
    uint32_t security_flags;         /* Security flags */
    bool secure;                     /* Process is secure */
    bool privileged;                 /* Process is privileged */
    bool sandboxed;                  /* Process is sandboxed */
    
    /* Memory protection */
    bool exec_shield;                /* Executable shield enabled */
    bool stack_guard;                /* Stack guard enabled */
    bool heap_guard;                 /* Heap guard enabled */
    
    /* Audit information */
    uint64_t audit_id;               /* Audit ID */
    bool audited;                    /* Process is audited */
    uint32_t audit_mask;             /* Audit mask */
    
    /* Security statistics */
    uint32_t security_violations;    /* Security violations */
    uint32_t privilege_escalations;  /* Privilege escalations */
    uint32_t access_denials;         /* Access denials */
};

/* Security flags */
#define SEC_FLAG_NO_NEW_PRIVS        (1 << 0)  /* No new privileges */
#define SEC_FLAG_DUMPABLE            (1 << 1)  /* Core dumpable */
#define SEC_FLAG_PTRACED             (1 << 2)  /* Being traced */
#define SEC_FLAG_PTRACER             (1 << 3)  /* Tracing others */
#define SEC_FLAG_SETUID              (1 << 4)  /* Set UID root */
#define SEC_FLAG_SETGID              (1 << 5)  /* Set GID root */
#define SEC_FLAG_MEMORY_LOCK         (1 << 6)  /* Memory locked */
#define SEC_FLAG_MEMORY_DIRTY         (1 << 7)  /* Memory dirty */
#define SEC_FLAG_CHILD_PTRACED       (1 << 8)  /* Child being traced */
#define SEC_FLAG_NO_NEW_PRIVS        (1 << 9)  /* No new privileges */
#define SEC_FLAG_ALL                 0xFFFFFFFF

/* ===================================================================== */
/* Module Security Structure */
/* ===================================================================== */

struct module_security {
    /* Module signing */
    bool signed;                     /* Module is signed */
    bool signature_valid;            /* Signature is valid */
    uint8_t signature[256];          /* Module signature */
    uint32_t signature_len;          /* Signature length */
    char signer[64];                 /* Module signer */
    
    /* Module capabilities */
    uint64_t required_caps;          /* Required capabilities */
    uint64_t granted_caps;           /* Granted capabilities */
    
    /* Module permissions */
    bool can_load;                   /* Can be loaded */
    bool can_unload;                 /* Can be unloaded */
    bool can_modify;                 /* Can modify kernel */
    
    /* Module validation */
    bool validated;                  /* Module is validated */
    bool checksum_valid;             /* Checksum is valid */
    uint32_t checksum;               /* Module checksum */
    
    /* Module restrictions */
    bool restricted;                 /* Module is restricted */
    bool sandboxed;                  /* Module is sandboxed */
    uint32_t restrictions;           /* Restriction flags */
    
    /* Security audit */
    bool audited;                    /* Module is audited */
    uint64_t load_time;              /* Load time */
    uint64_t unload_time;            /* Unload time */
    char load_source[256];           /* Load source */
};

/* Module restriction flags */
#define MODULE_RESTRICT_NONE          0x00000000  /* No restrictions */
#define MODULE_RESTRICT_NO_IO         (1 << 0)    /* No I/O operations */
#define MODULE_RESTRICT_NO_MEM        (1 << 1)    /* No memory operations */
#define MODULE_RESTRICT_NO_NET        (1 << 2)    /* No network operations */
#define MODULE_RESTRICT_NO_FS         (1 << 3)    /* No filesystem operations */
#define MODULE_RESTRICT_NO_PROC       (1 << 4)    /* No process operations */
#define MODULE_RESTRICT_NO_SYS        (1 << 5)    /* No system operations */
#define MODULE_RESTRICT_READ_ONLY     (1 << 6)    /* Read-only operations */
#define MODULE_RESTRICT_SANDBOX       (1 << 7)    /* Full sandbox */

/* ===================================================================== */
/* Security Audit Structure */
/* ===================================================================== */

struct security_audit {
    /* Audit record */
    uint64_t timestamp;              /* Audit timestamp */
    uint64_t audit_id;               /* Audit ID */
    pid_t pid;                       /* Process ID */
    uid_t uid;                       /* User ID */
    gid_t gid;                       /* Group ID */
    
    /* Audit event */
    enum {
        AUDIT_EVENT_SYSCALL,         /* System call */
        AUDIT_EVENT_MODULE_LOAD,      /* Module load */
        AUDIT_EVENT_MODULE_UNLOAD,    /* Module unload */
        AUDIT_EVENT_PRIV_ESCALATION,  /* Privilege escalation */
        AUDIT_EVENT_ACCESS_DENIED,    /* Access denied */
        AUDIT_EVENT_SECURITY_VIOLATION, /* Security violation */
        AUDIT_EVENT_MEMORY_VIOLATION,  /* Memory violation */
        AUDIT_EVENT_CAPABILITY_CHECK, /* Capability check */
        AUDIT_EVENT_AUTHENTICATION,   /* Authentication */
        AUDIT_EVENT_AUTHORIZATION,   /* Authorization */
    } event_type;
    
    /* Event data */
    char event_data[512];             /* Event data */
    uint32_t event_data_len;         /* Event data length */
    
    /* Event result */
    int result;                      /* Event result */
    int error_code;                  /* Error code */
    
    /* Event context */
    char filename[256];              /* Filename if applicable */
    char syscall_name[64];           /* Syscall name if applicable */
    uint64_t syscall_args[6];        /* Syscall arguments */
    
    /* Security context */
    uint64_t caps_effective;         /* Effective capabilities */
    uint32_t security_flags;         /* Security flags */
    bool privileged;                 /* Privileged operation */
};

/* ===================================================================== */
/* Security Functions */
/* ===================================================================== */

/**
 * security_init - Initialize security subsystem
 * @level: Security level (0-4)
 * 
 * Return: 0 on success, negative error on failure
 */
int security_init(int level);

/**
 * security_shutdown - Shutdown security subsystem
 */
void security_shutdown(void);

/**
 * security_set_level - Set security level
 * @level: Security level (0-4)
 * 
 * Return: 0 on success, negative error on failure
 */
int security_set_level(int level);

/**
 * security_get_level - Get current security level
 * 
 * Return: Current security level
 */
int security_get_level(void);

/* ===================================================================== */
/* Capability Management */
/* ===================================================================== */

/**
 * security_capable - Check if current process has capability
 * @cap: Capability to check
 * 
 * Return: 0 if capable, negative error if not
 */
int security_capable(uint64_t cap);

/**
 * security_capable_pid - Check if process has capability
 * @pid: Process ID
 * @cap: Capability to check
 * 
 * Return: 0 if capable, negative error if not
 */
int security_capable_pid(pid_t pid, uint64_t cap);

/**
 * security_capable_file - Check capability for file operation
 * @cap: Capability to check
 * @inode: File inode
 * 
 * Return: 0 if capable, negative error if not
 */
int security_capable_file(uint64_t cap, struct inode *inode);

/**
 * security_set_capability - Set process capability
 * @pid: Process ID
 * @cap: Capability to set
 * @value: Capability value
 * 
 * Return: 0 on success, negative error on failure
 */
int security_set_capability(pid_t pid, uint64_t cap, bool value);

/**
 * security_get_capabilities - Get process capabilities
 * @pid: Process ID
 * @effective: Output for effective capabilities
 * @permitted: Output for permitted capabilities
 * @inheritable: Output for inheritable capabilities
 * 
 * Return: 0 on success, negative error on failure
 */
int security_get_capabilities(pid_t pid, uint64_t *effective, uint64_t *permitted,
                            uint64_t *inheritable);

/* ===================================================================== */
/* Module Security */
/* ===================================================================== */

/**
 * security_module_verify - Verify module signature
 * @module: Module to verify
 * @data: Module data
 * @size: Module size
 * 
 * Return: 0 if valid, negative error if invalid
 */
int security_module_verify(struct module *module, const void *data, size_t size);

/**
 * security_module_load - Check if module can be loaded
 * @module: Module to load
 * @required_caps: Required capabilities
 * 
 * Return: 0 if allowed, negative error if not allowed
 */
int security_module_load(struct module *module, uint64_t required_caps);

/**
 * security_module_unload - Check if module can be unloaded
 * @module: Module to unload
 * 
 * Return: 0 if allowed, negative error if not allowed
 */
int security_module_unload(struct module *module);

/**
 * security_module_restrict - Apply restrictions to module
 * @module: Module to restrict
 * @restrictions: Restriction flags
 * 
 * Return: 0 on success, negative error on failure
 */
int security_module_restrict(struct module *module, uint32_t restrictions);

/* ===================================================================== */
/* Syscall Validation */
/* ===================================================================== */

/**
 * security_syscall_validate - Validate system call
 * @syscall: System call number
 * @args: System call arguments
 * @arg_count: Number of arguments
 * 
 * Return: 0 if valid, negative error if invalid
 */
int security_syscall_validate(int syscall, const uint64_t *args, int arg_count);

/**
 * security_syscall_check - Check if syscall is allowed
 * @syscall: System call number
 * @args: System call arguments
 * @arg_count: Number of arguments
 * 
 * Return: 0 if allowed, negative error if not allowed
 */
int security_syscall_check(int syscall, const uint64_t *args, int arg_count);

/**
 * security_syscall_audit - Audit system call
 * @syscall: System call number
 * @args: System call arguments
 * @arg_count: Number of arguments
 * @result: System call result
 */
void security_syscall_audit(int syscall, const uint64_t *args, int arg_count, int result);

/* ===================================================================== */
/* Memory Protection */
/* ===================================================================== */

/**
 * security_memory_protect - Protect memory region
 * @addr: Memory address
 * @size: Memory size
 * @prot: Protection flags
 * 
 * Return: 0 on success, negative error on failure
 */
int security_memory_protect(void *addr, size_t size, uint32_t prot);

/**
 * security_memory_guard - Add guard pages to memory
 * @addr: Memory address
 * @size: Memory size
 * 
 * Return: 0 on success, negative error on failure
 */
int security_memory_guard(void *addr, size_t size);

/**
 * security_memory_validate - Validate memory access
 * @addr: Memory address
 * @size: Memory size
 * @write: Write access
 * 
 * Return: 0 if valid, negative error if invalid
 */
int security_memory_validate(void *addr, size_t size, bool write);

/**
 * security_memory_aslr - Apply ASLR to memory mapping
 * @addr: Memory address
 * @size: Memory size
 * @entropy: ASLR entropy bits
 * 
 * Return: 0 on success, negative error on failure
 */
int security_memory_aslr(void *addr, size_t size, int entropy);

/* ===================================================================== */
/* Security Audit */
/* ===================================================================== */

/**
 * security_audit_log - Log security event
 * @event_type: Event type
 * @event_data: Event data
 * @result: Event result
 * 
 * Return: 0 on success, negative error on failure
 */
int security_audit_log(int event_type, const char *event_data, int result);

/**
 * security_audit_syscall - Audit system call
 * @syscall: System call number
 * @args: System call arguments
 * @result: System call result
 * 
 * Return: 0 on success, negative error on failure
 */
int security_audit_syscall(int syscall, const uint64_t *args, int result);

/**
 * security_audit_module - Audit module operation
 * @module: Module name
 * @operation: Operation type
 * @result: Operation result
 * 
 * Return: 0 on success, negative error on failure
 */
int security_audit_module(const char *module, const char *operation, int result);

/**
 * security_audit_get_logs - Get audit logs
 * @buf: Buffer to write logs to
 * @size: Buffer size
 * @count: Number of logs to retrieve
 * 
 * Return: Number of bytes written
 */
int security_audit_get_logs(char *buf, size_t size, int count);

/* ===================================================================== */
/* Process Security */
/* ===================================================================== */

/**
 * security_process_init - Initialize process security
 * @pid: Process ID
 * @uid: User ID
 * @gid: Group ID
 * 
 * Return: 0 on success, negative error on failure
 */
int security_process_init(pid_t pid, uid_t uid, gid_t gid);

/**
 * security_process_setuid - Set process UID
 * @pid: Process ID
 * @uid: New UID
 * 
 * Return: 0 on success, negative error on failure
 */
int security_process_setuid(pid_t pid, uid_t uid);

/**
 * security_process_setgid - Set process GID
 * @pid: Process ID
 * @gid: New GID
 * 
 * Return: 0 on success, negative error on failure
 */
int security_process_setgid(pid_t pid, gid_t gid);

/**
 * security_process_sandbox - Sandbox process
 * @pid: Process ID
 * @restrictions: Sandbox restrictions
 * 
 * Return: 0 on success, negative error on failure
 */
int security_process_sandbox(pid_t pid, uint32_t restrictions);

/**
 * security_process_validate - Validate process security
 * @pid: Process ID
 * 
 * Return: 0 if valid, negative error if invalid
 */
int security_process_validate(pid_t pid);

/* ===================================================================== */
/* Security Configuration */
/* ===================================================================== */

/**
 * security_set_config - Set security configuration
 * @strict_mode: Enable strict mode
 * @audit_enabled: Enable auditing
 * @enforce_caps: Enforce capabilities
 * @module_signing: Require module signing
 * @memory_protection: Enable memory protection
 * @syscall_validation: Enable syscall validation
 * 
 * Return: 0 on success, negative error on failure
 */
int security_set_config(bool strict_mode, bool audit_enabled, bool enforce_caps,
                       bool module_signing, bool memory_protection, bool syscall_validation);

/**
 * security_get_config - Get security configuration
 * @strict_mode: Output for strict mode
 * @audit_enabled: Output for auditing
 * @enforce_caps: Output for capability enforcement
 * @module_signing: Output for module signing
 * @memory_protection: Output for memory protection
 * @syscall_validation: Output for syscall validation
 * 
 * Return: 0 on success, negative error on failure
 */
int security_get_config(bool *strict_mode, bool *audit_enabled, bool *enforce_caps,
                       bool *module_signing, bool *memory_protection, bool *syscall_validation);

/* ===================================================================== */
/* Security Statistics */
/* ===================================================================== */

/**
 * security_get_stats - Get security statistics
 * @buf: Buffer to write stats to
 * @size: Buffer size
 * 
 * Return: Number of bytes written
 */
int security_get_stats(char *buf, size_t size);

/**
 * security_reset_stats - Reset security statistics
 */
void security_reset_stats(void);

/* ===================================================================== */
/* Helper Macros */
/* ===================================================================== */

#define security_check_cap(cap) \
    security_capable(cap)

#define security_check_cap_pid(pid, cap) \
    security_capable_pid(pid, cap)

#define security_check_syscall(syscall, args, count) \
    security_syscall_check(syscall, args, count)

#define security_audit_event(type, data, result) \
    security_audit_log(type, data, result)

#define security_protect_memory(addr, size, prot) \
    security_memory_protect(addr, size, prot)

/* ===================================================================== */
/* Error Codes */
/* ===================================================================== */

#define SECURITY_SUCCESS              0       /* Success */
#define SECURITY_ERROR_INVALID       -1      /* Invalid parameter */
#define SECURITY_ERROR_DENIED        -2      /* Access denied */
#define SECURITY_ERROR_CAPABILITY    -3      /* Insufficient capabilities */
#define SECURITY_ERROR_PERMISSION    -4      /* Insufficient permissions */
#define SECURITY_ERROR_SIGNATURE     -5      /* Invalid signature */
#define SECURITY_ERROR_CHECKSUM      -6      /* Invalid checksum */
#define SECURITY_ERROR_VALIDATION    -7      /* Validation failed */
#define SECURITY_ERROR_RESTRICTION  -8      /* Operation restricted */
#define SECURITY_ERROR_MEMORY        -9      /* Memory protection error */
#define SECURITY_ERROR_AUDIT         -10     /* Audit error */
#define SECURITY_ERROR_LEVEL         -11     /* Security level error */
#define SECURITY_ERROR_CONFIG        -12     /* Configuration error */
#define SECURITY_ERROR_CORRUPTION    -13     /* Corruption detected */
#define SECURITY_ERROR_OVERFLOW      -14     /* Buffer overflow */
#define SECURITY_ERROR_UNDERFLOW     -15     /* Buffer underflow */

#endif /* _KERNEL_SECURITY_H */
