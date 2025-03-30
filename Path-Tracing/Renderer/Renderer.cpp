#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>

#include <algorithm>
#include <fstream>
#include <memory>
#include <set>
#include <string_view>

#include "Core/Core.h"
#include "Renderer/Renderer.h"

namespace PathTracing
{

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

static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity, vk::DebugUtilsMessageTypeFlagsEXT messageType,
    const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData
)
{
    switch (messageSeverity)
    {
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
        logger::error(pCallbackData->pMessage);
        break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
        logger::warn(pCallbackData->pMessage);
        break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo:
        logger::info(pCallbackData->pMessage);
        break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose:
        logger::debug(pCallbackData->pMessage);
        break;
    }

    return VK_FALSE;
}

Renderer::Renderer(Window &window, Camera &camera) : m_Window(window), m_Camera(camera)
{
    uint32_t version = vk::enumerateInstanceVersion();

    uint32_t major = vk::versionMajor(version);
    uint32_t minor = vk::versionMinor(version);
    uint32_t patch = vk::versionPatch(version);

    logger::debug("Highest supported vulkan version: {}.{}.{}", major, minor, patch);

    version = vk::makeVersion(major, minor, 0u);
    logger::info("Selected vulkan version: {}.{}.{}", major, minor, 0u);

    vk::ApplicationInfo applicationInfo("Path Tracing", 1, "Path Tracing", 1, version);

    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char *> requestedExtensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    for (std::string_view extension : requestedExtensions)
        logger::info("Extension {} is required", extension);

    std::vector<const char *> requestedLayers;
#ifndef NDEBUG
    requestedExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    requestedLayers.push_back("VK_LAYER_KHRONOS_validation");
#endif

    for (std::string_view layer : requestedLayers)
        logger::info("Layer {} is required", layer);

    {
        std::vector<vk::ExtensionProperties> supportedExtensions = vk::enumerateInstanceExtensionProperties();
        std::vector<vk::LayerProperties> supportedLayers = vk::enumerateInstanceLayerProperties();
        if (!checkSupported(requestedExtensions, requestedLayers, supportedExtensions, supportedLayers))
            throw error("Instance doesn't have required extensions or layers");

        vk::InstanceCreateInfo createInfo(
            vk::InstanceCreateFlags(), &applicationInfo, requestedLayers, requestedExtensions
        );

        try
        {
            m_Instance = vk::createInstance(createInfo);
        }
        catch (vk::SystemError &error)
        {
            throw PathTracing::error(error.what());
        }
    }

    m_DispatchLoader = vk::detail::DispatchLoaderDynamic(m_Instance, vkGetInstanceProcAddr);

#ifndef NDEBUG
    {
        vk::DebugUtilsMessengerCreateInfoEXT createInfo(
            vk::DebugUtilsMessengerCreateFlagsEXT(),
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding,
            debugCallback, nullptr
        );

        m_DebugMessenger = m_Instance.createDebugUtilsMessengerEXT(createInfo, nullptr, m_DispatchLoader);
    }
#endif

    std::vector<const char *> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    };

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

    m_Surface = window.CreateSurface(m_Instance);

    m_Device = LogicalDevice(m_Instance, m_Surface, requestedLayers, deviceExtensions, &features);
    m_LogicalDevice = m_Device.GetHandle();

    m_GraphicsQueue = m_Device.GetGraphicsQueue();
    m_CommandPool = m_Device.GetGraphicsCommandPool();

    {
        vk::AttachmentDescription colorAttachment(
            vk::AttachmentDescriptionFlags(), vk::Format::eB8G8R8A8Unorm, vk::SampleCountFlagBits::e1,
            vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare,
            vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR
        );

        vk::AttachmentReference colorReference(0, vk::ImageLayout::eColorAttachmentOptimal);

        vk::SubpassDescription subpass(
            vk::SubpassDescriptionFlags(), vk::PipelineBindPoint::eGraphics, {}, { colorReference }, {}
        );

        vk::SubpassDependency dependency1(
            vk::SubpassExternal, 0, vk::PipelineStageFlagBits::eBottomOfPipe,
            vk::PipelineStageFlagBits::eColorAttachmentOutput |
                vk::PipelineStageFlagBits::eEarlyFragmentTests |
                vk::PipelineStageFlagBits::eLateFragmentTests,
            vk::AccessFlagBits::eNone,
            vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite |
                vk::AccessFlagBits::eDepthStencilAttachmentRead |
                vk::AccessFlagBits::eDepthStencilAttachmentWrite,
            vk::DependencyFlagBits::eByRegion
        );

        vk::SubpassDependency dependency2(
            0, vk::SubpassExternal,
            vk::PipelineStageFlagBits::eColorAttachmentOutput |
                vk::PipelineStageFlagBits::eEarlyFragmentTests |
                vk::PipelineStageFlagBits::eLateFragmentTests,
            vk::PipelineStageFlagBits::eBottomOfPipe,
            vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite |
                vk::AccessFlagBits::eDepthStencilAttachmentRead |
                vk::AccessFlagBits::eDepthStencilAttachmentWrite,
            vk::AccessFlagBits::eMemoryRead, vk::DependencyFlagBits::eByRegion
        );

        std::vector<vk::AttachmentDescription> attachments = { colorAttachment };
        std::vector<vk::SubpassDescription> subpasses = { subpass };
        std::vector<vk::SubpassDependency> dependencies = { dependency1, dependency2 };
        vk::RenderPassCreateInfo createInfo(
            vk::RenderPassCreateFlags(), attachments, subpasses, dependencies
        );

        m_RenderPass = m_LogicalDevice.createRenderPass(createInfo);
    }

    m_BufferBuilder = m_Device.CreateBufferBuilderUnique();
    m_ImageBuilder = m_Device.CreateImageBuilderUnique();

    vk::CommandBufferAllocateInfo allocateInfo(m_CommandPool, vk::CommandBufferLevel::ePrimary, 1);
    m_MainCommandBuffer.CommandBuffer = m_LogicalDevice.allocateCommandBuffers(allocateInfo)[0];
    m_MainCommandBuffer.Fence = m_LogicalDevice.createFence(vk::FenceCreateInfo());

    RecreateSwapchain();
    CreateScene();
    SetupPipeline();

    camera.OnResize(m_Width, m_Height);
}

