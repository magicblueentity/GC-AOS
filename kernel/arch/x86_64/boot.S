/*
 * GC AOS Kernel - x86_64 Boot Assembly
 *
 * Multiboot2 enters us in 32-bit protected mode. This file switches to
 * 64-bit long mode and then transfers control to kernel_main().
 */

.code64
.section .multiboot,"a"
.align 8

/* Multiboot2 header for GRUB/VirtualBox ISO boot. */
multiboot_header_start:
.long   0xE85250D6                  # magic
.long   0                           # architecture (i386/x86_64)
.long   multiboot_header_end - multiboot_header_start
.long   -(0xE85250D6 + 0 + (multiboot_header_end - multiboot_header_start))
.word   0                           # end tag type
.word   0                           # flags
.long   8                           # size
multiboot_header_end:

.code32
.section .text.boot
.global _start
.extern kernel_main
.extern __bss_start
.extern __bss_end
.extern __stack_top

.equ MULTIBOOT2_BOOTLOADER_MAGIC, 0x36d76289
.equ CR4_PAE, 1 << 5
.equ CR0_PG, 1 << 31
.equ IA32_EFER, 0xC0000080
.equ EFER_LME, 1 << 8

/*
 * Multiboot2 entry point:
 * - eax = multiboot magic
 * - ebx = multiboot info pointer
 * - CPU is in 32-bit protected mode
 */
_start:
    cli

    cmp     $MULTIBOOT2_BOOTLOADER_MAGIC, %eax
    jne     halt32

    mov     %ebx, multiboot_info_ptr
    mov     $boot_stack32_top, %esp
    xor     %ebp, %ebp

    call    setup_page_tables

    lgdt    gdt32_pointer

    mov     %cr4, %eax
    or      $CR4_PAE, %eax
    mov     %eax, %cr4

    mov     $pml4_table, %eax
    mov     %eax, %cr3

    mov     $IA32_EFER, %ecx
    rdmsr
    or      $EFER_LME, %eax
    wrmsr

    mov     %cr0, %eax
    or      $CR0_PG, %eax
    mov     %eax, %cr0

    ljmp    $0x08, $long_mode_entry

setup_page_tables:
    push    %edi
    push    %ecx
    push    %eax

    mov     $boot_pagetables_start, %edi
    mov     $(boot_pagetables_end - boot_pagetables_start) / 4, %ecx
    xor     %eax, %eax
    rep     stosl

    movl    $pdpt_table + 0x03, pml4_table

    movl    $pd0_table + 0x03, pdpt_table + 0
    movl    $pd1_table + 0x03, pdpt_table + 8
    movl    $pd2_table + 0x03, pdpt_table + 16
    movl    $pd3_table + 0x03, pdpt_table + 24

    xor     %ecx, %ecx

1:
    mov     %ecx, %eax
    shl     $21, %eax
    or      $0x83, %eax
    mov     %eax, pd0_table(,%ecx,8)

    mov     %ecx, %eax
    add     $512, %eax
    shl     $21, %eax
    or      $0x83, %eax
    mov     %eax, pd1_table(,%ecx,8)

    mov     %ecx, %eax
    add     $1024, %eax
    shl     $21, %eax
    or      $0x83, %eax
    mov     %eax, pd2_table(,%ecx,8)

    mov     %ecx, %eax
    add     $1536, %eax
    shl     $21, %eax
    or      $0x83, %eax
    mov     %eax, pd3_table(,%ecx,8)

    incl    %ecx
    cmpl    $512, %ecx
    jne     1b

    pop     %eax
    pop     %ecx
    pop     %edi
    ret

.code64
long_mode_entry:
    mov     $0x10, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %fs
    mov     %ax, %gs
    mov     %ax, %ss

    movabs  $__stack_top, %rsp
    xor     %rbp, %rbp

    movabs  $__bss_start, %rdi
    movabs  $__bss_end, %rcx
    sub     %rdi, %rcx
    xor     %rax, %rax
    rep     stosb

    lea     idt64_pointer(%rip), %rax
    lidt    (%rax)

    mov     %cr0, %rax
    and     $~(1 << 2), %rax
    or      $(1 << 1), %rax
    mov     %rax, %cr0

    mov     %cr4, %rax
    or      $(3 << 9), %rax
    mov     %rax, %cr4

    mov     multiboot_info_ptr(%rip), %edi
    call    kernel_main

halt:
    cli
    hlt
    jmp     halt

halt32:
    cli
    hlt
    jmp     halt32

