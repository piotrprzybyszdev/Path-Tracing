#include "Core/Core.h"

#include "LogicalDevice.h"

namespace PathTracing
{

static bool checkSupported(
    const std::vector<const char *> &extensions, const std::vector<const char *> &layers,
    const std::vector<vk::ExtensionProperties> &supportedExtensions,
    const std::vector<vk::LayerProperties> &supportedLayers
);

LogicalDevice::LogicalDevice(
    vk::Instance instance, vk::SurfaceKHR surface, std::vector<const char *> layers,
    std::vector<const char *> extensions, vk::PhysicalDeviceFeatures2 *features
)
{
    if (instance == nullptr)
        return;

    std::vector<vk::PhysicalDevice> availableDevices = instance.enumeratePhysicalDevices();

    std::vector<const vk::PhysicalDevice *> suitableDevices;
    for (const vk::PhysicalDevice &device : availableDevices)
    {
        vk::PhysicalDeviceProperties properties = device.getProperties();

        logger::debug(
            "Found physical device {} ({})", properties.deviceName.data(),
            vk::to_string(properties.deviceType)
        );

        if (checkSupported(
                extensions, layers, device.enumerateDeviceExtensionProperties(),
                device.enumerateDeviceLayerProperties()
            ))
        {
            logger::info("{} is a suitable device", properties.deviceName.data());
            suitableDevices.push_back(&device);
        }
    }

    if (suitableDevices.empty())
        throw error("No suitable devices found");

    Physical = PathTracing::PhysicalDevice(
        **std::max_element(
            suitableDevices.begin(), suitableDevices.end(),
            [](const vk::PhysicalDevice *device1, const vk::PhysicalDevice *device2) {
                vk::PhysicalDeviceMemoryProperties properties1 = device1->getMemoryProperties();
                vk::PhysicalDeviceMemoryProperties properties2 = device2->getMemoryProperties();
                return properties1.memoryHeapCount < properties2.memoryHeapCount;
            }
        ),
        surface
    );

    const float priorities[] = { 1.0f };

    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos = {};
    std::vector<uint32_t> queueFamilyIndices = {};

    for (vk::QueueFlags flags : QueueFlags)
    {
        uint32_t index = Physical.GetQueueFamilyIndex(flags);
        queueFamilyIndices.push_back(index);
        queueCreateInfos.push_back({ vk::DeviceQueueCreateFlags(), index, priorities });
    }

    m_GraphicsQueueFamilyIndex = queueFamilyIndices[0];
    logger::debug("Graphics Queue Family set to: {}", m_GraphicsQueueFamilyIndex);

    vk::DeviceCreateInfo createInfo(
        vk::DeviceCreateFlags(), queueCreateInfos, layers, extensions, nullptr, features
    );

    m_Handle = Physical.m_Handle.createDevice(createInfo);

    m_GraphicsQueue = m_Handle.getQueue(m_GraphicsQueueFamilyIndex, 0);
    m_GraphicsCommandPool = m_Handle.createCommandPool({ vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                                         m_GraphicsQueueFamilyIndex });
}

LogicalDevice::~LogicalDevice()
{
}

vk::Device LogicalDevice::GetHandle() const
{
    return m_Handle;
}

vk::Queue LogicalDevice::GetGraphicsQueue() const
{
    return m_GraphicsQueue;
}

vk::CommandPool LogicalDevice::GetGraphicsCommandPool() const
{
    return m_GraphicsCommandPool;
}

vk::SwapchainKHR LogicalDevice::CreateSwapchain(
    uint32_t width, uint32_t height, vk::SwapchainKHR oldSwapchain, vk::SurfaceKHR surface
) const
{
    vk::SurfaceCapabilitiesKHR surfaceCapabilities = Physical.m_Handle.getSurfaceCapabilitiesKHR(surface);
    logger::debug("Supported usage flags: {}", vk::to_string(surfaceCapabilities.supportedUsageFlags));
    logger::debug("Supported transforms: {}", vk::to_string(surfaceCapabilities.supportedTransforms));
    logger::debug(
        "Supported composite alpha: {}", vk::to_string(surfaceCapabilities.supportedCompositeAlpha)
    );

    std::vector<vk::SurfaceFormatKHR> surfaceFormats = Physical.m_Handle.getSurfaceFormatsKHR(surface);
    vk::SurfaceFormatKHR surfaceFormat = surfaceFormats[0];
    for (vk::SurfaceFormatKHR format : surfaceFormats)
    {
        logger::debug(
            "Supported format: {} ({})", vk::to_string(format.format), vk::to_string(format.colorSpace)
        );

        if (format.format == PreferredFormat && format.colorSpace == PreferredColorSpace)
            surfaceFormat = format;
    }

    logger::info(
        "Selected surface format {} ({})", vk::to_string(surfaceFormat.format),
        vk::to_string(surfaceFormat.colorSpace)
    );

    std::vector<vk::PresentModeKHR> modes = Physical.m_Handle.getSurfacePresentModesKHR(surface);

    for (vk::PresentModeKHR mode : modes)
        logger::debug("Supported present mode: {}", vk::to_string(mode));

    vk::PresentModeKHR selectedPresentMode = vk::PresentModeKHR::eFifo;
    if (std::find(modes.begin(), modes.end(), PreferredPresentMode) != modes.end())
        selectedPresentMode = PreferredPresentMode;
    logger::info("Selected present mode: {}", vk::to_string(selectedPresentMode));

    logger::debug(
        "Surface allowed image count: {} - {}", surfaceCapabilities.minImageCount,
        surfaceCapabilities.maxImageCount
    );

    uint32_t maxImageCount = surfaceCapabilities.maxImageCount;
    if (maxImageCount == 0)
        maxImageCount = std::numeric_limits<uint32_t>::max();

    uint32_t imageCount = std::clamp(3u, surfaceCapabilities.minImageCount, maxImageCount);
    logger::info("Image Count: {}", imageCount);

    vk::SwapchainCreateInfoKHR createInfo(
        vk::SwapchainCreateFlagsKHR(), surface, imageCount, surfaceFormat.format, surfaceFormat.colorSpace,
        vk::Extent2D(width, height), 1,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst,
        vk::SharingMode::eExclusive, { m_GraphicsQueueFamilyIndex }, surfaceCapabilities.currentTransform,
        vk::CompositeAlphaFlagBitsKHR::eOpaque, selectedPresentMode, vk::True, oldSwapchain
    );

    return m_Handle.createSwapchainKHR(createInfo);
}

BufferBuilder LogicalDevice::CreateBufferBuilder() const
{
    return BufferBuilder(m_Handle, Physical);
}

std::unique_ptr<BufferBuilder> LogicalDevice::CreateBufferBuilderUnique() const
{
    return std::make_unique<BufferBuilder>(m_Handle, Physical);
}

std::unique_ptr<ImageBuilder> LogicalDevice::CreateImageBuilderUnique() const
{
    return std::make_unique<ImageBuilder>(m_Handle, Physical);
}

std::unique_ptr<ShaderLibrary> LogicalDevice::CreateShaderLibrary() const
{
    return std::make_unique<ShaderLibrary>(*this, Physical);
}

static bool checkSupported(
    const std::vector<const char *> &extensions, const std::vector<const char *> &layers,
    const std::vector<vk::ExtensionProperties> &supportedExtensions,
    const std::vector<vk::LayerProperties> &supportedLayers
)
{
    for (const vk::ExtensionProperties &extension : supportedExtensions)
        logger::debug("Extension {} is supported", extension.extensionName.data());

    for (const char *extension : extensions)
        if (std::none_of(
                supportedExtensions.begin(), supportedExtensions.end(),
                [extension](vk::ExtensionProperties prop) {
                    return strcmp(prop.extensionName.data(), extension) == 0;
                }
            ))
        {
            logger::error("Extension {} is not supported", extension);
            return false;
        }

    for (const vk::LayerProperties &layer : supportedLayers)
        logger::debug("Layer {} is supported", layer.layerName.data());

    for (std::string_view layer : layers)
        if (std::none_of(supportedLayers.begin(), supportedLayers.end(), [layer](vk::LayerProperties prop) {
                return strcmp(prop.layerName.data(), layer.data()) == 0;
            }))
        {
            logger::error("Layer {} is not supported", layer);
            return false;
        }

    return true;
}

}