Renderer::~Renderer()
{
    m_LogicalDevice.waitIdle();

    m_Frames.clear();
    m_LogicalDevice.destroySwapchainKHR(m_Swapchain);

    m_LogicalDevice.destroyRenderPass(m_RenderPass);
    m_LogicalDevice.destroyDescriptorPool(m_DescriptorPool);
    m_LogicalDevice.destroyPipeline(m_Pipeline);

    m_ShaderLibrary.reset();

    m_LogicalDevice.destroyPipelineLayout(m_PipelineLayout);
    m_LogicalDevice.destroyDescriptorSetLayout(m_DescriptorSetLayout);

    m_StorageImage.reset();

    m_LogicalDevice.destroyAccelerationStructureKHR(
        m_TopLevelAccelerationStructure, nullptr, m_DispatchLoader
    );

    m_TopLevelAccelerationStructureBuffer.reset();

    m_LogicalDevice.destroyAccelerationStructureKHR(
        m_BottomLevelAccelerationStructure, nullptr, m_DispatchLoader
    );
    m_BottomLevelAccelerationStructureBuffer.reset();

    m_UniformBuffer.reset();
    m_TransformMatrixBuffer.reset();
    m_IndexBuffer.reset();
    m_VertexBuffer.reset();

    m_LogicalDevice.freeCommandBuffers(m_CommandPool, { m_MainCommandBuffer.CommandBuffer });
    m_LogicalDevice.destroyFence(m_MainCommandBuffer.Fence);

    m_LogicalDevice.destroyCommandPool(m_CommandPool);

    m_LogicalDevice.destroy();

    m_Instance.destroySurfaceKHR(m_Surface);
#ifndef NDEBUG
    m_Instance.destroyDebugUtilsMessengerEXT(m_DebugMessenger, nullptr, m_DispatchLoader);
#endif

    m_Instance.destroy();
}

