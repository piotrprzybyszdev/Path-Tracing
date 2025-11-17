#pragma once

#include <vulkan/vulkan.hpp>

#include <filesystem>
#include <memory>
#include <span>
#include <variant>
#include <vector>

#include "Core/Cache.h"
#include "Core/Threads.h"

#include "Shaders/ShaderRendererTypes.incl"

#include "DescriptorSet.h"
#include "ShaderLibrary.h"

#define REGISTER_CONFIG_HASH(Count)                                                                          \
    template<>                                                                                               \
    struct std::hash<std::array<PathTracing::Shaders::SpecializationConstant, Count>>                        \
        : public PathTracing::FNVHash<std::array<PathTracing::Shaders::SpecializationConstant, Count>>       \
    {                                                                                                        \
    };

REGISTER_CONFIG_HASH(0);
REGISTER_CONFIG_HASH(1);
REGISTER_CONFIG_HASH(2);
REGISTER_CONFIG_HASH(3);
REGISTER_CONFIG_HASH(4);

namespace PathTracing
{

using PipelineConfigView = std::span<const Shaders::SpecializationConstant>;

struct PipelineCache
{
    virtual ~PipelineCache() = default;
};

template<size_t N> struct PipelineConfig
{
    static_assert(N <= 4);

    using Type = std::array<Shaders::SpecializationConstant, N>;
    using Hash = PathTracing::FNVHash<Type>;

    Type Value;

    Shaders::SpecializationConstant &operator[](size_t index)
    {
        return Value[index];
    }

    operator PipelineConfigView() const
    {
        return Value;
    }

    class Cache : public PipelineCache
    {
    public:
        using Key = PipelineConfig<N>;

        Cache(uint32_t maxSize) : m_Cache(maxSize)
        {
        }

        ~Cache() override
        {
            Clear();
        }

        vk::Pipeline Get(const Key &key)
        {
            return m_Cache.Get(key.Value);
        }

        vk::Pipeline Insert(const Key &key, vk::Pipeline value)
        {
            return m_Cache.Insert(key.Value, value);
        }

        bool Contains(const Key &key)
        {
            return m_Cache.Contains(key.Value);
        }

        void Clear()
        {
            for (auto pipeline : m_Cache.GetValues())
                DeviceContext::GetLogical().destroyPipeline(pipeline);
            m_Cache.Clear();
        }

    private:
        LRUCache<Type, vk::Pipeline> m_Cache;
    };
};

struct RaytracingPipelineData
{
    uint32_t MaxRayPayloadSize;
    uint32_t MaxHitAttributeSize;
    uint32_t MaxRayRecursionDepth;
};

struct ComputePipelineData
{
};

using PipelineData = std::variant<RaytracingPipelineData, ComputePipelineData>;

class ShaderInfo
{
public:
    using Config = uint64_t;

public:
    ShaderInfo(ShaderLibrary &shaderLibrary, ShaderId id, vk::PipelineLayout layout, PipelineData data);
    ~ShaderInfo();

    ShaderInfo(const ShaderInfo &) = delete;
    ShaderInfo &operator=(const ShaderInfo &) = delete;

    ShaderInfo(ShaderInfo &&info) noexcept;

    [[nodiscard]] ShaderId GetId() const;
    [[nodiscard]] vk::ShaderStageFlagBits GetStage() const;
    [[nodiscard]] const std::filesystem::path &GetPath() const;
    [[nodiscard]] bool HasVariants() const;
    [[nodiscard]] Config GetConfig(PipelineConfigView config) const;
    [[nodiscard]] uint32_t GetVariantCount() const;
    [[nodiscard]] std::span<const vk::SpecializationMapEntry> GetSpecEntries() const;

    void UpdateSpecializations(PipelineConfigView maxConfig);
    void CompileVariants(std::stop_token stopToken);
    [[nodiscard]] vk::Pipeline GetVariant(PipelineConfigView config) const;

protected:
    ShaderLibrary &m_ShaderLibrary;
    const ShaderId m_Id;
    const vk::PipelineLayout m_Layout;
    const PipelineData m_PipelineData;

    std::vector<vk::SpecializationMapEntry> m_SpecEntries;
    std::vector<uint32_t> m_SpecVariantCount;

    std::unordered_map<Config, vk::Pipeline> m_Variants;
    vk::PipelineCache m_Cache;

private:
    const std::filesystem::path m_CachePath;
    bool m_IsMoved = false;

private:
    void CreateCache();
    void WriteCache() const;

