// ── Swapchain ───────────────────────────────────────────────────
// Swapchain creation/destruction, render pass, framebuffers,
// and surface format/present mode/extent helpers.

#include "renderer_impl.h"

namespace kuma {

// ── Swapchain ───────────────────────────────────────────────────

bool RendererImpl::create_swapchain() {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &capabilities);

    VkSurfaceFormatKHR format = choose_surface_format();
    VkPresentModeKHR present_mode = choose_present_mode();
    VkExtent2D extent = choose_extent();

    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = surface_;
    create_info.minImageCount = image_count;
    create_info.imageFormat = format.format;
    create_info.imageColorSpace = format.colorSpace;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.preTransform = capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = VK_NULL_HANDLE;

    VkResult result = vkCreateSwapchainKHR(device_, &create_info, nullptr, &swapchain_);
    if (result != VK_SUCCESS) {
        kuma::log::error("Failed to create swapchain (error %d)", result);
        return false;
    }

    swapchain_format_ = format.format;
    swapchain_extent_ = extent;

    vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr);
    swapchain_images_.resize(image_count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, swapchain_images_.data());

    swapchain_image_views_.resize(image_count);
    for (uint32_t i = 0; i < image_count; i++) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = swapchain_images_[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = swapchain_format_;
        view_info.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        result = vkCreateImageView(device_, &view_info, nullptr, &swapchain_image_views_[i]);
        if (result != VK_SUCCESS) {
            kuma::log::error("Failed to create image view %u", i);
            return false;
        }
    }

    kuma::log::info("Swapchain created: %ux%u (%u images)", extent.width, extent.height,
                    image_count);

    if (!create_depth_resources()) {
        return false;
    }
    return true;
}

// ── Depth Attachment ────────────────────────────────────────────
// Allocated alongside the swapchain (matching resolution), never
// sampled, never read back. The render pass clears it to 1.0 every
// frame via load_op = CLEAR and discards on store_op = DONT_CARE
// since nothing else needs the depth values once the frame ships.
//
// Format is VK_FORMAT_D32_SFLOAT - the only depth format Vulkan
// guarantees on every implementation, and float storage hands more
// precision to geometry near the camera (where perspective
// projection clusters depth values most aggressively).

bool RendererImpl::create_depth_resources() {
    VkImageCreateInfo img_info{};
    img_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType     = VK_IMAGE_TYPE_2D;
    img_info.extent.width  = swapchain_extent_.width;
    img_info.extent.height = swapchain_extent_.height;
    img_info.extent.depth  = 1;
    img_info.mipLevels     = 1;
    img_info.arrayLayers   = 1;
    img_info.format        = depth_format_;
    img_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    img_info.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    img_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    img_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device_, &img_info, nullptr, &depth_image_) != VK_SUCCESS) {
        kuma::log::error("Failed to create depth image");
        return false;
    }

    VkMemoryRequirements mem_reqs{};
    vkGetImageMemoryRequirements(device_, depth_image_, &mem_reqs);
    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize  = mem_reqs.size;
    alloc_info.memoryTypeIndex =
        find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device_, &alloc_info, nullptr, &depth_memory_) != VK_SUCCESS) {
        kuma::log::error("Failed to allocate depth memory");
        return false;
    }
    vkBindImageMemory(device_, depth_image_, depth_memory_, 0);

    VkImageViewCreateInfo view_info{};
    view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image                           = depth_image_;
    view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format                          = depth_format_;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;
    if (vkCreateImageView(device_, &view_info, nullptr, &depth_view_) != VK_SUCCESS) {
        kuma::log::error("Failed to create depth image view");
        return false;
    }

    return true;
}

void RendererImpl::destroy_depth_resources() {
    if (depth_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, depth_view_, nullptr);
        depth_view_ = VK_NULL_HANDLE;
    }
    if (depth_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, depth_image_, nullptr);
        depth_image_ = VK_NULL_HANDLE;
    }
    if (depth_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, depth_memory_, nullptr);
        depth_memory_ = VK_NULL_HANDLE;
    }
}

