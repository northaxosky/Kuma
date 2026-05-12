#include "core/debug_internal.h"

#include <kuma/log.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#include <array>
#include <filesystem>
#include <string>

namespace kuma::debug {

namespace {

// All ImGui state lives here so the API surface in debug.h stays clean.
struct DebugState {
    bool initialized = false;
    bool visible = false;
    Key toggle_key = Key::F3;

    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
};

DebugState g;

void apply_kuma_dark_style() {
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding   = 8.0f;
    s.ChildRounding    = 6.0f;
    s.FrameRounding    = 4.0f;
    s.GrabRounding     = 4.0f;
    s.PopupRounding    = 6.0f;
    s.ScrollbarRounding = 6.0f;
    s.TabRounding      = 4.0f;
    s.WindowPadding    = ImVec2(12, 12);
    s.FramePadding     = ImVec2(8, 4);
    s.ItemSpacing      = ImVec2(8, 6);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize  = 0.0f;
    s.ChildBorderSize  = 1.0f;
    s.PopupBorderSize  = 1.0f;

    auto rgba = [](int r, int g, int b, float a) {
        return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
    };
    auto& c = s.Colors;
    c[ImGuiCol_WindowBg]         = rgba( 20,  20,  24, 0.92f);
    c[ImGuiCol_ChildBg]          = rgba( 24,  24,  28, 0.50f);
    c[ImGuiCol_PopupBg]          = rgba( 28,  28,  32, 0.95f);
    c[ImGuiCol_TitleBg]          = rgba( 28,  28,  32, 1.00f);
    c[ImGuiCol_TitleBgActive]    = rgba( 32,  32,  36, 1.00f);
    c[ImGuiCol_TitleBgCollapsed] = rgba( 24,  24,  28, 0.80f);
    c[ImGuiCol_MenuBarBg]        = rgba( 28,  28,  32, 1.00f);
    c[ImGuiCol_FrameBg]          = rgba( 40,  40,  48, 0.65f);
    c[ImGuiCol_FrameBgHovered]   = rgba( 50,  50,  60, 0.80f);
    c[ImGuiCol_FrameBgActive]    = rgba( 60,  60,  72, 0.95f);
    c[ImGuiCol_Border]           = rgba(255, 255, 255, 0.08f);
    c[ImGuiCol_BorderShadow]     = rgba(  0,   0,   0, 0.00f);
    c[ImGuiCol_Text]             = rgba(245, 245, 245, 1.00f);
    c[ImGuiCol_TextDisabled]     = rgba(170, 170, 180, 1.00f);
    c[ImGuiCol_TextSelectedBg]   = rgba(  0, 120, 215, 0.45f);
    c[ImGuiCol_Separator]        = rgba(255, 255, 255, 0.10f);
    c[ImGuiCol_SeparatorHovered] = rgba(  0, 120, 215, 0.65f);
    c[ImGuiCol_SeparatorActive]  = rgba(  0, 120, 215, 0.85f);
    c[ImGuiCol_Header]           = rgba(  0, 120, 215, 0.40f);
    c[ImGuiCol_HeaderHovered]    = rgba(  0, 120, 215, 0.65f);
    c[ImGuiCol_HeaderActive]     = rgba(  0, 120, 215, 0.85f);
    c[ImGuiCol_Button]           = rgba(  0, 120, 215, 0.55f);
    c[ImGuiCol_ButtonHovered]    = rgba(  0, 130, 230, 0.85f);
    c[ImGuiCol_ButtonActive]     = rgba(  0, 110, 200, 1.00f);
    c[ImGuiCol_CheckMark]        = rgba(  0, 130, 230, 1.00f);
    c[ImGuiCol_SliderGrab]       = rgba(  0, 130, 230, 1.00f);
    c[ImGuiCol_SliderGrabActive] = rgba(  0, 145, 245, 1.00f);
    c[ImGuiCol_Tab]              = rgba( 32,  32,  36, 1.00f);
    c[ImGuiCol_TabHovered]       = rgba(  0, 120, 215, 0.65f);
    c[ImGuiCol_TabSelected]      = rgba(  0, 120, 215, 0.85f);
    c[ImGuiCol_TabDimmed]        = rgba( 28,  28,  32, 1.00f);
    c[ImGuiCol_TabDimmedSelected]= rgba( 40,  40,  48, 1.00f);
    c[ImGuiCol_PlotLines]        = rgba(  0, 180, 255, 1.00f);
    c[ImGuiCol_PlotLinesHovered] = rgba(255, 180,   0, 1.00f);
    c[ImGuiCol_PlotHistogram]    = rgba(  0, 180, 255, 0.85f);
    c[ImGuiCol_PlotHistogramHovered] = rgba(255, 180, 0, 1.00f);
    c[ImGuiCol_ScrollbarBg]      = rgba(  0,   0,   0, 0.20f);
    c[ImGuiCol_ScrollbarGrab]    = rgba( 60,  60,  72, 0.85f);
    c[ImGuiCol_ScrollbarGrabHovered] = rgba(80, 80, 96, 0.95f);
    c[ImGuiCol_ScrollbarGrabActive]  = rgba(100, 100, 120, 1.00f);
    c[ImGuiCol_NavHighlight]     = rgba(  0, 130, 230, 1.00f);
    c[ImGuiCol_DragDropTarget]   = rgba(  0, 180, 255, 0.90f);
}

void load_kuma_font() {
    ImGuiIO& io = ImGui::GetIO();

    // Try Cascadia Mono first (Windows 10/11 default), then Consolas
    // (older Windows). If both miss we fall back to ImGui's built-in
    // bitmap font and log a warning.
    const std::array<const char*, 4> candidates = {
        "C:/Windows/Fonts/CascadiaMono.ttf",
        "C:/Windows/Fonts/cascadiamono.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/Consolas.ttf",
    };

    for (const char* path : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            io.Fonts->AddFontFromFileTTF(path, 15.0f);
            return;
        }
    }
    kuma::log::warn("Debug overlay: no system mono font found; using ImGui default");
}

}  // namespace

