#include "context.hpp"

#include <xebble/log.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace xebble::vk {

struct Context::Impl {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphics_queue = VK_NULL_HANDLE;
    VkQueue present_queue = VK_NULL_HANDLE;
    uint32_t graphics_family = 0;
    uint32_t present_family = 0;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;

    ~Impl() {
        if (device)
            vkDeviceWaitIdle(device);
        if (command_pool)
            vkDestroyCommandPool(device, command_pool, nullptr);
        if (allocator)
            vmaDestroyAllocator(allocator);
        if (device)
            vkDestroyDevice(device, nullptr);
        if (surface)
            vkDestroySurfaceKHR(instance, surface, nullptr);
#ifndef NDEBUG
        if (debug_messenger) {
            auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (func)
                func(instance, debug_messenger, nullptr);
        }
#endif
        if (instance)
            vkDestroyInstance(instance, nullptr);
    }
};

namespace {

#ifndef NDEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* /*user_data*/) {
    LogLevel level = LogLevel::Debug;
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        level = LogLevel::Error;
    else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        level = LogLevel::Warn;
    else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        level = LogLevel::Info;

    log(level, data->pMessage);
    return VK_FALSE;
}
#endif

bool check_validation_layer_support() {
    uint32_t count;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());

    for (auto& layer : layers) {
        if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
            return true;
    }
    return false;
}

struct QueueFamilyIndices {
    uint32_t graphics = UINT32_MAX;
    uint32_t present = UINT32_MAX;
    bool complete() const { return graphics != UINT32_MAX && present != UINT32_MAX; }
};

QueueFamilyIndices find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices;
    uint32_t count;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; i++) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            indices.graphics = i;

        VkBool32 present_support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);
        if (present_support)
            indices.present = i;

        if (indices.complete())
            break;
    }
    return indices;
}

int score_device(VkPhysicalDevice device, VkSurfaceKHR surface) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    auto indices = find_queue_families(device, surface);
    if (!indices.complete())
        return 0;

    // Check swapchain extension support
    uint32_t ext_count;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> exts(ext_count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &ext_count, exts.data());

    bool has_swapchain = false;
    for (auto& ext : exts) {
        if (std::strcmp(ext.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            has_swapchain = true;
            break;
        }
    }
    if (!has_swapchain)
        return 0;

    int score = 1;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        score += 1000;
    else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        score += 100;

    return score;
}

bool device_has_extension(VkPhysicalDevice device, const char* name) {
    uint32_t count;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, exts.data());
    for (auto& ext : exts) {
        if (std::strcmp(ext.extensionName, name) == 0)
            return true;
    }
    return false;
}

} // anonymous namespace

std::expected<Context, Error> Context::create(GLFWwindow* window) {
    auto impl = std::make_unique<Impl>();

#ifdef __APPLE__
    // Ensure the Vulkan loader can find MoltenVK on macOS.
    // Searches: env var > .app bundle > build-time bundled copy.
    if (!std::getenv("VK_DRIVER_FILES") && !std::getenv("VK_ICD_FILENAMES")) {
        std::vector<std::string> icd_paths;

        // Check for .app bundle: Contents/MacOS/exe -> ../Resources/vulkan/icd.d/
        {
            char exe_buf[4096];
            uint32_t exe_size = sizeof(exe_buf);
            if (_NSGetExecutablePath(exe_buf, &exe_size) == 0) {
                auto exe_dir = std::filesystem::path(exe_buf).parent_path();
                auto bundle_icd = exe_dir / "../Resources/vulkan/icd.d/MoltenVK_icd.json";
                icd_paths.push_back(bundle_icd.string());
            }
        }

        // Build-time bundled copy (set by CMake)
#ifdef XEBBLE_MOLTENVK_ICD
        icd_paths.push_back(XEBBLE_MOLTENVK_ICD);
#endif

        for (auto& path : icd_paths) {
            if (std::filesystem::exists(path)) {
                setenv("VK_DRIVER_FILES", path.c_str(), 0);
                log(LogLevel::Info, "Using MoltenVK ICD: " + path);
                break;
            }
        }
    }
#endif

    // --- Instance ---
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Xebble";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "Xebble";
    app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = VK_API_VERSION_1_2;

    uint32_t glfw_ext_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_ext_count);
    if (!glfw_extensions) {
        return std::unexpected(Error{"GLFW: Vulkan not supported or GLFW not initialized"});
    }
    std::vector<const char*> extensions(glfw_extensions, glfw_extensions + glfw_ext_count);

#ifdef __APPLE__
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.push_back("VK_KHR_get_physical_device_properties2");
#endif

#ifndef NDEBUG
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

#ifdef __APPLE__
    create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    std::vector<const char*> layers;
#ifndef NDEBUG
    if (check_validation_layer_support()) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }
