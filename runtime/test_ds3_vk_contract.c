/* No guest, GPU or Android device: exercise the renderer's boundary helpers. */
#include "hle_d3d9.c"
#include <assert.h>

static int refs[2], presented, destroys;
static void retain(void *w) { ++*(int*)w; }
static void release_window(void *w) { assert(*(int*)w > 0); --*(int*)w; }
static void frame(void) { presented++; }
static VkResult negotiate_ok(uint32_t *v) { *v = 4; return VK_SUCCESS; }
static VkResult negotiate_bad(uint32_t *v) { *v = 4; return VK_ERROR_INCOMPATIBLE_DRIVER; }
static VkResult negotiate_bad_version(uint32_t *v) { *v = 6; return VK_SUCCESS; }
static VKAPI_ATTR void VKAPI_CALL destroy_device(VkDevice d, const VkAllocationCallbacks *a) { destroys++; }
static int waited, destroyed_replacement;
static VKAPI_ATTR VkResult VKAPI_CALL wait_idle(VkDevice d) { assert(d == (VkDevice)1); waited++; return VK_SUCCESS; }
static VKAPI_ATTR void VKAPI_CALL destroy_replacement(VkDevice d, const VkAllocationCallbacks *a) {
    assert(d == (VkDevice)2); destroyed_replacement++;
}
static VKAPI_ATTR void VKAPI_CALL unused_proc(void) {}
static const char *missing;
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL proc(VkDevice d, const char *name) {
    if (missing && !strcmp(name, missing)) return NULL;
    if (!strcmp(name, "vkDestroyDevice")) return d == (VkDevice)2 ?
        (PFN_vkVoidFunction)destroy_replacement : (PFN_vkVoidFunction)destroy_device;
    if (!strcmp(name, "vkDeviceWaitIdle")) return (PFN_vkVoidFunction)wait_idle;
    return unused_proc;
}
static int lock_result, post_result, size = 1, posts;
static uint8_t native_pixels[4], pixels[4] = { 12, 34, 56, 255 };
static int32_t geometry(void* w, int32_t x, int32_t y, int32_t f) { return 0; }
static int32_t dimension(void* w) { return size; }
static int32_t lock_window(void* w, AnwBuf* b, void* r) {
    if (lock_result) return lock_result;
    *b = (AnwBuf){ .width=1,.height=1,.stride=1,.bits=native_pixels }; return 0;
}
static int32_t post_window(void* w) { posts++; return post_result; }
static void cpu_failure_burst(VSlot* s, int lock_error, int count, int terminal) {
    int before = presented;
    lock_result = lock_error ? -1 : 0;
    post_result = lock_error ? 0 : -1;
    for (int i = 0; i < count; i++) {
        vcpu_post(s);
        assert(presented == before);
        assert(!!me1gl_present_failure() == (terminal && i == count - 1));
    }
    lock_result = post_result = 0;
}
static VKAPI_ATTR VkResult VKAPI_CALL end_command(VkCommandBuffer b) { return VK_SUCCESS; }
static VKAPI_ATTR VkResult VKAPI_CALL submit_lost(VkQueue q, uint32_t n, const VkSubmitInfo* s, VkFence f) { return VK_ERROR_DEVICE_LOST; }
static VKAPI_ATTR VkResult VKAPI_CALL acquire_lost(VkDevice d, VkSwapchainKHR sc, uint64_t timeout,
                                                 VkSemaphore sem, VkFence f, uint32_t* idx) {
    return VK_ERROR_DEVICE_LOST;
}

static void test_shim(void) {
    char* path = realpath("build/libds3_test_icd.so", NULL);
    assert(path);
    setenv("DS3_VK_DRIVER", path, 1);
    void* shim = vk_open_custom(path);
    assert(shim && dlsym(shim, "me1vk_set_icd"));
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(shim, "vkGetInstanceProcAddr");
    assert(gipa && !gipa(NULL, "vkEnumerateInstanceLayerProperties"));
    assert(!gipa(NULL, "vkGetDeviceProcAddr"));
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    VkInstance inst = VK_NULL_HANDLE;
    assert(create && create(&ici, NULL, &inst) == VK_SUCCESS && inst);
    PFN_vkGetDeviceProcAddr gdpa = (PFN_vkGetDeviceProcAddr)gipa(inst, "vkGetDeviceProcAddr");
    PFN_vkGetDeviceProcAddr shim_gdpa = (PFN_vkGetDeviceProcAddr)dlsym(shim, "vkGetDeviceProcAddr");
    assert(gdpa && gdpa == shim_gdpa);
    PFN_vkEnumeratePhysicalDevices enumerate = (PFN_vkEnumeratePhysicalDevices)gipa(inst, "vkEnumeratePhysicalDevices");
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t count = 1;
    assert(enumerate && enumerate(inst, &count, &physical) == VK_SUCCESS && count == 1 && physical);
    PFN_vkCreateDevice create_device = (PFN_vkCreateDevice)gipa(inst, "vkCreateDevice");
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    VkDevice device = VK_NULL_HANDLE;
    assert(create_device && create_device(physical, &dci, NULL, &device) == VK_SUCCESS && device);
    PFN_vkDeviceWaitIdle idle = (PFN_vkDeviceWaitIdle)gdpa(device, "vkDeviceWaitIdle");
    PFN_vkDestroyDevice destroy = (PFN_vkDestroyDevice)gdpa(device, "vkDestroyDevice");
    assert(idle && idle(device) == VK_SUCCESS && destroy);
    assert(!gdpa(device, "vkQueuePresentKHR"));
    destroy(device, NULL);
    PFN_vkDestroyInstance drop = (PFN_vkDestroyInstance)gipa(inst, "vkDestroyInstance");
    assert(drop); drop(inst, NULL);
    dlclose(shim);
    unsetenv("DS3_VK_DRIVER");
    free(path);
}

