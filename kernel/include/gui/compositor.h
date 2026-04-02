/*
 * GC-AOS - Compositor
 * 
 * Composites windows from offscreen buffers to final screen.
 * Features: dirty rectangle tracking, partial redraw, FPS limiting.
 */

#ifndef _COMPOSITOR_H
#define _COMPOSITOR_H

#include "types.h"
#include "gui/window_manager.h"
#include "sync/spinlock.h"

/* ===================================================================== */
/* Configuration */
/* ===================================================================== */

#define COMPOSITOR_MAX_DIRTY_RECTS  64
#define COMPOSITOR_FPS_LIMIT        60
#define COMPOSITOR_FPS_INTERVAL     (1000000000ULL / COMPOSITOR_FPS_LIMIT) /* ~16.6ms */
#define COMPOSITOR_BUFFER_COUNT     2   /* Double buffering */

/* ===================================================================== */
/* Dirty Rectangle Tracking */
/* ===================================================================== */

typedef struct {
    int x, y;
    int width, height;
} dirty_rect_t;

typedef struct {
    dirty_rect_t rects[COMPOSITOR_MAX_DIRTY_RECTS];
    int count;
    int full_redraw;  /* Flag for full screen redraw */
} dirty_region_t;

/* ===================================================================== */
/* Compositor Buffer */
/* ===================================================================== */

typedef struct {
    void *pixels;           /* Pixel data */
    uint32_t width;         /* Buffer width */
    uint32_t height;        /* Buffer height */
    uint32_t pitch;         /* Bytes per line */
    uint32_t format;        /* Pixel format */
    int attached;           /* Attached to window */
    window_t *window;       /* Attached window */
} compositor_buffer_t;

/* ===================================================================== */
/* Compositor State */
/* ===================================================================== */

typedef struct {
    /* Screen info */
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t screen_pitch;
    void *screen_buffer;    /* Final framebuffer */
    
    /* Offscreen buffers for windows */
    compositor_buffer_t *window_buffers[MAX_WINDOWS];
    int buffer_count;
    
    /* Dirty region tracking */
    dirty_region_t dirty_region;
    spinlock_t dirty_lock;
    
    /* Frame timing */
    uint64_t last_frame_time;
    uint64_t frame_count;
    uint64_t frame_duration_ns;
    
    /* Stats */
    uint64_t total_frames;
    uint64_t skipped_frames;
    uint64_t pixels_composited;
    
    /* Settings */
    int enabled;
    int vsync_enabled;
    int fps_limit;
    int show_fps;
} compositor_t;

extern compositor_t g_compositor;

/* ===================================================================== */
/* Compositor API */
/* ===================================================================== */

/**
 * compositor_init - Initialize compositor
 * @screen_buffer: Pointer to framebuffer
 * @width: Screen width
 * @height: Screen height
 * @pitch: Bytes per line
 * @format: Pixel format
 */
void compositor_init(void *screen_buffer, uint32_t width, uint32_t height,
                     uint32_t pitch, uint32_t format);

/**
 * compositor_shutdown - Shut down compositor
 */
void compositor_shutdown(void);

/**
 * compositor_create_buffer - Create offscreen buffer for window
 * @width: Buffer width
 * @height: Buffer height
 * @window: Window to attach (can be NULL)
 * 
 * Return: Buffer pointer, or NULL on failure
 */
compositor_buffer_t *compositor_create_buffer(uint32_t width, uint32_t height,
                                              window_t *window);

/**
 * compositor_destroy_buffer - Destroy offscreen buffer
 * @buffer: Buffer to destroy
 */
void compositor_destroy_buffer(compositor_buffer_t *buffer);

/**
 * compositor_resize_buffer - Resize buffer
 * @buffer: Buffer to resize
 * @width: New width
 * @height: New height
 * 
 * Return: 0 on success, negative on failure
 */
int compositor_resize_buffer(compositor_buffer_t *buffer, 
                             uint32_t width, uint32_t height);

/**
 * compositor_attach_buffer - Attach buffer to window
 * @buffer: Buffer to attach
 * @window: Window to attach to
 * 
 * Return: 0 on success, negative on failure
 */
int compositor_attach_buffer(compositor_buffer_t *buffer, window_t *window);

/**
 * compositor_detach_buffer - Detach buffer from window
 * @buffer: Buffer to detach
 */
void compositor_detach_buffer(compositor_buffer_t *buffer);

/* ===================================================================== */
/* Compositing */
/* ===================================================================== */

