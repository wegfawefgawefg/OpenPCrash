#include "render/vk_internal.hpp"
#include "render/vk_sprites.hpp"

#include <SDL3/SDL_vulkan.h>
#include <cstdio>
#include <cstring>

bool vk_check(VkResult result, const char* what) {
    if (result == VK_SUCCESS) {
        return true;
    }
    SDL_SetError("Vulkan %s failed (%d)", what, static_cast<int>(result));
    return false;
}

std::uint32_t vk_memory_type(RendererState& vk, std::uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(vk.physical, &memory);
    for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }
    return 0;
}

bool vk_create_buffer(RendererState& vk, VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags flags, GpuBuffer& out) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!vk_check(vkCreateBuffer(vk.device, &info, nullptr, &out.buffer), "create buffer")) {
        return false;
    }
    VkMemoryRequirements needs;
    vkGetBufferMemoryRequirements(vk.device, out.buffer, &needs);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = needs.size;
    allocate.memoryTypeIndex = vk_memory_type(vk, needs.memoryTypeBits, flags);
    if (!vk_check(vkAllocateMemory(vk.device, &allocate, nullptr, &out.memory),
                  "allocate buffer memory")) {
        return false;
    }
    vkBindBufferMemory(vk.device, out.buffer, out.memory, 0);
    out.size = size;
    return true;
}

bool vk_create_mapped_buffer(RendererState& vk, VkDeviceSize size, VkBufferUsageFlags usage,
                             GpuBuffer& out) {
    if (!vk_create_buffer(vk, size, usage,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          out)) {
        return false;
    }
    return vk_check(vkMapMemory(vk.device, out.memory, 0, size, 0, &out.mapped), "map buffer");
}

void vk_destroy_buffer(RendererState& vk, GpuBuffer& buffer) {
    if (buffer.mapped) {
        vkUnmapMemory(vk.device, buffer.memory);
    }
    if (buffer.buffer) {
        vkDestroyBuffer(vk.device, buffer.buffer, nullptr);
    }
    if (buffer.memory) {
        vkFreeMemory(vk.device, buffer.memory, nullptr);
    }
    buffer = {};
}

bool vk_create_image(RendererState& vk, int width, int height, int levels, VkFormat format,
                     VkImageUsageFlags usage, VkImageAspectFlags aspect, GpuImage& out) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
    info.mipLevels = static_cast<std::uint32_t>(levels);
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!vk_check(vkCreateImage(vk.device, &info, nullptr, &out.image), "create image")) {
        return false;
    }
    VkMemoryRequirements needs;
    vkGetImageMemoryRequirements(vk.device, out.image, &needs);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = needs.size;
    allocate.memoryTypeIndex =
        vk_memory_type(vk, needs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!vk_check(vkAllocateMemory(vk.device, &allocate, nullptr, &out.memory),
                  "allocate image memory")) {
        return false;
    }
    vkBindImageMemory(vk.device, out.image, out.memory, 0);
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = out.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {aspect, 0, static_cast<std::uint32_t>(levels), 0, 1};
    if (!vk_check(vkCreateImageView(vk.device, &view, nullptr, &out.view), "create image view")) {
        return false;
    }
    out.format = format;
    out.width = width;
    out.height = height;
    out.levels = levels;
    return true;
}

void vk_destroy_image(RendererState& vk, GpuImage& image) {
    if (image.depth_view) {
        vkDestroyImageView(vk.device, image.depth_view, nullptr);
    }
    if (image.view) {
        vkDestroyImageView(vk.device, image.view, nullptr);
    }
    if (image.image) {
        vkDestroyImage(vk.device, image.image, nullptr);
    }
    if (image.memory) {
        vkFreeMemory(vk.device, image.memory, nullptr);
    }
    image = {};
}

VkCommandBuffer vk_begin_once(RendererState& vk) {
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = vk.pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(vk.device, &allocate, &command);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(command, &begin);
    return command;
}

bool vk_end_once(RendererState& vk, VkCommandBuffer command) {
    vkEndCommandBuffer(command);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    const bool ok = vk_check(vkQueueSubmit(vk.queue, 1, &submit, VK_NULL_HANDLE), "submit") &&
                    vk_check(vkQueueWaitIdle(vk.queue), "wait");
    vkFreeCommandBuffers(vk.device, vk.pool, 1, &command);
    return ok;
}