void Renderer::RecreateSwapchain()
{
    vk::SwapchainKHR oldSwapchain = m_Swapchain;

    m_Swapchain = m_Device.CreateSwapchain(oldSwapchain, m_Surface);

    for (vk::Image image : m_LogicalDevice.getSwapchainImagesKHR(m_Swapchain))
        m_Frames.emplace_back(Frame(
            m_LogicalDevice, m_RenderPass, m_CommandPool, image, vk::Format::eB8G8R8A8Unorm, m_Width, m_Height
        ));

    m_LogicalDevice.destroySwapchainKHR(oldSwapchain);
}

void Renderer::CreateScene()
{
    struct Vertex
    {
        float pos[3];
    };
    std::vector<Vertex> vertices = { { { 1.0f, 1.0f, 0.0f } },
                                     { { -1.0f, 1.0f, 0.0f } },
                                     { { 1.0f, -1.0f, 0.0f } },
                                     { { -1.0f, -1.0f, 0.0f } } };
    std::vector<uint32_t> indices = { 0, 1, 2, 3, 1, 2 };

    vk::TransformMatrixKHR matrix(std::array<std::array<float, 4>, 3>({
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
    }));

    {
        m_BufferBuilder->ResetFlags()
            .SetUsageFlags(
                vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                vk::BufferUsageFlagBits::eShaderDeviceAddress
            )
            .SetMemoryFlags(
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
            )
            .SetAllocateFlags(vk::MemoryAllocateFlagBits::eDeviceAddress);

        m_VertexBuffer = m_BufferBuilder->CreateBufferUnique(vertices.size() * sizeof(Vertex));
        m_VertexBuffer->Upload(vertices.data());

        m_IndexBuffer = m_BufferBuilder->CreateBufferUnique(indices.size() * sizeof(uint32_t));
        m_IndexBuffer->Upload(indices.data());

        m_TransformMatrixBuffer = m_BufferBuilder->CreateBufferUnique(sizeof(matrix));
        m_TransformMatrixBuffer->Upload(matrix.matrix.data());

        m_BufferBuilder->SetUsageFlags(
            vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress
        );
        m_UniformBuffer = m_BufferBuilder->CreateBufferUnique(2 * sizeof(glm::mat4));
    }

    {
        vk::AccelerationStructureGeometryTrianglesDataKHR geometryData(
            vk::Format::eR32G32B32Sfloat, m_VertexBuffer->GetDeviceAddress(), sizeof(Vertex),
            vertices.size() - 1, vk::IndexType::eUint32, m_IndexBuffer->GetDeviceAddress(),
            m_TransformMatrixBuffer->GetDeviceAddress()
        );

        vk::AccelerationStructureGeometryKHR geometry(
            vk::GeometryTypeKHR::eTriangles, geometryData, vk::GeometryFlagBitsKHR::eOpaque
        );

        vk::AccelerationStructureBuildGeometryInfoKHR bottomBuildInfo =
            vk::AccelerationStructureBuildGeometryInfoKHR(
                vk::AccelerationStructureTypeKHR::eBottomLevel,
                vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace
            )
                .setGeometries({ geometry });

        const uint32_t primitiveCount[] = { 2 };
        vk::AccelerationStructureBuildSizesInfoKHR buildSizesInfo =
            m_LogicalDevice.getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice, bottomBuildInfo, primitiveCount,
                m_DispatchLoader
            );

        m_BottomLevelAccelerationStructureBuffer =
            m_BufferBuilder->ResetFlags()
                .SetUsageFlags(
                    vk::BufferUsageFlagBits::eStorageBuffer |
                    vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                    vk::BufferUsageFlagBits::eShaderDeviceAddress
                )
                .SetMemoryFlags(vk::MemoryPropertyFlagBits::eDeviceLocal)
                .SetAllocateFlags(vk::MemoryAllocateFlagBits::eDeviceAddress)
                .CreateBufferUnique(buildSizesInfo.accelerationStructureSize);

        Buffer scratchBuffer = m_BufferBuilder->CreateBuffer(buildSizesInfo.buildScratchSize);

        vk::AccelerationStructureCreateInfoKHR createInfo(
            vk::AccelerationStructureCreateFlagsKHR(), m_BottomLevelAccelerationStructureBuffer->GetHandle(),
            0, buildSizesInfo.accelerationStructureSize, vk::AccelerationStructureTypeKHR::eBottomLevel
        );

        m_BottomLevelAccelerationStructure =
            m_LogicalDevice.createAccelerationStructureKHR(createInfo, nullptr, m_DispatchLoader);

        bottomBuildInfo.setDstAccelerationStructure(m_BottomLevelAccelerationStructure)
            .setScratchData(scratchBuffer.GetDeviceAddress());

        vk::AccelerationStructureBuildRangeInfoKHR rangeInfo(2, 0, 0, 0);

        m_MainCommandBuffer.Begin();
        m_MainCommandBuffer.CommandBuffer.buildAccelerationStructuresKHR(
            { bottomBuildInfo }, { &rangeInfo }, m_DispatchLoader
        );
        m_MainCommandBuffer.Submit(m_LogicalDevice, m_GraphicsQueue);

        vk::AccelerationStructureDeviceAddressInfoKHR addressInfo(m_BottomLevelAccelerationStructure);
        m_BottomLevelAccelerationStructureAddress =
            m_LogicalDevice.getAccelerationStructureAddressKHR(addressInfo, m_DispatchLoader);
    }

    {
        vk::AccelerationStructureInstanceKHR instance(
            matrix, 0, 0xff, 0, vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable,
            m_BottomLevelAccelerationStructureAddress
        );

        Buffer instanceBuffer =
            m_BufferBuilder->ResetFlags()
                .SetUsageFlags(
                    vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                    vk::BufferUsageFlagBits::eShaderDeviceAddress
                )
                .SetMemoryFlags(
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
                )
                .SetAllocateFlags(vk::MemoryAllocateFlagBits::eDeviceAddress)
                .CreateBuffer(sizeof(vk::AccelerationStructureInstanceKHR));

        instanceBuffer.Upload(&instance);

        vk::AccelerationStructureGeometryInstancesDataKHR instancesData(
            vk::False, instanceBuffer.GetDeviceAddress()
        );

        vk::AccelerationStructureGeometryKHR geometry(
            vk::GeometryTypeKHR::eInstances, instancesData, vk::GeometryFlagBitsKHR::eOpaque
        );

        vk::AccelerationStructureBuildGeometryInfoKHR buildInfo =
            vk::AccelerationStructureBuildGeometryInfoKHR(
                vk::AccelerationStructureTypeKHR::eTopLevel,
                vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace
            )
                .setGeometries({ geometry });

        const uint32_t primitiveCount[] = { 1 };
        vk::AccelerationStructureBuildSizesInfoKHR buildSizesInfo =
            m_LogicalDevice.getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, primitiveCount, m_DispatchLoader
            );

        m_TopLevelAccelerationStructureBuffer =
            m_BufferBuilder->ResetFlags()
                .SetUsageFlags(
                    vk::BufferUsageFlagBits::eStorageBuffer |
                    vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                    vk::BufferUsageFlagBits::eShaderDeviceAddress
                )
                .SetMemoryFlags(vk::MemoryPropertyFlagBits::eDeviceLocal)
                .SetAllocateFlags(vk::MemoryAllocateFlagBits::eDeviceAddress)
                .CreateBufferUnique(buildSizesInfo.accelerationStructureSize);

        vk::AccelerationStructureCreateInfoKHR createInfo(
            vk::AccelerationStructureCreateFlagsKHR(), m_TopLevelAccelerationStructureBuffer->GetHandle(), 0,
            buildSizesInfo.accelerationStructureSize
        );

        m_TopLevelAccelerationStructure =
            m_LogicalDevice.createAccelerationStructureKHR(createInfo, nullptr, m_DispatchLoader);

        Buffer scratchBuffer = m_BufferBuilder->CreateBuffer(buildSizesInfo.buildScratchSize);

        vk::AccelerationStructureBuildGeometryInfoKHR geometryInfo =
            vk::AccelerationStructureBuildGeometryInfoKHR(
                vk::AccelerationStructureTypeKHR::eTopLevel,
                vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace
            )
                .setGeometries({ geometry })
                .setDstAccelerationStructure(m_TopLevelAccelerationStructure)
                .setScratchData(scratchBuffer.GetDeviceAddress());

        vk::AccelerationStructureBuildRangeInfoKHR rangeInfo(1, 0, 0, 0);

        m_MainCommandBuffer.Begin();
        m_MainCommandBuffer.CommandBuffer.buildAccelerationStructuresKHR(
            { geometryInfo }, { &rangeInfo }, m_DispatchLoader
        );
        m_MainCommandBuffer.Submit(m_LogicalDevice, m_GraphicsQueue);

        vk::AccelerationStructureDeviceAddressInfoKHR addressInfo(m_TopLevelAccelerationStructure);
        m_TopLevelAccelerationStructureAddress =
            m_LogicalDevice.getAccelerationStructureAddressKHR(addressInfo, m_DispatchLoader);
    }
}

