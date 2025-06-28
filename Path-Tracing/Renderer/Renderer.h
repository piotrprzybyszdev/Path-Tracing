#pragma once

#include <vulkan/vulkan.hpp>

#include <filesystem>

#include "Core/Camera.h"
#include "Core/Core.h"

#include "Buffer.h"
#include "Image.h"
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

    static void OnUpdate(float timeStep);
    static void OnResize(vk::Extent2D extent);
    static void Render(uint32_t frameInFlightIndex, const Camera &camera);

private:
    static const Swapchain *s_Swapchain;

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

    static vk::CommandPool s_MainCommandPool;

    struct RenderingResources
    {
        vk::CommandPool CommandPool;
        vk::CommandBuffer CommandBuffer;

        vk::DescriptorSet DescriptorSet;
        std::unique_ptr<Image> StorageImage;
    };

    static std::vector<RenderingResources> s_RenderingResources;

    static struct SceneData
    {
        std::unique_ptr<Buffer> VertexBuffer = nullptr;
        std::unique_ptr<Buffer> IndexBuffer = nullptr;
        std::unique_ptr<Buffer> TransformMatrixBuffer = nullptr;

        std::unique_ptr<Buffer> BottomLevelAccelerationStructureBuffer = nullptr;
        vk::AccelerationStructureKHR BottomLevelAccelerationStructure { nullptr };
        vk::DeviceAddress BottomLevelAccelerationStructureAddress { 0 };

        std::unique_ptr<Buffer> TopLevelAccelerationStructureBuffer = nullptr;
        vk::AccelerationStructureKHR TopLevelAccelerationStructure { nullptr };
        vk::DeviceAddress TopLevelAccelerationStructureAddress { 0 };
    } s_StaticSceneData;

    static std::unique_ptr<Buffer> s_UniformBuffer;

    static vk::DescriptorSetLayout s_DescriptorSetLayout;
    static vk::PipelineLayout s_PipelineLayout;

    static vk::Pipeline s_Pipeline;

    static vk::DescriptorPool s_DescriptorPool;

private:
    static std::unique_ptr<Image> CreateStorageImage(vk::Extent2D extent);

    static void CreateScene();
    static void SetupPipeline();

    static void RecordCommandBuffer(const RenderingResources &resources);

private:
    static std::unique_ptr<BufferBuilder> s_BufferBuilder;
    static std::unique_ptr<ImageBuilder> s_ImageBuilder;

    static std::unique_ptr<ShaderLibrary> s_ShaderLibrary;

    static void ImageTransition(
        vk::CommandBuffer buffer, vk::Image image, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo,
        vk::AccessFlagBits accessFrom, vk::AccessFlagBits accessTo, vk::PipelineStageFlagBits stageFrom,
        vk::PipelineStageFlagBits stageTo
    );
};

}
