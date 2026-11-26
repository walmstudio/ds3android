/* v1.1: desktop window systems (Linux port). The front end (linux/me1_linux.c, SDL2) creates the window and hands its
 * native handles to the renderers; the runtime itself does not link against X11, Wayland or SDL.
 *   me1_platform_set(kind, display, visual) - before the D3D9 device is created: the GLES backend opens its EGL display on
 *       this platform (EGL_PLATFORM_X11_KHR / EGL_PLATFORM_WAYLAND_KHR), the Vulkan renderer enables VK_KHR_xlib_surface /
 *       VK_KHR_wayland_surface. Without it (tests, servers) both stay surfaceless/headless as before.
 *   me1gl_set_window(Me1Window*) - the window (NULL: gone). The front end keeps the structure alive and updates w/h and
 *       bumps gen when the window is resized; the presenters pick that up (Wayland: wl_egl_window_resize; Vulkan: new
 *       swap chain).
 * On Android the window is an ANativeWindow* and none of this is used. */
#ifndef ME1_PLATFORM_H
#define ME1_PLATFORM_H
#include <stdint.h>
enum { ME1_WS_NONE = 0, ME1_WS_X11 = 1, ME1_WS_WAYLAND = 2 };
typedef struct Me1Window {
    int kind;                /* ME1_WS_X11 / ME1_WS_WAYLAND */
    void* display;           /* X11: Display*, Wayland: struct wl_display* */
    unsigned long xwindow;   /* X11: Window */
    void* surface;           /* Wayland: struct wl_surface* */
    _Atomic uint64_t drawable; /* gen:32, height:16, width:16; publish together */
} Me1Window;
static inline unsigned me1_width(uint64_t draw) { return draw & 0xffff; }
static inline unsigned me1_height(uint64_t draw) { return (draw >> 16) & 0xffff; }
static inline unsigned me1_generation(uint64_t draw) { return draw >> 32; }
void me1_platform_set(int kind, void* display, unsigned long x11_visual);
int  me1_platform_kind(void);
#endif