void Renderer::CreateStorageImage()
{
    m_StorageImage =
        m_ImageBuilder->ResetFlags()
            .SetFormat(vk::Format::eB8G8R8A8Unorm)
            .SetUsageFlags(vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eStorage)
            .SetMemoryFlags(vk::MemoryPropertyFlagBits::eDeviceLocal)
            .CreateImageUnique(m_Width, m_Height);

    vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    vk::ImageMemoryBarrier barrier;
    barrier.setNewLayout(vk::ImageLayout::eGeneral)
        .setImage(m_StorageImage->GetHandle())
        .setSubresourceRange(range);

    vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    m_MainCommandBuffer.Begin();
    m_MainCommandBuffer.CommandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands,
        vk::DependencyFlags(), {}, {}, { barrier }
    );
    m_MainCommandBuffer.Submit(m_LogicalDevice, m_GraphicsQueue);
}

void Renderer::CreateDescriptorSets()
{
    std::vector<vk::DescriptorPoolSize> poolSizes = {
        vk::DescriptorPoolSize(vk::DescriptorType::eAccelerationStructureKHR, 1),
        vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, 1)
    };

    vk::DescriptorPoolCreateInfo createInfo(vk::DescriptorPoolCreateFlags(), 1, poolSizes);
    m_DescriptorPool = m_LogicalDevice.createDescriptorPool(createInfo);

    vk::DescriptorSetAllocateInfo allocateInfo(m_DescriptorPool, { m_DescriptorSetLayout });
    m_DescriptorSet = m_LogicalDevice.allocateDescriptorSets(allocateInfo)[0];

    vk::WriteDescriptorSetAccelerationStructureKHR structureInfo({ m_TopLevelAccelerationStructure });

    vk::WriteDescriptorSet structureWrite =
        vk::WriteDescriptorSet(m_DescriptorSet, 0, 0, 1, vk::DescriptorType::eAccelerationStructureKHR)
            .setPNext(&structureInfo);

    vk::DescriptorImageInfo imageInfo =
        vk::DescriptorImageInfo(vk::Sampler(), m_StorageImage->GetView(), vk::ImageLayout::eGeneral);
    vk::WriteDescriptorSet imageWrite =
        vk::WriteDescriptorSet(m_DescriptorSet, 1, 0, 1, vk::DescriptorType::eStorageImage)
            .setPImageInfo(&imageInfo);

    vk::DescriptorBufferInfo bufferInfo =
        vk::DescriptorBufferInfo(m_UniformBuffer->GetHandle(), 0, m_UniformBuffer->GetSize());
    vk::WriteDescriptorSet bufferWrite =
        vk::WriteDescriptorSet(m_DescriptorSet, 2, 0, 1, vk::DescriptorType::eUniformBuffer)
            .setPBufferInfo(&bufferInfo);

    std::vector<vk::WriteDescriptorSet> sets = { structureWrite, imageWrite, bufferWrite };

    m_LogicalDevice.updateDescriptorSets(sets, {});
}

