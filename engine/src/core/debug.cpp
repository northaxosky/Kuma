#include "core/debug_internal.h"
#include "core/debug_test_hooks.h"

#include <kuma/input.h>
#include <kuma/log.h>
#include <kuma/time.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#include <algorithm>
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

    // Sampled at init from SDL. Drives the green/yellow/red frame-time
    // thresholds in the default panel so they match the user's monitor.
    // Hot-swapping monitors mid-session is a known limitation; logged
    // as a followup.
    float monitor_refresh_hz = 60.0f;

    // Stats. Keep ~120 samples (2 seconds @ 60fps) for the sparkline.
    static constexpr std::size_t kHistoryCapacity = 120;
    std::array<float, kHistoryCapacity> frame_ms_history{};   // ring buffer, ms
    std::size_t history_count = 0;       // how many slots are valid
    std::size_t history_head = 0;        // next write index

    // Exponential moving average smoothing. Smoothing factor chosen so
    // ~95% of weight comes from the last second of frames at 60fps
    // (alpha = 1 - exp(-dt / 0.5s)).
    float smoothed_frame_ms = 0.0f;
};

DebugState g;

// ── Stats helpers (pure functions, hammered by tests) ──────────

// Append a frame-time sample (in ms) to the ring buffer. When the
// buffer is full, oldest samples are overwritten.
void record_frame_sample(float frame_ms) {
    g.frame_ms_history[g.history_head] = frame_ms;
    g.history_head = (g.history_head + 1) % DebugState::kHistoryCapacity;
    if (g.history_count < DebugState::kHistoryCapacity) {
        ++g.history_count;
    }

    // EMA smoothing. Half-life ~0.5s -> alpha computed from frame_ms.
    if (g.smoothed_frame_ms == 0.0f) {
        g.smoothed_frame_ms = frame_ms;  // bootstrap on first sample
    } else {
        constexpr float kHalfLifeSec = 0.5f;
        const float dt_sec = frame_ms / 1000.0f;
        const float alpha = 1.0f - std::exp(-dt_sec / kHalfLifeSec);
        g.smoothed_frame_ms += (frame_ms - g.smoothed_frame_ms) * alpha;
    }
}

// Linearize the ring buffer into a contiguous oldest-first array
// for plotting. Cached between calls (rebuilt only when history_head
// changes, which is once per frame).
const float* linearize_history(std::size_t* out_count) {
    static std::array<float, DebugState::kHistoryCapacity> linear{};
    static std::size_t cached_head = SIZE_MAX;
    static std::size_t cached_count = 0;

    if (g.history_head != cached_head || g.history_count != cached_count) {
        cached_head = g.history_head;
        cached_count = g.history_count;
        if (g.history_count < DebugState::kHistoryCapacity) {
            // Buffer not yet full - history starts at index 0.
            for (std::size_t i = 0; i < g.history_count; ++i) {
                linear[i] = g.frame_ms_history[i];
            }
        } else {
            // Buffer full - oldest is at history_head (the next write slot).
            for (std::size_t i = 0; i < DebugState::kHistoryCapacity; ++i) {
                linear[i] = g.frame_ms_history[(g.history_head + i) % DebugState::kHistoryCapacity];
            }
        }
    }

    if (out_count) *out_count = g.history_count;
    return linear.data();
}

// 1% low = average of the worst (slowest = highest ms) 1% of recent
// samples. With 120 samples that's ~1-2 frames; we floor at 1.
float compute_one_percent_low_ms() {
    if (g.history_count == 0) return 0.0f;

    std::array<float, DebugState::kHistoryCapacity> sorted{};
    for (std::size_t i = 0; i < g.history_count; ++i) {
        sorted[i] = g.frame_ms_history[i];
    }
    std::sort(sorted.begin(), sorted.begin() + g.history_count, std::greater<float>{});

    const std::size_t n = std::max<std::size_t>(1, g.history_count / 100);
    float sum = 0.0f;
    for (std::size_t i = 0; i < n; ++i) sum += sorted[i];
    return sum / static_cast<float>(n);
}

// Test-visible helpers - exposed via the detail::debug_stats_for_testing
// namespace below so unit tests can drive them without ImGui being
// initialized. Production code paths call these through new_frame().
void reset_stats() {
    g.frame_ms_history.fill(0.0f);
    g.history_count = 0;
    g.history_head = 0;
    g.smoothed_frame_ms = 0.0f;
}

}  // namespace

namespace detail {

// Test hooks - not in the public header. Tests #include this file
// path or reach via debug_internal.h to call these directly. Keeps
// the production API clean while letting us hammer the pure math.
void debug_record_frame_sample_for_test(float frame_ms) { record_frame_sample(frame_ms); }
void debug_reset_stats_for_test() { reset_stats(); }
void debug_set_initialized_for_test(bool v) { g.initialized = v; }

}  // namespace detail

namespace {

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

    // Sample the monitor refresh rate so the panel's frame-time
    // thresholds match the user's hardware (60Hz baseline if SDL
    // can't tell us). Defaults to 60.0f if anything goes wrong.
    if (ctx.sdl_window) {
        const SDL_DisplayID display = SDL_GetDisplayForWindow(ctx.sdl_window);
        if (display != 0) {
            const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display);
            if (mode && mode->refresh_rate > 0.0f) {
                g.monitor_refresh_hz = mode->refresh_rate;
            }
        }
    }

    g.initialized = true;
    kuma::log::info("Debug overlay initialized (F3 to toggle, %.0fHz monitor)",
                    g.monitor_refresh_hz);
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

