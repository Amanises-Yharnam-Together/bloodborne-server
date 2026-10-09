/* bbport (Windows): can this GPU import the runtime's guest memory (VK_EXT_external_memory_host)?
 * The runtime keeps direct memory in a pagefile-backed section mapped twice: at the game's
 * addresses and as the backing view; the PC memory model (BbGuestMemory HostImport) imports a range
 * of the backing view. This maps a section the same way, imports a range inside the backing view
 * with each handle type the model tries (host allocation, then host mapped foreign memory), lets the
 * GPU write it and read CPU writes, and checks both through the other view.
 * Needs a Vulkan device; does not start the game. Not part of build-bbport -Test:
 *   ninja -C out/gpu host-import-test && out/gpu/host-import-test.exe
 * Exit 0 when one handle type works. */
#include <windows.h>
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTION_SIZE (UINT64_C(1088) << 20)
#define RANGE_OFFSET (UINT64_C(16) << 20) /* a chunk inside the backing view, as backing_base+phys */
#define RANGE_SIZE (UINT64_C(4) << 20)
#define LARGE_OFFSET (UINT64_C(64) << 20) /* a large direct allocation's chunk */
#define LARGE_SIZE (UINT64_C(1024) << 20)

static VkDevice device;
static VkQueue queue;
static uint32_t family;
static VkPhysicalDeviceMemoryProperties memory_props;
static PFN_vkGetMemoryHostPointerPropertiesEXT get_pointer_props;

static const char *type_name(VkExternalMemoryHandleTypeFlagBits type) {
    return type == VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT ? "host allocation"
                                                                          : "host mapped foreign memory";
}

/* Imports [pointer, pointer+size) with `type`, fills its first 4 KiB on the GPU and copies the
 * word at 4096 to 8192; 1 when the other view sees both. */
static int try_import(VkExternalMemoryHandleTypeFlagBits type, void *pointer, uint64_t size, volatile uint32_t *other,
                      int device_address) {
    VkMemoryHostPointerPropertiesEXT props = {.sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
    VkResult r = get_pointer_props(device, type, pointer, &props);
    if (r != VK_SUCCESS || !props.memoryTypeBits) {
        printf("  %s: vkGetMemoryHostPointerPropertiesEXT %d, types %#x\n", type_name(type), r,
               props.memoryTypeBits);
        return 0;
    }
    VkExternalMemoryBufferCreateInfo external = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
                                                 .handleTypes = type};
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .pNext = &external, .size = size,
                              .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                       (device_address ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : 0),
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buffer;
    if ((r = vkCreateBuffer(device, &bci, NULL, &buffer)) != VK_SUCCESS) {
        printf("  %s: vkCreateBuffer %d\n", type_name(type), r);
        return 0;
    }
    VkMemoryRequirements reqs;
    vkGetBufferMemoryRequirements(device, buffer, &reqs);
    uint32_t bits = reqs.memoryTypeBits & props.memoryTypeBits, index = ~0u;
    for (uint32_t i = 0; i < memory_props.memoryTypeCount && index == ~0u; ++i)
        if ((bits & (1u << i)) && (memory_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            index = i;
    if (index == ~0u) {
        printf("  %s: no host-visible type (buffer %#x, pointer %#x)\n", type_name(type), reqs.memoryTypeBits,
               props.memoryTypeBits);
        vkDestroyBuffer(device, buffer, NULL);
        return 0;
    }
    /* The layer's memory module gives chunk buffers a device address. */
    VkMemoryAllocateFlagsInfo address_flags = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
                                               .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
    VkImportMemoryHostPointerInfoEXT import = {.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
                                               .pNext = device_address ? &address_flags : NULL,
                                               .handleType = type, .pHostPointer = pointer};
    VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &import,
                                .allocationSize = size, .memoryTypeIndex = index};
    VkDeviceMemory memory;
    if ((r = vkAllocateMemory(device, &mai, NULL, &memory)) != VK_SUCCESS) {
        printf("  %s: vkAllocateMemory (import) %d\n", type_name(type), r);
        vkDestroyBuffer(device, buffer, NULL);
        return 0;
    }
    int ok = 0;
    if ((r = vkBindBufferMemory(device, buffer, memory, 0)) == VK_SUCCESS) {
        other[1024] = 0x5ca1ab1e;
        VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
        VkCommandPool pool;
        vkCreateCommandPool(device, &pci, NULL, &pool);
        VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
                                           .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device, &cai, &cmd);
        VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(cmd, &begin);
        vkCmdFillBuffer(cmd, buffer, 0, 4096, 0x600dcafe);
        VkBufferCopy copy = {4096, 8192, 4};
        vkCmdCopyBuffer(cmd, buffer, buffer, 1, &copy);
        VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                   .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0,
                             NULL);
        vkEndCommandBuffer(cmd);
        VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
        if (vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS && vkQueueWaitIdle(queue) == VK_SUCCESS) {
            const int gpu_to_cpu = other[0] == 0x600dcafe && other[1023] == 0x600dcafe;
            const int cpu_to_gpu = other[2048] == 0x5ca1ab1e;
            printf("  %s: %llu MiB imported (type %u, flags %#x%s); GPU writes %s by the CPU, CPU writes %s by "
                   "the GPU\n",
                   type_name(type), (unsigned long long)(size >> 20), index,
                   memory_props.memoryTypes[index].propertyFlags, device_address ? ", device address" : "",
                   gpu_to_cpu ? "seen" : "NOT seen", cpu_to_gpu ? "seen" : "NOT seen");
            ok = gpu_to_cpu && cpu_to_gpu;
        } else {
            printf("  %s: GPU work failed\n", type_name(type));
        }
        vkDestroyCommandPool(device, pool, NULL);
    } else {
        printf("  %s: vkBindBufferMemory %d\n", type_name(type), r);
    }
    vkFreeMemory(device, memory, NULL);
    vkDestroyBuffer(device, buffer, NULL);
    return ok;
}