void Renderer::UpdateDescriptorSets()
{
    vk::DescriptorImageInfo imageInfo =
        vk::DescriptorImageInfo(vk::Sampler(), m_StorageImage->GetView(), vk::ImageLayout::eGeneral);

    vk::WriteDescriptorSet imageWrite =
        vk::WriteDescriptorSet(m_DescriptorSet, 1, 0, 1, vk::DescriptorType::eStorageImage)
            .setPImageInfo(&imageInfo);

    m_LogicalDevice.updateDescriptorSets({ imageWrite }, {});
}

void Renderer::SetupPipeline()
{
    {
        vk::DescriptorSetLayoutBinding structure(
            0, vk::DescriptorType::eAccelerationStructureKHR, 1, vk::ShaderStageFlagBits::eRaygenKHR
        );

        vk::DescriptorSetLayoutBinding result(
            1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenKHR
        );

        vk::DescriptorSetLayoutBinding uniform(
            2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eRaygenKHR
        );

        std::vector<vk::DescriptorSetLayoutBinding> bindings = { structure, result, uniform };

        vk::DescriptorSetLayoutCreateInfo createInfo(vk::DescriptorSetLayoutCreateFlags(), bindings);
        m_DescriptorSetLayout = m_LogicalDevice.createDescriptorSetLayout(createInfo);
    }

    {
        vk::PipelineLayoutCreateInfo createInfo(vk::PipelineLayoutCreateFlags(), { m_DescriptorSetLayout });
        m_PipelineLayout = m_LogicalDevice.createPipelineLayout(createInfo);
    }

    {
        m_ShaderLibrary = m_Device.CreateShaderLibrary();
        m_ShaderLibrary->AddRaygenShader("Shaders/raygen.spv", "main");
        m_ShaderLibrary->AddMissShader("Shaders/miss.spv", "main");
        m_ShaderLibrary->AddClosestHitShader("Shaders/closestHit.spv", "main");
        m_Pipeline = m_ShaderLibrary->CreatePipeline(m_PipelineLayout, m_DispatchLoader);
    }

    CreateStorageImage();
    CreateDescriptorSets();
    RecordCommandBuffers();
}

