#version 450

// Debug visualizer: maps the (signed) vertex normal into RGB. A
// face pointing along +Y shows up as green-tinted, +X red, +Z blue,
// etc. Intentionally NOT a real material - this is a placeholder
// pipeline used to render meshes that don't have a texture
// assigned, so they're visibly distinct from the textured pipeline.

layout(location = 0) in vec3 frag_normal;
layout(location = 0) out vec4 out_color;

void main() {
    // Remap normal from [-1, 1] -> [0, 1].
    out_color = vec4(normalize(frag_normal) * 0.5 + 0.5, 1.0);
}