#endif
    create_info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    create_info.ppEnabledLayerNames = layers.data();

    VkResult instance_result = vkCreateInstance(&create_info, nullptr, &impl->instance);
    if (instance_result != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to create Vulkan instance (VkResult: " +
                                     std::to_string(static_cast<int>(instance_result)) + ")"});
    }

    log(LogLevel::Info, "Vulkan instance created");

    // --- Debug messenger ---
#ifndef NDEBUG
    VkDebugUtilsMessengerCreateInfoEXT debug_info{};
    debug_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debug_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug_info.pfnUserCallback = debug_callback;

    auto create_debug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(impl->instance, "vkCreateDebugUtilsMessengerEXT"));
    if (create_debug) {
        create_debug(impl->instance, &debug_info, nullptr, &impl->debug_messenger);
    }
#endif

    // --- Surface ---
    if (glfwCreateWindowSurface(impl->instance, window, nullptr, &impl->surface) != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to create window surface"});
    }

    // --- Physical device ---
    uint32_t device_count;
    vkEnumeratePhysicalDevices(impl->instance, &device_count, nullptr);
    if (device_count == 0) {
        return std::unexpected(Error{"No Vulkan-capable GPU found"});
    }
    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(impl->instance, &device_count, devices.data());

    int best_score = 0;
    for (auto& dev : devices) {
        int s = score_device(dev, impl->surface);
        if (s > best_score) {
            best_score = s;
            impl->physical_device = dev;
        }
    }
    if (impl->physical_device == VK_NULL_HANDLE) {
        return std::unexpected(Error{"No suitable GPU found"});
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(impl->physical_device, &props);
    log(LogLevel::Info, std::string("Selected GPU: ") + props.deviceName);

    // --- Logical device ---
    auto indices = find_queue_families(impl->physical_device, impl->surface);
    impl->graphics_family = indices.graphics;
    impl->present_family = indices.present;

    std::set<uint32_t> unique_families = {indices.graphics, indices.present};
    std::vector<VkDeviceQueueCreateInfo> queue_infos;
    float priority = 1.0f;
    for (uint32_t family : unique_families) {
        VkDeviceQueueCreateInfo qi{};
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = family;
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        queue_infos.push_back(qi);
    }

    VkPhysicalDeviceFeatures features{};

    std::vector<const char*> device_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
#ifdef __APPLE__
    if (device_has_extension(impl->physical_device, "VK_KHR_portability_subset")) {
        device_extensions.push_back("VK_KHR_portability_subset");
    }
#endif

    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size());
    device_info.pQueueCreateInfos = queue_infos.data();
    device_info.pEnabledFeatures = &features;
    device_info.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size());
    device_info.ppEnabledExtensionNames = device_extensions.data();

    if (vkCreateDevice(impl->physical_device, &device_info, nullptr, &impl->device) != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to create logical device"});
    }

    vkGetDeviceQueue(impl->device, indices.graphics, 0, &impl->graphics_queue);
    vkGetDeviceQueue(impl->device, indices.present, 0, &impl->present_queue);

    // --- VMA ---
    VmaAllocatorCreateInfo alloc_info{};
    alloc_info.physicalDevice = impl->physical_device;
    alloc_info.device = impl->device;
    alloc_info.instance = impl->instance;
    alloc_info.vulkanApiVersion = VK_API_VERSION_1_2;

    if (vmaCreateAllocator(&alloc_info, &impl->allocator) != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to create VMA allocator"});
    }

    // --- Command pool ---
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = indices.graphics;

    if (vkCreateCommandPool(impl->device, &pool_info, nullptr, &impl->command_pool) != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to create command pool"});
    }

    log(LogLevel::Info, "Vulkan context initialized");

    Context ctx;
    ctx.impl_ = std::move(impl);
    return ctx;
}

Context::~Context() = default;
Context::Context(Context&&) noexcept = default;
Context& Context::operator=(Context&&) noexcept = default;

VkInstance Context::instance() const {
    return impl_->instance;
}
VkPhysicalDevice Context::physical_device() const {
    return impl_->physical_device;
}
VkDevice Context::device() const {
    return impl_->device;
}
VkQueue Context::graphics_queue() const {
    return impl_->graphics_queue;
}
VkQueue Context::present_queue() const {
    return impl_->present_queue;
}
uint32_t Context::graphics_queue_family() const {
    return impl_->graphics_family;
}
uint32_t Context::present_queue_family() const {
    return impl_->present_family;
}
VkSurfaceKHR Context::surface() const {
    return impl_->surface;
}
VmaAllocator Context::allocator() const {
    return impl_->allocator;
}
VkCommandPool Context::command_pool() const {
    return impl_->command_pool;
}

} // namespace xebble::vk
