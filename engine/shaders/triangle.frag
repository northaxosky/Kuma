#version 450

// ── Inputs ─────────────────────────────────────────────
// This value was output by the vertex shader and interpolated
// across the triangle by the rasterizer. Each pixel gets a
// slightly different blend of the three vertex colors.

layout(location = 0) in vec3 frag_color;

// ── Outputs ────────────────────────────────────────────
// location 0 = the first (and only) color attachment in our
// render pass. This is the swapchain image — the screen.

layout(location = 0) out vec4 out_color;

// ── Main ───────────────────────────────────────────────
// Runs once per pixel covered by the triangle.

void main() {
    // Output the interpolated color with full opacity.
    out_color = vec4(frag_color, 1.0);
}
