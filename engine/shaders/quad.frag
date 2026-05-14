#version 450

// ── Inputs from vertex shader ──────────────────────────────────
layout(location = 0) in vec2 frag_uv;

// ── Material texture set ───────────────────────────────────────
// Five combined image-sampler bindings, one per glTF PBR texture
// slot. The current shader only samples diffuse - the remaining
// bindings exist so the descriptor set layout is stable when a
// future lit shader reads normal / metallic-roughness / occlusion /
// emissive from the same set.
//
// Materials that don't supply a particular slot get a small default
// texture from the renderer (white diffuse, etc), so every binding
// is always valid to sample. This keeps the shader free of branches
// and avoids a separate "untextured" pipeline.

layout(set = 0, binding = 0) uniform sampler2D tex_diffuse;
layout(set = 0, binding = 1) uniform sampler2D tex_normal;
layout(set = 0, binding = 2) uniform sampler2D tex_metallic_roughness;
layout(set = 0, binding = 3) uniform sampler2D tex_occlusion;
layout(set = 0, binding = 4) uniform sampler2D tex_emissive;

// ── Output ─────────────────────────────────────────────────────
layout(location = 0) out vec4 out_color;

void main() {
    out_color = texture(tex_diffuse, frag_uv);
}