void Renderer::RecordCommandBuffers()
{
    vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

    for (const Frame &frame : m_Frames)
    {
        vk::CommandBuffer commandBuffer = frame.GetCommandBuffer();
        vk::Image image = frame.GetImage();

        commandBuffer.begin(vk::CommandBufferBeginInfo());

        vk::StridedDeviceAddressRegionKHR callableShaderEntry;

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, m_Pipeline);
        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eRayTracingKHR, m_PipelineLayout, 0, { m_DescriptorSet }, {}
        );

        commandBuffer.traceRaysKHR(
            m_ShaderLibrary->GetRaygenTableEntry(), m_ShaderLibrary->GetMissTableEntry(),
            m_ShaderLibrary->GetClosestHitTableEntry(), callableShaderEntry, m_Width, m_Height, 1,
            m_DispatchLoader
        );

        vk::ImageMemoryBarrier barrier(
            vk::AccessFlagBits::eNone, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, image, range
        );

        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
            vk::DependencyFlags(), {}, {}, { barrier }
        );

        vk::ImageMemoryBarrier barrier2(
            vk::AccessFlagBits::eNone, vk::AccessFlagBits::eTransferRead, vk::ImageLayout::eGeneral,
            vk::ImageLayout::eTransferSrcOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
            m_StorageImage->GetHandle(), range
        );

        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTransfer,
            vk::DependencyFlags(), {}, {}, { barrier2 }
        );

        vk::ImageSubresourceLayers subresource(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
        vk::Offset3D offset(0, 0, 0);
        vk::ImageCopy copy(subresource, offset, subresource, offset, { m_Width, m_Height, 1 });

        commandBuffer.copyImage(
            m_StorageImage->GetHandle(), vk::ImageLayout::eTransferSrcOptimal, image,
            vk::ImageLayout::eTransferDstOptimal, { copy }
        );

        vk::ImageMemoryBarrier barrier3(
            vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eNone,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::ePresentSrcKHR, vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored, image, range
        );

        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
            vk::DependencyFlags(), {}, {}, { barrier3 }
        );

        vk::ImageMemoryBarrier barrier4(
            vk::AccessFlagBits::eTransferRead, vk::AccessFlagBits::eNone,
            vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eGeneral, vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored, m_StorageImage->GetHandle(), range
        );

        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands,
            vk::DependencyFlags(), {}, {}, { barrier4 }
        );

        commandBuffer.end();
    }
}

