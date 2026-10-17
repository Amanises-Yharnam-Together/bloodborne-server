// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: checks bbport_dlss.dll (gpu/dlss_bridge) on this PC's GPU without the game: loads it from
// next to this program, makes a Vulkan device with the extensions NGX asks for, initializes NGX and
// prints what DLSS Super Resolution and Frame Generation can do here. Windows only.
//   ninja -C out/gpu dlss-bridge-check && cp out/gpu/dlss-bridge-check.exe out/ && out/dlss-bridge-check.exe
// Exit code 0 when DLSS initialized, 1 when it did not, 2 when the bridge is missing or too old.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <vulkan/vulkan.h>

#include "../gpu/dlss_bridge/bbport_dlss_bridge.h"

static void Log(int warning, const char* message) {
    printf("  bridge%s: %s\n", warning ? " (warning)" : "", message);
}

int main(void) {
    wchar_t dir[MAX_PATH];
    GetModuleFileNameW(NULL, dir, MAX_PATH);
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (slash) {
        *slash = 0;
    }
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\bbport_dlss.dll", dir);
    HMODULE module = LoadLibraryW(path);
    BbDlssGetApiFn get_api = module ? (BbDlssGetApiFn)(void*)GetProcAddress(module, "BbDlssGetApi") : NULL;
    const BbDlssApi* api = get_api ? get_api() : NULL;
    if (!api) {
        printf("bbport_dlss.dll: not found next to this program\n");
        return 2;
    }
    printf("bbport_dlss.dll: ABI %u (this check: %d)\n", api->abi, BBPORT_DLSS_BRIDGE_ABI);
    if (api->abi < 2) {
        printf("Frame Generation: not in this bridge (rebuild it with packaging/windows/build_dlss.sh)\n");
        return 2;
    }
    wchar_t data[MAX_PATH];
    GetTempPathW(MAX_PATH, data);
    if (!api->Configure(dir, data, Log)) {
        printf("NGX: configuration failed\n");
        return 1;
    }

    uint32_t count = 0;
    const VkExtensionProperties* extensions = NULL;
    api->InstanceExtensions(&count, &extensions);
    const char* names[64];
    uint32_t n = 0;
    for (uint32_t i = 0; i < count && n < 64; ++i) {
        names[n++] = extensions[i].extensionName;
    }
    const VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                   .pApplicationName = "dlss-bridge-check",
                                   .apiVersion = VK_API_VERSION_1_3};
    const VkInstanceCreateInfo instance_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                                .pApplicationInfo = &app,
                                                .enabledExtensionCount = n,
                                                .ppEnabledExtensionNames = names};
    VkInstance instance;
    if (vkCreateInstance(&instance_info, NULL, &instance) != VK_SUCCESS) {
        printf("Vulkan: no instance\n");
        return 1;
    }
    uint32_t devices = 8;
    VkPhysicalDevice physical[8];
    vkEnumeratePhysicalDevices(instance, &devices, physical);
    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties;
    for (uint32_t i = 0; i < devices; ++i) {
        vkGetPhysicalDeviceProperties(physical[i], &properties);
        if (properties.vendorID == 0x10DE) {
            chosen = physical[i];
            break;
        }
    }
    if (!chosen) {
        printf("GPU: no NVIDIA GPU\n");
        return 1;
    }
    printf("GPU: %s (driver %u.%u)\n", properties.deviceName, properties.driverVersion >> 22,
           (properties.driverVersion >> 14) & 0xFF);

    api->DeviceExtensions(instance, chosen, &count, &extensions);
    n = 0;
    for (uint32_t i = 0; i < count && n < 64; ++i) {
        names[n++] = extensions[i].extensionName;
    }
    uint32_t families = 16;
    VkQueueFamilyProperties family[16];
    vkGetPhysicalDeviceQueueFamilyProperties(chosen, &families, family);
    uint32_t graphics = 0;
    while (graphics < families && !(family[graphics].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
        ++graphics;
    }
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                           .queueFamilyIndex = graphics,
                                           .queueCount = 1,
                                           .pQueuePriorities = &priority};
    VkPhysicalDeviceVulkan12Features features12 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .timelineSemaphore = VK_TRUE,
        .bufferDeviceAddress = VK_TRUE};
    VkPhysicalDeviceVulkan13Features features13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &features12,
        .synchronization2 = VK_TRUE};
    const VkDeviceCreateInfo device_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                            .pNext = &features13,
                                            .queueCreateInfoCount = 1,
                                            .pQueueCreateInfos = &queue,
                                            .enabledExtensionCount = n,
                                            .ppEnabledExtensionNames = names};
    VkDevice device;
    if (vkCreateDevice(chosen, &device_info, NULL, &device) != VK_SUCCESS) {
        printf("Vulkan: no device with NGX's %u extensions\n", n);
        return 1;
    }
    const int ok = api->Initialize(instance, chosen, device, vkGetInstanceProcAddr, vkGetDeviceProcAddr);
    printf("DLSS Super Resolution: %s\n", ok ? "ready" : "unavailable");
    const int frames = ok ? api->FrameGenMaxCount() : 0;
    printf("DLSS Frame Generation: %s\n", frames >= 3   ? "up to 4x (3 generated frames)"
                                          : frames >= 1 ? "2x (1 generated frame)"
                                                        : "unavailable");
    api->Shutdown();
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return ok ? 0 : 1;
}
