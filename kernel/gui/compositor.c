/*
 * GC-AOS - Compositor Implementation
 */

#include "gui/compositor.h"
#include "gui/window_manager.h"
#include "mm/kmalloc.h"
#include "printk.h"
#include <string.h>

/* Global compositor state */
compositor_t g_compositor;

/* ===================================================================== */
/* Internal Helpers */
/* ===================================================================== */

static compositor_buffer_t *alloc_buffer(void)
{
    compositor_buffer_t *buf = kmalloc(sizeof(compositor_buffer_t));
    if (!buf) {
        return NULL;
    }
    memset(buf, 0, sizeof(compositor_buffer_t));
    return buf;
}

static void free_buffer(compositor_buffer_t *buf)
{
    if (buf) {
        if (buf->pixels) {
            kfree_pages(buf->pixels, (buf->width * buf->height * 4 + PAGE_SIZE - 1) / PAGE_SIZE);
        }
        kfree(buf);
    }
}

static void allocate_buffer_pixels(compositor_buffer_t *buf, uint32_t width, uint32_t height)
{
    size_t size = width * height * 4; /* 32-bit ARGB */
    buf->pixels = kmalloc_aligned(size, PAGE_SIZE);
    if (buf->pixels) {
        memset(buf->pixels, 0, size);
        buf->width = width;
        buf->height = height;
        buf->pitch = width * 4;
    }
}

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

void compositor_init(void *screen_buffer, uint32_t width, uint32_t height,
                     uint32_t pitch, uint32_t format)
{
    printk(KERN_INFO "COMP: Initializing compositor\n");
    
    memset(&g_compositor, 0, sizeof(g_compositor));
    spin_lock_init(&g_compositor.dirty_lock);
    
    g_compositor.screen_buffer = screen_buffer;
    g_compositor.screen_width = width;
    g_compositor.screen_height = height;
    g_compositor.screen_pitch = pitch;
    g_compositor.enabled = 1;
    g_compositor.fps_limit = COMPOSITOR_FPS_LIMIT;
    g_compositor.frame_duration_ns = COMPOSITOR_FPS_INTERVAL;
    g_compositor.last_frame_time = get_time_ns();
    
    printk(KERN_INFO "COMP: Compositor initialized (%dx%d, format=%d)\n",
           width, height, format);
}

void compositor_shutdown(void)
{
    g_compositor.enabled = 0;
    
    /* Destroy all buffers */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_compositor.window_buffers[i]) {
            compositor_destroy_buffer(g_compositor.window_buffers[i]);
            g_compositor.window_buffers[i] = NULL;
        }
    }
    
    printk(KERN_INFO "COMP: Compositor shutdown\n");
}

/* ===================================================================== */
/* Buffer Management */
/* ===================================================================== */

compositor_buffer_t *compositor_create_buffer(uint32_t width, uint32_t height,
                                              window_t *window)
{
    if (width == 0 || height == 0 || width > 4096 || height > 2160) {
        printk(KERN_ERR "COMP: Invalid buffer size %dx%d\n", width, height);
        return NULL;
    }
    
    compositor_buffer_t *buf = alloc_buffer();
    if (!buf) {
        printk(KERN_ERR "COMP: Failed to allocate buffer struct\n");
        return NULL;
    }
    
    allocate_buffer_pixels(buf, width, height);
    if (!buf->pixels) {
        printk(KERN_ERR "COMP: Failed to allocate buffer pixels\n");
        free_buffer(buf);
        return NULL;
    }
    
    buf->window = window;
    buf->attached = (window != NULL);
    buf->format = 0; /* ARGB8888 */
    
    /* Add to buffer table */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_compositor.window_buffers[i] == NULL) {
            g_compositor.window_buffers[i] = buf;
            g_compositor.buffer_count++;
            break;
        }
    }
    
    printk(KERN_INFO "COMP: Created buffer %dx%d for window %d\n",
           width, height, window ? window->id : -1);
    
    return buf;
}

void compositor_destroy_buffer(compositor_buffer_t *buffer)
{
    if (!buffer) {
        return;
    }
    
    /* Remove from table */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_compositor.window_buffers[i] == buffer) {
            g_compositor.window_buffers[i] = NULL;
            g_compositor.buffer_count--;
            break;
        }
    }
    
    if (buffer->window) {
        buffer->window->backbuffer = NULL;
    }
    
    free_buffer(buffer);
}

