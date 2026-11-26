#include <assert.h>
#include "../../../runtime/hle_exe.c"

static int dx_seen, dy_seen, exclusive;
int hle_input_mouse_exclusive(void) { return exclusive; }
void hle_input_mouse(int dx, int dy, int dz, unsigned btn) { dx_seen = dx; dy_seen = dy; }
unsigned hle_d3d9_frames(void) { return 1; }

int main(void) {
    g_win[0] = (Win){.used=1, .hwnd=123, .w=1280, .h=720};
    g_active = 123;
    g_showcount = -1;  /* Windows cursor hidden, Android overlay can still show it. */
    g_curx = 600; g_cury = 300;
    hle_ui_mouse_rel(30, -20);
    assert(dx_seen == 30 && dy_seen == -20);
    assert(g_curx == 630 && g_cury == 280 && "unclipped touchpad must move guest pointer");
    assert(g_mq_head != g_mq_tail && g_mq[(g_mq_head-1) % MQ].msg == 0x0200);
    unsigned count = g_mq_head;
    exclusive = 1;
    hle_ui_mouse_rel(90, 10);
    assert(dx_seen == 90 && dy_seen == 10);
    assert(g_curx == 630 && g_cury == 280 && g_mq_head == count);
    exclusive = 0;
    g_cursor_clipped = 1;
    hle_ui_mouse_rel(11, 12);
    assert(dx_seen == 11 && dy_seen == 12 && g_curx == 630 && g_cury == 280 && g_mq_head == count);
    puts("PASS: touchpad moves unclipped pointer; exclusive look stays relative");
}
