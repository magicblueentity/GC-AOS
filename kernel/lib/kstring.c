/*
 * GC-AOS Kernel - String Operations Implementation
 * 
 * Minimal kernel-safe string functions
 */

#include "kernel/string.h"
#include "kernel/stdarg.h"
#include "printk.h"

/* Forward declaration */
static int kernel_vsnprintf(char *buf, size_t size, const char *fmt, va_list args);

/**
 * snprintf - Format string with size limit
 * 
 * Simple implementation without floating point support
 * Returns number of characters written (excluding null terminator)
 */
int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list args;
    int written;
    
    if (!buf || size == 0) {
        return 0;
    }
    
    va_start(args, fmt);
    written = kernel_vsnprintf(buf, size, fmt, args);
    va_end(args);
    
    /* Ensure null termination */
    if (written >= 0 && (size_t)written < size) {
        buf[written] = '\0';
    }
    
    return written;
}

/**
 * kernel_vsnprintf - Format string with va_list
 * 
 * Internal function for snprintf
 */
static int kernel_vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
    const char *p = fmt;
    char *buf_end = buf + size - 1;
    int written = 0;
    
    /* Simple format string processing */
    while (*p && (size_t)(buf_end - buf) > 0) {
        if (*p == '%') {
            p++;
            if (*p == 's') {
                const char *str = va_arg(args, const char *);
                while (*str && (size_t)(buf_end - buf) > 0) {
                    *buf++ = *str++;
                    written++;
                }
                p++;
            } else if (*p == 'd') {
                int num = va_arg(args, int);
                char num_buf[16];
                int num_len = 0;
                
                if (num == 0) {
                    num_buf[num_len++] = '0';
                } else {
                    while (num > 0 && (size_t)num_len < (size_t)(sizeof(num_buf) - 1)) {
                        num_buf[num_len++] = '0' + (num % 10);
                        num /= 10;
                    }
                }
                
                for (int i = 0; i < (size_t)num_len && (size_t)(buf_end - buf) > 0; i++) {
                    *buf++ = num_buf[i];
                    written++;
                }
                p++;
            } else if (*p == 'x') {
                unsigned int num = va_arg(args, unsigned int);
                char hex_chars[] = "0123456789ABCDEF";
                char num_buf[16];
                int num_len = 0;
                
                if (num == 0) {
                    num_buf[num_len++] = '0';
                } else {
                    while (num > 0 && (size_t)num_len < (size_t)(sizeof(num_buf) - 1)) {
                        num_buf[num_len++] = hex_chars[(num >> 12) & 0xF];
                        num >>= 4;
                    }
                }
                
                for (int i = 0; i < (size_t)num_len && (size_t)(buf_end - buf) > 0; i++) {
                    *buf++ = num_buf[i];
                    written++;
                }
                p++;
            } else if (*p == 'u') {
                unsigned int num = va_arg(args, unsigned int);
                char hex_chars[] = "0123456789ABCDEF";
                char num_buf[16];
                int num_len = 0;
                
                if (num == 0) {
                    num_buf[num_len++] = '0';
                } else {
                    while (num > 0 && (size_t)num_len < (size_t)(sizeof(num_buf) - 1)) {
                        num_buf[num_len++] = hex_chars[(num >> 12) & 0xF];
                        num >>= 4;
                    }
                }
                
                for (int i = 0; i < (size_t)num_len && (size_t)(buf_end - buf) > 0; i++) {
                    *buf++ = num_buf[i];
                    written++;
                }
                p++;
            } else {
                /* Copy literal character */
                *buf++ = *p++;
                written++;
            }
        } else {
            /* Copy literal character */
            *buf++ = *p++;
            written++;
        }
    }
    
    return written;
}
