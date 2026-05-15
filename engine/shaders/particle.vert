#version 450

// Particle vertex shader.
//
// Two vertex bindings:
//   binding 0 (per-vertex)   = corner of a unit quad in [-0.5, 0.5]^2
//   binding 1 (per-instance) = position, size, color of one particle
//
// The shader builds a camera-facing quad without any per-particle CPU
// rotation work: it pulls the camera right + up vectors directly from
// the view matrix's first two rows, then projects
//
//   world = particle_position + cam_right * corner.x * size
//                             + cam_up    * corner.y * size
//
// into clip space via the view-projection matrix.

layout(location = 0) in vec2 corner;             // per-vertex
layout(location = 1) in vec3 instance_position;  // per-instance
layout(location = 2) in float instance_size;
layout(location = 3) in vec4 instance_color;

layout(push_constant) uniform PushConstants {
    mat4 view_projection;
    vec4 camera_right;   // xyz used; w padding
    vec4 camera_up;      // xyz used; w padding
} pc;

layout(location = 0) out vec2 frag_uv;
layout(location = 1) out vec4 frag_color;

void main() {
    vec3 world_pos = instance_position
                   + pc.camera_right.xyz * corner.x * instance_size
                   + pc.camera_up.xyz    * corner.y * instance_size;

    gl_Position = pc.view_projection * vec4(world_pos, 1.0);

    // Map corner from [-0.5, 0.5] to [0, 1] for sampling the diffuse texture.
    frag_uv    = corner + vec2(0.5);
    frag_color = instance_color;
}