bool init(const InitContext& ctx) {
    if (g.initialized) {
        kuma::log::warn("kuma::debug::init called twice; ignoring");
        return true;
    }

    g.device = ctx.device;

    // Separate descriptor pool sized generously for ImGui. ImGui mostly
    // needs combined-image-samplers (font texture + any user-uploaded
    // textures); 64 is overkill for a debug overlay but cheap.
    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 64;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 64;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;

    if (vkCreateDescriptorPool(ctx.device, &pool_info, nullptr, &g.descriptor_pool)
        != VK_SUCCESS) {
        kuma::log::error("Debug overlay: failed to create ImGui descriptor pool");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // don't write imgui.ini next to the exe
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    apply_kuma_dark_style();
    load_kuma_font();

    if (!ImGui_ImplSDL3_InitForVulkan(ctx.sdl_window)) {
        kuma::log::error("Debug overlay: ImGui_ImplSDL3_InitForVulkan failed");
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(ctx.device, g.descriptor_pool, nullptr);
        g.descriptor_pool = VK_NULL_HANDLE;
        return false;
    }

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.Instance        = ctx.instance;
    init_info.PhysicalDevice  = ctx.physical_device;
    init_info.Device          = ctx.device;
    init_info.QueueFamily     = ctx.queue_family;
    init_info.Queue           = ctx.queue;
    init_info.DescriptorPool  = g.descriptor_pool;
    init_info.RenderPass      = ctx.render_pass;
    init_info.Subpass         = 0;
    init_info.MinImageCount   = ctx.min_image_count;
    init_info.ImageCount      = ctx.image_count;
    init_info.MSAASamples     = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        kuma::log::error("Debug overlay: ImGui_ImplVulkan_Init failed");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(ctx.device, g.descriptor_pool, nullptr);
        g.descriptor_pool = VK_NULL_HANDLE;
        return false;
    }

    // ImGui 1.91 manages the font upload internally; just trigger it.
    ImGui_ImplVulkan_CreateFontsTexture();

    g.initialized = true;
    kuma::log::info("Debug overlay initialized (F3 to toggle)");
    return true;
}

void shutdown() {
    if (!g.initialized) return;

    // ImGui's Vulkan backend uses GPU resources we just submitted; wait
    // for them to drain before we tear anything down.
    vkDeviceWaitIdle(g.device);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (g.descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(g.device, g.descriptor_pool, nullptr);
        g.descriptor_pool = VK_NULL_HANDLE;
    }
    g.device = VK_NULL_HANDLE;
    g.initialized = false;
}

// ── Visibility (public API stubs - filled in next commit) ──────

void set_visible(bool visible) { g.visible = visible; }
bool is_visible()              { return g.visible; }
void toggle()                  { g.visible = !g.visible; }
void set_toggle_key(Key key)   { g.toggle_key = key; }

// ── Stats stubs (filled in commit 4) ───────────────────────────

float fps()                  { return 0.0f; }
float frame_time_ms()        { return 0.0f; }
float one_percent_low_ms()   { return 0.0f; }
const float* frame_time_history(std::size_t* out_count) {
    if (out_count) *out_count = 0;
    return nullptr;
}

// ── Frame integration stubs (filled in commit 3) ───────────────

void process_event(const SDL_Event& /*event*/) {}
void new_frame() {}
void render(VkCommandBuffer /*cmd*/) {}
void draw_default_panel() {}
void show_imgui_demo() {}

}  // namespace kuma::debug
