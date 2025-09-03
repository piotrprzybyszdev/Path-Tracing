#include <vulkan/vulkan.hpp>

#include <ranges>

#include "DeviceContext.h"
#include "Utils.h"

#include "Core/Core.h"

namespace PathTracing
{

DeviceContext::PhysicalDevice DeviceContext::s_PhysicalDevice = {};
DeviceContext::LogicalDevice DeviceContext::s_LogicalDevice = {};
VmaAllocator DeviceContext::s_Allocator = nullptr;

void DeviceContext::Init(
    vk::Instance instance, const std::vector<const char *> &requestedLayers, vk::SurfaceKHR surface
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

    s_PhysicalDevice.Handle = *std::ranges::max_element(
        suitableDevices,
        [](vk::PhysicalDevice device1, vk::PhysicalDevice device2) {
            auto properties1 = device1.getMemoryProperties();
            auto properties2 = device2.getMemoryProperties();
            return properties1.memoryHeapCount < properties2.memoryHeapCount;
        }
    );

    s_PhysicalDevice.Properties = s_PhysicalDevice.Handle.getProperties();
    s_PhysicalDevice.MemoryProperties = s_PhysicalDevice.Handle.getMemoryProperties();
    s_PhysicalDevice.QueueFamilyProperties = s_PhysicalDevice.Handle.getQueueFamilyProperties();
    std::tie(
        s_PhysicalDevice.RayTracingPipelineProperties, s_PhysicalDevice.AccelerationStructureProperties
    ) = s_PhysicalDevice.Handle
            .getProperties2<
                vk::PhysicalDeviceProperties2, vk::PhysicalDeviceRayTracingPipelinePropertiesKHR,
                vk::PhysicalDeviceAccelerationStructurePropertiesKHR>()
            .get<
                vk::PhysicalDeviceRayTracingPipelinePropertiesKHR,
                vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();

    logger::info("Selected physical device: {}", s_PhysicalDevice.Properties.deviceName.data());

    FindQueueFamilies(surface);

    std::vector<std::vector<float>> priorities;
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;

    GetQueueCreateInfos(priorities, queueCreateInfos);

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

    vk::PhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures;
    descriptorIndexingFeatures.setDescriptorBindingPartiallyBound(vk::True);
    descriptorIndexingFeatures.setRuntimeDescriptorArray(vk::True);
    dynamicRenderingFeatures.setPNext(&descriptorIndexingFeatures);

    vk::DeviceCreateInfo createInfo(
        vk::DeviceCreateFlags(), queueCreateInfos, requestedLayers, deviceExtensions, nullptr, &features
    );

    s_LogicalDevice.Handle = s_PhysicalDevice.Handle.createDevice(createInfo);

    GetQueues();

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = s_PhysicalDevice.Handle;
    allocatorInfo.device = s_LogicalDevice.Handle;
    allocatorInfo.instance = instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorInfo.vulkanApiVersion = Application::GetVulkanApiVersion();
    vmaCreateAllocator(&allocatorInfo, &s_Allocator);
}

void DeviceContext::Shutdown()
{
    vmaDestroyAllocator(s_Allocator);
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
    if (s_LogicalDevice.PresentQueueFamilyIndex == s_LogicalDevice.GraphicsQueueFamilyIndex)
        return { s_LogicalDevice.GraphicsQueueFamilyIndex };
    return { s_LogicalDevice.PresentQueueFamilyIndex, s_LogicalDevice.GraphicsQueueFamilyIndex };
}

uint32_t DeviceContext::GetGraphicsQueueFamilyIndex()
{
    return s_LogicalDevice.GraphicsQueueFamilyIndex;
}

uint32_t DeviceContext::GetTransferQueueFamilyIndex()
{
    return s_LogicalDevice.TransferQueueFamilyIndex;
}

vk::Queue DeviceContext::GetPresentQueue()
{
    return s_LogicalDevice.PresentQueue;
}

vk::Queue DeviceContext::GetGraphicsQueue()
{
    return s_LogicalDevice.GraphicsQueue;
}

vk::Queue DeviceContext::GetMipQueue()
{
    return s_LogicalDevice.MipQueue;
}

vk::Queue DeviceContext::GetTransferQueue()
{
    return s_LogicalDevice.TransferQueue;
}

bool DeviceContext::HasMipQueue()
{
    return s_LogicalDevice.MipQueue != nullptr;
}

bool DeviceContext::HasTransferQueue()
{
    return s_LogicalDevice.TransferQueue != nullptr;
}

VmaAllocator DeviceContext::GetAllocator()
{
    return s_Allocator;
}

const vk::PhysicalDeviceRayTracingPipelinePropertiesKHR &DeviceContext::GetRayTracingPipelineProperties()
{
    return s_PhysicalDevice.RayTracingPipelineProperties;
}

const vk::PhysicalDeviceAccelerationStructurePropertiesKHR &DeviceContext::GetAccelerationStructureProperties(
)
{
    return s_PhysicalDevice.AccelerationStructureProperties;
}

bool DeviceContext::CheckSuitable(
    vk::PhysicalDevice device, const std::vector<const char *> &requestedExtensions,
    const std::vector<const char *> &requestedLayers
)
{
    const auto *deviceName = device.getProperties().deviceName.data();

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

void DeviceContext::FindQueueFamilies(vk::SurfaceKHR surface)
{
    for (vk::QueueFamilyProperties prop : s_PhysicalDevice.QueueFamilyProperties)
        logger::debug("Found queue family ({}): {}", prop.queueCount, vk::to_string(prop.queueFlags));

    auto checkHasFlags = [](vk::QueueFamilyProperties properties, vk::QueueFlags flags) {
        return (properties.queueFlags & flags) == flags;
    };

    // Try getting one queue family for graphics and present
    for (uint32_t index = 0; index < s_PhysicalDevice.QueueFamilyProperties.size(); index++)
    {
        const auto &properties = s_PhysicalDevice.QueueFamilyProperties[index];
        if (s_PhysicalDevice.Handle.getSurfaceSupportKHR(index, surface) == vk::True &&
            checkHasFlags(properties, vk::QueueFlagBits::eGraphics))
        {
            s_LogicalDevice.PresentQueueFamilyIndex = index;
            s_LogicalDevice.GraphicsQueueFamilyIndex = index;

            if (properties.queueCount > 1)
                s_LogicalDevice.MipQueueFamilyIndex = index;

            break;
        }
    }

    // Make sure we have a present queue family
    if (s_LogicalDevice.PresentQueueFamilyIndex == vk::QueueFamilyIgnored)
        for (uint32_t index = 0; index < s_PhysicalDevice.QueueFamilyProperties.size(); index++)
            if (s_PhysicalDevice.Handle.getSurfaceSupportKHR(index, surface) == vk::True)
            {
                s_LogicalDevice.PresentQueueFamilyIndex = index;
                break;
            }

    if (s_LogicalDevice.PresentQueueFamilyIndex == vk::QueueFamilyIgnored)
        throw error("No appropriate present queue family found");

    // Make sure we have a graphics family
    if (s_LogicalDevice.GraphicsQueueFamilyIndex == vk::QueueFamilyIgnored)
        for (uint32_t index = 0; index < s_PhysicalDevice.QueueFamilyProperties.size(); index++)
        {
            const auto &properties = s_PhysicalDevice.QueueFamilyProperties[index];
            if (checkHasFlags(properties, vk::QueueFlagBits::eGraphics))
            {
                s_LogicalDevice.GraphicsQueueFamilyIndex = index;

                if (properties.queueCount > 1)
                    s_LogicalDevice.MipQueueFamilyIndex = index;

                break;
            }
        }

    if (s_LogicalDevice.GraphicsQueueFamilyIndex == vk::QueueFamilyIgnored)
        throw error("No appropriate graphics queue family found");

    // Get a mip queue family (override if any was found eariler as this will be better)
    for (uint32_t index = 0; index < s_PhysicalDevice.QueueFamilyProperties.size(); index++)
    {
        const auto &properties = s_PhysicalDevice.QueueFamilyProperties[index];
        if (s_LogicalDevice.GraphicsQueueFamilyIndex != index &&
            checkHasFlags(properties, vk::QueueFlagBits::eGraphics))
        {
            s_LogicalDevice.MipQueueFamilyIndex = index;
            break;
        }
    }

    // Get a dedicated transfer queue
    for (uint32_t index = 0; index < s_PhysicalDevice.QueueFamilyProperties.size(); index++)
    {
        const auto &properties = s_PhysicalDevice.QueueFamilyProperties[index];
        if (!checkHasFlags(properties, vk::QueueFlagBits::eGraphics) &&
            checkHasFlags(properties, vk::QueueFlagBits::eTransfer))
        {
            s_LogicalDevice.TransferQueueFamilyIndex = index;
            break;
        }
    }

    // This one would be even better
    for (uint32_t index = 0; index < s_PhysicalDevice.QueueFamilyProperties.size(); index++)
    {
        const auto &properties = s_PhysicalDevice.QueueFamilyProperties[index];
        if (!checkHasFlags(properties, vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute) &&
            checkHasFlags(properties, vk::QueueFlagBits::eTransfer))
        {
            s_LogicalDevice.TransferQueueFamilyIndex = index;
            break;
        }
    }

    logger::debug("Set PresentQueueFamily to index: {}", s_LogicalDevice.PresentQueueFamilyIndex);
    logger::debug("Set GraphicsQueueFamily to index: {}", s_LogicalDevice.GraphicsQueueFamilyIndex);
    logger::debug("Set MipQueueFamily to index: {}", s_LogicalDevice.MipQueueFamilyIndex);
    logger::debug("Set TransferQueueFamily to index: {}", s_LogicalDevice.TransferQueueFamilyIndex);

    if (s_LogicalDevice.MipQueueFamilyIndex == vk::QueueFamilyIgnored)
        logger::warn("Couldn't find a second graphics queue. Asynchronous texture loading will be unavailable"
        );

    if (s_LogicalDevice.TransferQueueFamilyIndex == vk::QueueFamilyIgnored)
        logger::warn("Couldn't find a dedicated transfer queue family. Falling back to a graphics queue");
}

void DeviceContext::GetQueueCreateInfos(
    std::vector<std::vector<float>> &priorities, std::vector<vk::DeviceQueueCreateInfo> &createInfos
)
{
    if (s_LogicalDevice.PresentQueueFamilyIndex != s_LogicalDevice.GraphicsQueueFamilyIndex)
    {
        priorities.push_back({ 1.0f });
        createInfos.emplace_back(
            vk::DeviceQueueCreateFlags(), s_LogicalDevice.PresentQueueFamilyIndex, priorities.back()
        );
    }

    {
        std::vector<float> prio = { 1.0f };
        if (s_LogicalDevice.GraphicsQueueFamilyIndex == s_LogicalDevice.MipQueueFamilyIndex)
            prio.push_back(0.5f);
        priorities.push_back(prio);

        createInfos.emplace_back(
            vk::DeviceQueueCreateFlags(), s_LogicalDevice.GraphicsQueueFamilyIndex, priorities.back()
        );
    }

    if (s_LogicalDevice.MipQueueFamilyIndex != s_LogicalDevice.GraphicsQueueFamilyIndex &&
        s_LogicalDevice.MipQueueFamilyIndex != vk::QueueFamilyIgnored)
    {
        priorities.push_back({ 1.0f });
        createInfos.emplace_back(
            vk::DeviceQueueCreateFlags(), s_LogicalDevice.MipQueueFamilyIndex, priorities.back()
        );
    }

    if (s_LogicalDevice.TransferQueueFamilyIndex != vk::QueueFamilyIgnored)
    {
        priorities.push_back({ 1.0f });
        createInfos.emplace_back(
            vk::DeviceQueueCreateFlags(), s_LogicalDevice.TransferQueueFamilyIndex, priorities.back()
        );
    }
}

void DeviceContext::GetQueues()
{
    s_LogicalDevice.GraphicsQueue =
        s_LogicalDevice.Handle.getQueue(s_LogicalDevice.GraphicsQueueFamilyIndex, 0);

    if (s_LogicalDevice.PresentQueueFamilyIndex != s_LogicalDevice.GraphicsQueueFamilyIndex)
    {
        s_LogicalDevice.PresentQueue =
            s_LogicalDevice.Handle.getQueue(s_LogicalDevice.PresentQueueFamilyIndex, 0);
        Utils::SetDebugName(s_LogicalDevice.PresentQueue, "Present Queue");
    }
    else
    {
        s_LogicalDevice.PresentQueue = s_LogicalDevice.GraphicsQueue;
        Utils::SetDebugName(s_LogicalDevice.GraphicsQueue, "Graphics & Present Queue");
    }

    if (s_LogicalDevice.MipQueueFamilyIndex != vk::QueueFamilyIgnored)
    {
        if (s_LogicalDevice.MipQueueFamilyIndex == s_LogicalDevice.GraphicsQueueFamilyIndex)
            s_LogicalDevice.MipQueue =
                s_LogicalDevice.Handle.getQueue(s_LogicalDevice.MipQueueFamilyIndex, 1);
        else
            s_LogicalDevice.MipQueue =
                s_LogicalDevice.Handle.getQueue(s_LogicalDevice.MipQueueFamilyIndex, 0);
        Utils::SetDebugName(s_LogicalDevice.GraphicsQueue, "Mip Queue");
    }

    if (s_LogicalDevice.TransferQueueFamilyIndex != vk::QueueFamilyIgnored)
    {
        s_LogicalDevice.TransferQueue =
            s_LogicalDevice.Handle.getQueue(s_LogicalDevice.TransferQueueFamilyIndex, 0);
        Utils::SetDebugName(s_LogicalDevice.TransferQueue, "Transfer Queue");
    }
}

}