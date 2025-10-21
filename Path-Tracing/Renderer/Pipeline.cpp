#include <fstream>
#include <numeric>

#include "Core/Core.h"

#include "Application.h"
#include "DeviceContext.h"
#include "Pipeline.h"

namespace PathTracing
{

namespace
{

uint32_t GetCompilationThreadCount()
{
    const uint32_t desiredCompilationThreadCount = 3;
    return std::min(Application::GetConfig().MaxShaderCompilationThreads, desiredCompilationThreadCount);
}

}

RaytracingPipeline::RaytracingPipeline(
    ShaderLibrary &shaderLibrary, const std::vector<vk::RayTracingShaderGroupCreateInfoKHR> &groups,
    const std::vector<ShaderId> &shaders, DescriptorSetBuilder &&descriptorSetBuilder,
    vk::PipelineLayout layout
)
    : m_ShaderLibrary(shaderLibrary), m_DescriptorSetBuilder(std::move(descriptorSetBuilder)),
      m_Layout(layout), m_Groups(groups), m_Cache(Application::GetConfig().MaxPipelineVariantCacheSize),
      m_CompilationDispatch(GetCompilationThreadCount())
{
    for (ShaderId id : shaders)
        m_Shaders.emplace_back(m_ShaderLibrary, id, m_Layout);
}

RaytracingPipeline::~RaytracingPipeline()
{
    CancelUpdate();

    for (auto pipeline : m_Cache.GetValues())
        DeviceContext::GetLogical().destroyPipeline(pipeline);

    DeviceContext::GetLogical().destroyPipelineLayout(m_Layout);
}

void RaytracingPipeline::CreateDescriptorSet(uint32_t framesInFlight)
{
    m_DescriptorSet = m_DescriptorSetBuilder.CreateSetUnique(framesInFlight);
}

DescriptorSet *RaytracingPipeline::GetDescriptorSet()
{
    return m_DescriptorSet.get();
}

void RaytracingPipeline::CancelUpdate()
{
    m_CompilationDispatch.Cancel();
}

void RaytracingPipeline::Update(const PipelineConfig &config)
{
    CancelUpdate();

    std::vector<ShaderId> shaderIds;
    for (const RaytracingShaderInfo &info : m_Shaders)
        shaderIds.push_back(info.GetId());

    std::vector<bool> isUpToDate = m_ShaderLibrary.RecompileChanged(shaderIds);
    bool allUpToDate = std::ranges::all_of(isUpToDate, [](bool value) { return value; });
    
    std::vector<bool> needsRecompilation(isUpToDate);
    for (int i = 0; i < m_Shaders.size(); i++)
        needsRecompilation[i] = !needsRecompilation[i] || !m_Shaders[i].HasVariants();

    bool anyNeedsRecompilation = std::ranges::any_of(needsRecompilation, [](bool value) { return value; });

    if (allUpToDate && m_Cache.Contains(config))
    {
        logger::trace("Requested pipeline config is cached");
        m_Handle = m_Cache.Get(config);
        return;
    }

    for (int i = 0; i < m_Shaders.size(); i++)
        if (needsRecompilation[i])
            m_Shaders[i].UpdateSpecializations();

    // Compile the immediately necessary version of the pipeline right away
    if (anyNeedsRecompilation)
        m_Handle = CreateVariantImmediate(config);

    uint32_t taskCount = 0;
    for (int i = 0; i < m_Shaders.size(); i++)
        if (needsRecompilation[i])
            taskCount += m_Shaders[i].GetVariantCount();
    Application::AddBackgroundTask(BackgroundTaskType::ShaderCompilation, taskCount);

    // Now compile shader variants of those that have changed
    m_CompilationDispatch.Dispatch(
        m_Shaders.size(),
        [needsRecompilation, this](uint32_t threadId, uint32_t index) {
            if (needsRecompilation[index])
                m_Shaders[index].CompileVariants();
        }
    );

    if (!allUpToDate)
    {
        logger::trace("A shader isn't up to date - destroying all cached pipelines");
        for (auto pipeline : m_Cache.GetValues())
            DeviceContext::GetLogical().destroyPipeline(pipeline);
        m_Cache.Clear();
    }

    if (!anyNeedsRecompilation)
        m_Handle = CreateVariant(config);

    vk::Pipeline removed = m_Cache.Insert(config, m_Handle);
    DeviceContext::GetLogical().destroyPipeline(removed);
}

vk::PipelineLayout RaytracingPipeline::GetLayout() const
{
    return m_Layout;
}

vk::Pipeline RaytracingPipeline::GetHandle() const
{
    return m_Handle;
}

vk::Pipeline RaytracingPipeline::CreateVariant(const PipelineConfig &config)
{
    std::vector<vk::Pipeline> libraries;
    libraries.reserve(m_Shaders.size());
    for (const ShaderInfo &info : m_Shaders)
        libraries.push_back(info.GetVariant(config));

    vk::PipelineLibraryCreateInfoKHR libraryInfo(libraries);

    vk::RayTracingPipelineInterfaceCreateInfoKHR interface(
        Shaders::MaxPayloadSize, Shaders::MaxHitAttributeSize
    );

    assert(
        DeviceContext::GetRayTracingPipelineProperties().maxRayRecursionDepth >= Shaders::MaxRecursionDepth
    );
    vk::RayTracingPipelineCreateInfoKHR createInfo(
        vk::PipelineCreateFlags(), nullptr, m_Groups, Shaders::MaxRecursionDepth
    );
    createInfo.setLayout(m_Layout);
    createInfo.setPLibraryInfo(&libraryInfo);
    createInfo.setPLibraryInterface(&interface);

    vk::ResultValue<vk::Pipeline> result = DeviceContext::GetLogical().createRayTracingPipelineKHR(
        nullptr, nullptr, createInfo, nullptr, Application::GetDispatchLoader()
    );

    assert(result.result == vk::Result::eSuccess);
    if (result.value == nullptr)
        throw error("Pipeline creation failed!");

    logger::info("Raytracing pipeline creation successfull!");
    return result.value;
}

vk::Pipeline RaytracingPipeline::CreateVariantImmediate(const PipelineConfig &config)
{
    std::vector<RaytracingShaderInfo::Config> configs;
    std::vector<vk::SpecializationInfo> specInfos;
    std::vector<vk::PipelineShaderStageCreateInfo> stages;
    configs.reserve(m_Shaders.size());
    specInfos.reserve(m_Shaders.size());
    stages.reserve(m_Shaders.size());

    for (const auto &info : m_Shaders)
        configs.push_back(info.GetConfig(config));

    for (int i = 0; i < m_Shaders.size(); i++)
    {
        const auto specEntries = m_Shaders[i].GetSpecEntries();
        specInfos.emplace_back(
            static_cast<uint32_t>(specEntries.size()), specEntries.data(),
            static_cast<uint32_t>(sizeof(ShaderInfo::Config)), configs.data() + i
        );
    }

    for (int i = 0; i < m_Shaders.size(); i++)
    {
        stages.push_back(m_ShaderLibrary.GetShader(m_Shaders[i].GetId()).GetStageCreateInfo());
        stages.back().setPSpecializationInfo(specInfos.data() + i);
    }

    assert(DeviceContext::GetRayTracingPipelineProperties().maxRayRecursionDepth >= 2);
    vk::RayTracingPipelineCreateInfoKHR createInfo(vk::PipelineCreateFlags(), stages, m_Groups, 2);
    createInfo.setLayout(m_Layout);

    vk::ResultValue<vk::Pipeline> result = DeviceContext::GetLogical().createRayTracingPipelineKHR(
        nullptr, nullptr, createInfo, nullptr, Application::GetDispatchLoader()
    );

    assert(result.result == vk::Result::eSuccess);
    if (result.value == nullptr)
        throw error("Pipeline creation failed!");

    logger::info("Raytracing pipeline creation successfull!");
    return result.value;
}

ShaderInfo::ShaderInfo(ShaderLibrary &shaderLibrary, ShaderId id, vk::PipelineLayout layout)
    : m_ShaderLibrary(shaderLibrary), m_Id(id), m_Layout(layout), m_CachePath(ToCachePath(GetPath()))
{
    CreateCache();
}

ShaderInfo::~ShaderInfo()
{
    if (m_IsMoved)
        return;

    for (auto pipeline : m_Variants | std::views::values)
        DeviceContext::GetLogical().destroyPipeline(pipeline);

    WriteCache();

    DeviceContext::GetLogical().destroyPipelineCache(m_Cache);
}

ShaderInfo::ShaderInfo(ShaderInfo &&info) noexcept
    : m_ShaderLibrary(info.m_ShaderLibrary), m_Id(info.m_Id), m_Layout(info.m_Layout),
      m_CachePath(info.m_CachePath), m_Variants(std::move(info.m_Variants)), m_Cache(info.m_Cache)
{
    info.m_IsMoved = true;
}

void ShaderInfo::UpdateSpecializations()
{
    const auto constantIds = m_ShaderLibrary.GetShader(m_Id).GetSpecializationConstantIds();

    m_SpecEntries.clear();
    m_SpecVariantCount.clear();
    m_SpecEntries.reserve(constantIds.size());
    m_SpecVariantCount.reserve(constantIds.size());
    for (int i = 0; i < constantIds.size(); i++)
    {
        m_SpecEntries.emplace_back(constantIds[i], i * 4, 4);
        m_SpecVariantCount.push_back(Shaders::GetMaxSpecializationConstantValue(constantIds[i]) + 1);
    }
}

uint32_t ShaderInfo::GetVariantCount() const
{
    return std::accumulate(m_SpecVariantCount.begin(), m_SpecVariantCount.end(), 1, std::multiplies<uint32_t>());
}

void ShaderInfo::CompileVariants()
{
    if (Application::GetConfig().ShaderPrecompilation == false)
        return;

    for (auto pipeline : m_Variants | std::views::values)
        DeviceContext::GetLogical().destroyPipeline(pipeline);
    m_Variants.clear();

    // TODO: Support more than two spec constants -> ShaderInfo::Config has to be larger
    assert(m_SpecEntries.size() <= 2);

    auto getMaxSpecValue = [this](uint32_t idx) { 
        return idx >= m_SpecVariantCount.size() ? 1 : m_SpecVariantCount[idx];
    };

    const Shaders::SpecializationConstant maxSpec1 = getMaxSpecValue(0);
    const Shaders::SpecializationConstant maxSpec2 = getMaxSpecValue(1);
    assert(maxSpec1 * maxSpec2 == GetVariantCount());

    std::vector<Config> configs;
    configs.reserve(maxSpec1 * maxSpec2);
    for (Shaders::SpecializationConstant spec1Value = 0; spec1Value < maxSpec1; spec1Value++)
        for (Shaders::SpecializationConstant spec2Value = 0; spec2Value < maxSpec2; spec2Value++)
            configs.push_back(MakeConfig(spec1Value, spec2Value));
    assert(configs.size() == maxSpec1 * maxSpec2);

    // TODO: Split compilation into smaller batches so that cancellation is faster
    Compile(configs);
    Application::IncrementBackgroundTaskDone(BackgroundTaskType::ShaderCompilation, configs.size());
    logger::debug("Precompiled {} variants of shader `{}`", configs.size(), GetPath().string());
}

ShaderInfo::Config ShaderInfo::GetConfig(const PipelineConfig &config) const
{
    uint32_t spec1id = m_SpecEntries.size() < 1 ? -1 : m_SpecEntries[0].constantID;
    uint32_t spec2id = m_SpecEntries.size() < 2 ? -1 : m_SpecEntries[1].constantID;

    Shaders::SpecializationConstant spec1Value = Shaders::GetSpecializationConstant(config, spec1id);
    Shaders::SpecializationConstant spec2Value = Shaders::GetSpecializationConstant(config, spec2id);

    return MakeConfig(spec1Value, spec2Value);
}

std::span<const vk::SpecializationMapEntry> ShaderInfo::GetSpecEntries() const
{
    return m_SpecEntries;
}

vk::Pipeline ShaderInfo::GetVariant(const PipelineConfig &config) const
{
    assert(m_Variants.contains(GetConfig(config)));
    return m_Variants.at(GetConfig(config));
}

RaytracingShaderInfo::RaytracingShaderInfo(RaytracingShaderInfo &&info) noexcept : ShaderInfo(std::move(info))
{
}

void RaytracingShaderInfo::Compile(const std::vector<Config> &configs)
{
    const auto shaderCreateInfo = m_ShaderLibrary.GetShader(m_Id).GetStageCreateInfo();

    vk::RayTracingPipelineInterfaceCreateInfoKHR interface(
        Shaders::MaxPayloadSize, Shaders::MaxHitAttributeSize
    );

    std::vector<vk::PipelineShaderStageCreateInfo> shaderCreateInfos;
    std::vector<vk::SpecializationInfo> specInfos;
    std::vector<vk::RayTracingPipelineCreateInfoKHR> createInfos;

    shaderCreateInfos.reserve(configs.size());
    specInfos.reserve(configs.size());
    createInfos.reserve(configs.size());

    assert(
        DeviceContext::GetRayTracingPipelineProperties().maxRayRecursionDepth >= Shaders::MaxRecursionDepth
    );

    for (int i = 0; i < configs.size(); i++)
    {
        shaderCreateInfos.push_back(shaderCreateInfo);
        auto &specInfo = specInfos.emplace_back(
            static_cast<uint32_t>(m_SpecEntries.size()), m_SpecEntries.data(),
            static_cast<uint32_t>(sizeof(Config)), configs.data() + i
        );
        shaderCreateInfos.back().setPSpecializationInfo(&specInfo);
        auto &createInfo =
            createInfos.emplace_back(vk::PipelineCreateFlagBits::eLibraryKHR, shaderCreateInfos.back());
        createInfo.setMaxPipelineRayRecursionDepth(Shaders::MaxRecursionDepth);
        createInfo.setLayout(m_Layout);
        createInfo.setPLibraryInterface(&interface);
    }

    vk::ResultValue<std::vector<vk::Pipeline>> result =
        DeviceContext::GetLogical().createRayTracingPipelinesKHR(
            nullptr, m_Cache, createInfos, nullptr, Application::GetDispatchLoader()
        );

    assert(result.result == vk::Result::eSuccess);
    const auto &binaries = result.value;

    if (std::ranges::find(binaries, nullptr) != binaries.end())
        throw error(std::format("Compilation for shader `{}` failed!", GetPath().string()));

    assert(m_Variants.empty());
    for (int i = 0; i < binaries.size(); i++)
        m_Variants.emplace(configs[i], binaries[i]);
}

ShaderId ShaderInfo::GetId() const
{
    return m_Id;
}

vk::ShaderStageFlagBits ShaderInfo::GetStage() const
{
    return m_ShaderLibrary.GetShader(m_Id).GetStage();
}

const std::filesystem::path &ShaderInfo::GetPath() const
{
    return m_ShaderLibrary.GetShader(m_Id).GetPath();
}

bool ShaderInfo::HasVariants() const
{
    return !m_Variants.empty();
}

void ShaderInfo::CreateCache()
{
    std::vector<char> buffer;
    if (std::filesystem::is_regular_file(m_CachePath))
    {
        std::ifstream file(m_CachePath, std::ios::ate | std::ios::binary);

        if (!file.is_open())
            throw error(std::format("Cache file {} cannot be opened", m_CachePath.string()));

        size_t size = file.tellg();
        buffer.resize(size);

        file.seekg(0);
        file.read(buffer.data(), size);

        file.close();
    }

    vk::PipelineCacheCreateInfo createInfo(vk::PipelineCacheCreateFlags(), buffer.size(), buffer.data());
    m_Cache = DeviceContext::GetLogical().createPipelineCache(createInfo);
}

void ShaderInfo::WriteCache() const
{
    std::ofstream file(m_CachePath, std::ios::binary);
    auto data = DeviceContext::GetLogical().getPipelineCacheData(m_Cache);
    file.write(reinterpret_cast<const char *>(data.data()), data.size());
}

ShaderInfo::Config ShaderInfo::MakeConfig(
    Shaders::SpecializationConstant spec1, Shaders::SpecializationConstant spec2
)
{
    return (static_cast<uint64_t>(spec2) << 32) | spec1;
}

std::filesystem::path ShaderInfo::ToCachePath(std::filesystem::path path)
{
    const auto &config = Application::GetConfig();
    return config.ShaderCachePath / path.filename().replace_extension(config.ShaderCacheExtension);
}

ComputeShaderInfo::ComputeShaderInfo(ComputeShaderInfo &&info) noexcept : ShaderInfo(std::move(info))
{
}

void ComputeShaderInfo::Compile(const std::vector<Config> &configs)
{
    const auto shaderCreateInfo = m_ShaderLibrary.GetShader(m_Id).GetStageCreateInfo();

    std::vector<vk::PipelineShaderStageCreateInfo> shaderCreateInfos;
    std::vector<vk::SpecializationInfo> specInfos;
    std::vector<vk::ComputePipelineCreateInfo> createInfos;

    shaderCreateInfos.reserve(configs.size());
    specInfos.reserve(configs.size());
    createInfos.reserve(configs.size());

    for (int i = 0; i < configs.size(); i++)
    {
        shaderCreateInfos.push_back(shaderCreateInfo);
        auto &specInfo = specInfos.emplace_back(
            static_cast<uint32_t>(m_SpecEntries.size()), m_SpecEntries.data(),
            static_cast<uint32_t>(sizeof(Config)), configs.data() + i
        );
        shaderCreateInfos.back().setPSpecializationInfo(&specInfo);
        auto &createInfo = createInfos.emplace_back(vk::PipelineCreateFlags(), shaderCreateInfos.back());
        createInfo.setLayout(m_Layout);
    }

    vk::ResultValue<std::vector<vk::Pipeline>> result =
        DeviceContext::GetLogical().createComputePipelines(m_Cache, createInfos);

    assert(result.result == vk::Result::eSuccess);
    const auto &binaries = result.value;

    if (std::ranges::find(binaries, nullptr) != binaries.end())
        throw error(std::format("Compilation for shader `{}` failed!", GetPath().string()));

    assert(m_Variants.empty());
    for (int i = 0; i < binaries.size(); i++)
        m_Variants.emplace(configs[i], binaries[i]);
}

ComputePipeline::ComputePipeline(
    ShaderLibrary &shaderLibrary, DescriptorSetBuilder &&descriptorSetBuilder, vk::PipelineLayout layout,
    ShaderId shaderId
)
    : m_ShaderLibrary(shaderLibrary), m_DescriptorSetBuilder(std::move(descriptorSetBuilder)),
      m_Layout(layout), m_Shader(shaderLibrary, shaderId, layout)
{
}

ComputePipeline::~ComputePipeline()
{
    CancelUpdate();

    DeviceContext::GetLogical().destroyPipelineLayout(m_Layout);

    if (m_IsHandleImmediate)
        DeviceContext::GetLogical().destroyPipeline(m_Handle);
}

void ComputePipeline::CancelUpdate()
{
    if (m_CompileThread.joinable())
    {
        m_CompileThread.request_stop();
        m_CompileThread.join();
    }
}

void ComputePipeline::Update(const PipelineConfig &config)
{
    ShaderId id = m_Shader.GetId();
    std::vector<bool> isUpToDate = m_ShaderLibrary.RecompileChanged(std::span(&id, 1));

    if (m_IsHandleImmediate)
        DeviceContext::GetLogical().destroyPipeline(m_Handle);
    m_IsHandleImmediate = false;

    if (!isUpToDate.front() || !m_Shader.HasVariants())
    {
        m_Shader.UpdateSpecializations();
        m_Handle = CreateVariantImmediate(config);
        m_IsHandleImmediate = true;

        Application::AddBackgroundTask(BackgroundTaskType::ShaderCompilation, m_Shader.GetVariantCount());
        m_CompileThread = std::jthread([this]() { m_Shader.CompileVariants(); });
    }
    else
        m_Handle = m_Shader.GetVariant(config);
}

vk::Pipeline ComputePipeline::CreateVariantImmediate(const PipelineConfig &config)
{
    auto shaderCreateInfo = m_ShaderLibrary.GetShader(m_Shader.GetId()).GetStageCreateInfo();
    const auto specEntries = m_Shader.GetSpecEntries();

    vk::SpecializationInfo specInfo(
        static_cast<uint32_t>(specEntries.size()), specEntries.data(),
        static_cast<uint32_t>(sizeof(ComputeShaderInfo::Config)), &config
    );

    shaderCreateInfo.setPSpecializationInfo(&specInfo);

    vk::ComputePipelineCreateInfo createInfo(vk::PipelineCreateFlags(), shaderCreateInfo, m_Layout);

    vk::ResultValue<vk::Pipeline> result =
        DeviceContext::GetLogical().createComputePipeline(nullptr, createInfo);

    assert(result.result == vk::Result::eSuccess);
    if (result.value == nullptr)
        throw error("Pipeline creation failed!");

    logger::info("Compute pipeline creation successfull!");
    return result.value;
}

void ComputePipeline::CreateDescriptorSet(uint32_t framesInFlight)
{
    m_DescriptorSet = m_DescriptorSetBuilder.CreateSetUnique(framesInFlight);
}

DescriptorSet *ComputePipeline::GetDescriptorSet()
{
    return m_DescriptorSet.get();
}

vk::PipelineLayout ComputePipeline::GetLayout() const
{
    return m_Layout;
}

vk::Pipeline ComputePipeline::GetHandle() const
{
    return m_Handle;
}

PipelineBuilder::PipelineBuilder(ShaderLibrary &shaderLibrary) : m_ShaderLibrary(shaderLibrary)
{
}

void PipelineBuilder::AddHintIsPartial(uint32_t binding, bool isPartial)
{
    m_IsPartialHints.emplace_back(binding, isPartial);
}

void PipelineBuilder::AddHintSize(uint32_t binding, uint32_t size)
{
    m_SizeHints.emplace_back(binding, size);
}

uint32_t PipelineBuilder::AddShader(ShaderId shaderId)
{
    if (shaderId == ShaderLibrary::g_UnusedShaderId)
        return vk::ShaderUnusedKHR;

    for (int i = 0; i < m_ShaderIds.size(); i++)
        if (m_ShaderIds[i] == shaderId)
            return i;

    auto pushConstants = m_ShaderLibrary.GetShader(shaderId).GetPushConstants();
    if (pushConstants.size > 0)
        m_PushConstants.push_back(pushConstants);

    auto layoutBindings = m_ShaderLibrary.GetShader(shaderId).GetSetLayoutBindings();

    for (const auto &layoutBinding : layoutBindings)
    {
        if (m_Bindings.size() <= layoutBinding.binding)
        {
            m_IsUsed.resize(layoutBinding.binding + 1);
            m_Bindings.resize(layoutBinding.binding + 1);
        }

        auto &currentlayoutBinding = m_Bindings[layoutBinding.binding];
        if (m_IsUsed[layoutBinding.binding])
        {
            assert(currentlayoutBinding.first.descriptorType == layoutBinding.descriptorType);
            currentlayoutBinding.first.stageFlags |= layoutBinding.stageFlags;
        }
        else
        {
            m_IsUsed[layoutBinding.binding] = true;
            currentlayoutBinding = { layoutBinding, false };
        }
    }

    m_ShaderIds.push_back(shaderId);
    return m_ShaderIds.size() - 1;
}

vk::PipelineLayout PipelineBuilder::CreateLayout()
{
    for (auto [binding, isPartial] : m_IsPartialHints)
        m_Bindings[binding].second = isPartial;

    for (auto [binding, size] : m_SizeHints)
        m_Bindings[binding].first.descriptorCount = size;

    for (int i = 0; i < m_Bindings.size(); i++)
        if (m_IsUsed[i])
            m_DescriptorSetBuilder.SetDescriptor(m_Bindings[i].first, m_Bindings[i].second);

    std::array<vk::DescriptorSetLayout, 1> layouts = { m_DescriptorSetBuilder.CreateLayout() };
    vk::PipelineLayoutCreateInfo createInfo(vk::PipelineLayoutCreateFlags(), layouts, m_PushConstants);
    return DeviceContext::GetLogical().createPipelineLayout(createInfo);
}

RaytracingPipelineBuilder::RaytracingPipelineBuilder(ShaderLibrary &shaderLibrary)
    : PipelineBuilder(shaderLibrary)
{
}

uint32_t RaytracingPipelineBuilder::AddGeneralGroup(ShaderId shaderId)
{
    const uint32_t shaderIndex = AddShader(shaderId);
    m_Groups.emplace_back(vk::RayTracingShaderGroupTypeKHR::eGeneral, shaderIndex);
    return m_Groups.size() - 1;
}

uint32_t RaytracingPipelineBuilder::AddHitGroup(ShaderId closestHitId, ShaderId anyHitId)
{
    const uint32_t closestHitIndex = AddShader(closestHitId);
    const uint32_t anyHitIndex = AddShader(anyHitId);
    m_Groups.emplace_back(
        vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup, vk::ShaderUnusedKHR, closestHitIndex,
        anyHitIndex
    );
    return m_Groups.size() - 1;
}

std::unique_ptr<RaytracingPipeline> RaytracingPipelineBuilder::CreatePipelineUnique()
{
    auto layout = CreateLayout();
    return std::make_unique<RaytracingPipeline>(
        m_ShaderLibrary, m_Groups, m_ShaderIds, std::move(m_DescriptorSetBuilder), layout
    );
}

ComputePipelineBuilder::ComputePipelineBuilder(ShaderLibrary &shaderLibrary, ShaderId shaderId)
    : PipelineBuilder(shaderLibrary)
{
    AddShader(shaderId);
}

std::unique_ptr<ComputePipeline> ComputePipelineBuilder::CreatePipelineUnique()
{
    auto layout = CreateLayout();
    return std::make_unique<ComputePipeline>(
        m_ShaderLibrary, std::move(m_DescriptorSetBuilder), layout, m_ShaderIds.front()
    );
}

}
