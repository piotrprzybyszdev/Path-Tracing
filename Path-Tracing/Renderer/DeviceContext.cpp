#include <vulkan/vulkan.hpp>

#include <ranges>

#include "DeviceContext.h"

#include "Core/Core.h"

namespace PathTracing
{

DeviceContext::PhysicalDevice DeviceContext::s_PhysicalDevice = {};
DeviceContext::LogicalDevice DeviceContext::s_LogicalDevice = {};

void DeviceContext::Init(
    vk::Instance instance, std::vector<const char *> requestedLayers, vk::SurfaceKHR surface
)
{
    std::vector<const char *> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
    };

    for (const char *extension : deviceExtensions)
        logger::info("Device Extension {} is required", extension);

    std::vector<vk::PhysicalDevice> suitableDevices;
    for (vk::PhysicalDevice device : instance.enumeratePhysicalDevices())
    {
        auto properties = device.getProperties();

        logger::debug(
            "Found physical device {} ({})", properties.deviceName.data(),
            vk::to_string(properties.deviceType)
        );

        if (CheckSuitable(device, deviceExtensions, requestedLayers))
            suitableDevices.push_back(device);
    }

    if (suitableDevices.empty())
        throw error("No suitable devices found");

    s_PhysicalDevice.Handle = *std::max_element(
        suitableDevices.begin(), suitableDevices.end(),
        [](vk::PhysicalDevice device1, vk::PhysicalDevice device2) {
            auto properties1 = device1.getMemoryProperties();
            auto properties2 = device2.getMemoryProperties();
            return properties1.memoryHeapCount < properties2.memoryHeapCount;
        }
    );

    s_PhysicalDevice.Properties = s_PhysicalDevice.Handle.getProperties();
    s_PhysicalDevice.MemoryProperties = s_PhysicalDevice.Handle.getMemoryProperties();
    s_PhysicalDevice.QueueFamilyProperties = s_PhysicalDevice.Handle.getQueueFamilyProperties();

    logger::info("Selected physical device: {}", s_PhysicalDevice.Properties.deviceName.data());

    constexpr auto flags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eTransfer;
    for (uint32_t index = 0; index < s_PhysicalDevice.QueueFamilyProperties.size(); index++)
    {
        if (s_PhysicalDevice.Handle.getSurfaceSupportKHR(index, surface) == vk::True &&
            (s_PhysicalDevice.QueueFamilyProperties[index].queueFlags & flags) == flags)
        {
            s_LogicalDevice.MainQueueFamilyIndex = index;
            break;
        }
    }

    if (s_LogicalDevice.MainQueueFamilyIndex == vk::QueueFamilyIgnored)
        throw error("No appropriate queue family found");

    const float priorities[] = { 1.0f };
    vk::DeviceQueueCreateInfo mainQueueCreateInfo(
        vk::DeviceQueueCreateFlags(), s_LogicalDevice.MainQueueFamilyIndex, priorities
    );

    vk::PhysicalDeviceFeatures2 features;
    vk::PhysicalDeviceBufferDeviceAddressFeatures bufferFeatures;

    bufferFeatures.setBufferDeviceAddress(vk::True);
    features.setPNext(&bufferFeatures);

    vk::PhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures;
    accelerationStructureFeatures.setAccelerationStructure(vk::True);
    bufferFeatures.setPNext(&accelerationStructureFeatures);

    vk::PhysicalDeviceRayTracingPipelineFeaturesKHR pipelineFeatures;
    pipelineFeatures.setRayTracingPipeline(vk::True);
    accelerationStructureFeatures.setPNext(&pipelineFeatures);

    vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures;
    dynamicRenderingFeatures.setDynamicRendering(vk::True);
    pipelineFeatures.setPNext(&dynamicRenderingFeatures);

    vk::DeviceCreateInfo createInfo(
        vk::DeviceCreateFlags(), { mainQueueCreateInfo }, requestedLayers, deviceExtensions, nullptr,
        &features
    );

    s_LogicalDevice.Handle = s_PhysicalDevice.Handle.createDevice(createInfo);
    s_LogicalDevice.MainQueue = s_LogicalDevice.Handle.getQueue(s_LogicalDevice.MainQueueFamilyIndex, 0);
}

void DeviceContext::Shutdown()
{
    s_LogicalDevice.Handle.destroy();
}

vk::PhysicalDevice DeviceContext::GetPhysical()
{
    return s_PhysicalDevice.Handle;
}

vk::Device DeviceContext::GetLogical()
{
    return s_LogicalDevice.Handle;
}

std::vector<uint32_t> DeviceContext::GetQueueFamilyIndices()
{
    return { s_LogicalDevice.MainQueueFamilyIndex };
}

uint32_t DeviceContext::GetGraphicsQueueFamilyIndex()
{
    return s_LogicalDevice.MainQueueFamilyIndex;
}

vk::Queue DeviceContext::GetPresentQueue()
{
    return s_LogicalDevice.MainQueue;
}

vk::Queue DeviceContext::GetGraphicsQueue()
{
    return s_LogicalDevice.MainQueue;
}

uint32_t DeviceContext::FindMemoryTypeIndex(
    vk::MemoryRequirements requirements, vk::MemoryPropertyFlags flags
)
{
    for (int index = 0; index < s_PhysicalDevice.MemoryProperties.memoryTypeCount; index++)
    {
        const vk::MemoryType memoryType = s_PhysicalDevice.MemoryProperties.memoryTypes[index];

        if (((0x1u << index) & requirements.memoryTypeBits) != 0x0u &&
            (memoryType.propertyFlags & flags) == flags)
            return index;
    }

    throw PathTracing::error("No suitable memory type found");
}

bool DeviceContext::CheckSuitable(
    vk::PhysicalDevice device, const std::vector<const char *> &requestedExtensions,
    const std::vector<const char *> &requestedLayers
)
{
    auto deviceName = device.getProperties().deviceName.data();

    std::vector<vk::ExtensionProperties> supportedExtensions = device.enumerateDeviceExtensionProperties();
    auto supportedExtensionNames =
        supportedExtensions | std::views::transform([](auto &props) { return props.extensionName.data(); });
    for (const auto &extension : supportedExtensionNames)
        logger::debug("{} supports extension {}", deviceName, extension);

    for (std::string_view extension : requestedExtensions)
    {
        if (std::ranges::find(supportedExtensionNames, extension) == supportedExtensionNames.end())
        {
            logger::warn("{} does not support Extension {}", deviceName, extension);
            return false;
        }
    }

    std::vector<vk::LayerProperties> supportedLayers = device.enumerateDeviceLayerProperties();
    auto supportedLayerNames =
        supportedLayers | std::views::transform([](auto &props) { return props.layerName.data(); });

    for (const auto &extension : supportedLayerNames)
        logger::debug("{} supports layer {}", deviceName, extension);

    for (std::string_view layer : requestedLayers)
    {
        if (std::ranges::find(supportedLayerNames, layer) == supportedLayerNames.end())
        {
            logger::warn("{} does not support Layer {}", deviceName, layer);
            return false;
        }
    }

    logger::info("{} is a suitable device", deviceName);
    return true;
}

}