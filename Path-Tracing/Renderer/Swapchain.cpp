#include "Application.h"
#include "DeviceContext.h"
#include "Swapchain.h"

#include "Core/Core.h"

namespace PathTracing
{

Swapchain::Swapchain(
    vk::SurfaceKHR surface, vk::SurfaceFormatKHR surfaceFormat, vk::PresentModeKHR presentMode
)
    : m_Surface(surface), m_SurfaceFormat(surfaceFormat)
{
    Recreate(presentMode);
}

uint32_t Swapchain::GetImageCount(vk::PresentModeKHR presentMode) const
{
    switch (presentMode)
    {
    case vk::PresentModeKHR::eMailbox:
        return 3;
    case vk::PresentModeKHR::eFifo:
        return 2;
    case vk::PresentModeKHR::eImmediate:
        return 2;
    default:
        throw error(std::format("Present mode {} not supported", vk::to_string(presentMode)));
    }
}

Swapchain::~Swapchain()
{
    for (const SynchronizationObjects &sync : m_SynchronizationObjects)
    {
        DeviceContext::GetLogical().destroyFence(sync.InFlightFence);
        DeviceContext::GetLogical().destroySemaphore(sync.RenderCompleteSemaphore);
        DeviceContext::GetLogical().destroySemaphore(sync.ImageAcquiredSemaphore);
    }

    for (const Frame &frame : m_Frames)
    {
        DeviceContext::GetLogical().destroyImageView(frame.ImageView);
    }

    DeviceContext::GetLogical().destroySwapchainKHR(m_Handle);
}

void Swapchain::Recreate()
{
    Recreate(m_PresentMode);
}

void Swapchain::Recreate(vk::PresentModeKHR presentMode)
{
    auto surfaceCapabilities = DeviceContext::GetPhysical().getSurfaceCapabilitiesKHR(m_Surface);
    logger::debug("Supported usage flags: {}", vk::to_string(surfaceCapabilities.supportedUsageFlags));
    logger::debug("Supported transforms: {}", vk::to_string(surfaceCapabilities.supportedTransforms));
    logger::debug(
        "Supported composite alpha: {}", vk::to_string(surfaceCapabilities.supportedCompositeAlpha)
    );

    auto formats = DeviceContext::GetPhysical().getSurfaceFormatsKHR(m_Surface);
    for (vk::SurfaceFormatKHR format : formats)
        logger::trace(
            "Supported format: {} ({})", vk::to_string(format.format), vk::to_string(format.colorSpace)
        );

    if (std::find(formats.begin(), formats.end(), m_SurfaceFormat) == formats.end())
        throw error("Desired surface format not supported");

    auto modes = DeviceContext::GetPhysical().getSurfacePresentModesKHR(m_Surface);

    for (vk::PresentModeKHR mode : modes)
        logger::debug("Supported present mode: {}", vk::to_string(mode));

    if (std::find(modes.begin(), modes.end(), presentMode) != modes.end())
        m_PresentMode = presentMode;
    logger::info("Selected present mode: {}", vk::to_string(m_PresentMode));

    logger::debug(
        "Surface allowed image count: {} - {}", surfaceCapabilities.minImageCount,
        surfaceCapabilities.maxImageCount
    );

    uint32_t maxImageCount = surfaceCapabilities.maxImageCount;
    if (maxImageCount == 0)
        maxImageCount = std::numeric_limits<uint32_t>::max();

    m_ImageCount = std::clamp(GetImageCount(presentMode), surfaceCapabilities.minImageCount, maxImageCount);
    m_InFlightCount = m_ImageCount - 1;
    logger::info("Swapchain Image Count: {}", m_ImageCount);
    logger::info("Frame In Flight Count: {}", m_InFlightCount);

    std::array<std::pair<std::string_view, vk::Extent2D>, 3> extents = { {
        { "min", surfaceCapabilities.minImageExtent },
        { "max", surfaceCapabilities.maxImageExtent },
        { "current", surfaceCapabilities.currentExtent },
    } };

    for (auto &[name, extent] : extents)
        logger::debug("Surface {} extent: {}x{}", name, extent.width, extent.height);

    m_Extent = surfaceCapabilities.currentExtent;
    auto queueFamilyIndices = DeviceContext::GetQueueFamilyIndices();

    vk::SwapchainCreateInfoKHR createInfo(
        vk::SwapchainCreateFlagsKHR(), m_Surface, m_ImageCount, m_SurfaceFormat.format,
        m_SurfaceFormat.colorSpace, m_Extent, 1,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst,
        queueFamilyIndices.size() > 1 ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive,
        queueFamilyIndices, surfaceCapabilities.currentTransform, vk::CompositeAlphaFlagBitsKHR::eOpaque,
        m_PresentMode, vk::True, m_Handle
    );

    vk::SwapchainKHR oldSwapchainHandle = m_Handle;
    m_Handle = DeviceContext::GetLogical().createSwapchainKHR(createInfo);

    for (const Frame &frame : m_Frames)
        DeviceContext::GetLogical().destroyImageView(frame.ImageView);
    m_Frames.clear();

    for (vk::Image image : DeviceContext::GetLogical().getSwapchainImagesKHR(m_Handle))
    {
        vk::ImageViewCreateInfo createInfo(
            vk::ImageViewCreateFlags(), image, vk::ImageViewType::e2D, m_SurfaceFormat.format,
            vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
        );

        m_Frames.push_back({ image, DeviceContext::GetLogical().createImageView(createInfo) });
    }

    while (m_SynchronizationObjects.size() < m_Frames.size())
    {
        m_SynchronizationObjects.push_back({
            DeviceContext::GetLogical().createSemaphore(vk::SemaphoreCreateInfo()),
            DeviceContext::GetLogical().createSemaphore(vk::SemaphoreCreateInfo()),
            DeviceContext::GetLogical().createFence(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled)),
        });
    }

