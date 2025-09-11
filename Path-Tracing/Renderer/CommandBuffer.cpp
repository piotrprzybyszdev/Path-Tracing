#include "Core/Core.h"

#include "CommandBuffer.h"
#include "DeviceContext.h"

namespace PathTracing
{

CommandBuffer::CommandBuffer(uint32_t queueFamilyIndex, vk::Queue queue) : m_Queue(queue)
{
    if (queueFamilyIndex == vk::QueueFamilyIgnored)
        return;

    vk::CommandPoolCreateInfo createInfo(
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer, queueFamilyIndex
    );
    m_CommandPool = DeviceContext::GetLogical().createCommandPool(createInfo);
    m_Fence = DeviceContext::GetLogical().createFence(vk::FenceCreateInfo());

    vk::CommandBufferAllocateInfo allocateInfo(m_CommandPool, vk::CommandBufferLevel::ePrimary, 1);
    Buffer = DeviceContext::GetLogical().allocateCommandBuffers(allocateInfo)[0];
    m_SignalSemaphore = DeviceContext::GetLogical().createSemaphore(vk::SemaphoreCreateInfo());
}

CommandBuffer::~CommandBuffer()
{
    DeviceContext::GetLogical().destroySemaphore(m_SignalSemaphore);
    DeviceContext::GetLogical().destroyFence(m_Fence);
    DeviceContext::GetLogical().destroyCommandPool(m_CommandPool);
}

void CommandBuffer::Begin(vk::Semaphore waitSemaphore, vk::PipelineStageFlags2 stage)
{
    assert(m_IsOpen == false);

    Buffer.reset();

    vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    Buffer.begin(beginInfo);

    m_WaitSemaphore = waitSemaphore;
    m_WaitStageMask = stage;
    m_IsOpen = true;
}

vk::Semaphore CommandBuffer::Signal()
{
    m_ShouldSignal = true;
    return m_SignalSemaphore;
}

void CommandBuffer::End()
{
    assert(m_IsOpen == true);

    Buffer.end();
    m_IsOpen = false;
}

void CommandBuffer::Submit()
{
    if (m_IsOpen)
        End();

    Submit(nullptr);
}

void CommandBuffer::SubmitBlocking()
{
    Submit(m_Fence);
    WaitFence();
}

void CommandBuffer::Submit(vk::Fence waitFence)
{
    if (m_IsOpen)
        End();

    vk::SubmitInfo2 info;
    vk::CommandBufferSubmitInfo cmdInfo(Buffer);
    info.setCommandBufferInfos(cmdInfo);
    if (m_ShouldSignal)
    {
        vk::SemaphoreSubmitInfo signalInfo(m_SignalSemaphore);
        info.setSignalSemaphoreInfos(signalInfo);
    }
    if (m_WaitSemaphore != nullptr)
    {
        vk::SemaphoreSubmitInfo waitInfo(m_WaitSemaphore, 0, m_WaitStageMask);
        info.setWaitSemaphoreInfos(waitInfo);
    }

    m_Queue.submit2(info, waitFence);

    m_WaitSemaphore = nullptr;
    m_WaitStageMask = vk::PipelineStageFlags2();
    m_ShouldSignal = false;
}

void CommandBuffer::WaitFence()
{
    try
    {
        vk::Result result = DeviceContext::GetLogical().waitForFences(
            { m_Fence }, vk::True, std::numeric_limits<uint64_t>::max()
        );
        assert(result == vk::Result::eSuccess);
    }
    catch (const vk::SystemError &err)
    {
        throw PathTracing::error(err.what());
    }
    DeviceContext::GetLogical().resetFences({ m_Fence });
}

}
