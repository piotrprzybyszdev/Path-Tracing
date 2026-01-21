#include <array>

#include "Core/Core.h"

#include "DeviceContext.h"
#include "Renderer.h"
#include "Swapchain.h"
#include "Utils.h"

namespace PathTracing
{

Swapchain::Swapchain(
    vk::SurfaceKHR surface, vk::PresentModeKHR presentMode, vk::Extent2D extent, uint32_t imageCount
)
    : m_Surface(surface), m_Extent(extent), m_ImageCount(imageCount)
{
    Recreate(presentMode);
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
        DeviceContext::GetLogical().destroyImageView(frame.LinearImageView);
        DeviceContext::GetLogical().destroyImageView(frame.NonLinearImageView);
    }

    DeviceContext::GetLogical().destroySwapchainKHR(m_Handle);
}

void Swapchain::Recreate()
{
    Recreate(m_PresentMode);
}

void Swapchain::Recreate(vk::Extent2D extent)
{
    if (m_Extent == extent)
        return;

    m_Extent = extent;
    Recreate();
}

void Swapchain::Recreate(uint32_t imageCount)
{
    if (m_ImageCount == imageCount)
        return;

    m_ImageCount = imageCount;
    Recreate();
}

void Swapchain::Recreate(bool allowHdr)
{
    if (m_IsHdrAllowed == allowHdr)
        return;

    m_IsHdrAllowed = allowHdr;
    Recreate();
}

void Swapchain::Recreate(vk::PresentModeKHR presentMode)
{
    auto surfaceCapabilities = DeviceContext::GetPhysical().getSurfaceCapabilitiesKHR(m_Surface);
    logger::debug("Supported usage flags: {}", vk::to_string(surfaceCapabilities.supportedUsageFlags));
    logger::debug("Supported transforms: {}", vk::to_string(surfaceCapabilities.supportedTransforms));
    logger::debug(
        "Supported composite alpha: {}", vk::to_string(surfaceCapabilities.supportedCompositeAlpha)
    );

    auto supportedFormats = DeviceContext::GetPhysical().getSurfaceFormatsKHR(m_Surface);
    for (vk::SurfaceFormatKHR format : supportedFormats)
        logger::trace(
            "Supported format: {} ({})", vk::to_string(format.format), vk::to_string(format.colorSpace)
        );

    SelectFormat(supportedFormats);

    m_PresentModes = DeviceContext::GetPhysical().getSurfacePresentModesKHR(m_Surface);

    for (vk::PresentModeKHR mode : m_PresentModes)
        logger::debug("Supported present mode: {}", vk::to_string(mode));

    if (std::ranges::find(m_PresentModes, presentMode) != m_PresentModes.end())
        m_PresentMode = presentMode;
    logger::info("Selected present mode: {}", vk::to_string(m_PresentMode));

    logger::debug(
        "Surface allowed image count: {} - {}", surfaceCapabilities.minImageCount,
        surfaceCapabilities.maxImageCount
    );

    uint32_t maxImageCount = surfaceCapabilities.maxImageCount;
    if (maxImageCount == 0)
        maxImageCount = std::numeric_limits<uint32_t>::max();

    m_ImageCount = std::clamp(m_ImageCount, surfaceCapabilities.minImageCount, maxImageCount);
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

    if (Utils::LtExtent(m_Extent, surfaceCapabilities.minImageExtent) ||
        Utils::LtExtent(surfaceCapabilities.maxImageExtent, m_Extent))
        m_Extent = surfaceCapabilities.currentExtent;

    logger::info("Swapchain resizing to: {}x{}", m_Extent.width, m_Extent.height);

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
    {
        DeviceContext::GetLogical().destroyImageView(frame.LinearImageView);
        DeviceContext::GetLogical().destroyImageView(frame.NonLinearImageView);
    }
    m_Frames.clear();

    for (vk::Image image : DeviceContext::GetLogical().getSwapchainImagesKHR(m_Handle))
    {
        vk::ImageViewCreateInfo createInfo(
            vk::ImageViewCreateFlags(), image, vk::ImageViewType::e2D, m_SurfaceFormat.format,
            vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
        );

        auto nonLinearImageView = DeviceContext::GetLogical().createImageView(createInfo);

        createInfo.setFormat(m_SurfaceFormat.format);
        auto linearImageView = DeviceContext::GetLogical().createImageView(createInfo);

        m_Frames.emplace_back(image, linearImageView, nonLinearImageView);
    }

    while (m_SynchronizationObjects.size() < m_Frames.size())
    {
        m_SynchronizationObjects.push_back(
            {
                DeviceContext::GetLogical().createSemaphore(vk::SemaphoreCreateInfo()),
                DeviceContext::GetLogical().createSemaphore(vk::SemaphoreCreateInfo()),
                DeviceContext::GetLogical().createFence(
                    vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled)
                ),
            }
        );
    }

    DeviceContext::GetLogical().destroySwapchainKHR(oldSwapchainHandle);

    m_CurrentFrameInFlightIndex = 0;
    m_CurrentFrameIndex = 0;

    for (int i = 0; i < m_Frames.size(); i++)
    {
        Utils::SetDebugName(m_Frames[i].Image, std::format("Swapchain Image {}", i));
        Utils::SetDebugName(m_Frames[i].LinearImageView, std::format("Swapchain Linear ImageView {}", i));
        Utils::SetDebugName(
            m_Frames[i].NonLinearImageView, std::format("Swapchain NonLinear ImageView {}", i)
        );
    }
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
    const SynchronizationObjects &sync = m_SynchronizationObjects[m_CurrentFrameInFlightIndex];

    {
        vk::Result result = DeviceContext::GetLogical().waitForFences(
            { sync.InFlightFence }, vk::True, std::numeric_limits<uint64_t>::max()
        );
        assert(result == vk::Result::eSuccess);
    }

    try
    {
        vk::ResultValue result = DeviceContext::GetLogical().acquireNextImageKHR(
            m_Handle, std::numeric_limits<uint64_t>::max(), sync.ImageAcquiredSemaphore, nullptr
        );

        m_CurrentFrameIndex = result.value;

        if (result.result == vk::Result::eSuboptimalKHR)
            logger::warn("Swapchain Acquire: {}", vk::to_string(result.result));
        else
            assert(result.result == vk::Result::eSuccess);
    }
    catch (const vk::OutOfDateKHRError &error)
    {
        logger::warn(error.what());
        return false;
    }

    DeviceContext::GetLogical().resetFences({ sync.InFlightFence });

    return true;
}

