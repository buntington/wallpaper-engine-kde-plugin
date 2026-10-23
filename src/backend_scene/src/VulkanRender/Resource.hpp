#pragma once
#include "Core/NoCopyMove.hpp"
#include "Vulkan/StagingBuffer.hpp"
#include "Vulkan/Parameters.hpp"
#include <map>
#include <memory>

namespace wallpaper
{
namespace vulkan
{

class Device;

// Depth images shared by the 3D passes that draw into targets of the same size.
class DepthTargets {
public:
    static constexpr VkFormat Format = VK_FORMAT_D32_SFLOAT;

    explicit DepthTargets(const Device& device): m_device(device) {}
    // Creates the image on first use.
    const VmaImageParameters* Get(VkExtent3D);
    void                      Clear() { m_images.clear(); }

private:
    const Device&                                                m_device;
    std::map<std::pair<uint32_t, uint32_t>, VmaImageParameters> m_images;
};

struct RenderingResources {
    vvk::CommandBuffer command;

    vvk::Semaphore sem_swap_wait_image;
    vvk::Semaphore sem_swap_finish;
    vvk::Fence     fence_frame;

    StagingBuffer* vertex_buf;
    StagingBuffer* dyn_buf;
    DepthTargets*  depth { nullptr };
};
} // namespace vulkan
} // namespace wallpaper