int compositor_resize_buffer(compositor_buffer_t *buffer, uint32_t width, uint32_t height)
{
    if (!buffer || width == 0 || height == 0) {
        return -1;
    }
    
    /* Free old pixels */
    if (buffer->pixels) {
        kfree_pages(buffer->pixels, 
                   (buffer->width * buffer->height * 4 + PAGE_SIZE - 1) / PAGE_SIZE);
    }
    
    /* Allocate new pixels */
    allocate_buffer_pixels(buffer, width, height);
    if (!buffer->pixels) {
        return -2;
    }
    
    return 0;
}

int compositor_attach_buffer(compositor_buffer_t *buffer, window_t *window)
{
    if (!buffer || !window) {
        return -1;
    }
    
    /* Detach from old window if any */
    if (buffer->window && buffer->window != window) {
        buffer->window->backbuffer = NULL;
    }
    
    /* Attach to new window */
    buffer->window = window;
    buffer->attached = 1;
    window->backbuffer = buffer->pixels;
    window->buffer_pitch = buffer->pitch;
    
    return 0;
}

void compositor_detach_buffer(compositor_buffer_t *buffer)
{
    if (!buffer) {
        return;
    }
    
    if (buffer->window) {
        buffer->window->backbuffer = NULL;
        buffer->window->buffer_pitch = 0;
    }
    
    buffer->window = NULL;
    buffer->attached = 0;
}

/* ===================================================================== */
/* Core Compositing */
/* ===================================================================== */

void compositor_frame(void)
{
    if (!g_compositor.enabled) {
        return;
    }
    
    /* Check FPS limit */
    if (!compositor_should_render()) {
        g_compositor.skipped_frames++;
        return;
    }
    
    uint64_t frame_start = get_time_ns();
    
    /* Clear screen if full redraw */
    if (g_compositor.dirty_region.full_redraw) {
        memset(g_compositor.screen_buffer, 0, 
               g_compositor.screen_height * g_compositor.screen_pitch);
    }
    
    /* Composite all windows from back to front */
    window_t *win;
    list_for_each_entry_reverse(win, &g_window_manager.z_ordered, z_list) {
        if (win->visible && win->state == WINDOW_STATE_NORMAL) {
            compositor_render_window(win, g_compositor.screen_buffer);
        }
    }
    
    /* Clear dirty region */
    compositor_clear_dirty();
    
    /* Update frame timing */
    g_compositor.last_frame_time = frame_start;
    g_compositor.total_frames++;
    g_complicator.frame_count++;
}

int compositor_render_window(window_t *win, void *target)
{
    if (!win || !target) {
        return -1;
    }
    
    /* Get window buffer */
    void *src = win->backbuffer;
    if (!src) {
        return -2; /* No backbuffer */
    }
    
    /* Calculate window position including decorations */
    int win_x = win->x;
    int win_y = win->y;
    int client_x = win_x + (win->has_border ? WINDOW_BORDER_WIDTH : 0);
    int client_y = win_y + (win->has_titlebar ? WINDOW_TITLEBAR_HEIGHT : 0);
    
    /* Render titlebar if present */
    if (win->has_titlebar) {
        compositor_fill_rect(target, win_x, win_y, 
                            win->width + (win->has_border ? 2 : 0) * WINDOW_BORDER_WIDTH,
                            WINDOW_TITLEBAR_HEIGHT,
                            win->focused ? THEME_TITLEBAR : THEME_TITLEBAR_INACTIVE,
                            g_compositor.screen_pitch);
        
        /* Draw traffic light buttons */
        int btn_y = win_y + 6;
        int close_x = win_x + 12;
        int min_x = win_x + 32;
        int max_x = win_x + 52;
        
        compositor_fill_rect(target, close_x, btn_y, 12, 12, COLOR_BTN_CLOSE,
                            g_compositor.screen_pitch);
        if (win->flags & WINDOW_FLAG_MINIMIZABLE) {
            compositor_fill_rect(target, min_x, btn_y, 12, 12, COLOR_BTN_MINIMIZE,
                                g_compositor.screen_pitch);
        }
        if (win->flags & WINDOW_FLAG_MAXIMIZABLE) {
            compositor_fill_rect(target, max_x, btn_y, 12, 12, COLOR_BTN_ZOOM,
                                g_compositor.screen_pitch);
        }
    }
    
    /* Render border if present */
    if (win->has_border) {
        uint32_t border_color = win->focused ? THEME_BORDER : THEME_DARK_GRAY;
        int total_w = win->width + 2 * WINDOW_BORDER_WIDTH;
        int total_h = win->height + WINDOW_BORDER_WIDTH + 
                     (win->has_titlebar ? WINDOW_TITLEBAR_HEIGHT : 0);
        
        /* Top border (under titlebar) */
        compositor_fill_rect(target, win_x, win_y + WINDOW_TITLEBAR_HEIGHT,
                            total_w, WINDOW_BORDER_WIDTH,
                            border_color, g_compositor.screen_pitch);
        /* Bottom border */
        compositor_fill_rect(target, win_x, win_y + total_h - WINDOW_BORDER_WIDTH,
                            total_w, WINDOW_BORDER_WIDTH,
                            border_color, g_compositor.screen_pitch);
        /* Left border */
        compositor_fill_rect(target, win_x, win_y, WINDOW_BORDER_WIDTH, total_h,
                            border_color, g_compositor.screen_pitch);
        /* Right border */
        compositor_fill_rect(target, win_x + total_w - WINDOW_BORDER_WIDTH, win_y,
                            WINDOW_BORDER_WIDTH, total_h,
                            border_color, g_compositor.screen_pitch);
    }
    
    /* Composite client area */
    compositor_blend_window(win, src, target, client_x, client_y);
    
    return 0;
}