bool Swapchain::Present()
{
    const SynchronizationObjects &sync = m_SynchronizationObjects[m_CurrentFrameInFlightIndex];

    vk::PresentInfoKHR presentInfo({ sync.RenderCompleteSemaphore }, { m_Handle }, { m_CurrentFrameIndex });
    try
    {
        vk::Result res;
        {
            auto lock = DeviceContext::GetPresentQueue().GetLock();
            res = DeviceContext::GetPresentQueue().Handle.presentKHR(presentInfo);
        }
        if (res == vk::Result::eSuboptimalKHR)
        {
            logger::warn("Swapchain Present: {}", vk::to_string(res));
            return false;
        }
        assert(res == vk::Result::eSuccess);
    }
    catch (const vk::OutOfDateKHRError &error)
    {
        logger::warn(error.what());
        return false;
    }

    m_CurrentFrameInFlightIndex++;
    if (m_CurrentFrameInFlightIndex == m_InFlightCount)
        m_CurrentFrameInFlightIndex = 0;

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

std::span<const vk::PresentModeKHR> Swapchain::GetPresentModes() const
{
    return m_PresentModes;
}

vk::PresentModeKHR Swapchain::GetPresentMode() const
{
    return m_PresentMode;
}

bool Swapchain::IsHdr() const
{
    return m_SurfaceFormat.colorSpace == vk::ColorSpaceKHR::eHdr10St2084EXT;
}

bool Swapchain::IsHdrAllowed() const
{
    return m_IsHdrAllowed;
}

bool Swapchain::IsHdrSupported() const
{
    return m_IsHdrSupported;
}

void Swapchain::SelectFormat(std::span<const vk::SurfaceFormatKHR> supportedFormats)
{
    auto hdr = FindColorSpace(supportedFormats, vk::ColorSpaceKHR::eHdr10St2084EXT);
    m_IsHdrSupported = hdr.has_value();
    if (hdr.has_value() && m_IsHdrAllowed)
    {
        m_SurfaceFormat = hdr.value();
        logger::info("HDR Enabled");
        return;
    }

    std::array<vk::Format, 2> formats = { vk::Format::eB8G8R8A8Srgb, vk::Format::eR8G8B8A8Srgb };
    for (auto format : formats)
    {
        auto fmt = FindFormat(supportedFormats, format);
        if (fmt.has_value())
        {
            m_SurfaceFormat = fmt.value();
            return;
        }
    }

    throw error("No desired surface formats are supported");
}

std::optional<vk::SurfaceFormatKHR> Swapchain::FindColorSpace(
    std::span<const vk::SurfaceFormatKHR> formats, vk::ColorSpaceKHR space
)
{
    for (auto format : formats)
        if (format.colorSpace == space)
            return format;

    return std::nullopt;
}

std::optional<vk::SurfaceFormatKHR> Swapchain::FindFormat(
    std::span<const vk::SurfaceFormatKHR> formats, vk::Format format
)
{
    for (auto surfaceFormat : formats)
        if (surfaceFormat.format == format && surfaceFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
            return surfaceFormat;

    return std::nullopt;
}

}