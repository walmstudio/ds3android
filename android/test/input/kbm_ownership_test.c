/* Execute the production JNI handlers and observe their effective guest-facing edges. */
#include <assert.h>
#include "../../../android/jni/game_jni.c"

static int mouse, key, mouse_edges, key_edges, frame = 1;
static int last_button, last_vk, last_dik, last_down, positioned;
int me1gl_get_view(int *x, int *y, int *w, int *h, int *fw, int *fh) {
    *x = *y = 0; *w = *fw = 640; *h = *fh = 360; return frame;
}
void hle_ui_mouse_button(int button) {
    last_button = button;
    if (button == 1 || button == 2) { mouse = button == 1; ++mouse_edges; }
}
void hle_ui_pointer(int x, int y, int w, int h, int button) { ++positioned; hle_ui_mouse_button(button); }
void hle_ui_key(int vk, int dik, int down, int uni) {
    last_vk = vk; last_dik = dik; last_down = down;
    if (vk == 'E') { key = down; ++key_edges; }
}
void hle_ui_wheel(int by) {}
void hle_al_set_paused(int paused) {}

#define KEY(source, down) FN(nativeKeyFrom)(NULL, NULL, source, 33, down, 0)
#define BUTTON(source, down) FN(nativeMouseButtonFrom)(NULL, NULL, source, 0, down)
#define POINT(source, action) FN(nativePointer)(NULL, NULL, source, action, 100, 100)
#define ACTIVE(on) FN(nativeActive)(NULL, NULL, on)

static void test_pointer(void) {
    BUTTON(KBM_PAD, 1);
    POINT(KBM_POINTER, 0);
    POINT(KBM_POINTER, 1);
    assert(mouse && mouse_edges == 1 && "touch up must not release held RT");
    POINT(KBM_POINTER, 0);
    frame = 0;
    POINT(KBM_POINTER, 1);
    assert(mouse && mouse_edges == 1 && "touch cancellation with no frame must not release RT");
    frame = 1;
    BUTTON(KBM_PAD, 0);
    assert(!mouse && mouse_edges == 2);
    POINT(KBM_POINTER, 0);
    frame = 0;
    POINT(KBM_POINTER, 1);
    assert(!mouse && mouse_edges == 4 && "last touch owner still releases without frame");
}
static void test_bindings(void) {
    BUTTON(KBM_PAD, 1); BUTTON(KBM_OVERLAY, 1); BUTTON(KBM_OVERLAY, 0);
    assert(mouse && mouse_edges == 1 && "touch LMB release must not release held RT");
    BUTTON(KBM_OVERLAY, 1); BUTTON(KBM_PAD, 0);
    assert(mouse && mouse_edges == 1 && "pad removal must not release held touch LMB");
    BUTTON(KBM_OVERLAY, 0);
    assert(!mouse && mouse_edges == 2);
}
static void test_keys(void) {
    KEY(KBM_PAD, 1); KEY(KBM_OVERLAY, 1); KEY(KBM_OVERLAY, 0);
    assert(key && key_edges == 1 && "touch key release must not release held pad key");
    KEY(KBM_OVERLAY, 1); KEY(KBM_PAD, 0);
    assert(key && key_edges == 1 && "pad removal must not release held touch key");
    KEY(KBM_OVERLAY, 0);
    assert(!key && key_edges == 2);
}
static void test_hardware_cleanup(void) {
    KEY(KBM_DIRECT, 1); KEY(KBM_DIRECT, 1); // genuine keyboard repeat is still delivered
    KEY(KBM_PAD, 1); KEY(KBM_PAD, 0);
    assert(key && key_edges == 2);
    POINT(KBM_DIRECT, 0); POINT(KBM_POINTER, 0); POINT(KBM_POINTER, 1);
    assert(mouse && mouse_edges == 1);
    ACTIVE(0);
    assert(!mouse && !key && mouse_edges == 2 && key_edges == 3);
    ACTIVE(1);
    KEY(KBM_OVERLAY, 1); KEY(KBM_OVERLAY, 0);
    BUTTON(KBM_PAD, 1); BUTTON(KBM_PAD, 0);
    assert(!mouse && !key && mouse_edges == 4 && key_edges == 5);
}
static void test_layout1(void) {
    const int codes[][3] = {{51,'W',0x11},{29,'A',0x1e},{47,'S',0x1f},{32,'D',0x20},
                            {33,'E',0x12},{111,27,1},{62,' ',0x39},{61,9,0x0f}};
    for (unsigned i=0; i<sizeof codes/sizeof *codes; ++i) for (int down=1; down>=0; --down) {
        last_vk = last_dik = last_down = -1;
        FN(nativeKeyFrom)(NULL,NULL,KBM_OVERLAY,codes[i][0],down,0);
        assert(last_vk==codes[i][1] && last_dik==codes[i][2] && last_down==down);
    }
    for (int button=0; button<2; ++button) for (int down=1; down>=0; --down) {
        last_button = -1;
        FN(nativeMouseButtonFrom)(NULL,NULL,KBM_OVERLAY,button,down);
        assert(last_button==2*button+(down?1:2) && !positioned);
    }
    BUTTON(KBM_OVERLAY, 1);
    BUTTON(KBM_POINTER, 1); BUTTON(KBM_POINTER, 0);
    assert(mouse && !positioned);
    BUTTON(KBM_OVERLAY, 0);
    assert(!mouse);
}
int main(int argc, char **argv) {
    assert(argc == 2); ACTIVE(1);
    if (!strcmp(argv[1], "pointer")) test_pointer();
    else if (!strcmp(argv[1], "bindings")) test_bindings();
    else if (!strcmp(argv[1], "keys")) test_keys();
    else if (!strcmp(argv[1], "hardware_cleanup")) test_hardware_cleanup();
    else if (!strcmp(argv[1], "layout1")) test_layout1();
    else assert(0);
    printf("JNI KB/M %s: PASS\n", argv[1]);
}
