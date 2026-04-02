// Simple test to check window.c structure
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// Mock the required types and functions
struct window { char title[64]; };
void gui_draw_rect(int x, int y, int w, int h, uint32_t color) {}
void gui_draw_string(int x, int y, const char *str, uint32_t fg, uint32_t bg) {}
void gui_end_surface(void) {}
void gui_blit_window_surface(struct window *win) {}

// Test the basic structure
static void draw_window(struct window *win) {
    int content_x = 0, content_y = 0, content_w = 100, content_h = 100;
    
    if (win->title[0] == 'C' && win->title[1] == 'a' && win->title[2] == 'l') {
        gui_draw_rect(content_x, content_y, content_w, content_h, 0x1C1C1E);
    }
    else if (win->title[0] == 'T' && win->title[1] == 'e' && win->title[2] == 'r') {
        gui_draw_rect(content_x, content_y, content_w, content_h, 0x1E1E2E);
    }
    
    gui_end_surface();
    gui_blit_window_surface(win);
}

int main() {
    struct window win = {"Calculator"};
    draw_window(&win);
    return 0;
}