int compositor_blend_window(window_t *win, void *src, void *dst,
                            int dst_x, int dst_y)
{
    if (!win || !src || !dst) {
        return -1;
    }
    
    int width = win->width;
    int height = win->height;
    
    /* Clip to screen bounds */
    if (dst_x < 0) {
        src = (void *)((uint8_t *)src + (-dst_x) * 4);
        width += dst_x;
        dst_x = 0;
    }
    if (dst_y < 0) {
        src = (void *)((uint8_t *)src + (-dst_y) * win->buffer_pitch);
        height += dst_y;
        dst_y = 0;
    }
    if (dst_x + width > (int)g_compositor.screen_width) {
        width = g_compositor.screen_width - dst_x;
    }
    if (dst_y + height > (int)g_compositor.screen_height) {
        height = g_compositor.screen_height - dst_y;
    }
    
    if (width <= 0 || height <= 0) {
        return 0; /* Nothing to draw */
    }
    
    /* Blend pixels */
    compositor_blend(dst, src, dst_x, dst_y, width, height,
                     win->buffer_pitch, g_compositor.screen_pitch, 255);
    
    g_compositor.pixels_composited += width * height;
    
    return 0;
}

/* ===================================================================== */
/* Dirty Region Management */
/* ===================================================================== */

void compositor_mark_dirty(int x, int y, int width, int height)
{
    spin_lock(&g_compositor.dirty_lock);
    
    /* Clamp to screen */
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > (int)g_compositor.screen_width) {
        width = g_compositor.screen_width - x;
    }
    if (y + height > (int)g_compositor.screen_height) {
        height = g_compositor.screen_height - y;
    }
    
    if (width <= 0 || height <= 0) {
        spin_unlock(&g_compositor.dirty_lock);
        return;
    }
    
    /* Check if we need full redraw */
    if (g_compositor.dirty_region.count >= COMPOSITOR_MAX_DIRTY_RECTS) {
        g_compositor.dirty_region.full_redraw = 1;
        spin_unlock(&g_compositor.dirty_lock);
        return;
    }
    
    /* Check for overlap with existing rects and merge if possible */
    for (int i = 0; i < g_compositor.dirty_region.count; i++) {
        dirty_rect_t *existing = &g_complicator.dirty_region.rects[i];
        
        /* Check if new rect is contained in existing */
        if (x >= existing->x && y >= existing->y &&
            x + width <= existing->x + existing->width &&
            y + height <= existing->y + existing->height) {
            spin_unlock(&g_compositor.dirty_lock);
            return; /* Already covered */
        }
        
        /* Check if existing is contained in new */
        if (existing->x >= x && existing->y >= y &&
            existing->x + existing->width <= x + width &&
            existing->y + existing->height <= y + height) {
            /* Replace existing with new */
            existing->x = x;
            existing->y = y;
            existing->width = width;
            existing->height = height;
            spin_unlock(&g_compositor.dirty_lock);
            return;
        }
    }
    
    /* Add new dirty rect */
    dirty_rect_t *rect = &g_compositor.dirty_region.rects[g_compositor.dirty_region.count++];
    rect->x = x;
    rect->y = y;
    rect->width = width;
    rect->height = height;
    
    spin_unlock(&g_compositor.dirty_lock);
}