    DeviceContext::GetLogical().destroySwapchainKHR(oldSwapchainHandle);

    m_CurrentFrameInFlightIndex = 0;
    m_CurrentFrameIndex = 0;
}

uint32_t Swapchain::GetImageCount() const
{
    return m_ImageCount;
}

uint32_t Swapchain::GetInFlightCount() const
{
    return m_InFlightCount;
}

uint32_t Swapchain::GetCurrentFrameInFlightIndex() const
{
    return m_CurrentFrameInFlightIndex;
}

const Swapchain::Frame &Swapchain::GetCurrentFrame() const
{
    return m_Frames[m_CurrentFrameIndex];
}

const Swapchain::SynchronizationObjects &Swapchain::GetCurrentSyncObjects() const
{
    return m_SynchronizationObjects[m_CurrentFrameInFlightIndex];
}

bool Swapchain::AcquireImage()
{
    m_CurrentFrameInFlightIndex++;
    if (m_CurrentFrameInFlightIndex == m_InFlightCount)
        m_CurrentFrameInFlightIndex = 0;

    const SynchronizationObjects &sync = m_SynchronizationObjects[m_CurrentFrameInFlightIndex];

    {
        vk::Result result = DeviceContext::GetLogical().waitForFences(
            { sync.InFlightFence }, vk::True, std::numeric_limits<uint64_t>::max()
        );
        assert(result == vk::Result::eSuccess);

        DeviceContext::GetLogical().resetFences({ sync.InFlightFence });
    }

    try
    {
        vk::ResultValue result = DeviceContext::GetLogical().acquireNextImageKHR(
            m_Handle, std::numeric_limits<uint64_t>::max(), sync.ImageAcquiredSemaphore, nullptr
        );

        assert(result.result == vk::Result::eSuccess);
        m_CurrentFrameIndex = result.value;
    }
    catch (vk::OutOfDateKHRError error)
    {
        logger::warn(error.what());
        return false;
    }

    return true;
}

bool Swapchain::Present()
{
    const SynchronizationObjects &sync = m_SynchronizationObjects[m_CurrentFrameInFlightIndex];

    vk::PresentInfoKHR presentInfo({ sync.RenderCompleteSemaphore }, { m_Handle }, { m_CurrentFrameIndex });
    try
    {
        vk::Result res = DeviceContext::GetPresentQueue().presentKHR(presentInfo);
        assert(res == vk::Result::eSuccess);
    }
    catch (vk::OutOfDateKHRError error)
    {
        logger::warn(error.what());
        return false;
    }

    return true;
}

vk::Extent2D Swapchain::GetExtent() const
{
    return m_Extent;
}

vk::SurfaceFormatKHR Swapchain::GetSurfaceFormat() const
{
    return m_SurfaceFormat;
}

vk::PresentModeKHR Swapchain::GetPresentMode() const
{
    return m_PresentMode;
}

}