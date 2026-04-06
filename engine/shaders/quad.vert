#version 450

// ── Inputs from vertex buffer ──────────────────────────────────
layout(location = 0) in vec2 in_position;   // x, y (clip space)
layout(location = 1) in vec2 in_uv;         // u, v (texture coordinates)

// ── Outputs to fragment shader ─────────────────────────────────
layout(location = 0) out vec2 frag_uv;

void main() {
    gl_Position = vec4(in_position, 0.0, 1.0);

    // Pass UV coordinates through — the rasterizer interpolates them
    // across the quad's surface, just like it interpolated colors
    // for the triangle.
    frag_uv = in_uv;
}