void compositor_mark_window_dirty(window_t *win)
{
    if (!win) {
        return;
    }
    
    int total_w = win->width + (win->has_border ? 2 : 0) * WINDOW_BORDER_WIDTH;
    int total_h = win->height + (win->has_titlebar ? WINDOW_TITLEBAR_HEIGHT : 0) +
                  (win->has_border ? WINDOW_BORDER_WIDTH : 0);
    
    compositor_mark_dirty(win->x, win->y, total_w, total_h);
}

void compositor_mark_full_redraw(void)
{
    spin_lock(&g_compositor.dirty_lock);
    g_compositor.dirty_region.full_redraw = 1;
    spin_unlock(&g_compositor.dirty_lock);
}

void compositor_clear_dirty(void)
{
    spin_lock(&g_compositor.dirty_lock);
    g_compositor.dirty_region.count = 0;
    g_compositor.dirty_region.full_redraw = 0;
    spin_unlock(&g_compositor.dirty_lock);
}

dirty_region_t *compositor_get_dirty_region(void)
{
    return &g_compositor.dirty_region;
}

void compositor_optimize_dirty(void)
{
    /* Merge overlapping rectangles to reduce draw calls */
    spin_lock(&g_compositor.dirty_lock);
    
    int i = 0;
    while (i < g_compositor.dirty_region.count) {
        dirty_rect_t *a = &g_complicator.dirty_region.rects[i];
        int merged = 0;
        
        for (int j = i + 1; j < g_compositor.dirty_region.count; j++) {
            dirty_rect_t *b = &g_complicator.dirty_region.rects[j];
            
            /* Check if rectangles overlap or are adjacent */
            if (a->x <= b->x + b->width && a->x + a->width >= b->x &&
                a->y <= b->y + b->height && a->y + a->height >= b->y) {
                
                /* Merge b into a */
                int x2 = a->x + a->width;
                int y2 = a->y + a->height;
                int bx2 = b->x + b->width;
                int by2 = b->y + b->height;
                
                if (b->x < a->x) a->x = b->x;
                if (b->y < a->y) a->y = b->y;
                if (bx2 > x2) a->width = bx2 - a->x;
                if (by2 > y2) a->height = by2 - a->y;
                
                /* Remove b by shifting remaining rects */
                memmove(b, b + 1, 
                       (g_compositor.dirty_region.count - j - 1) * sizeof(dirty_rect_t));
                g_compositor.dirty_region.count--;
                merged = 1;
                break;
            }
        }
        
        if (!merged) {
            i++;
        }
    }
    
    spin_unlock(&g_compositor.dirty_lock);
}

/* ===================================================================== */
/* Frame Timing */
/* ===================================================================== */

int compositor_should_render(void)
{
    uint64_t now = get_time_ns();
    uint64_t elapsed = now - g_compositor.last_frame_time;
    
    return elapsed >= g_compositor.frame_duration_ns;
}

void compositor_wait_for_frame(void)
{
    uint64_t now = get_time_ns();
    uint64_t elapsed = now - g_compositor.last_frame_time;
    
    if (elapsed < g_compositor.frame_duration_ns) {
        uint64_t sleep_ns = g_compositor.frame_duration_ns - elapsed;
        process_sleep(NULL, sleep_ns);
    }
}

void compositor_set_fps_limit(int fps)
{
    if (fps <= 0) {
        g_compositor.fps_limit = 0;
        g_compositor.frame_duration_ns = 0;
    } else {
        g_compositor.fps_limit = fps;
        g_compositor.frame_duration_ns = 1000000000ULL / fps;
    }
}

/* ===================================================================== */
/* Blending & Drawing */
/* ===================================================================== */

