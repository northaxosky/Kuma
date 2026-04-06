// ── Device Initialization ───────────────────────────────────────
// Vulkan instance, debug messenger, surface, GPU selection, logical device.

#include "renderer_impl.h"

namespace kuma {

// ── Debug Messenger Callback ────────────────────────────────────
// Vulkan validation layers call this when they detect an error.

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* /*user_data*/)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        kuma::log::warn("[Vulkan] %s", callback_data->pMessage);
    }
    return VK_FALSE;
}

// ── Instance ────────────────────────────────────────────────────

bool RendererImpl::create_instance() {
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Kuma Engine";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "Kuma";
    app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    uint32_t sdl_ext_count = 0;
    const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);

    std::vector<const char*> extensions(sdl_extensions, sdl_extensions + sdl_ext_count);

    const char* validation_layer = "VK_LAYER_KHRONOS_validation";
    std::vector<const char*> layers;

    if (validation_enabled_) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        layers.push_back(validation_layer);
    }

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();
    create_info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    create_info.ppEnabledLayerNames = layers.data();

    VkResult result = vkCreateInstance(&create_info, nullptr, &instance_);
    if (result != VK_SUCCESS) {
        kuma::log::error("Failed to create Vulkan instance (error %d)", result);
        return false;
    }

    return true;
}

// ── Debug Messenger ─────────────────────────────────────────────

bool RendererImpl::create_debug_messenger() {
    if (!validation_enabled_) return true;

    VkDebugUtilsMessengerCreateInfoEXT create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    create_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    create_info.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    create_info.pfnUserCallback = debug_callback;

    auto create_fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));

    if (!create_fn) {
        kuma::log::info("Debug messenger extension not available");
        return true;
    }

    VkResult result = create_fn(instance_, &create_info, nullptr, &debug_messenger_);
    if (result != VK_SUCCESS) {
        kuma::log::error("Failed to create debug messenger");
        return false;
    }

    return true;
}

// ── Surface ─────────────────────────────────────────────────────

bool RendererImpl::create_surface() {
    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
        kuma::log::error("Failed to create Vulkan surface: %s", SDL_GetError());
        return false;
    }
    return true;
}

// ── Physical Device ─────────────────────────────────────────────

bool RendererImpl::pick_physical_device() {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);

    if (device_count == 0) {
        kuma::log::error("No Vulkan-capable GPUs found");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());

    for (const auto& device : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);

        uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, families.data());

        for (uint32_t i = 0; i < family_count; i++) {
            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &present_support);

            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present_support) {
                physical_device_ = device;
                queue_family_index_ = i;

                kuma::log::info("Selected GPU: %s", props.deviceName);
                return true;
            }
        }
    }

    kuma::log::error("No suitable GPU found");
    return false;
}

// ── Logical Device + Queues ─────────────────────────────────────

bool RendererImpl::create_logical_device() {
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = queue_family_index_;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    const char* device_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VkPhysicalDeviceFeatures features{};

    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = 1;
    create_info.pQueueCreateInfos = &queue_info;
    create_info.enabledExtensionCount = 1;
    create_info.ppEnabledExtensionNames = device_extensions;
    create_info.pEnabledFeatures = &features;

    VkResult result = vkCreateDevice(physical_device_, &create_info, nullptr, &device_);
    if (result != VK_SUCCESS) {
        kuma::log::error("Failed to create logical device (error %d)", result);
        return false;
    }

    vkGetDeviceQueue(device_, queue_family_index_, 0, &graphics_queue_);
    present_queue_ = graphics_queue_;

    return true;
}

} // namespace kuma