    void CompileRaytracing(std::span<const Config> configs);
    void CompileCompute(std::span<const Config> configs);

private:
    static Config MakeConfig(Shaders::SpecializationConstant spec1, Shaders::SpecializationConstant spec2);

    static std::filesystem::path ToCachePath(std::filesystem::path path);
};

class RaytracingPipeline
{
public:
    RaytracingPipeline(
        ShaderLibrary &shaderLibrary, const std::vector<vk::RayTracingShaderGroupCreateInfoKHR> &groups,
        const std::vector<ShaderId> &shaders, DescriptorSetBuilder &&descriptorSetBuilder,
        vk::PipelineLayout layout, PipelineConfigView maxConfig, RaytracingPipelineData data
    );
    ~RaytracingPipeline();

    RaytracingPipeline(const RaytracingPipeline &) = delete;
    RaytracingPipeline &operator=(const RaytracingPipeline &) = delete;

    template<size_t N> void Update(const PipelineConfig<N> &config);
    void CancelUpdate();

    void CreateDescriptorSet(uint32_t framesInFlight);
    [[nodiscard]] DescriptorSet *GetDescriptorSet();

    [[nodiscard]] vk::PipelineLayout GetLayout() const;
    [[nodiscard]] vk::Pipeline GetHandle() const;

private:
    ShaderLibrary &m_ShaderLibrary;
    DescriptorSetBuilder m_DescriptorSetBuilder;
    const vk::PipelineLayout m_Layout;
    std::unique_ptr<DescriptorSet> m_DescriptorSet;
    const PipelineConfigView m_MaxConfig;
    const RaytracingPipelineData m_Data;

    std::vector<ShaderInfo> m_Shaders;
    std::vector<vk::RayTracingShaderGroupCreateInfoKHR> m_Groups;

    vk::Pipeline m_Handle;

    std::unique_ptr<PipelineCache> m_Cache = nullptr;

    ThreadDispatch<uint32_t> m_CompilationDispatch;

private:
    vk::Pipeline CreateVariant(PipelineConfigView config);
    vk::Pipeline CreateVariantImmediate(PipelineConfigView config);
};

class ComputePipeline
{
public:
    ComputePipeline(
        ShaderLibrary &shaderLibrary, DescriptorSetBuilder &&descriptorSetBuilder, vk::PipelineLayout layout,
        ShaderId shaderId, PipelineConfigView maxConfig
    );
    ~ComputePipeline();

    template<size_t N> void Update(const PipelineConfig<N> &config);
    void CancelUpdate();

    void CreateDescriptorSet(uint32_t framesInFlight);
    [[nodiscard]] DescriptorSet *GetDescriptorSet();

    [[nodiscard]] vk::PipelineLayout GetLayout() const;
    [[nodiscard]] vk::Pipeline GetHandle() const;

private:
    ShaderLibrary &m_ShaderLibrary;
    DescriptorSetBuilder m_DescriptorSetBuilder;
    const vk::PipelineLayout m_Layout;
    ShaderInfo m_Shader;
    std::unique_ptr<DescriptorSet> m_DescriptorSet;
    const PipelineConfigView m_MaxConfig;

    std::jthread m_CompileThread;

    vk::Pipeline m_Handle;
    bool m_IsHandleImmediate = false;

private:
    vk::Pipeline CreateVariantImmediate(PipelineConfigView config);
};

class PipelineBuilder
{
public:
    PipelineBuilder(ShaderLibrary &shaderLibrary);
    virtual ~PipelineBuilder() = default;

    void AddHintIsPartial(uint32_t binding, bool isPartial);
    void AddHintSize(uint32_t binding, uint32_t size);

protected:
    uint32_t AddShader(ShaderId shaderId);
    vk::PipelineLayout CreateLayout();

protected:
    ShaderLibrary &m_ShaderLibrary;
    DescriptorSetBuilder m_DescriptorSetBuilder;

    std::vector<ShaderId> m_ShaderIds;

private:
    std::vector<std::pair<uint32_t, bool>> m_IsPartialHints;
    std::vector<std::pair<uint32_t, uint32_t>> m_SizeHints;

    std::vector<bool> m_IsUsed;
    std::vector<std::pair<vk::DescriptorSetLayoutBinding, bool>> m_Bindings;
    std::vector<vk::PushConstantRange> m_PushConstants;
};

class RaytracingPipelineBuilder : public PipelineBuilder
{
public:
    RaytracingPipelineBuilder(ShaderLibrary &shaderLibrary);
    ~RaytracingPipelineBuilder() override = default;

