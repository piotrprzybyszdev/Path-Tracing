#include "Core/Core.h"

#include "DescriptorSet.h"
#include "DeviceContext.h"

namespace PathTracing
{
DescriptorSet::DescriptorSet(
    uint32_t framesInFlight, vk::DescriptorSetLayout layout, vk::DescriptorPool pool,
    std::vector<vk::DescriptorType> &&types
)
    : m_FramesInFlight(framesInFlight), m_Layout(layout), m_Pool(pool), m_Types(types)
{
    std::vector<vk::DescriptorSetLayout> layouts = {};
    for (int i = 0; i < m_FramesInFlight; i++)
        layouts.push_back(layout);

    vk::DescriptorSetAllocateInfo allocateInfo(m_Pool, layouts);
    m_Sets = DeviceContext::GetLogical().allocateDescriptorSets(allocateInfo);
}

DescriptorSet::~DescriptorSet()
{
    DeviceContext::GetLogical().destroyDescriptorPool(m_Pool);
    DeviceContext::GetLogical().destroyDescriptorSetLayout(m_Layout);
}

vk::DescriptorSetLayout DescriptorSet::GetLayout() const
{
    return m_Layout;
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
    uint32_t binding, uint32_t frameIndex, std::span<const Image> images, vk::Sampler sampler, vk::ImageLayout layout, uint32_t firstIndex
)
{
    assert(frameIndex < m_FramesInFlight);

    std::vector<vk::DescriptorImageInfo> imageInfos = {};

    for (const Image &image : images)
        imageInfos.emplace_back(sampler, image.GetView(), layout);

    AddWrite(binding, frameIndex, images.size());
    m_ImageInfos.push_back(std::move(imageInfos));
    m_Writes.back().setImageInfo(m_ImageInfos.back());
}

void DescriptorSet::AddWrite(uint32_t binding, uint32_t frameIndex, uint32_t count)
{
    m_Writes.emplace_back(m_Sets[frameIndex], binding, 0, count, m_Types[binding]);
}

DescriptorSetBuilder::DescriptorSetBuilder(uint32_t framesInFlight) : m_FramesInFlight(framesInFlight)
{
}

DescriptorSetBuilder &DescriptorSetBuilder::AddDescriptor(
    vk::DescriptorType type, uint32_t count, vk::ShaderStageFlagBits stage, bool isPerFrame
)
{
    m_Bindings.emplace_back(m_BindingIndex, type, count, stage);
    m_Counts[type] += isPerFrame ? count * m_FramesInFlight : count;
    m_Types.push_back(type);
    m_Flags.push_back(
        count == 1 ? vk::DescriptorBindingFlags() : vk::DescriptorBindingFlagBits::ePartiallyBound
    );

    m_BindingIndex++;

    return *this;
}

std::unique_ptr<DescriptorSet> DescriptorSetBuilder::CreateSetUnique()
{
    vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsCreateInfo(m_Flags);
    vk::DescriptorSetLayoutCreateInfo layoutCreateInfo(vk::DescriptorSetLayoutCreateFlags(), m_Bindings);
    layoutCreateInfo.setPNext(&flagsCreateInfo);

    auto layout = DeviceContext::GetLogical().createDescriptorSetLayout(layoutCreateInfo);

    std::vector<vk::DescriptorPoolSize> poolSizes = {};
    for (auto &[type, count] : m_Counts)
        poolSizes.emplace_back(type, count);

    vk::DescriptorPoolCreateInfo poolCreateInfo(vk::DescriptorPoolCreateFlags(), m_FramesInFlight, poolSizes);
    auto pool = DeviceContext::GetLogical().createDescriptorPool(poolCreateInfo);

    return std::make_unique<DescriptorSet>(m_FramesInFlight, layout, pool, std::move(m_Types));
}

void DescriptorSet::FlushUpdate()
{
    DeviceContext::GetLogical().updateDescriptorSets(m_Writes, {});

    m_AccelerationStructureInfos.clear();
    m_BufferInfos.clear();
    m_ImageInfos.clear();
    m_Writes.clear();
}

}