/* ===================================================================== */
/* Global Descriptor Table (GDT) */
/* ===================================================================== */
.align 16
gdt64:
    .quad   0x0000000000000000      # Null descriptor
    .quad   0x00AF9A000000FFFF      # Code segment (64-bit)
    .quad   0x00CF92000000FFFF      # Data segment
    .quad   0x00AFFA000000FFFF      # User code segment
    .quad   0x00CFF2000000FFFF      # User data segment
gdt64_end:

gdt64_pointer:
    .word   gdt64_end - gdt64 - 1   # Limit
    .quad   gdt64                   # Base

/* ===================================================================== */
/* Interrupt Descriptor Table (IDT) - Placeholder */
/* ===================================================================== */
.align 16
idt64:
    .fill   256, 16, 0              # 256 entries, 16 bytes each
idt64_end:

idt64_pointer:
    .word   idt64_end - idt64 - 1   # Limit
    .quad   idt64                   # Base

/* ===================================================================== */
/* Exception/Interrupt Handlers */
/* ===================================================================== */

.extern handle_exception
.extern handle_irq
.extern handle_syscall

/* Macro to create exception handler with error code */
.macro ISR_ERROR_CODE num
.global isr\num
isr\num:
    push    $\num               # Push interrupt number
    jmp     isr_common
.endm

/* Macro to create exception handler without error code */
.macro ISR_NO_ERROR_CODE num
.global isr\num
isr\num:
    push    $0                  # Push dummy error code
    push    $\num               # Push interrupt number
    jmp     isr_common
.endm

/* Define all ISRs */
ISR_NO_ERROR_CODE 0     # Divide by zero
ISR_NO_ERROR_CODE 1     # Debug
ISR_NO_ERROR_CODE 2     # NMI
ISR_NO_ERROR_CODE 3     # Breakpoint
ISR_NO_ERROR_CODE 4     # Overflow
ISR_NO_ERROR_CODE 5     # Bound range exceeded
ISR_NO_ERROR_CODE 6     # Invalid opcode
ISR_NO_ERROR_CODE 7     # Device not available
ISR_ERROR_CODE 8        # Double fault
ISR_NO_ERROR_CODE 9     # Coprocessor segment overrun
ISR_ERROR_CODE 10       # Invalid TSS
ISR_ERROR_CODE 11       # Segment not present
ISR_ERROR_CODE 12       # Stack segment fault
ISR_ERROR_CODE 13       # General protection fault
ISR_ERROR_CODE 14       # Page fault
ISR_NO_ERROR_CODE 15    # Reserved
ISR_NO_ERROR_CODE 16    # x87 FPU error
ISR_ERROR_CODE 17       # Alignment check
ISR_NO_ERROR_CODE 18    # Machine check
ISR_NO_ERROR_CODE 19    # SIMD floating point
ISR_NO_ERROR_CODE 20    # Virtualization
ISR_NO_ERROR_CODE 21    # Reserved
ISR_NO_ERROR_CODE 22    # Reserved
ISR_NO_ERROR_CODE 23    # Reserved
ISR_NO_ERROR_CODE 24    # Reserved
ISR_NO_ERROR_CODE 25    # Reserved
ISR_NO_ERROR_CODE 26    # Reserved
ISR_NO_ERROR_CODE 27    # Reserved
ISR_NO_ERROR_CODE 28    # Reserved
ISR_NO_ERROR_CODE 29    # Reserved
ISR_ERROR_CODE 30       # Security exception
ISR_NO_ERROR_CODE 31    # Reserved

/* IRQ handlers (32-47) */
.irp num,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
ISR_NO_ERROR_CODE \num
.endr

/* System call handler (128) */
ISR_NO_ERROR_CODE 128

/* Common ISR handler */
isr_common:
    /* Save all registers */
    push    %rax
    push    %rbx
    push    %rcx
    push    %rdx
    push    %rsi
    push    %rdi
    push    %rbp
    push    %r8
    push    %r9
    push    %r10
    push    %r11
    push    %r12
    push    %r13
    push    %r14
    push    %r15

    /* Call C handler with pointer to registers */
    mov     %rsp, %rdi
    call    handle_exception

    /* Restore registers */
    pop     %r15
    pop     %r14
    pop     %r13
    pop     %r12
    pop     %r11
    pop     %r10
    pop     %r9
    pop     %r8
    pop     %rbp
    pop     %rdi
    pop     %rsi
    pop     %rdx
    pop     %rcx
    pop     %rbx
    pop     %rax

    /* Remove error code and interrupt number */
    add     $16, %rsp

    /* Return from interrupt */
    iretq
