#include "Core/Core.h"

#include "DescriptorSet.h"
#include "DeviceContext.h"

namespace PathTracing
{

DescriptorSet::DescriptorSet(
    uint32_t framesInFlight, vk::DescriptorSetLayout layout, vk::DescriptorPool pool,
    std::vector<vk::DescriptorType> &&types
)
    : m_FramesInFlight(framesInFlight), m_Pool(pool), m_Types(types)
{
    std::vector<vk::DescriptorSetLayout> layouts(m_FramesInFlight, layout);

    vk::DescriptorSetAllocateInfo allocateInfo(m_Pool, layouts);
    m_Sets = DeviceContext::GetLogical().allocateDescriptorSets(allocateInfo);
}

DescriptorSet::~DescriptorSet()
{
    DeviceContext::GetLogical().destroyDescriptorPool(m_Pool);
}

vk::DescriptorSet DescriptorSet::GetSet(uint32_t frameIndex) const
{
    return m_Sets[frameIndex];
}

void DescriptorSet::UpdateAccelerationStructures(
    uint32_t binding, uint32_t frameIndex, std::vector<vk::AccelerationStructureKHR> &&structures
)
{
    assert(m_Types[binding] == vk::DescriptorType::eAccelerationStructureKHR);

    m_AccelerationStructures.push_back(structures);
    m_AccelerationStructureInfos.emplace_back(m_AccelerationStructures.back());
    AddWrite(binding, frameIndex);
    m_Writes.back().setPNext(&m_AccelerationStructureInfos.back());
}

void DescriptorSet::UpdateBuffer(uint32_t binding, uint32_t frameIndex, const Buffer &buffer)
{
    assert(frameIndex < m_FramesInFlight);

    m_BufferInfos.emplace_back(buffer.GetHandle(), 0, buffer.GetSize());
    AddWrite(binding, frameIndex);
    m_Writes.back().setPBufferInfo(&m_BufferInfos.back());
}

void DescriptorSet::UpdateImage(
    uint32_t binding, uint32_t frameIndex, const Image &image, vk::Sampler sampler, vk::ImageLayout layout
)
{
    assert(frameIndex < m_FramesInFlight);

    m_ImageInfos.push_back({ vk::DescriptorImageInfo(sampler, image.GetView(), layout) });
    AddWrite(binding, frameIndex);
    m_Writes.back().setPImageInfo(&m_ImageInfos.back().back());
}

void DescriptorSet::UpdateImageArray(
    uint32_t binding, uint32_t frameIndex, std::span<const Image> images, vk::Sampler sampler,
    vk::ImageLayout layout, uint32_t firstIndex
)
{
    assert(frameIndex < m_FramesInFlight);

    std::vector<vk::DescriptorImageInfo> imageInfos = {};

    for (const Image &image : images)
        imageInfos.emplace_back(sampler, image.GetView(), layout);

    AddWrite(binding, frameIndex, images.size(), firstIndex);
    m_ImageInfos.push_back(std::move(imageInfos));
    m_Writes.back().setImageInfo(m_ImageInfos.back());
}

void DescriptorSet::AddWrite(uint32_t binding, uint32_t frameIndex, uint32_t count, uint32_t arrayIndex)
{
    m_Writes.emplace_back(m_Sets[frameIndex], binding, arrayIndex, count, m_Types[binding]);
}

void DescriptorSet::FlushUpdate()
{
    DeviceContext::GetLogical().updateDescriptorSets(m_Writes, {});

    m_AccelerationStructureInfos.clear();
    m_BufferInfos.clear();
    m_ImageInfos.clear();
    m_Writes.clear();
}

DescriptorSetBuilder::~DescriptorSetBuilder()
{
    DeviceContext::GetLogical().destroyDescriptorSetLayout(m_Layout);
}

DescriptorSetBuilder &DescriptorSetBuilder::SetDescriptor(vk::DescriptorSetLayoutBinding binding, bool partial)
{
    const uint32_t bindingIndex = binding.binding;

    if (bindingIndex + 1 > m_Bindings.size())
    {
        m_Types.resize(bindingIndex + 1);
        m_Flags.resize(bindingIndex + 1);
        m_Bindings.resize(bindingIndex + 1);
    }

    m_Types[bindingIndex] = binding.descriptorType;
    
    m_Flags[bindingIndex] = !partial && binding.descriptorCount == 1
                                ? vk::DescriptorBindingFlags()
                                : vk::DescriptorBindingFlagBits::ePartiallyBound;
    m_Bindings[bindingIndex] = binding;
    return *this;
}

vk::DescriptorSetLayout DescriptorSetBuilder::CreateLayout()
{
    vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsCreateInfo(m_Flags);
    vk::DescriptorSetLayoutCreateInfo layoutCreateInfo(vk::DescriptorSetLayoutCreateFlags(), m_Bindings);
    layoutCreateInfo.setPNext(&flagsCreateInfo);

    m_Layout = DeviceContext::GetLogical().createDescriptorSetLayout(layoutCreateInfo);
    return m_Layout;
}

std::unique_ptr<DescriptorSet> DescriptorSetBuilder::CreateSetUnique(uint32_t framesInFlight)
{
    std::vector<vk::DescriptorPoolSize> poolSizes = {};
    poolSizes.reserve(m_Types.size());
    for (int i = 0; i < m_Bindings.size(); i++)
    {
        const auto &binding = m_Bindings[i];
        poolSizes.emplace_back(binding.descriptorType, binding.descriptorCount * framesInFlight);
    }

    vk::DescriptorPoolCreateInfo poolCreateInfo(vk::DescriptorPoolCreateFlags(), framesInFlight, poolSizes);
    auto pool = DeviceContext::GetLogical().createDescriptorPool(poolCreateInfo);

    return std::make_unique<DescriptorSet>(framesInFlight, m_Layout, pool, std::move(m_Types));
}

}