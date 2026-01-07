#include "Core/Core.h"

#include "DescriptorSet.h"
#include "DeviceContext.h"

namespace PathTracing
{

DescriptorSet::DescriptorSet(
    uint32_t framesInFlight, vk::DescriptorSetLayout layout, vk::DescriptorPool pool,
    std::vector<vk::DescriptorType> &&types
)
    : m_FramesInFlight(framesInFlight), m_Pool(pool), m_Types(types), m_Descriptors(framesInFlight)
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
    assert(frameIndex < m_FramesInFlight);
    assert(m_Types[binding] == vk::DescriptorType::eAccelerationStructureKHR);

    FrameDescriptors &desc = m_Descriptors[frameIndex];

    desc.AccelerationStructures.push_back(structures);
    desc.AccelerationStructureInfos.emplace_back(desc.AccelerationStructures.back());
    AddWrite(binding, frameIndex);
    desc.Writes.back().setPNext(&desc.AccelerationStructureInfos.back());
}

void DescriptorSet::UpdateBuffer(uint32_t binding, uint32_t frameIndex, const Buffer &buffer)
{
    assert(frameIndex < m_FramesInFlight);

    FrameDescriptors &desc = m_Descriptors[frameIndex];

    desc.BufferInfos.emplace_back(buffer.GetHandle(), 0, buffer.GetSize());
    AddWrite(binding, frameIndex);
    desc.Writes.back().setPBufferInfo(&desc.BufferInfos.back());
}

void DescriptorSet::UpdateImage(
    uint32_t binding, uint32_t frameIndex, const Image &image, vk::Sampler sampler, vk::ImageLayout layout,
    uint32_t index
)
{
    assert(frameIndex < m_FramesInFlight);

    FrameDescriptors &desc = m_Descriptors[frameIndex];

    desc.ImageInfos.push_back({ vk::DescriptorImageInfo(sampler, image.GetView(), layout) });
    AddWrite(binding, frameIndex, index);
    desc.Writes.back().setPImageInfo(&desc.ImageInfos.back().back());
}

void DescriptorSet::UpdateImageArray(
    uint32_t binding, uint32_t frameIndex, std::span<const Image> images, std::span<const uint32_t> imageMap,
    vk::Sampler sampler, vk::ImageLayout layout, uint32_t firstIndex
)
{
    assert(frameIndex < m_FramesInFlight);

    FrameDescriptors &desc = m_Descriptors[frameIndex];

    std::vector<vk::DescriptorImageInfo> imageInfos = {};

    for (uint32_t index : imageMap)
        imageInfos.emplace_back(sampler, images[index].GetView(), layout);

    AddWrite(binding, frameIndex, firstIndex, imageMap.size());
    desc.ImageInfos.push_back(std::move(imageInfos));
    desc.Writes.back().setImageInfo(desc.ImageInfos.back());
}

void DescriptorSet::AddWrite(uint32_t binding, uint32_t frameIndex, uint32_t arrayIndex, uint32_t count)
{
    std::erase_if(m_Descriptors[frameIndex].Writes, [binding, arrayIndex](vk::WriteDescriptorSet write) {
        assert(
            !(write.dstBinding == binding && write.dstArrayElement == arrayIndex) ||
            write.descriptorCount == 1
        );
        return write.dstBinding == binding && write.dstArrayElement == arrayIndex;
    });

    m_Descriptors[frameIndex].Writes.emplace_back(
        m_Sets[frameIndex], binding, arrayIndex, count, m_Types[binding]
    );
}

void DescriptorSet::FlushUpdate(uint32_t frameIndex)
{
    FrameDescriptors &desc = m_Descriptors[frameIndex];

    DeviceContext::GetLogical().updateDescriptorSets(desc.Writes, {});

    desc.AccelerationStructures.clear();
    desc.AccelerationStructureInfos.clear();
    desc.BufferInfos.clear();
    desc.ImageInfos.clear();
    desc.Writes.clear();
}

DescriptorSetBuilder::~DescriptorSetBuilder()
{
    DeviceContext::GetLogical().destroyDescriptorSetLayout(m_Layout);
}

DescriptorSetBuilder::DescriptorSetBuilder(DescriptorSetBuilder &&descriptorSetBuilder) noexcept
    : m_Bindings(std::move(descriptorSetBuilder.m_Bindings)),
      m_Types(std::move(descriptorSetBuilder.m_Types)), m_Flags(std::move(descriptorSetBuilder.m_Flags)),
      m_Layout(std::move(descriptorSetBuilder.m_Layout))
{
    descriptorSetBuilder.m_Layout = nullptr;
}

DescriptorSetBuilder &DescriptorSetBuilder::SetDescriptor(
    vk::DescriptorSetLayoutBinding binding, bool partial
)
{
    const uint32_t bindingIndex = binding.binding;

    if (bindingIndex + 1 > m_Bindings.size())
    {
        m_Types.resize(bindingIndex + 1);
        m_Flags.resize(bindingIndex + 1);
        m_Bindings.resize(bindingIndex + 1);
        m_IsUsed.resize(bindingIndex + 1);
    }

    m_Types[bindingIndex] = binding.descriptorType;

    m_Flags[bindingIndex] = !partial && binding.descriptorCount == 1
                                ? vk::DescriptorBindingFlags()
                                : vk::DescriptorBindingFlagBits::ePartiallyBound;
    m_Bindings[bindingIndex] = binding;
    m_IsUsed[bindingIndex] = true;

    return *this;
}

vk::DescriptorSetLayout DescriptorSetBuilder::CreateLayout()
{
    std::vector<vk::DescriptorBindingFlags> usedFlags;
    std::vector<vk::DescriptorSetLayoutBinding> usedBindings;

    for (int i = 0; i < m_Bindings.size(); i++)
    {
        if (!m_IsUsed[i])
            continue;
        
        usedFlags.push_back(m_Flags[i]);
        usedBindings.push_back(m_Bindings[i]);
    }

    vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsCreateInfo(usedFlags);
    vk::DescriptorSetLayoutCreateInfo layoutCreateInfo(vk::DescriptorSetLayoutCreateFlags(), usedBindings);
    layoutCreateInfo.setPNext(&flagsCreateInfo);

    m_Layout = DeviceContext::GetLogical().createDescriptorSetLayout(layoutCreateInfo);
    return m_Layout;
}

std::unique_ptr<DescriptorSet> DescriptorSetBuilder::CreateSetUnique(uint32_t framesInFlight)
{
    std::vector<vk::DescriptorPoolSize> poolSizes = {};
    poolSizes.reserve(m_Types.size());
    for (const auto &binding : m_Bindings)
        poolSizes.emplace_back(binding.descriptorType, binding.descriptorCount * framesInFlight);

    vk::DescriptorPoolCreateInfo poolCreateInfo(vk::DescriptorPoolCreateFlags(), framesInFlight, poolSizes);
    auto pool = DeviceContext::GetLogical().createDescriptorPool(poolCreateInfo);

    return std::make_unique<DescriptorSet>(framesInFlight, m_Layout, pool, std::move(m_Types));
}

}