#version 450

// Particle fragment shader. Samples the bound diffuse texture and
// modulates by the per-particle color. The bound texture comes from
// the same 5-binding material descriptor set that opaque meshes use,
// so an emitter can either point at a real Material or use the
// renderer's default white via the particles module's helper.

layout(location = 0) in vec2 frag_uv;
layout(location = 1) in vec4 frag_color;

layout(set = 0, binding = 0) uniform sampler2D tex_diffuse;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = texture(tex_diffuse, frag_uv) * frag_color;
}
