#version 450

// ── Inputs ─────────────────────────────────────────────
// These come from the vertex buffer (our C++ triangle data).
// "location = 0" means the first attribute, "location = 1" the second.

layout(location = 0) in vec2 in_position;   // x, y
layout(location = 1) in vec3 in_color;      // r, g, b

// ── Outputs ────────────────────────────────────────────
// Passed to the fragment shader. The rasterizer interpolates
// these values across the triangle's surface automatically.

layout(location = 0) out vec3 frag_color;

// ── Main ───────────────────────────────────────────────
// Runs once per vertex (3 times for our triangle).

void main() {
    // gl_Position is a built-in output: the final position on screen.
    // It's a vec4(x, y, z, w) in "clip space" coordinates:
    //   x: -1 (left)  to +1 (right)
    //   y: -1 (top)   to +1 (bottom)
    //   z:  0 (near)  to  1 (far)
    //   w:  1 (no perspective divide — we're 2D for now)
    gl_Position = vec4(in_position, 0.0, 1.0);

    // Pass the color through to the fragment shader unchanged.
    frag_color = in_color;
}