void compositor_blend(void *dst, void *src, int x, int y,
                      int width, int height,
                      int src_pitch, int dst_pitch, uint8_t alpha)
{
    if (!dst || !src) {
        return;
    }
    
    uint32_t *dst_pixels = (uint32_t *)((uint8_t *)dst + y * dst_pitch + x * 4);
    uint32_t *src_pixels = (uint32_t *)src;
    
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            uint32_t src_pixel = src_pixels[col];
            uint32_t dst_pixel = dst_pixels[col];
            
            /* Simple alpha blend (assuming premultiplied or straight alpha) */
            uint8_t src_a = (src_pixel >> 24) & 0xFF;
            src_a = (src_a * alpha) / 255;
            
            if (src_a == 255) {
                dst_pixels[col] = src_pixel;
            } else if (src_a > 0) {
                uint8_t dst_a = (dst_pixel >> 24) & 0xFF;
                uint8_t src_r = (src_pixel >> 16) & 0xFF;
                uint8_t src_g = (src_pixel >> 8) & 0xFF;
                uint8_t src_b = src_pixel & 0xFF;
                uint8_t dst_r = (dst_pixel >> 16) & 0xFF;
                uint8_t dst_g = (dst_pixel >> 8) & 0xFF;
                uint8_t dst_b = dst_pixel & 0xFF;
                
                uint8_t out_a = src_a + dst_a * (255 - src_a) / 255;
                uint8_t out_r = (src_r * src_a + dst_r * dst_a * (255 - src_a) / 255) / out_a;
                uint8_t out_g = (src_g * src_a + dst_g * dst_a * (255 - src_a) / 255) / out_a;
                uint8_t out_b = (src_b * src_a + dst_b * dst_a * (255 - src_a) / 255) / out_a;
                
                dst_pixels[col] = (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
            }
            /* If src_a == 0, keep dst unchanged */
        }
        
        dst_pixels = (uint32_t *)((uint8_t *)dst_pixels + dst_pitch);
        src_pixels = (uint32_t *)((uint8_t *)src_pixels + src_pitch);
    }
}

void compositor_fill_rect(void *buffer, int x, int y,
                          int width, int height,
                          uint32_t color, int pitch)
{
    if (!buffer || x < 0 || y < 0 || width <= 0 || height <= 0) {
        return;
    }
    
    uint32_t *pixels = (uint32_t *)((uint8_t *)buffer + y * pitch + x * 4);
    
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            pixels[col] = color;
        }
        pixels = (uint32_t *)((uint8_t *)pixels + pitch);
    }
}

void compositor_copy_rect(void *dst, void *src, int x, int y,
                          int width, int height,
                          int src_pitch, int dst_pitch)
{
    if (!dst || !src) {
        return;
    }
    
    uint8_t *dst_pixels = (uint8_t *)dst + y * dst_pitch + x * 4;
    uint8_t *src_pixels = (uint8_t *)src;
    
    for (int row = 0; row < height; row++) {
        memcpy(dst_pixels, src_pixels, width * 4);
        dst_pixels += dst_pitch;
        src_pixels += src_pitch;
    }
}

/* ===================================================================== */
/* Stats & Debug */
/* ===================================================================== */

void compositor_get_stats(uint64_t *total_frames, uint64_t *skipped_frames,
                          float *avg_fps)
{
    if (total_frames) {
        *total_frames = g_compositor.total_frames;
    }
    if (skipped_frames) {
        *skipped_frames = g_compositor.skipped_frames;
    }
    if (avg_fps) {
        uint64_t elapsed = get_time_ns() - g_compositor.last_frame_time;
        if (elapsed > 0) {
            *avg_fps = (float)(g_compositor.total_frames * 1000000000ULL) / elapsed;
        } else {
            *avg_fps = 0;
        }
    }
}

void compositor_dump_stats(void)
{
    uint64_t total, skipped;
    float fps;
    
    compositor_get_stats(&total, &skipped, &fps);
    
    printk(KERN_INFO "=== Compositor Stats ===\n");
    printk(KERN_INFO "Total frames: %llu\n", total);
    printk(KERN_INFO "Skipped: %llu (%.1f%%)\n", skipped, 
           total > 0 ? (skipped * 100.0f / total) : 0);
    printk(KERN_INFO "FPS: %.1f (limit: %d)\n", fps, g_compositor.fps_limit);
    printk(KERN_INFO "Pixels composited: %llu\n", g_compositor.pixels_composited);
    printk(KERN_INFO "Buffers: %d\n", g_compositor.buffer_count);
}

void compositor_show_fps(int show)
{
    g_compositor.show_fps = show;
}