VkCommandBuffer vk_upload_command(RendererState& vk) {
    if (!vk.uploads) {
        vk.uploads = vk_begin_once(vk);
    }
    return vk.uploads;
}

void vk_upload_keep(RendererState& vk, GpuBuffer& staging) {
    vk.upload_bytes += staging.size;
    vk.upload_staging.push_back(staging);
    staging = {};
    // a long load runs its uploads in batches, so staging memory stays bounded
    if (vk.upload_bytes > (64u << 20)) {
        vk_flush_uploads(vk);
    }
}

bool vk_flush_uploads(RendererState& vk) {
    if (!vk.uploads) {
        return true;
    }
    const bool ok = vk_end_once(vk, vk.uploads);
    vk.uploads = VK_NULL_HANDLE;
    for (GpuBuffer& staging : vk.upload_staging) {
        vk_destroy_buffer(vk, staging);
    }
    vk.upload_staging.clear();
    vk.upload_bytes = 0;
    return ok;
}

void vk_barrier(VkCommandBuffer command, VkImage image, VkImageAspectFlags aspect,
                VkImageLayout from, VkImageLayout to, int levels) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, static_cast<std::uint32_t>(levels), 0, 1};
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
}

namespace {

// Vulkan didn't start: the reason in the log and a box a player reads (a Windows release has no
// console), with what to do
void vulkan_failure_box(const std::string& reason, SDL_Window* window) {
    std::fprintf(stderr, "vulkan: %s\n", reason.c_str());
    SDL_Log("vulkan: %s", reason.c_str());
    const std::string text =
        "OpenPCrash draws with Vulkan, and it couldn't start here: " + reason +
        ".\n\nVulkan comes with your graphics card's driver. Install the latest driver from the "
        "card's maker (NVIDIA, AMD or Intel), then start OpenPCrash again.";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenPCrash", text.c_str(), window);
}

bool start_vulkan(Renderer& renderer, SDL_Window* window, std::string& error) {
    renderer.vk = new RendererState;
    RendererState& vk = *renderer.vk;
    vk.window = window;

    // instance, with what SDL needs for the window's surface
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "OpenPCrash";
    // 1.1 for the negative viewport height (docs/shaders.md, "Conventions")
    app.apiVersion = VK_API_VERSION_1_1;
    Uint32 extension_count = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
    VkInstanceCreateInfo instance{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance.pApplicationInfo = &app;
    instance.enabledExtensionCount = extension_count;
    instance.ppEnabledExtensionNames = extensions;
    const char* validation = "VK_LAYER_KHRONOS_validation";
    if (SDL_getenv("PHANTOMCRASH_VK_VALIDATION")) {
        instance.enabledLayerCount = 1;
        instance.ppEnabledLayerNames = &validation;
    }
    if (!vk_check(vkCreateInstance(&instance, nullptr, &vk.instance), "create instance") ||
        !SDL_Vulkan_CreateSurface(window, vk.instance, nullptr, &vk.surface)) {
        error = SDL_GetError();
        return false;
    }

    // the device: a discrete GPU over an integrated one over a CPU one, with a queue that draws and
    // presents
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(vk.instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(vk.instance, &count, devices.data());
    int best = -1;
    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);
        std::uint32_t queue_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queue_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, queues.data());
        for (std::uint32_t q = 0; q < queue_count; ++q) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, q, vk.surface, &present);
            if (!present || !(queues[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                continue;
            }
            const int rank = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
                             : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                                                                                               : 1;
            if (rank > best) {
                best = rank;
                vk.physical = device;
                vk.family = q;
            }
        }
    }
    if (!vk.physical) {
        error = "no Vulkan device that can draw to the window";
        return false;
    }
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(vk.physical, &properties);
    std::printf("Vulkan device: %s\n", properties.deviceName);
    vk.uniform_alignment = properties.limits.minUniformBufferOffsetAlignment;
    float priority = 1;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = vk.family;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    const char* swapchain_extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    // the cooked textures are BC1, BC3 and BC7 (src/cook, docs/assets.md)
    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(vk.physical, &supported);
    if (!supported.textureCompressionBC) {
        error = "the Vulkan device can't sample BC-compressed textures";
        return false;
    }
    VkPhysicalDeviceFeatures features{};
    features.textureCompressionBC = VK_TRUE;
    // anisotropic filtering for the PC's texture filtering setting, where the device has it
    if (supported.samplerAnisotropy) {
        features.samplerAnisotropy = VK_TRUE;
        vk.max_anisotropy = properties.limits.maxSamplerAnisotropy;
    }
    VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device.queueCreateInfoCount = 1;
    device.pQueueCreateInfos = &queue;
    device.enabledExtensionCount = 1;
    device.ppEnabledExtensionNames = &swapchain_extension;
    device.pEnabledFeatures = &features;
    if (!vk_check(vkCreateDevice(vk.physical, &device, nullptr, &vk.device), "create device")) {
        error = SDL_GetError();
        return false;
    }
    vkGetDeviceQueue(vk.device, vk.family, 0, &vk.queue);

