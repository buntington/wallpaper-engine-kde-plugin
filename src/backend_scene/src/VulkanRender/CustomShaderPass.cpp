#include "CustomShaderPass.hpp"
#include "Scene/Scene.h"
#include "Scene/SceneShader.h"

#include "SpecTexs.hpp"
#include "Vulkan/Shader.hpp"
#include "Utils/Logging.h"
#include "Utils/AutoDeletor.hpp"
#include "Resource.hpp"
#include "PassCommon.hpp"
#include "Interface/IImageParser.h"

#include "Core/ArrayHelper.hpp"

#include <cassert>

using namespace wallpaper::vulkan;

CustomShaderPass::CustomShaderPass(const Desc& desc) {
    m_desc.node        = desc.node;
    m_desc.textures    = desc.textures;
    m_desc.output      = desc.output;
    m_desc.sprites_map = desc.sprites_map;
    m_desc.depth       = desc.depth;
    m_desc.depth_clear = desc.depth_clear;
};
CustomShaderPass::~CustomShaderPass() {}

// Color plus a depth attachment, for 3D passes.
std::optional<vvk::RenderPass> CreateDepthRenderPass(const vvk::Device& device, VkFormat format,
                                                     VkAttachmentLoadOp loadOp, bool clear_depth) {
    std::array<VkAttachmentDescription, 2> attachments {
        VkAttachmentDescription {
            .format         = format,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = loadOp,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = loadOp == VK_ATTACHMENT_LOAD_OP_LOAD
                                  ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                  : VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        },
        VkAttachmentDescription {
            .format         = DepthTargets::Format,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = clear_depth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = clear_depth ? VK_IMAGE_LAYOUT_UNDEFINED
                                          : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
    };
    VkAttachmentReference color_ref { .attachment = 0,
                                      .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depth_ref { .attachment = 1,
                                      .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription  subpass {
         .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
         .colorAttachmentCount    = 1,
         .pColorAttachments       = &color_ref,
         .pDepthStencilAttachment = &depth_ref,
    };
    // Earlier passes may still write the color target and depth image.
    VkSubpassDependency dependency {
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    };
    VkRenderPassCreateInfo creatinfo {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = (uint32_t)attachments.size(),
        .pAttachments    = attachments.data(),
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = &dependency,
    };
    vvk::RenderPass pass;
    if (auto res = device.CreateRenderPass(creatinfo, pass); res == VK_SUCCESS) return pass;
    else {
        VVK_CHECK(res);
        return std::nullopt;
    }
}

std::optional<vvk::RenderPass> CreateRenderPass(const vvk::Device& device, VkFormat format,
                                                VkAttachmentLoadOp loadOp,
                                                VkImageLayout      finalLayout) {
    VkAttachmentDescription attachment {
        .format         = format,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = loadOp, // VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = finalLayout, // ShaderReadOnlyOptimal
    };

    if (loadOp == VK_ATTACHMENT_LOAD_OP_LOAD) {
        attachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkAttachmentReference attachment_ref {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass {
        .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &attachment_ref,
    };

    VkSubpassDependency dependency {
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = {},
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    VkRenderPassCreateInfo creatinfo {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &attachment,
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = &dependency,
    };
    vvk::RenderPass pass;
    if (auto res = device.CreateRenderPass(creatinfo, pass); res == VK_SUCCESS) {
        return pass;
    } else {
        VVK_CHECK(res);
        return std::nullopt;
    }
}

static void UpdateUniform(StagingBuffer* buf, const StagingBufferRef& bufref,
                          const ShaderReflected::Block& block, std::string_view name,
                          const wallpaper::ShaderValue& value) {
    using namespace wallpaper;
    std::span<uint8_t> value_u8 { (uint8_t*)value.data(),
                                  value.size() * sizeof(ShaderValue::value_type) };
    auto               uni = block.member_map.find(name);
    if (uni == block.member_map.end()) {
        // log
        return;
    }

    const auto& u      = uni->second;
    size_t      offset = u.offset;
    size_t      type_size = sizeof(float) * u.num;
    if (type_size != value_u8.size()) {
        // assert(type_size == value_u8.size());
        ; // to do
    }
    // mat4 values given to a mat3 uniform: write the upper-left 3x3, column by column.
    if (u.mat3_stride > 0 && value_u8.size() == 16 * sizeof(float)) {
        for (size_t c = 0; c < 3; c++)
            buf->writeToBuf(bufref, value_u8.subspan(c * 4 * sizeof(float), 3 * sizeof(float)),
                            offset + c * u.mat3_stride);
        return;
    }
    // Padded arrays (e.g. float[16] in std140, 16 bytes per element) given tightly packed
    // are written per element; data already laid out with the stride is copied as is.
    if (u.array_len > 0 && u.elem_size > 0 && u.array_stride > u.elem_size &&
        value_u8.size() < u.array_len * u.array_stride) {
        const size_t count = std::min(u.array_len, value_u8.size() / u.elem_size);
        for (size_t i = 0; i < count; i++)
            buf->writeToBuf(bufref, value_u8.subspan(i * u.elem_size, u.elem_size),
                            offset + i * u.array_stride);
        return;
    }
    buf->writeToBuf(bufref, value_u8, offset);
}

void CustomShaderPass::prepare(Scene& scene, const Device& device, RenderingResources& rr) {
    m_desc.vk_textures.resize(m_desc.textures.size());
    for (usize i = 0; i < m_desc.textures.size(); i++) {
        auto& tex_name = m_desc.textures[i];
        if (tex_name.empty()) continue;

        ImageSlotsRef img_slots;
        if (IsSpecTex(tex_name)) {
            if (scene.renderTargets.count(tex_name) == 0) continue;
            auto& rt  = scene.renderTargets.at(tex_name);
            auto  opt = device.tex_cache().Query(tex_name, ToTexKey(rt), ! rt.allowReuse);
            if (! opt.has_value()) continue;
            img_slots.slots = { opt.value() };
        } else {
            auto image = scene.imageParser->Parse(tex_name);
            if (image) {
                img_slots = device.tex_cache().CreateTex(*image);
            } else {
                LOG_ERROR("parse tex \"%s\" failed", tex_name.c_str());
            }
        }
        m_desc.vk_textures[i] = img_slots;
    }
    {
        auto& tex_name = m_desc.output;
        assert(IsSpecTex(tex_name));
        assert(scene.renderTargets.count(tex_name) > 0);
        auto& rt = scene.renderTargets.at(tex_name);
        if (auto opt = device.tex_cache().Query(tex_name, ToTexKey(rt), ! rt.allowReuse);
            opt.has_value()) {
            m_desc.vk_output = opt.value();
        } else
            return;
    }

    SceneMesh& mesh = *(m_desc.node->Mesh());

    std::vector<Uni_ShaderSpv> spvs;
    DescriptorSetInfo          descriptor_info;
    ShaderReflected            ref;
    {
        SceneShader& shader = *(mesh.Material()->customShader.shader);

        if (! GenReflect(shader.codes, spvs, ref)) {
            LOG_ERROR("gen spv reflect failed, %s", shader.name.c_str());
            return;
        }

        auto& bindings = descriptor_info.bindings;
        bindings.resize(ref.binding_map.size());

        /*
        LOG_INFO("----shader------");
        LOG_INFO("%s", shader.name.c_str());
        LOG_INFO("--inputs:");
        for (auto& i : ref.input_location_map) {
            LOG_INFO("%d %s", i.second, i.first.c_str());
        }
        LOG_INFO("--bindings:");
        */

        std::transform(
            ref.binding_map.begin(), ref.binding_map.end(), bindings.begin(), [](auto& item) {
                // LOG_INFO("%d %s", item.second.binding, item.first.c_str());
                return item.second;
            });

        for (usize i = 0; i < m_desc.vk_textures.size(); i++) {
            i32 binding { -1 };
            if (exists(ref.binding_map, WE_GLTEX_NAMES[i]))
                binding = (i32)ref.binding_map.at(WE_GLTEX_NAMES[i]).binding;
            m_desc.vk_tex_binding.push_back(binding);
        }
    }

    m_desc.draw_count = 0;
    std::vector<VkVertexInputBindingDescription>   bind_descriptions;
    std::vector<VkVertexInputAttributeDescription> attr_descriptions;
    {
        m_desc.dyn_vertex = mesh.Dynamic();
        m_desc.vertex_bufs.resize(mesh.VertexCount());

        for (uint i = 0; i < mesh.VertexCount(); i++) {
            const auto& vertex    = mesh.GetVertexArray(i);
            auto        attrs_map = vertex.GetAttrOffsetMap();

            VkVertexInputBindingDescription bind_desc {
                .binding   = i,
                .stride    = (uint32_t)vertex.OneSizeOf(),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            };
            bind_descriptions.push_back(bind_desc);

            for (auto& item : ref.input_location_map) {
                auto& name   = item.first;
                auto& input  = item.second;
                usize offset = exists(attrs_map, name) ? attrs_map[name].offset : 0;

                // Packed attributes keep their compact format; the GPU expands them to
                // the float type the shader declares.
                VkFormat format = input.format;
                if (exists(attrs_map, name)) {
                    switch (attrs_map[name].attr.type) {
                    case VertexType::SNORM8x4: format = VK_FORMAT_R8G8B8A8_SNORM; break;
                    case VertexType::UNORM8x4: format = VK_FORMAT_R8G8B8A8_UNORM; break;
                    case VertexType::HALF2: format = VK_FORMAT_R16G16_SFLOAT; break;
                    case VertexType::HALF4: format = VK_FORMAT_R16G16B16A16_SFLOAT; break;
                    case VertexType::SNORM16x4: format = VK_FORMAT_R16G16B16A16_SNORM; break;
                    case VertexType::UNORM16x2: format = VK_FORMAT_R16G16_UNORM; break;
                    case VertexType::UNORM16x4: format = VK_FORMAT_R16G16B16A16_UNORM; break;
                    default: break;
                    }
                }

                VkVertexInputAttributeDescription attr_desc {
                    .location = input.location,
                    .binding  = i,
                    .format   = format,
                    .offset   = (u32)offset,
                };
                attr_descriptions.push_back(attr_desc);
            }
            {
                auto& buf = m_desc.vertex_bufs[i];
                if (! m_desc.dyn_vertex) {
                    if (! rr.vertex_buf->allocateSubRef(vertex.CapacitySizeOf(), buf)) { LOG_ERROR("pass '%s': %s failed", m_desc.node->Mesh()->Material()->name.c_str(), "vertex buffer"); return; }
                    if (! rr.vertex_buf->writeToBuf(buf, { (uint8_t*)vertex.Data(), buf.size }))
                        { LOG_ERROR("pass '%s': %s failed", m_desc.node->Mesh()->Material()->name.c_str(), "vertex upload"); return; }
                } else {
                    if (! rr.dyn_buf->allocateSubRef(vertex.CapacitySizeOf(), buf)) { LOG_ERROR("pass '%s': %s failed", m_desc.node->Mesh()->Material()->name.c_str(), "dynamic vertex buffer"); return; }
                }
            }
            m_desc.draw_count += (u32)(vertex.DataSize() / vertex.OneSize());
        }

        if (mesh.IndexCount() > 0) {
            auto&  indice     = mesh.GetIndexArray(0);
            size_t count      = (indice.DataCount() * 2) / 3;
            m_desc.draw_count = (u32)count * 3;
            auto& buf         = m_desc.index_buf;
            if (! m_desc.dyn_vertex) {
                if (! rr.vertex_buf->allocateSubRef(indice.CapacitySizeof(), buf)) { LOG_ERROR("pass '%s': %s failed", m_desc.node->Mesh()->Material()->name.c_str(), "index buffer"); return; }
                if (! rr.vertex_buf->writeToBuf(buf, { (uint8_t*)indice.Data(), buf.size })) { LOG_ERROR("pass '%s': %s failed", m_desc.node->Mesh()->Material()->name.c_str(), "index upload"); return; }
            } else {
                if (! rr.dyn_buf->allocateSubRef(indice.CapacitySizeof(), buf)) { LOG_ERROR("pass '%s': %s failed", m_desc.node->Mesh()->Material()->name.c_str(), "dynamic index buffer"); return; }
            }
        }
    }
    {
        VkPipelineColorBlendAttachmentState color_blend;
        VkAttachmentLoadOp                  loadOp { VK_ATTACHMENT_LOAD_OP_DONT_CARE };
        {
            VkColorComponentFlags colorMask =
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
            // The screen stays opaque, and so do 3D model targets, whatever alpha the
            // shaders output (e.g. the translucent camera fade).
            bool alpha =
                ! (m_desc.node->Camera().empty() || sstart_with(m_desc.node->Camera(), "global")) &&
                ! m_desc.depth && m_desc.output != SpecTex_Default;

            if (alpha) colorMask |= VK_COLOR_COMPONENT_A_BIT;
            color_blend.colorWriteMask = colorMask;

            auto blendmode = mesh.Material()->blenmode;
            SetBlend(blendmode, color_blend);
            m_desc.blending = color_blend.blendEnable;

            SetAttachmentLoadOp(blendmode, loadOp);
            // models cover part of the target, so keep what is already drawn
            if (m_desc.depth) loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        }
        if (m_desc.depth) m_desc.depth_image = rr.depth->Get(m_desc.output, m_desc.vk_output.extent);
        if (m_desc.depth && m_desc.depth_image == nullptr) { LOG_ERROR("pass '%s': %s failed", m_desc.node->Mesh()->Material()->name.c_str(), "depth image"); return; }
        auto opt = m_desc.depth
                       ? CreateDepthRenderPass(
                             device.handle(), VK_FORMAT_R8G8B8A8_UNORM, loadOp, m_desc.depth_clear)
                       : CreateRenderPass(device.handle(),
                                          VK_FORMAT_R8G8B8A8_UNORM,
                                          loadOp,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (! opt.has_value()) { LOG_ERROR("pass '%s': %s failed", m_desc.node->Mesh()->Material()->name.c_str(), "render pass"); return; }
        auto& pass = opt.value();

        descriptor_info.push_descriptor = true;
        GraphicsPipeline pipeline;
        pipeline.toDefault();
        pipeline.addDescriptorSetInfo(spanone { descriptor_info })
            .setColorBlendStates(spanone { color_blend })
            .setTopology(m_desc.index_buf ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
                                          : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
            .addInputBindingDescription(bind_descriptions)
            .addInputAttributeDescription(attr_descriptions);
        for (auto& spv : spvs) pipeline.addStage(std::move(spv));
        if (m_desc.depth) {
            const auto* mat       = mesh.Material();
            pipeline.depth.depthTestEnable  = mat->depthTest;
            pipeline.depth.depthWriteEnable = mat->depthWrite;
            pipeline.depth.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;
            pipeline.raster.cullMode = mat->cullBack ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
        }

        if (! pipeline.create(device, pass, m_desc.pipeline)) { LOG_ERROR("pass '%s': %s failed", m_desc.node->Mesh()->Material()->name.c_str(), "pipeline"); return; }
    }

    {
        const std::array<VkImageView, 2> views {
            m_desc.vk_output.view, m_desc.depth ? *m_desc.depth_image->view : VK_NULL_HANDLE
        };
        VkFramebufferCreateInfo info {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext           = nullptr,
            .renderPass      = *m_desc.pipeline.pass,
            .attachmentCount = m_desc.depth ? 2u : 1u,
            .pAttachments    = views.data(),
            .width           = m_desc.vk_output.extent.width,
            .height          = m_desc.vk_output.extent.height,
            .layers          = 1,
        };
        VVK_CHECK_VOID_RE(device.handle().CreateFramebuffer(info, m_desc.fb));
    }

    if (! ref.blocks.empty()) {
        auto& block = ref.blocks.front();
        rr.dyn_buf->allocateSubRef(
            block.size, m_desc.ubo_buf, device.limits().minUniformBufferOffsetAlignment);
    }

    if (! ref.blocks.empty()) {
        std::function<void()> update_dyn_buf_op;
        if (m_desc.dyn_vertex) {
            auto& mesh        = *m_desc.node->Mesh();
            auto* dyn_buf     = rr.dyn_buf;
            auto& vertex_bufs = m_desc.vertex_bufs;
            auto& draw_count  = m_desc.draw_count;
            auto& index_buf   = m_desc.index_buf;
            update_dyn_buf_op = [&mesh, &vertex_bufs, &draw_count, &index_buf, dyn_buf]() {
                if (mesh.Dirty().exchange(false)) {
                    for (usize i = 0; i < mesh.VertexCount(); i++) {
                        const auto& vertex = mesh.GetVertexArray(i);
                        auto&       buf    = vertex_bufs[i];
                        if (! dyn_buf->writeToBuf(buf,
                                                  { (uint8_t*)vertex.Data(), vertex.DataSizeOf() }))
                            return;
                    }
                    if (mesh.IndexCount() > 0) {
                        auto& indice = mesh.GetIndexArray(0);
                        u32   count  = (u32)((indice.RenderDataCount() * 2) / 3);
                        draw_count   = count * 3;
                        auto& buf = index_buf;
                        if (! dyn_buf->writeToBuf(buf,
                                                  { (uint8_t*)indice.Data(), indice.DataSizeOf() }))
                            return;
                    }
                }
            };
        }

        auto  block  = ref.blocks.front();
        auto* buf    = rr.dyn_buf;
        auto* bufref = &m_desc.ubo_buf;

        auto* node           = m_desc.node;
        auto* shader_updater = scene.shaderValueUpdater.get();
        auto& sprites        = m_desc.sprites_map;
        auto& vk_textures    = m_desc.vk_textures;

        m_desc.update_op = [shader_updater,
                            block,
                            buf,
                            bufref,
                            node,
                            &sprites,
                            &vk_textures,
                            update_dyn_buf_op]() {
            auto update_unf_op = [&block, buf, bufref](std::string_view       name,
                                                       wallpaper::ShaderValue value) {
                UpdateUniform(buf, *bufref, block, name, value);
            };
            shader_updater->UpdateUniforms(node, sprites, update_unf_op);
            // update image slot for sprites
            {
                for (auto& [i, sp] : sprites) {
                    if (i >= vk_textures.size()) continue;
                    vk_textures.at(i).active = sp.GetCurFrame().imageId;
                }
            }
            if (update_dyn_buf_op) update_dyn_buf_op();
        };

        auto exists_unf_op = [&block](std::string_view name) {
            return exists(block.member_map, name);
        };
        shader_updater->InitUniforms(node, exists_unf_op);

        // memset uniform buf
        buf->fillBuf(*bufref, 0, bufref->size, 0);
        {
            auto&      default_values = mesh.Material()->customShader.shader->default_uniforms;
            auto&      const_values   = mesh.Material()->customShader.constValues;
            std::array values_array   = { &default_values, &const_values };
            for (auto& values : values_array) {
                for (auto& v : *values) {
                    if (exists(block.member_map, v.first)) {
                        UpdateUniform(buf, *bufref, block, v.first, v.second);
                    }
                }
            }
        }
        m_desc.update_op();
    }

    {
        auto& sc           = scene.clearColor;
        m_desc.clear_values[0] = VkClearValue {
            .color = { sc[0], sc[1], sc[2], 1.0f },
        };
        m_desc.clear_values[1] = VkClearValue { .depthStencil = { 1.0f, 0 } };
    }
    for (auto& tex : releaseTexs()) {
        device.tex_cache().MarkShareReady(tex);
    }
    setPrepared();
}

void CustomShaderPass::execute(const Device&, RenderingResources& rr) {
    if (m_desc.update_op) m_desc.update_op();

    auto&                   cmd    = rr.command;
    auto&                   outext = m_desc.vk_output.extent;
    VkImageSubresourceRange base_srang {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = VK_REMAINING_ARRAY_LAYERS,
        .baseArrayLayer = 0,
        .layerCount     = VK_REMAINING_MIP_LEVELS,
    };
    for (usize i = 0; i < m_desc.vk_textures.size(); i++) {
        auto& slot    = m_desc.vk_textures[i];
        int   binding = m_desc.vk_tex_binding[i];
        if (binding < 0) continue;
        if (slot.slots.empty()) continue;
        auto&                 img = slot.getActive();
        VkDescriptorImageInfo desc_img { img.sampler,
                                         img.view,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet  wset {
             .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
             .pNext           = nullptr,
             .dstSet          = {},
             .dstBinding      = (uint32_t)binding,
             .descriptorCount = 1,
             .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
             .pImageInfo      = &desc_img,
        };
        cmd.PushDescriptorSetKHR(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_desc.pipeline.layout, 0, wset);

        VkImageMemoryBarrier imb {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext            = nullptr,
            .srcAccessMask    = VK_ACCESS_MEMORY_READ_BIT,
            .dstAccessMask    = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image            = img.handle,
            .subresourceRange = base_srang,
        };

        cmd.PipelineBarrier(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_DEPENDENCY_BY_REGION_BIT,
                            imb);
    }

    if (m_desc.ubo_buf) {
        VkDescriptorBufferInfo desc_buf {
            rr.dyn_buf->gpuBuf(),
            m_desc.ubo_buf.offset,
            m_desc.ubo_buf.size,
        };
        VkWriteDescriptorSet wset {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext           = nullptr,
            .dstSet          = {},
            .dstBinding      = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo     = &desc_buf,
        };
        cmd.PushDescriptorSetKHR(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_desc.pipeline.layout, 0, wset);
    }

    VkRenderPassBeginInfo pass_begin_info {
        .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext       = nullptr,
        .renderPass  = *m_desc.pipeline.pass,
        .framebuffer = *m_desc.fb,
        .renderArea =
            VkRect2D {
                .offset = { 0, 0 },
                .extent = { outext.width, outext.height },
            },
        .clearValueCount = m_desc.depth ? 2u : 1u,
        .pClearValues    = m_desc.clear_values.data(),
    };
    cmd.BeginRenderPass(pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    cmd.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_desc.pipeline.handle);
    VkViewport viewport {
        .x        = 0,
        .y        = (float)outext.height,
        .width    = (float)outext.width,
        .height   = -(float)outext.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor { { 0, 0 }, { outext.width, outext.height } };

    cmd.SetViewport(0, viewport);
    cmd.SetScissor(0, scissor);

    auto gpu_buf = m_desc.dyn_vertex ? rr.dyn_buf->gpuBuf() : rr.vertex_buf->gpuBuf();

    for (usize i = 0; i < m_desc.vertex_bufs.size(); i++) {
        auto& buf = m_desc.vertex_bufs[i];
        cmd.BindVertexBuffers((u32)i, 1, &gpu_buf, &buf.offset);
    }
    if (m_desc.index_buf) {
        cmd.BindIndexBuffer(gpu_buf, m_desc.index_buf.offset, VK_INDEX_TYPE_UINT16);
        cmd.DrawIndexed(m_desc.draw_count, 1, 0, 0, 0);
    } else {
        cmd.Draw(m_desc.draw_count, 1, 0, 0);
    }

    cmd.EndRenderPass();
}

void CustomShaderPass::destory(const Device&, RenderingResources& rr) {
    m_desc.update_op = {};
    {
        auto& buf = m_desc.dyn_vertex ? rr.dyn_buf : rr.vertex_buf;
        for (auto& bufref : m_desc.vertex_bufs) {
            buf->unallocateSubRef(bufref);
        }
    }
    rr.dyn_buf->unallocateSubRef(m_desc.ubo_buf);
}

void CustomShaderPass::setDescTex(u32 index, std::string_view tex_key) {
    assert(index < m_desc.textures.size());
    if (index >= m_desc.textures.size()) return;
    m_desc.textures[index] = tex_key;
}
