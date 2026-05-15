// ── Graphics Pipeline ───────────────────────────────────────────
// Pipeline creation, shader module loading, and file I/O.

#include "renderer_impl.h"

#include "platform/paths.h"

namespace kuma {

// ── File I/O ────────────────────────────────────────────────────

std::vector<char> read_binary_file(const char* path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        kuma::log::error("Failed to open file: %s", path);
        return {};
    }

    auto file_size = file.tellg();
    std::vector<char> buffer(static_cast<size_t>(file_size));

    file.seekg(0);
    file.read(buffer.data(), file_size);

    return buffer;
}

// ── Shader Modules ──────────────────────────────────────────────

VkShaderModule RendererImpl::create_shader_module(const std::vector<char>& code) const {
    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = code.size();
    create_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shader_module = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(device_, &create_info, nullptr, &shader_module);
    if (result != VK_SUCCESS) {
        kuma::log::error("Failed to create shader module");
        return VK_NULL_HANDLE;
    }

    return shader_module;
}

// ── Graphics Pipelines ──────────────────────────────────────────
// Two pipelines share everything except shaders:
//   pipeline 0 = textured quad shader (samples sampler2D)
//   pipeline 1 = debug-normal viz shader (no texture sample)
// Same vertex layout, same descriptor set layout, same push constant
// range. The pipeline LAYOUT is shared; only the shader-stage modules
// and the resulting VkPipeline differ.

// ── Pipeline build options ──────────────────────────────────────
// Captures the small set of state that varies between pipelines so
// build_pipeline can stay one function. Defaults match the original
// opaque-mesh pipelines that shipped before particles existed; the
// new transparent particle pipeline overrides them.
struct PipelineCreateOptions {
    bool depth_write = true;     // opaque writes; transparent doesn't (preserves the
                                 // depth values opaque draws established for blend tests)
    bool alpha_blend = false;    // opaque overwrites; transparent blends
    bool instanced   = false;    // particles use a per-instance vertex binding;
                                 // mesh pipelines stick with per-vertex only
};

bool RendererImpl::create_graphics_pipelines() {
    graphics_pipeline_ = build_pipeline("shaders/quad.vert.spv",
                                        "shaders/quad.frag.spv", {});
    if (graphics_pipeline_ == VK_NULL_HANDLE) return false;

    debug_normal_pipeline_ =
        build_pipeline("shaders/debug_normal.vert.spv",
                       "shaders/debug_normal.frag.spv", {});
    if (debug_normal_pipeline_ == VK_NULL_HANDLE) return false;

    PipelineCreateOptions particle_opts{};
    particle_opts.depth_write = false;
    particle_opts.alpha_blend = true;
    particle_opts.instanced   = true;
    transparent_pipeline_ = build_pipeline("shaders/particle.vert.spv",
                                           "shaders/particle.frag.spv",
                                           particle_opts);
    if (transparent_pipeline_ == VK_NULL_HANDLE) return false;

    kuma::log::info("Graphics pipelines created (textured + debug normal + particle)");
    return true;
}

