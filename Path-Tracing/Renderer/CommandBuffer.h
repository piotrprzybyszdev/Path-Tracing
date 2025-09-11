#pragma once

#include <vulkan/vulkan.hpp>

namespace PathTracing
{

class CommandBuffer
{
public:
    CommandBuffer(uint32_t queueFamilyIndex, vk::Queue queue);
    ~CommandBuffer();

    vk::CommandBuffer Buffer;

    void Begin(
        vk::Semaphore waitSemaphore = nullptr,
        vk::PipelineStageFlags2 stage = vk::PipelineStageFlagBits2::eNone
    );
    [[nodiscard]] vk::Semaphore Signal();

    void End();

    void Submit();
    void SubmitBlocking();

private:
    const vk::Queue m_Queue;
    vk::CommandPool m_CommandPool;

    bool m_IsOpen = false;
    bool m_ShouldSignal = false;

    vk::Fence m_Fence = nullptr;
    vk::Semaphore m_SignalSemaphore = nullptr;

    vk::Semaphore m_WaitSemaphore = nullptr;
    vk::PipelineStageFlags2 m_WaitStageMask;

private:
    void Submit(vk::Fence waitFence);
    void WaitFence();
};

}
