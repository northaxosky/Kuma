#version 450

// Same vertex layout as quad.vert (position, uv, normal). This
// shader passes the normal through unchanged for the debug
// fragment shader to visualize as RGB.

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_normal;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
} push;

layout(location = 0) out vec3 frag_normal;

void main() {
    gl_Position = push.mvp * vec4(in_position, 1.0);
    frag_normal = in_normal;
}