void Renderer::OnUpdate(float timeStep)
{
    auto [width, height] = m_Window.GetSize();

    if (width != m_Width || height != m_Height)
        OnResize(width, height);

    glm::mat4 uniformData[2] = { m_Camera.GetInvViewMatrix(), m_Camera.GetInvProjectionMatrix() };
    m_UniformBuffer->Upload(uniformData);
}

void Renderer::OnRender()
{
    if (m_Width == 0 || m_Height == 0)
        return;

    const Frame &frame = m_Frames[m_CurrentFrame];
    SynchronizationObjects sync = frame.GetSynchronizationObjects();

    {
        vk::Result result = m_LogicalDevice.waitForFences(
            { sync.InFlightFence }, vk::True, std::numeric_limits<uint64_t>::max()
        );
        assert(result == vk::Result::eSuccess);
    }

    uint32_t imageIndex;
    try
    {
        vk::ResultValue result = m_LogicalDevice.acquireNextImageKHR(
            m_Swapchain, std::numeric_limits<uint64_t>::max(), sync.ImageAcquiredSemaphore, nullptr
        );

        assert(result.result == vk::Result::eSuccess);
        imageIndex = result.value;

        assert(imageIndex == m_CurrentFrame);
    }
    catch (vk::OutOfDateKHRError error)
    {
        logger::warn(error.what());
        return;
    }

    m_CurrentFrame++;
    if (m_CurrentFrame == 3)
        m_CurrentFrame = 0;

    m_LogicalDevice.resetFences({ sync.InFlightFence });

    std::vector<vk::PipelineStageFlags> stages = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
    std::vector<vk::CommandBuffer> commandBuffers = { frame.GetCommandBuffer() };
    vk::SubmitInfo submitInfo(
        { sync.ImageAcquiredSemaphore }, stages, commandBuffers, { sync.RenderCompleteSemaphore }
    );
    m_GraphicsQueue.submit({ submitInfo }, sync.InFlightFence);

    vk::PresentInfoKHR presentInfo({ sync.RenderCompleteSemaphore }, { m_Swapchain }, { imageIndex });
    try
    {
        vk::Result res = m_GraphicsQueue.presentKHR(presentInfo);
        assert(res == vk::Result::eSuccess);
    }
    catch (vk::OutOfDateKHRError error)
    {
        logger::warn(error.what());
        return;
    }
}

void Renderer::OnResize(uint32_t width, uint32_t height)
{
    m_LogicalDevice.waitIdle();
    m_Frames.clear();

    m_Width = width;
    m_Height = height;

    if (m_Width == 0 || m_Height == 0)
        return;

    CreateStorageImage();

    m_CurrentFrame = 0;

    RecreateSwapchain();
    UpdateDescriptorSets();
    RecordCommandBuffers();
    m_Camera.OnResize(m_Width, m_Height);
}

}