/**
 * compositor_frame - Composite a frame
 * 
 * This is the main compositing function. It:
 * 1. Checks if enough time has passed (FPS limit)
 * 2. Composites all windows to screen buffer
 * 3. Handles dirty rectangle optimization
 * 4. Swaps buffers if double buffered
 */
void compositor_frame(void);

/**
 * compositor_render_window - Render a single window
 * @win: Window to render
 * @target: Target buffer (screen or intermediate)
 * 
 * Return: 0 on success, negative on failure
 */
int compositor_render_window(window_t *win, void *target);

/**
 * compositor_blend_window - Blend window onto target
 * @win: Window to blend
 * @src: Source buffer (window backbuffer)
 * @dst: Destination buffer
 * @dst_x, @dst_y: Position in destination
 * 
 * Return: 0 on success, negative on failure
 */
int compositor_blend_window(window_t *win, void *src, void *dst,
                            int dst_x, int dst_y);

/* ===================================================================== */
/* Dirty Region Management */
/* ===================================================================== */

/**
 * compositor_mark_dirty - Mark region as dirty
 * @x, @y: Position
 * @width, @height: Size
 */
void compositor_mark_dirty(int x, int y, int width, int height);

/**
 * compositor_mark_window_dirty - Mark window as dirty
 * @win: Window to mark
 */
void compositor_mark_window_dirty(window_t *win);

/**
 * compositor_mark_full_redraw - Mark entire screen for redraw
 */
void compositor_mark_full_redraw(void);

/**
 * compositor_clear_dirty - Clear dirty region
 */
void compositor_clear_dirty(void);

/**
 * compositor_get_dirty_region - Get current dirty region
 * 
 * Return: Pointer to dirty region
 */
dirty_region_t *compositor_get_dirty_region(void);

/**
 * compositor_optimize_dirty - Merge overlapping dirty rects
 */
void compositor_optimize_dirty(void);

/* ===================================================================== */
/* Frame Timing */
/* ===================================================================== */

/**
 * compositor_should_render - Check if it's time to render
 * 
 * Return: 1 if should render, 0 if should skip
 */
int compositor_should_render(void);

/**
 * compositor_wait_for_frame - Wait until next frame time
 */
void compositor_wait_for_frame(void);

/**
 * compositor_set_fps_limit - Change FPS limit
 * @fps: New FPS limit (0 = unlimited)
 */
void compositor_set_fps_limit(int fps);

/* ===================================================================== */
/* Blending & Effects */
/* ===================================================================== */

/**
 * compositor_blend - Blend two pixel buffers
 * @dst: Destination buffer
 * @src: Source buffer
 * @x, @y: Position in destination
 * @width, @height: Size to blend
 * @src_pitch: Source pitch
 * @dst_pitch: Destination pitch
 * @alpha: Global alpha (0-255)
 */
void compositor_blend(void *dst, void *src, int x, int y,
                      int width, int height,
                      int src_pitch, int dst_pitch, uint8_t alpha);

/**
 * compositor_fill_rect - Fill rectangle with color
 * @buffer: Target buffer
 * @x, @y: Position
 * @width, @height: Size
 * @color: ARGB color
 * @pitch: Buffer pitch
 */
void compositor_fill_rect(void *buffer, int x, int y,
                          int width, int height,
                          uint32_t color, int pitch);

/**
 * compositor_copy_rect - Copy rectangle between buffers
 * @dst: Destination buffer
 * @src: Source buffer
 * @x, @y: Position in destination
 * @width, @height: Size
 * @src_pitch: Source pitch
 * @dst_pitch: Destination pitch
 */
void compositor_copy_rect(void *dst, void *src, int x, int y,
                          int width, int height,
                          int src_pitch, int dst_pitch);

/* ===================================================================== */
/* Stats & Debug */
/* ===================================================================== */

/**
 * compositor_get_stats - Get compositor statistics
 * @total_frames: Output for total frames rendered
 * @skipped_frames: Output for skipped frames
 * @avg_fps: Output for average FPS
 */
void compositor_get_stats(uint64_t *total_frames, uint64_t *skipped_frames,
                          float *avg_fps);

/**
 * compositor_dump_stats - Print compositor stats
 */
void compositor_dump_stats(void);

/**
 * compositor_show_fps - Enable/disable FPS overlay
 * @show: 1 to show, 0 to hide
 */
void compositor_show_fps(int show);

#endif /* _COMPOSITOR_H */