void RendererImpl::destroy_swapchain() {
    for (auto fb : framebuffers_) {
        vkDestroyFramebuffer(device_, fb, nullptr);
    }
    framebuffers_.clear();

    destroy_depth_resources();

    for (auto view : swapchain_image_views_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    swapchain_image_views_.clear();
    swapchain_images_.clear();

    for (auto sem : render_finished_semaphores_) {
        vkDestroySemaphore(device_, sem, nullptr);
    }
    render_finished_semaphores_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

bool RendererImpl::recreate_swapchain() {
    vkDeviceWaitIdle(device_);
    destroy_swapchain();

    if (!create_swapchain())
        return false;
    if (!create_framebuffers())
        return false;

    uint32_t image_count = static_cast<uint32_t>(swapchain_images_.size());
    render_finished_semaphores_.resize(image_count);
    VkSemaphoreCreateInfo sem_info{};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < image_count; i++) {
        vkCreateSemaphore(device_, &sem_info, nullptr, &render_finished_semaphores_[i]);
    }

    return true;
}

// ── Render Pass ─────────────────────────────────────────────────

bool RendererImpl::create_render_pass() {
    VkAttachmentDescription color_attachment{};
    color_attachment.format = swapchain_format_;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Depth attachment lives in slot 1. CLEAR every frame, DONT_CARE
    // on store - nothing reads depth after the frame submits, so the
    // driver is free to skip writing it back to memory.
    VkAttachmentDescription depth_attachment{};
    depth_attachment.format         = depth_format_;
    depth_attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_ref{};
    depth_ref.attachment = 1;
    depth_ref.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    // Subpass dependency now spans both color output AND the
    // early/late fragment-test stages where the depth load_op runs.
    // Without including the depth stages, the render pass could
    // start clearing depth before previous frames' depth reads
    // complete, producing validation errors.
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {color_attachment, depth_attachment};

    VkRenderPassCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create_info.attachmentCount = static_cast<uint32_t>(attachments.size());
    create_info.pAttachments = attachments.data();
    create_info.subpassCount = 1;
    create_info.pSubpasses = &subpass;
    create_info.dependencyCount = 1;
    create_info.pDependencies = &dependency;

    VkResult result = vkCreateRenderPass(device_, &create_info, nullptr, &render_pass_);
    if (result != VK_SUCCESS) {
        kuma::log::error("Failed to create render pass");
        return false;
    }

    return true;
}

// ── Framebuffers ────────────────────────────────────────────────

bool RendererImpl::create_framebuffers() {
    framebuffers_.resize(swapchain_image_views_.size());

    for (size_t i = 0; i < swapchain_image_views_.size(); i++) {
        // Two attachments per framebuffer: the per-image swapchain
        // color view (different for each framebuffer) and the single
        // shared depth view (same for every framebuffer because we
        // only use one frame in flight's worth of depth at a time -
        // the render pass clears it on each begin).
        std::array<VkImageView, 2> attachments = {swapchain_image_views_[i], depth_view_};

        VkFramebufferCreateInfo fb_info{};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass = render_pass_;
        fb_info.attachmentCount = static_cast<uint32_t>(attachments.size());
        fb_info.pAttachments = attachments.data();
        fb_info.width = swapchain_extent_.width;
        fb_info.height = swapchain_extent_.height;
        fb_info.layers = 1;

        VkResult result = vkCreateFramebuffer(device_, &fb_info, nullptr, &framebuffers_[i]);
        if (result != VK_SUCCESS) {
            kuma::log::error("Failed to create framebuffer %zu", i);
            return false;
        }
    }

    return true;
}

// ── Helpers ─────────────────────────────────────────────────────

VkSurfaceFormatKHR RendererImpl::choose_surface_format() const {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &count, formats.data());

    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    return formats[0];
}

VkPresentModeKHR RendererImpl::choose_present_mode() const {
    // Map the engine-level PresentMode to a Vulkan enum, then verify
    // the GPU supports it. FIFO is the only mode the Vulkan spec
    // guarantees on every implementation, so it's the universal
    // fallback when the user's first choice isn't available.
    VkPresentModeKHR preferred = VK_PRESENT_MODE_FIFO_KHR;
    const char* preferred_name = "FIFO (vsync)";
    switch (requested_present_mode_) {
    case PresentMode::Vsync:
        preferred = VK_PRESENT_MODE_FIFO_KHR;
        preferred_name = "FIFO (vsync)";
        break;
    case PresentMode::Mailbox:
        preferred = VK_PRESENT_MODE_MAILBOX_KHR;
        preferred_name = "MAILBOX (low-latency, uncapped)";
        break;
    case PresentMode::Immediate:
        preferred = VK_PRESENT_MODE_IMMEDIATE_KHR;
        preferred_name = "IMMEDIATE (uncapped, may tear)";
        break;
    }

    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &count, modes.data());

    for (const auto& mode : modes) {
        if (mode == preferred) {
            kuma::log::info("Present mode: %s", preferred_name);
            return mode;
        }
    }

    if (preferred != VK_PRESENT_MODE_FIFO_KHR) {
        kuma::log::warn("Present mode %s not supported, falling back to FIFO (vsync)",
                        preferred_name);
    } else {
        kuma::log::info("Present mode: FIFO (vsync)");
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D RendererImpl::choose_extent() const {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &capabilities);

    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    VkExtent2D extent = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_)};

    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width,
                              capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height,
                               capabilities.maxImageExtent.height);

    return extent;
}

}  // namespace kuma
