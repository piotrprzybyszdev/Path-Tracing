#pragma once

#include <list>
#include <map>
#include <vector>

#include <vulkan/vulkan.hpp>

#include "Buffer.h"
#include "Image.h"

namespace PathTracing
{

class DescriptorSet
{
public:
    DescriptorSet(
        uint32_t framesInFlight, vk::DescriptorSetLayout layout, vk::DescriptorPool pool,
        std::vector<vk::DescriptorType> &&types
    );
    ~DescriptorSet();

    [[nodiscard]] vk::DescriptorSet GetSet(uint32_t frameIndex) const;

    void UpdateAccelerationStructures(
        uint32_t binding, uint32_t frameIndex, std::vector<vk::AccelerationStructureKHR> &&structures
    );
    void UpdateBuffer(uint32_t binding, uint32_t frameIndex, const Buffer &buffer);
    void UpdateImage(
        uint32_t binding, uint32_t frameIndex, const Image &image, vk::Sampler sampler,
        vk::ImageLayout layout, uint32_t index = 0
    );
    void UpdateImageArray(
        uint32_t binding, uint32_t frameIndex, std::span<const Image> images,
        std::span<const uint32_t> imageMap, vk::Sampler sampler, vk::ImageLayout layout,
        uint32_t firstIndex = 0
    );

    void FlushUpdate(uint32_t frameIndex);

private:
    const uint32_t m_FramesInFlight;
    const vk::DescriptorPool m_Pool;

    const std::vector<vk::DescriptorType> m_Types;

    std::vector<vk::DescriptorSet> m_Sets;

    struct FrameDescriptors
    {
        std::list<std::vector<vk::AccelerationStructureKHR>> AccelerationStructures;
        std::list<vk::WriteDescriptorSetAccelerationStructureKHR> AccelerationStructureInfos;
        std::list<vk::DescriptorBufferInfo> BufferInfos;
        std::list<std::vector<vk::DescriptorImageInfo>> ImageInfos;

        std::vector<vk::WriteDescriptorSet> Writes;
    };

    std::vector<FrameDescriptors> m_Descriptors;

private:
    void AddWrite(uint32_t binding, uint32_t frameIndex, uint32_t arrayIndex = 0, uint32_t count = 1);
};

class DescriptorSetBuilder
{
public:
    DescriptorSetBuilder() = default;
    ~DescriptorSetBuilder();

    DescriptorSetBuilder(const DescriptorSetBuilder &) = delete;
    DescriptorSetBuilder &operator=(const DescriptorSetBuilder &) = delete;

    DescriptorSetBuilder(DescriptorSetBuilder &&descriptorSetBuilder) noexcept;

    DescriptorSetBuilder &SetDescriptor(vk::DescriptorSetLayoutBinding binding, bool partial = false);

    [[nodiscard]] vk::DescriptorSetLayout CreateLayout();
    [[nodiscard]] std::unique_ptr<DescriptorSet> CreateSetUnique(uint32_t framesInFlight);

private:
    std::vector<vk::DescriptorSetLayoutBinding> m_Bindings;
    std::vector<vk::DescriptorType> m_Types;
    std::vector<vk::DescriptorBindingFlags> m_Flags;
    std::vector<bool> m_IsUsed;

    vk::DescriptorSetLayout m_Layout;
};

}