VkPipeline RendererImpl::build_pipeline(const char* vert_spv, const char* frag_spv,
                                        const PipelineCreateOptions& opts) {
    auto vert_code = read_binary_file(platform::exe_relative(vert_spv).c_str());
    auto frag_code = read_binary_file(platform::exe_relative(frag_spv).c_str());

    if (vert_code.empty() || frag_code.empty()) {
        kuma::log::error("Failed to load shader files: %s / %s", vert_spv, frag_spv);
        return VK_NULL_HANDLE;
    }

    VkShaderModule vert_module = create_shader_module(vert_code);
    VkShaderModule frag_module = create_shader_module(frag_code);

    if (vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    // Shader stages
    VkPipelineShaderStageCreateInfo vert_stage{};
    vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage.module = vert_module;
    vert_stage.pName = "main";

    VkPipelineShaderStageCreateInfo frag_stage{};
    frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage.module = frag_module;
    frag_stage.pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = {vert_stage, frag_stage};

    // Vertex input layout - varies between mesh pipelines (one
    // per-vertex binding) and the particle pipeline (one per-vertex
    // for the unit quad, one per-instance for per-particle data).
    VkVertexInputBindingDescription                  bindings[2]{};
    std::array<VkVertexInputAttributeDescription, 5> attr_descs{};
    uint32_t binding_count = 0;
    uint32_t attr_count    = 0;

    if (opts.instanced) {
        // Particle pipeline:
        //   binding 0 = 4 unit-quad corners    (vec2, per-vertex)
        //   binding 1 = per-particle instance  (vec3 pos + float size + vec4 color)
        bindings[0].binding   = 0;
        bindings[0].stride    = sizeof(ParticleQuadVertex);
        bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        bindings[1].binding   = 1;
        bindings[1].stride    = sizeof(ParticleInstance);
        bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

        binding_count = 2;

        attr_descs[0].binding  = 0;
        attr_descs[0].location = 0;
        attr_descs[0].format   = VK_FORMAT_R32G32_SFLOAT;
        attr_descs[0].offset   = offsetof(ParticleQuadVertex, corner);

        attr_descs[1].binding  = 1;
        attr_descs[1].location = 1;
        attr_descs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
        attr_descs[1].offset   = offsetof(ParticleInstance, position);

        attr_descs[2].binding  = 1;
        attr_descs[2].location = 2;
        attr_descs[2].format   = VK_FORMAT_R32_SFLOAT;
        attr_descs[2].offset   = offsetof(ParticleInstance, size);

        attr_descs[3].binding  = 1;
        attr_descs[3].location = 3;
        attr_descs[3].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
        attr_descs[3].offset   = offsetof(ParticleInstance, color);

        attr_count = 4;
    } else {
        // Mesh pipeline: one per-vertex binding for the standard
        // position+uv+normal vertex layout shared with the bake.
        bindings[0].binding   = 0;
        bindings[0].stride    = sizeof(Vertex);
        bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        binding_count = 1;

        attr_descs[0].binding  = 0;
        attr_descs[0].location = 0;
        attr_descs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
        attr_descs[0].offset   = offsetof(Vertex, pos);

        attr_descs[1].binding  = 0;
        attr_descs[1].location = 1;
        attr_descs[1].format   = VK_FORMAT_R32G32_SFLOAT;
        attr_descs[1].offset   = offsetof(Vertex, uv);

        attr_descs[2].binding  = 0;
        attr_descs[2].location = 2;
        attr_descs[2].format   = VK_FORMAT_R32G32B32_SFLOAT;
        attr_descs[2].offset   = offsetof(Vertex, normal);

        attr_count = 3;
    }

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = binding_count;
    vertex_input.pVertexBindingDescriptions    = bindings;
    vertex_input.vertexAttributeDescriptionCount = attr_count;
    vertex_input.pVertexAttributeDescriptions  = attr_descs.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    // Dynamic state
    std::array<VkDynamicState, 2> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state.pDynamicStates = dynamic_states.data();

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    // Particles render as camera-facing quads. Their CCW/CW winding
    // is decided by the vertex shader's corner output and there's no
    // meaningful "back" of a billboard, so we disable face culling
    // entirely for the instanced pipeline. Mesh pipelines still cull
    // backfaces - skips invisible interior triangles.
    rasterizer.cullMode = opts.instanced ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Color blending. Opaque pipelines do "new color replaces pixel";
    // transparent pipelines do standard alpha blend
    //   final = SRC.rgb * SRC.a + DST.rgb * (1 - SRC.a)
    // which is the equation expected for un-premultiplied source
    // textures. Premultiplied / additive / multiplicative blends are
    // followups (one per-emitter blend mode field would carry them).
    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (opts.alpha_blend) {
        blend_attachment.blendEnable         = VK_TRUE;
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.colorBlendOp        = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.alphaBlendOp        = VK_BLEND_OP_ADD;
    } else {
        blend_attachment.blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &blend_attachment;

    // Descriptor set layout + pipeline layout - both pipelines share
    // the same layout (5 combined-image-samplers at set 0 bindings 0..4,
    // mat4 push constant in vertex stage). Five bindings is the full
    // set of slots a glTF PBR material can supply (diffuse, normal,
    // metallic-roughness, occlusion, emissive); the current quad
    // shader only samples diffuse and the debug-normal pipeline reads
    // none of them, but having a stable layout means new shaders can
    // start sampling the other slots without touching the C++ side.
    if (descriptor_set_layout_ == VK_NULL_HANDLE) {
        std::array<VkDescriptorSetLayoutBinding, 5> sampler_bindings{};
        for (uint32_t i = 0; i < sampler_bindings.size(); ++i) {
            sampler_bindings[i].binding         = i;
            sampler_bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            sampler_bindings[i].descriptorCount = 1;
            sampler_bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        VkDescriptorSetLayoutCreateInfo layout_binding_info{};
        layout_binding_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_binding_info.bindingCount = static_cast<uint32_t>(sampler_bindings.size());
        layout_binding_info.pBindings    = sampler_bindings.data();

        if (vkCreateDescriptorSetLayout(device_, &layout_binding_info, nullptr,
                                        &descriptor_set_layout_) != VK_SUCCESS) {
            kuma::log::error("Failed to create descriptor set layout");
            vkDestroyShaderModule(device_, vert_module, nullptr);
            vkDestroyShaderModule(device_, frag_module, nullptr);
            return VK_NULL_HANDLE;
        }
    }

    if (pipeline_layout_ == VK_NULL_HANDLE) {
        // Push constant range: 96 bytes accessible from the vertex
        // stage. Layout is shared across all pipelines:
        //   bytes  0..63  = mat4 view_projection
        //                    (mesh shaders use this as MVP after
        //                    multiplying model in on the CPU side;
        //                    particle shader uses it as VP and adds
        //                    instance position in the vertex shader)
        //   bytes 64..79  = vec4 camera_right (xyz; w padding)
        //   bytes 80..95  = vec4 camera_up    (xyz; w padding)
        //
        // Mesh draws push only the first 64 bytes; particle draws
        // push all 96. Vulkan allows partial pushes against a larger
        // declared range, so existing mesh shaders see no change.
        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_range.offset = 0;
        push_range.size = sizeof(float) * 24;  // mat4 (16) + vec4 (4) + vec4 (4)

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &descriptor_set_layout_;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_range;

        if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &pipeline_layout_)
            != VK_SUCCESS) {
            kuma::log::error("Failed to create pipeline layout");
            vkDestroyShaderModule(device_, vert_module, nullptr);
            vkDestroyShaderModule(device_, frag_module, nullptr);
            return VK_NULL_HANDLE;
        }
    }

    // Depth state. Opaque pipelines write depth so meshes interact
    // correctly when mixed in the same frame. Transparent pipelines
    // (particles) test against the depth values opaque draws wrote
    // - that's why opaque must render first - but DON'T write,
    // because a transparent fragment shouldn't occlude the things
    // behind it. Compare op LESS = "smaller depth wins" since we
    // clear the buffer to 1.0.
    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable       = VK_TRUE;
    depth_stencil.depthWriteEnable      = opts.depth_write ? VK_TRUE : VK_FALSE;
    depth_stencil.depthCompareOp        = VK_COMPARE_OP_LESS;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable     = VK_FALSE;
    depth_stencil.minDepthBounds        = 0.0f;
    depth_stencil.maxDepthBounds        = 1.0f;

    VkResult result = VK_SUCCESS;

    // Create the pipeline
    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = static_cast<uint32_t>(shader_stages.size());
    pipeline_info.pStages = shader_stages.data();
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = pipeline_layout_;
    pipeline_info.renderPass = render_pass_;
    pipeline_info.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr,
                                       &pipeline);

    vkDestroyShaderModule(device_, vert_module, nullptr);
    vkDestroyShaderModule(device_, frag_module, nullptr);

    if (result != VK_SUCCESS) {
        kuma::log::error("Failed to create pipeline (%s / %s, error %d)", vert_spv, frag_spv,
                         result);
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

}  // namespace kuma
