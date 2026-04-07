#version 450

// ── Inputs from vertex buffer ──────────────────────────────────
layout(location = 0) in vec2 in_position;   // x, y (model space)
layout(location = 1) in vec2 in_uv;         // u, v (texture coordinates)

// ── Push constants ─────────────────────────────────────────────
// Small, fast data embedded directly in the command buffer.
// We use this for the MVP matrix (64 bytes = 16 floats).

layout(push_constant) uniform PushConstants {
    mat4 mvp;    // model-view-projection matrix
} push;

// ── Outputs to fragment shader ─────────────────────────────────
layout(location = 0) out vec2 frag_uv;

void main() {
    // Transform the vertex position through all three stages:
    //   model space → world space → camera space → clip space
    gl_Position = push.mvp * vec4(in_position, 0.0, 1.0);

    frag_uv = in_uv;
}
