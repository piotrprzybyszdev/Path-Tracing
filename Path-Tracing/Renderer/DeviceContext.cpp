#include <vulkan/vulkan.hpp>

#include <ranges>

#include "DeviceContext.h"
#include "Utils.h"

#include "Core/Core.h"

namespace PathTracing
{

std::unique_lock<std::mutex> Queue::GetLock()
{
    if (m_ShouldLock)
        return std::unique_lock(m_Mutex);
    return std::unique_lock(m_Mutex, std::defer_lock);
}

void Queue::WaitIdle()
{
    auto lock = GetLock();
    Handle.waitIdle();
}

DeviceContext::PhysicalDevice DeviceContext::s_PhysicalDevice = {};
DeviceContext::LogicalDevice DeviceContext::s_LogicalDevice = {};
VmaAllocator DeviceContext::s_Allocator = nullptr;

void DeviceContext::Init(vk::Instance instance, vk::SurfaceKHR surface)
{
    std::vector<const char *> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME,
    };

    for (const char *extension : deviceExtensions)
        logger::info("Device Extension {} is required", extension);

    std::vector<vk::PhysicalDevice> suitableDevices;
    for (vk::PhysicalDevice device : instance.enumeratePhysicalDevices())
    {
        auto properties = device.getProperties();

        logger::info(
            "Found physical device {} ({})", properties.deviceName.data(),
            vk::to_string(properties.deviceType)
        );

        if (CheckSuitable(device, deviceExtensions))
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

    s_PhysicalDevice.Properties = s_PhysicalDevice.Handle.getProperties2();
    s_PhysicalDevice.QueueFamilyProperties = s_PhysicalDevice.Handle.getQueueFamilyProperties2();
    std::tie(
        s_PhysicalDevice.RayTracingPipelineProperties, s_PhysicalDevice.AccelerationStructureProperties
    ) = s_PhysicalDevice.Handle
            .getProperties2<
                vk::PhysicalDeviceProperties2, vk::PhysicalDeviceRayTracingPipelinePropertiesKHR,
                vk::PhysicalDeviceAccelerationStructurePropertiesKHR>()
            .get<
                vk::PhysicalDeviceRayTracingPipelinePropertiesKHR,
                vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();

    logger::info("Selected physical device: {}", s_PhysicalDevice.Properties.properties.deviceName.data());

    FindQueueFamilies(surface);

    std::vector<std::vector<float>> priorities;
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;

    GetQueueCreateInfos(priorities, queueCreateInfos);

    vk::PhysicalDeviceFeatures2 features;

    vk::PhysicalDeviceSynchronization2Features synchronizationFeatures;
    synchronizationFeatures.setSynchronization2(vk::True);
    features.setPNext(&synchronizationFeatures);

    vk::PhysicalDeviceBufferDeviceAddressFeatures bufferFeatures;
    bufferFeatures.setBufferDeviceAddress(vk::True);
    synchronizationFeatures.setPNext(&bufferFeatures);

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
        vk::DeviceCreateFlags(), queueCreateInfos, {}, deviceExtensions, nullptr, &features
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
    if (s_LogicalDevice.PresentQueue.FamilyIndex == s_LogicalDevice.GraphicsQueue.FamilyIndex)
        return { s_LogicalDevice.GraphicsQueue.FamilyIndex };
    return { s_LogicalDevice.PresentQueue.FamilyIndex, s_LogicalDevice.GraphicsQueue.FamilyIndex };
}

Queue &DeviceContext::GetPresentQueue()
{
    if (s_LogicalDevice.PresentQueue.FamilyIndex == s_LogicalDevice.GraphicsQueue.FamilyIndex)
        return s_LogicalDevice.GraphicsQueue;
    return s_LogicalDevice.PresentQueue;
}

Queue &DeviceContext::GetGraphicsQueue()
{
    return s_LogicalDevice.GraphicsQueue;
}

Queue &DeviceContext::GetMipQueue()
{
    if (s_LogicalDevice.MipQueue.FamilyIndex == vk::QueueFamilyIgnored)
        return s_LogicalDevice.GraphicsQueue;
    return s_LogicalDevice.MipQueue;
}

Queue &DeviceContext::GetTransferQueue()
{
    return s_LogicalDevice.TransferQueue;
}

bool DeviceContext::HasMipQueue()
{
    return s_LogicalDevice.MipQueue.Handle != nullptr;
}

bool DeviceContext::HasTransferQueue()
{
    return s_LogicalDevice.TransferQueue.Handle != nullptr;
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

const vk::FormatProperties2 &DeviceContext::GetFormatProperties(vk::Format format)
{
    const size_t index = static_cast<size_t>(format);

    if (s_PhysicalDevice.FormatProperties.size() <= index ||
        s_PhysicalDevice.FormatProperties[index].second == false)
    {
        if (s_PhysicalDevice.FormatProperties.size() <= index)
            s_PhysicalDevice.FormatProperties.resize(
                index + 1, std::make_pair(vk::FormatProperties2(), false)
            );

        s_PhysicalDevice.FormatProperties[index] =
            std::make_pair(s_PhysicalDevice.Handle.getFormatProperties2(format), true);
    }

    return s_PhysicalDevice.FormatProperties[index].first;
}

bool DeviceContext::CheckSuitable(
    vk::PhysicalDevice device, const std::vector<const char *> &requestedExtensions
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

    logger::info("{} is a suitable device", deviceName);
    return true;
}

void DeviceContext::FindQueueFamilies(vk::SurfaceKHR surface)
{
    for (vk::QueueFamilyProperties2 prop : s_PhysicalDevice.QueueFamilyProperties)
        logger::debug(
            "Found queue family ({}): {}", prop.queueFamilyProperties.queueCount,
            vk::to_string(prop.queueFamilyProperties.queueFlags)
        );

    auto checkHasFlags = [](vk::QueueFamilyProperties properties, vk::QueueFlags flags) {
        return (properties.queueFlags & flags) == flags;
    };

    // Try getting one queue family for graphics and present
    for (uint32_t index = 0; index < s_PhysicalDevice.QueueFamilyProperties.size(); index++)
    {
        const auto &properties = s_PhysicalDevice.QueueFamilyProperties[index].queueFamilyProperties;
        if (s_PhysicalDevice.Handle.getSurfaceSupportKHR(index, surface) == vk::True &&
            checkHasFlags(properties, vk::QueueFlagBits::eGraphics))
        {
            s_LogicalDevice.PresentQueue.FamilyIndex = index;
            s_LogicalDevice.GraphicsQueue.FamilyIndex = index;

            if (properties.queueCount > 1)
                s_LogicalDevice.MipQueue.FamilyIndex = index;

            break;
        }
    }

    // Make sure we have a present queue family
    if (s_LogicalDevice.PresentQueue.FamilyIndex == vk::QueueFamilyIgnored)
        for (uint32_t index = 0; index < s_PhysicalDevice.QueueFamilyProperties.size(); index++)
            if (s_PhysicalDevice.Handle.getSurfaceSupportKHR(index, surface) == vk::True)
            {
                s_LogicalDevice.PresentQueue.FamilyIndex = index;
                break;
            }

    if (s_LogicalDevice.PresentQueue.FamilyIndex == vk::QueueFamilyIgnored)
        throw error("No appropriate present queue family found");

    // Make sure we have a graphics family
    if (s_LogicalDevice.GraphicsQueue.FamilyIndex == vk::QueueFamilyIgnored)
        for (uint32_t index = 0; index < s_PhysicalDevice.QueueFamilyProperties.size(); index++)
        {
            const auto &properties = s_PhysicalDevice.QueueFamilyProperties[index].queueFamilyProperties;
            if (checkHasFlags(properties, vk::QueueFlagBits::eGraphics))
            {
                s_LogicalDevice.GraphicsQueue.FamilyIndex = index;

                if (properties.queueCount > 1)
                    s_LogicalDevice.MipQueue.FamilyIndex = index;

                break;
            }
        }

    if (s_LogicalDevice.GraphicsQueue.FamilyIndex == vk::QueueFamilyIgnored)
        throw error("No appropriate graphics queue family found");

    // Get a mip queue family (override if any was found eariler as this will be better)
    for (uint32_t index = 0; index < s_PhysicalDevice.QueueFamilyProperties.size(); index++)
    {
        const auto &properties = s_PhysicalDevice.QueueFamilyProperties[index].queueFamilyProperties;
        if (s_LogicalDevice.GraphicsQueue.FamilyIndex != index &&
            checkHasFlags(properties, vk::QueueFlagBits::eGraphics))
        {
            s_LogicalDevice.MipQueue.FamilyIndex = index;
            break;
        }
    }

    // Get a dedicated transfer queue
    for (uint32_t index = 0; index < s_PhysicalDevice.QueueFamilyProperties.size(); index++)
    {
        const auto &properties = s_PhysicalDevice.QueueFamilyProperties[index].queueFamilyProperties;
        if (!checkHasFlags(properties, vk::QueueFlagBits::eGraphics) &&
            checkHasFlags(properties, vk::QueueFlagBits::eTransfer))
        {
            s_LogicalDevice.TransferQueue.FamilyIndex = index;
            break;
        }
    }

    // This one would be even better
    for (uint32_t index = 0; index < s_PhysicalDevice.QueueFamilyProperties.size(); index++)
    {
        const auto &properties = s_PhysicalDevice.QueueFamilyProperties[index].queueFamilyProperties;
        if (!checkHasFlags(properties, vk::QueueFlagBits::eGraphics) &&
            !checkHasFlags(properties, vk::QueueFlagBits::eCompute) &&
            checkHasFlags(properties, vk::QueueFlagBits::eTransfer))
        {
            s_LogicalDevice.TransferQueue.FamilyIndex = index;
            break;
        }
    }

    logger::debug("Set PresentQueueFamily to index: {}", s_LogicalDevice.PresentQueue.FamilyIndex);
    logger::debug("Set GraphicsQueueFamily to index: {}", s_LogicalDevice.GraphicsQueue.FamilyIndex);
    logger::debug("Set MipQueueFamily to index: {}", s_LogicalDevice.MipQueue.FamilyIndex);
    logger::debug("Set TransferQueueFamily to index: {}", s_LogicalDevice.TransferQueue.FamilyIndex);

    if (s_LogicalDevice.MipQueue.FamilyIndex == vk::QueueFamilyIgnored)
        logger::warn("Couldn't find a second graphics queue");

    if (s_LogicalDevice.TransferQueue.FamilyIndex == vk::QueueFamilyIgnored)
        logger::warn("Couldn't find a dedicated transfer queue family.");
}

void DeviceContext::GetQueueCreateInfos(
    std::vector<std::vector<float>> &priorities, std::vector<vk::DeviceQueueCreateInfo> &createInfos
)
{
    if (s_LogicalDevice.PresentQueue.FamilyIndex != s_LogicalDevice.GraphicsQueue.FamilyIndex)
    {
        priorities.push_back({ 1.0f });
        createInfos.emplace_back(
            vk::DeviceQueueCreateFlags(), s_LogicalDevice.PresentQueue.FamilyIndex, priorities.back()
        );
    }

    {
        std::vector<float> prio = { 1.0f };
        if (s_LogicalDevice.GraphicsQueue.FamilyIndex == s_LogicalDevice.MipQueue.FamilyIndex)
            prio.push_back(0.5f);
        priorities.push_back(prio);

        createInfos.emplace_back(
            vk::DeviceQueueCreateFlags(), s_LogicalDevice.GraphicsQueue.FamilyIndex, priorities.back()
        );
    }

    if (s_LogicalDevice.MipQueue.FamilyIndex != s_LogicalDevice.GraphicsQueue.FamilyIndex &&
        s_LogicalDevice.MipQueue.FamilyIndex != vk::QueueFamilyIgnored)
    {
        priorities.push_back({ 1.0f });
        createInfos.emplace_back(
            vk::DeviceQueueCreateFlags(), s_LogicalDevice.MipQueue.FamilyIndex, priorities.back()
        );
    }

    if (s_LogicalDevice.TransferQueue.FamilyIndex != vk::QueueFamilyIgnored)
    {
        priorities.push_back({ 1.0f });
        createInfos.emplace_back(
            vk::DeviceQueueCreateFlags(), s_LogicalDevice.TransferQueue.FamilyIndex, priorities.back()
        );
    }
}

void DeviceContext::GetQueues()
{
    s_LogicalDevice.GraphicsQueue.Handle =
        s_LogicalDevice.Handle.getQueue(s_LogicalDevice.GraphicsQueue.FamilyIndex, 0);
    Utils::SetDebugName(s_LogicalDevice.GraphicsQueue.Handle, "Graphics Queue");

    if (s_LogicalDevice.PresentQueue.FamilyIndex != s_LogicalDevice.GraphicsQueue.FamilyIndex)
    {
        s_LogicalDevice.PresentQueue.Handle =
            s_LogicalDevice.Handle.getQueue(s_LogicalDevice.PresentQueue.FamilyIndex, 0);
        Utils::SetDebugName(s_LogicalDevice.PresentQueue.Handle, "Present Queue");
    }
    else
        Utils::SetDebugName(s_LogicalDevice.GraphicsQueue.Handle, "Graphics & Present Queue");

    if (s_LogicalDevice.MipQueue.FamilyIndex != vk::QueueFamilyIgnored)
    {
        if (s_LogicalDevice.MipQueue.FamilyIndex == s_LogicalDevice.GraphicsQueue.FamilyIndex)
            s_LogicalDevice.MipQueue.Handle =
                s_LogicalDevice.Handle.getQueue(s_LogicalDevice.MipQueue.FamilyIndex, 1);
        else
            s_LogicalDevice.MipQueue.Handle =
                s_LogicalDevice.Handle.getQueue(s_LogicalDevice.MipQueue.FamilyIndex, 0);
        Utils::SetDebugName(s_LogicalDevice.MipQueue.Handle, "Mip Queue");
    }
    else
    {
        s_LogicalDevice.GraphicsQueue.m_ShouldLock = true;
        if (s_LogicalDevice.PresentQueue.FamilyIndex == s_LogicalDevice.GraphicsQueue.FamilyIndex)
            Utils::SetDebugName(s_LogicalDevice.GraphicsQueue.Handle, "Graphics & Present & Mip Queue");
    }

    if (s_LogicalDevice.TransferQueue.FamilyIndex != vk::QueueFamilyIgnored)
    {
        s_LogicalDevice.TransferQueue.Handle =
            s_LogicalDevice.Handle.getQueue(s_LogicalDevice.TransferQueue.FamilyIndex, 0);
        Utils::SetDebugName(s_LogicalDevice.TransferQueue.Handle, "Transfer Queue");
    }
}

}