int main(int argc, char** argv) {
    int recovery = argc == 2 && !strcmp(argv[1], "--recovery");
    if (recovery) setenv("ME1_VK_LOSTRESET", "1", 1);
    else unsetenv("ME1_VK_LOSTRESET");
    void* compiler = open_renderer_helper("libme1spv.so");
    assert(compiler && dlsym(compiler,"me1spv_compile")); dlclose(compiler);
    uint32_t v;
    assert(vk_direct_negotiate(negotiate_ok, &v) == VK_SUCCESS && v == 4);
    assert(vk_direct_negotiate(negotiate_bad, &v) == VK_ERROR_INCOMPATIBLE_DRIVER);
    assert(vk_direct_negotiate(negotiate_bad_version, &v) == VK_ERROR_INCOMPATIBLE_DRIVER);
    assert(vk_direct_negotiate(NULL, &v) == VK_ERROR_INCOMPATIBLE_DRIVER);
    test_shim();

    vkGetDeviceProcAddr = proc;
    VD = (VkDevice)1; missing = "vkDeviceWaitIdle";
    assert(!vk_device_dispatch());
    vb_teardown(); assert(!VD && destroys == 1);
    VD = (VkDevice)1; missing = NULL; VX.swapext = 1;
    assert(vk_device_dispatch() && vkDeviceWaitIdle && vkQueuePresentKHR);
    vb_teardown(); assert(waited == 1 && destroys == 2);
    VD = (VkDevice)2; missing = "vkGetDeviceQueue"; VX.swapext = 0;
    assert(!vk_device_dispatch());
    /* Inspect before cleanup: never call a previous device's entry point. */
    assert(!vkDeviceWaitIdle && !vkQueuePresentKHR);
    vb_teardown(); assert(waited == 1 && destroys == 2 && destroyed_replacement == 1);
    VD = (VkDevice)1; missing = "vkDestroySwapchainKHR"; VX.swapext = 1;
    assert(vk_device_dispatch() && !VX.swapext);
    assert(!vk_surface_dispatch(1)); /* no real surface function or WSI pointers */
    vb_teardown(); assert(waited == 2 && destroys == 3);

    g_win_retain = retain; g_win_release = release_window;
    window_publish(&refs[0]);
    int oldgen = g_win_gen;
    void *active = window_snapshot();
    assert(active == &refs[0] && refs[0] == 2);
    window_publish(&refs[1]); /* UI supersedes a busy frame; renderer still owns the old window */
    assert(refs[0] == 1 && refs[1] == 1);
    window_publish(NULL); /* detach before B is ever applied */
    assert(refs[0] == 1 && refs[1] == 0);
    window_drop(active); assert(refs[0] == 0);

    hle_present_hook = frame;
    assert(!vk_window_presented(oldgen, 1, 0, 1)); /* stale */
    window_publish(&refs[0]); int gen = g_win_gen;
    assert(!vk_window_presented(gen, 0, 0, 1)); /* failed submit / failed post */
    assert(!vk_window_presented(gen, 1, 1, 1)); /* headless */
    assert(!vk_window_presented(gen, 1, 0, 0)); /* no real window */
    assert(presented == 0);
    assert(vk_window_presented(gen, 1, 0, 1) && presented == 1);
    VW.cpu = 1;
    me1gl_test_window_api(geometry,lock_window,post_window,dimension,dimension);
    size = 0; vwin_apply(); assert(VW.win == &refs[0] && refs[0] == 2 && !me1gl_present_failure());
    size = 1; assert(vcpu_attach());
    MBlk *block = calloc(1, sizeof *block);
    block->map = pixels; block->mem = (VkDeviceMemory)1;
    g_mb = block; g_nmb = 1;
    VMP.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VSlot s = { .rb=(VkBuffer)1,.rbw=1,.rbh=1,.rbpend=1 };
    lock_result = -1;
    vcpu_post(&s); assert(!me1gl_present_failure() && presented == 1 && posts == 0);
    lock_result = 0;
    post_result = -1;
    vcpu_post(&s); assert(!me1gl_present_failure() && presented == 1 && posts == 1);
    post_result = 0;
    vcpu_post(&s); assert(!memcmp(pixels,native_pixels,4) && presented == 2);
    window_publish(&refs[1]); vwin_apply(); assert(!me1gl_present_failure() && refs[0] == 0 && refs[1] == 2);
    vk_window_failure(VW.gen, "fixture: terminal window failure");
    int64_t failure_a = me1gl_present_failure();
    assert(failure_a);
    vk_window_failure(VW.gen, "fixture: repeated window failure");
    assert(me1gl_present_failure() == failure_a);
    assert(!vk_window_presented(gen, 1, 0, 1)); /* old generation */
    assert(!vk_window_presented(VW.gen, 0, 0, 1));
    assert(!vk_window_presented(VW.gen, 1, 1, 1));
    assert(presented == 2 && me1gl_present_failure() == failure_a);
    /* A successful real post followed by B on the same window, without a status read in between. */
    vcpu_post(&s); assert(presented == 3);
    vk_window_failure(VW.gen, "fixture: terminal window failure before replacement");
    int64_t failure_b = me1gl_present_failure();
    assert(failure_b && failure_b != failure_a);
    window_publish(&refs[0]); vwin_apply(); assert(!me1gl_present_failure() && refs[0] == 2 && refs[1] == 0);
    for (int lock_error = 0; lock_error <= 1; lock_error++) {
        int before = presented;
        cpu_failure_burst(&s, lock_error, 7, 0);
        vcpu_post(&s); assert(presented == before + 1 && !me1gl_present_failure());
        cpu_failure_burst(&s, lock_error, 8, 1); /* first seven still tolerated after success */
        vcpu_post(&s); assert(presented == before + 2 && !me1gl_present_failure());
        cpu_failure_burst(&s, lock_error, 7, 0);
        window_publish(&refs[1]); /* old active window, new pending generation */
        cpu_failure_burst(&s, lock_error, 8, 0);
        vcpu_post(&s); assert(presented == before + 2 && !me1gl_present_failure());
        vwin_apply();
        cpu_failure_burst(&s, lock_error, 8, 1); /* replacement starts at zero */
        VW.fake = 1;
        vcpu_post(&s); assert(presented == before + 2 && me1gl_present_failure());
        VW.fake = 0;
        vcpu_post(&s); assert(presented == before + 3 && !me1gl_present_failure());
        window_publish(&refs[0]); vwin_apply();
    }
    window_publish(NULL); vwin_apply(); assert(!refs[0] && !refs[1]);
    free(block); g_mb = NULL; g_nmb = 0;

    g_vk = 1;
    vkQueueSubmit = submit_lost; vkEndCommandBuffer = end_command;
    window_publish(&refs[0]); vwin_apply();
    vk_window_failure(VW.gen, "fixture: window failure before device loss");
    int64_t window_id = me1gl_present_failure();
    assert(window_id && window_id != failure_b);
    /* Device loss supersedes a live window incident only when terminal. Exercise both entry points. */
    if (recovery) {
        vkAcquireNextImageKHR = acquire_lost;
        VW.sc = (VkSwapchainKHR)1;
        uint32_t idx;
        assert(!vswap_acquire(&idx));
    } else vb_submit(-1,0,1);
    assert(g_vklost && presented == 9);
    int64_t device_id = me1gl_present_failure();
    if (recovery) assert(device_id == window_id);
    else assert(device_id && device_id != window_id);
    window_publish(&refs[1]);
    assert(me1gl_present_failure() == (recovery ? 0 : device_id));
    if (recovery) {
        assert(g_d3dlost);
        unsetenv("ME1_VK_RECOVER_MAX");
        setenv("ME1_SPV_LIB", "/dev/null", 1); /* deterministic init failure, no driver calls */
        assert(!vk_device_reset());
        device_id = me1gl_present_failure();
        assert(g_vklost && device_id && device_id != window_id);
    }
    vk_device_failure("fixture: repeated device failure");
    assert(me1gl_present_failure() == device_id);
    window_publish(NULL);
    window_publish(&refs[1]);
    vk_window_failure(g_win_gen, "fixture: window failure after device loss");
    vwin_apply();
    assert(me1gl_present_failure() == device_id && VW.win == &refs[0]);
    assert(!vk_window_presented(g_win_gen, 1, 0, 1));
    assert(me1gl_present_failure() == device_id);
    window_publish(NULL); window_drop(VW.win); assert(!refs[0] && !refs[1]);
    puts("Vulkan direct dispatch, window ownership and present contracts passed");
    return 0;
}
