#pragma once

#include <vulkan/vulkan.hpp>

#include <filesystem>

#include "Core/Camera.h"
#include "Core/Core.h"

#include "AccelerationStructure.h"
#include "Buffer.h"
#include "DescriptorSet.h"
#include "Image.h"
#include "MaterialSystem.h"
#include "ShaderLibrary.h"
#include "Swapchain.h"
#include "Window.h"

namespace PathTracing
{

class Renderer
{
public:
    static void Init(const Swapchain *swapchain);
    static void Shutdown();

    static void SetScene();

    static void OnUpdate(float timeStep);
    static void OnResize(vk::Extent2D extent);
    static void Render(const Camera &camera);

    static Shaders::RenderModeFlags s_RenderMode;
    static Shaders::EnabledTextureFlags s_EnabledTextures;

    // Blocking one time submission buffer
    static struct CommandBuffer
    {
        vk::CommandBuffer CommandBuffer;
        vk::Fence Fence;

        void Init();
        void Destroy();

        void Begin() const;
        void Submit(vk::Queue queue) const;
    } s_MainCommandBuffer;

private:
    static const Swapchain *s_Swapchain;

    static vk::CommandPool s_MainCommandPool;

    struct RenderingResources
    {
        vk::CommandPool CommandPool;
        vk::CommandBuffer CommandBuffer;

        std::unique_ptr<Image> StorageImage;
    };

    static std::vector<RenderingResources> s_RenderingResources;

    static struct SceneData
    {
        std::unique_ptr<AccelerationStructure> AcceleraionStructure = nullptr;
    } s_StaticSceneData;

    static std::unique_ptr<Buffer> s_RaygenUniformBuffer;
    static std::unique_ptr<Buffer> s_ClosestHitUniformBuffer;

    static std::unique_ptr<DescriptorSet> s_DescriptorSet;

    static vk::PipelineLayout s_PipelineLayout;
    static vk::Pipeline s_Pipeline;

private:
    static std::unique_ptr<Image> CreateStorageImage(vk::Extent2D extent);

    static void CreateScene();
    static void SetupPipeline();

    static void RecordCommandBuffer(const RenderingResources &resources);

private:
    static std::unique_ptr<BufferBuilder> s_BufferBuilder;
    static std::unique_ptr<ImageBuilder> s_ImageBuilder;

    static std::unique_ptr<ShaderLibrary> s_ShaderLibrary;

    static vk::Sampler s_Sampler;
};

}
