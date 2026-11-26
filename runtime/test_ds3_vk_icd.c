/* Trusted host-only ICD fixture. No loader, GPU, layers, or WSI required. */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <assert.h>
#include <string.h>

#define EXPORT __attribute__((visibility("default")))
static int negotiated, instance, physical, device;
#define INST ((VkInstance)&instance)
#define PHYS ((VkPhysicalDevice)&physical)
#define DEV ((VkDevice)&device)

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* version) {
    if (*version < 4) return VK_ERROR_INCOMPATIBLE_DRIVER;
    *version = 4;
    negotiated = 1;
    return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL create_instance(const VkInstanceCreateInfo* ci, const VkAllocationCallbacks* a, VkInstance* out) {
    if (!negotiated) return VK_ERROR_INCOMPATIBLE_DRIVER;
    instance = 1;
    *out = INST;
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL destroy_instance(VkInstance inst, const VkAllocationCallbacks* a) {
    assert(inst == INST && instance && !device);
    instance = 0;
}
static VKAPI_ATTR VkResult VKAPI_CALL enumerate_devices(VkInstance inst, uint32_t* count, VkPhysicalDevice* out) {
    assert(inst == INST && instance);
    if (out && !*count) return VK_INCOMPLETE;
    if (out) *out = PHYS;
    *count = 1;
    return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL create_device(VkPhysicalDevice p, const VkDeviceCreateInfo* ci, const VkAllocationCallbacks* a, VkDevice* out) {
    assert(p == PHYS && instance);
    device = 1;
    *out = DEV;
    return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL wait_idle(VkDevice d) {
    assert(d == DEV && device);
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL destroy_device(VkDevice d, const VkAllocationCallbacks* a) {
    assert(d == DEV && device);
    device = 0;
}
#define PROC(symbol, fn) if (!strcmp(name, #symbol)) return (PFN_vkVoidFunction)fn
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL gdpa(VkDevice d, const char* name) {
    if (d != DEV || !device) return NULL;
    PROC(vkDeviceWaitIdle, wait_idle);
    PROC(vkDestroyDevice, destroy_device);
    return NULL;
}
EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance inst, const char* name) {
    PROC(vkCreateInstance, create_instance);
    if (inst != INST || !instance) return NULL;
    PROC(vkGetDeviceProcAddr, gdpa);
    PROC(vkDestroyInstance, destroy_instance);
    PROC(vkEnumeratePhysicalDevices, enumerate_devices);
    PROC(vkCreateDevice, create_device);
    return NULL;
}
