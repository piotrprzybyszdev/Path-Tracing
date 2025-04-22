#pragma once

#include <vulkan/vulkan.hpp>

#include <filesystem>

#include "Core/Camera.h"

#include "Buffer.h"
#include "Frame.h"
#include "Image.h"
#include "LogicalDevice.h"
#include "PhysicalDevice.h"
#include "ShaderLibrary.h"
#include "Window.h"

namespace PathTracing
{

class Renderer
{
public:
    Renderer(Window &window, Camera &camera);
    ~Renderer();

    void OnUpdate(float timeStep);
    void OnRender();

private:
    Window &m_Window;
    Camera &m_Camera;

    uint32_t m_Width = 1280, m_Height = 720;

    vk::Instance m_Instance { nullptr };
    vk::SurfaceKHR m_Surface { nullptr };

    vk::detail::DispatchLoaderDynamic m_DispatchLoader;

#ifndef NDEBUG
    vk::DebugUtilsMessengerEXT m_DebugMessenger { nullptr };
#endif

    // Blocking one time submission buffer
    struct CommandBuffer
    {
        vk::CommandBuffer CommandBuffer;
        vk::Fence Fence;

        void Begin()
        {
            vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
            CommandBuffer.begin(beginInfo);
        }

        void Submit(vk::Device device, vk::Queue queue)
        {
            CommandBuffer.end();
            vk::SubmitInfo submitInfo = {};
            submitInfo.setCommandBuffers({ CommandBuffer });
            device.resetFences({ Fence });
            queue.submit({ submitInfo }, Fence);

            try
            {
                vk::Result result =
                    device.waitForFences({ Fence }, vk::True, std::numeric_limits<uint64_t>::max());
                assert(result == vk::Result::eSuccess);
            }
            catch (vk::SystemError error)
            {
                throw PathTracing::error(error.what());
            }
        }
    } m_MainCommandBuffer;

    LogicalDevice m_Device;

    vk::Device m_LogicalDevice { nullptr };
    vk::Queue m_GraphicsQueue { nullptr };
    vk::CommandPool m_CommandPool;

    struct SynchronizationObjects
    {
        vk::Semaphore ImageAcquiredSemaphore;
        vk::Semaphore RenderCompleteSemaphore;
        vk::Fence InFlightFence;
    };

    vk::SwapchainKHR m_Swapchain { nullptr };
    std::vector<Frame> m_Frames;
    std::vector<SynchronizationObjects> m_SynchronizationObjects;

    std::unique_ptr<Buffer> m_VertexBuffer = nullptr;
    std::unique_ptr<Buffer> m_IndexBuffer = nullptr;
    std::unique_ptr<Buffer> m_TransformMatrixBuffer = nullptr;
    std::unique_ptr<Buffer> m_UniformBuffer = nullptr;

    std::unique_ptr<Buffer> m_BottomLevelAccelerationStructureBuffer = nullptr;
    vk::AccelerationStructureKHR m_BottomLevelAccelerationStructure { nullptr };
    vk::DeviceAddress m_BottomLevelAccelerationStructureAddress { 0 };

    std::unique_ptr<Buffer> m_TopLevelAccelerationStructureBuffer = nullptr;
    vk::AccelerationStructureKHR m_TopLevelAccelerationStructure { nullptr };
    vk::DeviceAddress m_TopLevelAccelerationStructureAddress { 0 };

    std::unique_ptr<Image> m_StorageImage = nullptr;

    vk::DescriptorSetLayout m_DescriptorSetLayout { nullptr };
    vk::PipelineLayout m_PipelineLayout { nullptr };

    std::unique_ptr<ShaderLibrary> m_ShaderLibrary = nullptr;

    vk::Pipeline m_Pipeline { nullptr };

    vk::DescriptorPool m_DescriptorPool { nullptr };
    vk::DescriptorSet m_DescriptorSet { nullptr };

    vk::RenderPass m_RenderPass { nullptr };

private:
    void OnResize(uint32_t width, uint32_t height);

    void RecreateSwapchain();

    void CreateStorageImage();

    void CreateScene();
    void SetupPipeline();

    void CreateDescriptorSets();
    void UpdateDescriptorSets();
    void RecordCommandBuffers();

    int m_CurrentFrame = 0;

private:
    std::unique_ptr<BufferBuilder> m_BufferBuilder = nullptr;
    std::unique_ptr<ImageBuilder> m_ImageBuilder = nullptr;
};

}
