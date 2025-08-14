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
        uint32_t binding, uint32_t frameIndex, const Image &image, vk::Sampler sampler, vk::ImageLayout layout
    );
    void UpdateImageArray(
        uint32_t binding, uint32_t frameIndex, std::span<const Image> images, vk::Sampler sampler,
        vk::ImageLayout layout, uint32_t firstIndex = 0
    );

    void FlushUpdate();

private:
    const uint32_t m_FramesInFlight;
    const vk::DescriptorPool m_Pool;

    const std::vector<vk::DescriptorType> m_Types;

    std::vector<vk::DescriptorSet> m_Sets;
    std::vector<vk::WriteDescriptorSet> m_Writes;

    std::list<std::vector<vk::AccelerationStructureKHR>> m_AccelerationStructures;
    std::list<vk::WriteDescriptorSetAccelerationStructureKHR> m_AccelerationStructureInfos;
    std::list<vk::DescriptorBufferInfo> m_BufferInfos;
    std::list<std::vector<vk::DescriptorImageInfo>> m_ImageInfos;

private:
    void AddWrite(uint32_t binding, uint32_t frameIndex, uint32_t count = 1, uint32_t arrayIndex = 0);
};

class DescriptorSetBuilder
{
public:
    DescriptorSetBuilder() = default;
    ~DescriptorSetBuilder();

    DescriptorSetBuilder(const DescriptorSetBuilder &) = delete;
    DescriptorSetBuilder &operator=(const DescriptorSetBuilder &) = delete;

    DescriptorSetBuilder &SetDescriptor(vk::DescriptorSetLayoutBinding binding, bool partial = false);

    [[nodiscard]] vk::DescriptorSetLayout CreateLayout();
    [[nodiscard]] std::unique_ptr<DescriptorSet> CreateSetUnique(uint32_t framesInFlight);

private:
    std::vector<vk::DescriptorSetLayoutBinding> m_Bindings;
    std::vector<vk::DescriptorType> m_Types;
    std::vector<vk::DescriptorBindingFlags> m_Flags;

    vk::DescriptorSetLayout m_Layout;
};

}
