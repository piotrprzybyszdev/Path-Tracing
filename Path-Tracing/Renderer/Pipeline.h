#pragma once

#include <vulkan/vulkan.hpp>

#include <filesystem>
#include <span>
#include <vector>

#include "Core/Cache.h"
#include "Core/Threads.h"

#include "Shaders/ShaderRendererTypes.incl"

#include "DescriptorSet.h"
#include "ShaderLibrary.h"

namespace PathTracing
{

using PipelineConfig = Shaders::SpecializationData;

}

template<> struct std::hash<PathTracing::PipelineConfig> : public PathTracing::FNVHash<PathTracing::PipelineConfig>
{
};

namespace PathTracing
{

class ShaderInfo
{
public:
    using Config = uint64_t;

public:
    ShaderInfo(ShaderLibrary &shaderLibrary, ShaderId id, vk::PipelineLayout layout);
    virtual ~ShaderInfo();

    ShaderInfo(const ShaderInfo &) = delete;
    ShaderInfo &operator=(const ShaderInfo &) = delete;

    ShaderInfo(ShaderInfo &&info) noexcept;

    [[nodiscard]] ShaderId GetId() const;
    [[nodiscard]] vk::ShaderStageFlagBits GetStage() const;
    [[nodiscard]] const std::filesystem::path &GetPath() const;
    [[nodiscard]] bool HasVariants() const;
    [[nodiscard]] Config GetConfig(const PipelineConfig &config) const;
    [[nodiscard]] uint32_t GetVariantCount() const;
    [[nodiscard]] std::span<const vk::SpecializationMapEntry> GetSpecEntries() const;

    void UpdateSpecializations();
    void CompileVariants(std::stop_token stopToken);
    [[nodiscard]] vk::Pipeline GetVariant(const PipelineConfig &config) const;

protected:
    virtual void Compile(std::span<const Config> configs) = 0;

protected:
    ShaderLibrary &m_ShaderLibrary;
    const ShaderId m_Id;
    const vk::PipelineLayout m_Layout;

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

private:
    static Config MakeConfig(Shaders::SpecializationConstant spec1, Shaders::SpecializationConstant spec2);

    static std::filesystem::path ToCachePath(std::filesystem::path path);
};

class RaytracingShaderInfo : public ShaderInfo
{
public:
    using ShaderInfo::ShaderInfo;
    ~RaytracingShaderInfo() override = default;

    RaytracingShaderInfo(const RaytracingShaderInfo &) = delete;
    RaytracingShaderInfo &operator=(const RaytracingShaderInfo &) = delete;

    RaytracingShaderInfo(RaytracingShaderInfo &&info) noexcept;

protected:
    void Compile(std::span<const Config> configs) override;

private:
};

class ComputeShaderInfo : public ShaderInfo
{
public:
    using ShaderInfo::ShaderInfo;
    ~ComputeShaderInfo() override = default;

    ComputeShaderInfo(const ComputeShaderInfo &) = delete;
    ComputeShaderInfo &operator=(const ComputeShaderInfo &) = delete;

    ComputeShaderInfo(ComputeShaderInfo &&info) noexcept;

protected:
    void Compile(std::span<const Config> configs) override;
};

// TODO: Conider templating pipelines instead of hard coding `PipelineConfig`
// TODO: Consider taking MaxRayPayloadSize etc. as constructor parameters

class RaytracingPipeline
{
public:
    RaytracingPipeline(
        ShaderLibrary &shaderLibrary, const std::vector<vk::RayTracingShaderGroupCreateInfoKHR> &groups,
        const std::vector<ShaderId> &shaders, DescriptorSetBuilder &&descriptorSetBuilder,
        vk::PipelineLayout layout
    );
    ~RaytracingPipeline();

    RaytracingPipeline(const RaytracingPipeline &) = delete;
    RaytracingPipeline &operator=(const RaytracingPipeline &) = delete;

    void Update(const PipelineConfig &config);
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

    std::vector<RaytracingShaderInfo> m_Shaders;
    std::vector<vk::RayTracingShaderGroupCreateInfoKHR> m_Groups;

    vk::Pipeline m_Handle;
    LRUCache<PipelineConfig, vk::Pipeline> m_Cache;

    ThreadDispatch<uint32_t> m_CompilationDispatch;

private:
    vk::Pipeline CreateVariant(const PipelineConfig &config);
    vk::Pipeline CreateVariantImmediate(const PipelineConfig &config);
};

class ComputePipeline
{
public:
    ComputePipeline(
        ShaderLibrary &shaderLibrary, DescriptorSetBuilder &&descriptorSetBuilder, vk::PipelineLayout layout,
        ShaderId shaderId
    );
    ~ComputePipeline();

    void Update(const PipelineConfig &config);
    void CancelUpdate();

    void CreateDescriptorSet(uint32_t framesInFlight);
    [[nodiscard]] DescriptorSet *GetDescriptorSet();

    [[nodiscard]] vk::PipelineLayout GetLayout() const;
    [[nodiscard]] vk::Pipeline GetHandle() const;

private:
    ShaderLibrary &m_ShaderLibrary;
    DescriptorSetBuilder m_DescriptorSetBuilder;
    const vk::PipelineLayout m_Layout;
    ComputeShaderInfo m_Shader;
    std::unique_ptr<DescriptorSet> m_DescriptorSet;

    std::jthread m_CompileThread;

    vk::Pipeline m_Handle;
    bool m_IsHandleImmediate = false;

private:
    vk::Pipeline CreateVariantImmediate(const PipelineConfig &config);
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

    std::unique_ptr<RaytracingPipeline> CreatePipelineUnique();

private:
    std::vector<vk::RayTracingShaderGroupCreateInfoKHR> m_Groups;
};

class ComputePipelineBuilder : public PipelineBuilder
{
public:
    ComputePipelineBuilder(ShaderLibrary &shaderLibrary, ShaderId shaderId);
    ~ComputePipelineBuilder() override = default;

    std::unique_ptr<ComputePipeline> CreatePipelineUnique();
};

}
