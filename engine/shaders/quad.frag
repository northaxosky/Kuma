#version 450

// ── Inputs from vertex shader ──────────────────────────────────
layout(location = 0) in vec2 frag_uv;

// ── Texture binding ────────────────────────────────────────────
// This is how shaders access textures in Vulkan:
//   set = 0    → first descriptor set
//   binding = 0 → first binding within that set
// "sampler2D" combines a texture image with sampling settings
// (filtering, wrapping, etc.) into one object.

layout(set = 0, binding = 0) uniform sampler2D tex_sampler;

// ── Output ─────────────────────────────────────────────────────
layout(location = 0) out vec4 out_color;

void main() {
    // texture() samples the image at the given UV coordinate.
    // The sampler controls what happens between pixels (filtering)
    // and at the edges (wrapping/clamping).
    out_color = texture(tex_sampler, frag_uv);
}