int main(void) {
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "bbport host import test",
                             .apiVersion = VK_API_VERSION_1_2};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance instance;
    if (vkCreateInstance(&ici, NULL, &instance) != VK_SUCCESS) {
        puts("host import: no Vulkan instance");
        return 2;
    }
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, NULL);
    VkPhysicalDevice devices[16], physical = VK_NULL_HANDLE;
    if (count > 16) count = 16;
    vkEnumeratePhysicalDevices(instance, &count, devices);
    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(devices[i], &p);
        if (!physical || p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) physical = devices[i];
    }
    if (!physical) {
        puts("host import: no Vulkan device");
        return 2;
    }
    VkPhysicalDeviceExternalMemoryHostPropertiesEXT host = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2 props2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &host};
    vkGetPhysicalDeviceProperties2(physical, &props2);
    printf("host import: %s (vendor %#x, driver %#x), minImportedHostPointerAlignment %llu\n",
           props2.properties.deviceName, props2.properties.vendorID, props2.properties.driverVersion,
           (unsigned long long)host.minImportedHostPointerAlignment);
    vkGetPhysicalDeviceMemoryProperties(physical, &memory_props);
    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &nq, NULL);
    VkQueueFamilyProperties families[16];
    if (nq > 16) nq = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &nq, families);
    while (family < nq && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
    float priority = 1;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family,
                                   .queueCount = 1, .pQueuePriorities = &priority};
    const char *extensions[] = {VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME};
    VkPhysicalDeviceVulkan12Features features12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                                   .bufferDeviceAddress = VK_TRUE};
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &features12, .queueCreateInfoCount = 1,
                              .pQueueCreateInfos = &qci, .enabledExtensionCount = 1, .ppEnabledExtensionNames = extensions};
    if (vkCreateDevice(physical, &dci, NULL, &device) != VK_SUCCESS) {
        puts("host import: VK_EXT_external_memory_host unavailable");
        return 1;
    }
    vkGetDeviceQueue(device, family, 0, &queue);
    get_pointer_props = (PFN_vkGetMemoryHostPointerPropertiesEXT)vkGetDeviceProcAddr(
        device, "vkGetMemoryHostPointerPropertiesEXT");

    /* The runtime's layout: a pagefile-backed section, the backing view and a second view. */
    HANDLE section = CreateFileMapping2(INVALID_HANDLE_VALUE, NULL, FILE_MAP_ALL_ACCESS, PAGE_EXECUTE_READWRITE,
                                        SEC_COMMIT, SECTION_SIZE, NULL, NULL, 0);
    unsigned char *backing = section ? MapViewOfFile3(section, GetCurrentProcess(), NULL, 0, SECTION_SIZE, 0,
                                                      PAGE_READWRITE, NULL, 0)
                                     : NULL;
    unsigned char *guest = section ? MapViewOfFile3(section, GetCurrentProcess(), NULL, RANGE_OFFSET, RANGE_SIZE, 0,
                                                    PAGE_READWRITE, NULL, 0)
                                   : NULL;
    unsigned char *large_guest = section ? MapViewOfFile3(section, GetCurrentProcess(), NULL, LARGE_OFFSET, LARGE_SIZE,
                                                          0, PAGE_READWRITE, NULL, 0)
                                         : NULL;
    if (!backing || !guest || !large_guest) {
        printf("host import: section views failed (error %lu)\n", GetLastError());
        return 2;
    }
    int works = 0;
    const VkExternalMemoryHandleTypeFlagBits types[] = {VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
                                                        VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_MAPPED_FOREIGN_MEMORY_BIT_EXT};
    for (int i = 0; i < 2; ++i) {
        memset(guest, 0, 16384);
        if (try_import(types[i], backing + RANGE_OFFSET, RANGE_SIZE, (volatile uint32_t *)guest, 0) &&
            try_import(types[i], backing + RANGE_OFFSET, RANGE_SIZE, (volatile uint32_t *)guest, 1)) {
            printf("host import: %s of a section view WORKS\n", type_name(types[i]));
            memset(large_guest, 0, 16384);
            printf("host import: %s of 1 GiB of a section view %s\n", type_name(types[i]),
                   try_import(types[i], backing + LARGE_OFFSET, LARGE_SIZE, (volatile uint32_t *)large_guest, 1)
                       ? "works"
                       : "FAILS");
            works = 1;
        }
    }
    /* For comparison: private committed memory (VirtualAlloc). */
    unsigned char *private_memory = VirtualAlloc(NULL, RANGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    printf("host import: VirtualAlloc memory (comparison):\n");
    for (int i = 0; i < 2; ++i) try_import(types[i], private_memory, RANGE_SIZE, (volatile uint32_t *)private_memory, 0);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    printf("host import: %s\n", works ? "ok" : "a section view cannot be imported");
    return works ? 0 : 1;
}