// ── Stats accessors ────────────────────────────────────────────

float fps() {
    return g.smoothed_frame_ms > 0.0f ? 1000.0f / g.smoothed_frame_ms : 0.0f;
}

float frame_time_ms() {
    return g.smoothed_frame_ms;
}

float one_percent_low_ms() {
    return compute_one_percent_low_ms();
}

const float* frame_time_history(std::size_t* out_count) {
    return linearize_history(out_count);
}

// ── Frame integration ──────────────────────────────────────────

void process_event(const SDL_Event& event) {
    if (!g.initialized) return;
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void new_frame() {
    if (!g.initialized) return;

    // Record the frame time first so stats reflect "previous frame".
    // delta() is 0 on frame 1 (no previous) - skip recording then.
    const float dt_ms = kuma::time::delta() * 1000.0f;
    if (dt_ms > 0.0f) {
        record_frame_sample(dt_ms);
    }

    // F3 (or whatever toggle key is configured) flips visibility.
    // Done AFTER input::begin_frame has snapshotted edges, BEFORE
    // user UPDATE so the F3-press-this-frame fires immediately.
    if (g.toggle_key != Key::Count && input::was_key_pressed(g.toggle_key)) {
        g.visible = !g.visible;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void render(VkCommandBuffer cmd) {
    if (!g.initialized) return;

    // ImGui::Render() finalizes the draw list whether or not anything
    // was actually drawn this frame - safe to call unconditionally.
    // RenderDrawData with a no-op draw list is also safe.
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data) {
        ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
    }
}

// ── Default panel ──────────────────────────────────────────────

// ── Color helpers + monitor-derived thresholds ─────────────────

float monitor_refresh_hz() { return g.monitor_refresh_hz; }

StatusThresholds frame_time_thresholds() {
    const float target_ms = 1000.0f / g.monitor_refresh_hz;
    // 1.5x = noticeably uneven (yellow), 2.0x = dropped frames (red).
    return StatusThresholds{
        .warn_above = target_ms * 1.5f,
        .bad_above  = target_ms * 2.0f,
    };
}

namespace {

// Picks a color for `value` given thresholds. `higher_is_better`
// inverts the comparison so FPS-style metrics (where bigger is
// better) work without the caller having to negate thresholds.
ImVec4 status_color(float value, StatusThresholds t, bool higher_is_better) {
    constexpr ImVec4 green  = ImVec4(0.40f, 0.85f, 0.40f, 1.0f);
    constexpr ImVec4 yellow = ImVec4(1.00f, 0.85f, 0.30f, 1.0f);
    constexpr ImVec4 red    = ImVec4(1.00f, 0.40f, 0.40f, 1.0f);

    bool warn, bad;
    if (higher_is_better) {
        warn = value < t.warn_above;
        bad  = value < t.bad_above;
    } else {
        warn = value > t.warn_above;
        bad  = value > t.bad_above;
    }
    if (bad)  return red;
    if (warn) return yellow;
    return green;
}

}  // namespace

void status_text(const char* label, const char* fmt, float value,
                 StatusThresholds thresholds, bool higher_is_better) {
    if (!g.initialized) return;
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::TextColored(status_color(value, thresholds, higher_is_better), fmt, value);
}

void section_header(const char* text) {
    if (!g.initialized) return;
    // Accent blue (matches the Kuma Dark style accent).
    constexpr ImVec4 accent = ImVec4(0.0f, 0.71f, 1.0f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::Separator();
}

// ── Default panel ──────────────────────────────────────────────

void draw_default_panel() {
    if (!g.initialized || !g.visible) return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 16.0f, vp->WorkPos.y + 16.0f),
                            ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(280.0f, 0.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Kuma Debug", nullptr, ImGuiWindowFlags_NoCollapse)) {
        section_header("Performance");

        const StatusThresholds frame_t = frame_time_thresholds();

        // FPS uses the same thresholds but inverted (higher is better).
        // The crossover points work out to the same wall-clock budget.
        const float warn_fps = 1000.0f / frame_t.warn_above;
        const float bad_fps  = 1000.0f / frame_t.bad_above;
        status_text("FPS:       ", "%.1f",   fps(),
                    {warn_fps, bad_fps}, /*higher_is_better=*/true);
        status_text("Frame:     ", "%.2f ms", frame_time_ms(),    frame_t);
        status_text("1%% low:   ", "%.2f ms", one_percent_low_ms(),
                    {frame_t.warn_above * 2.0f, frame_t.bad_above * 2.0f});

        ImGui::TextDisabled("Target: %.1fms (%.0fHz monitor)",
                            1000.0f / g.monitor_refresh_hz, g.monitor_refresh_hz);

        std::size_t count = 0;
        const float* history = frame_time_history(&count);
        if (history && count > 1) {
            ImGui::PlotLines("##frametime", history, static_cast<int>(count), 0, nullptr,
                             0.0f, FLT_MAX, ImVec2(0, 60));
        }
    }
    ImGui::End();
}

void show_imgui_demo() {
    if (!g.initialized || !g.visible) return;
    bool open = true;
    ImGui::ShowDemoWindow(&open);
}

}  // namespace kuma::debug