    // one frame's command buffer, its fence and the swapchain's two semaphores
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.queueFamilyIndex = vk.family;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    if (!vk_check(vkCreateCommandPool(vk.device, &pool, nullptr, &vk.pool), "command pool")) {
        error = SDL_GetError();
        return false;
    }
    command.commandPool = vk.pool;
    command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command.commandBufferCount = 1;
    if (!vk_check(vkAllocateCommandBuffers(vk.device, &command, &vk.command), "command buffer") ||
        !vk_check(vkCreateFence(vk.device, &fence, nullptr, &vk.fence), "fence") ||
        !vk_check(vkCreateSemaphore(vk.device, &semaphore, nullptr, &vk.acquired), "semaphore") ||
        !vk_check(vkCreateSemaphore(vk.device, &semaphore, nullptr, &vk.finished), "semaphore") ||
        !vk_create_shading(vk)) {
        error = SDL_GetError();
        return false;
    }
    // a white texel for untextured stages (texture_white), before anything binds textures
    const std::vector<std::uint8_t> white(4, 255);
    if (renderer_upload_texture(renderer, 1, 1, white) != texture_white ||
        !vk_create_composite(vk) || !vk_fit_window(vk, renderer)) {
        error = SDL_GetError();
        return false;
    }
    return true;
}

} // namespace

bool vulkan_loader_check() {
#ifdef _WIN32
    SDL_SharedObject* loader = SDL_LoadObject("vulkan-1.dll");
    if (!loader) {
        vulkan_failure_box("this computer has no Vulkan driver (vulkan-1.dll is missing)", nullptr);
        return false;
    }
    SDL_UnloadObject(loader);
#endif
    return true;
}

bool renderer_init(Renderer& renderer, SDL_Window* window, std::string& error) {
    if (start_vulkan(renderer, window, error)) {
        return true;
    }
    vulkan_failure_box(error, window);
    return false;
}

void renderer_shutdown(Renderer& renderer) {
    if (!renderer.vk) {
        return;
    }
    RendererState& vk = *renderer.vk;
    if (vk.device) {
        vk_flush_uploads(vk);
        vkDeviceWaitIdle(vk.device);
        for (GpuMesh& mesh : vk.meshes) {
            vk_destroy_buffer(vk, mesh.vertices);
            vk_destroy_buffer(vk, mesh.indices);
        }
        for (GpuImage& texture : vk.textures) {
            vk_destroy_image(vk, texture);
        }
        vk_release_freed(vk);
        vk_sprites_destroy(vk);
        vk_destroy_targets(vk);
        vk_destroy_shading(vk);
        if (vk.swapchain) {
            vkDestroySwapchainKHR(vk.device, vk.swapchain, nullptr);
        }
        vkDestroySemaphore(vk.device, vk.acquired, nullptr);
        vkDestroySemaphore(vk.device, vk.finished, nullptr);
        vkDestroyFence(vk.device, vk.fence, nullptr);
        vkDestroyCommandPool(vk.device, vk.pool, nullptr);
        vkDestroyDevice(vk.device, nullptr);
    }
    if (vk.surface) {
        vkDestroySurfaceKHR(vk.instance, vk.surface, nullptr);
    }
    if (vk.instance) {
        vkDestroyInstance(vk.instance, nullptr);
    }
    delete renderer.vk;
    renderer.vk = nullptr;
}