    uint32_t AddGeneralGroup(ShaderId shaderId);
    uint32_t AddHitGroup(ShaderId closestHitId, ShaderId anyHitId);

    std::unique_ptr<RaytracingPipeline> CreatePipelineUnique(
        PipelineConfigView maxConfig, RaytracingPipelineData data
    );

private:
    std::vector<vk::RayTracingShaderGroupCreateInfoKHR> m_Groups;
};

class ComputePipelineBuilder : public PipelineBuilder
{
public:
    ComputePipelineBuilder(ShaderLibrary &shaderLibrary, ShaderId shaderId);
    ~ComputePipelineBuilder() override = default;

    std::unique_ptr<ComputePipeline> CreatePipelineUnique(PipelineConfigView maxConfig);
};

template<size_t N> inline void RaytracingPipeline::Update(const PipelineConfig<N> &config)
{
    using Cache = PipelineConfig<N>::Cache;

    if (m_Cache == nullptr)
        m_Cache = std::make_unique<Cache>(Application::GetConfig().MaxPipelineVariantCacheSize);

    Cache &cache = *static_cast<Cache *>(m_Cache.get());

    CancelUpdate();

    std::vector<ShaderId> shaderIds;
    for (const ShaderInfo &info : m_Shaders)
        shaderIds.push_back(info.GetId());

    std::vector<bool> isUpToDate = m_ShaderLibrary.RecompileChanged(shaderIds);
    bool allUpToDate = std::ranges::all_of(isUpToDate, [](bool value) { return value; });

    std::vector<bool> needsCompilingVariants(isUpToDate.size());
    for (int i = 0; i < m_Shaders.size(); i++)
        needsCompilingVariants[i] = !isUpToDate[i] || !m_Shaders[i].HasVariants();

    bool allHasVariants = std::ranges::none_of(needsCompilingVariants, [](bool value) { return value; });

    for (int i = 0; i < m_Shaders.size(); i++)
        if (needsCompilingVariants[i])
            m_Shaders[i].UpdateSpecializations(m_MaxConfig);

    if (!allUpToDate)
    {
        logger::trace("A shader isn't up to date - destroying all cached pipelines");
        cache.Clear();
    }

    if (allUpToDate && cache.Contains(config))
    {
        logger::trace("Requested pipeline config is cached");
        m_Handle = cache.Get(config);
    }
    else if (allHasVariants)
    {
        logger::trace("All shader variants are up to date - combining them into pipeline variant");
        m_Handle = CreateVariant(config);
    }
    else
    {
        logger::trace("Creating the immediately necessary pipeline immediately");
        m_Handle = CreateVariantImmediate(config);
    }

    vk::Pipeline removed = cache.Insert(config, m_Handle);
    DeviceContext::GetLogical().destroyPipeline(removed);

    uint32_t taskCount = 0;
    for (int i = 0; i < m_Shaders.size(); i++)
        if (needsCompilingVariants[i])
            taskCount += m_Shaders[i].GetVariantCount();
    Application::AddBackgroundTask(BackgroundTaskType::ShaderCompilation, taskCount);

    m_CompilationDispatch.Dispatch(
        m_Shaders.size(),
        [needsCompilingVariants, this](uint32_t threadId, uint32_t index, std::stop_token stopToken) {
            if (needsCompilingVariants[index])
                m_Shaders[index].CompileVariants(stopToken);
        }
    );
}

template<size_t N> inline void ComputePipeline::Update(const PipelineConfig<N> &config)
{
    CancelUpdate();

    ShaderId id = m_Shader.GetId();
    std::vector<bool> isUpToDate = m_ShaderLibrary.RecompileChanged(std::span(&id, 1));

    if (m_IsHandleImmediate)
        DeviceContext::GetLogical().destroyPipeline(m_Handle);
    m_IsHandleImmediate = false;

    if (!isUpToDate.front() || !m_Shader.HasVariants())
    {
        m_Shader.UpdateSpecializations(m_MaxConfig);
        m_Handle = CreateVariantImmediate(config);
        m_IsHandleImmediate = true;

        Application::AddBackgroundTask(BackgroundTaskType::ShaderCompilation, m_Shader.GetVariantCount());
        m_CompileThread =
            std::jthread([this](std::stop_token stopToken) { m_Shader.CompileVariants(stopToken); });
    }
    else
        m_Handle = m_Shader.GetVariant(config);
}

}
