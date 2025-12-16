#pragma once

#include <vulkan/vulkan.hpp>

#include <array>
#include <string>

#include "Core/Config.h"

#include "Application.h"
#include "DeviceContext.h"

namespace PathTracing::Utils
{

template<typename T>
concept uploadable = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>;

namespace
{

inline constexpr bool LtExtent(vk::Extent2D a, vk::Extent2D b)
{
    return a.width < b.width && a.height < b.height;
}

inline constexpr bool LteExtent(vk::Extent2D a, vk::Extent2D b)
{
    return a.width <= b.width && a.height <= b.width;
}

inline constexpr uint32_t AlignTo(uint32_t size, uint32_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

template<typename T> requires vk::isVulkanHandleType<T>::value
inline void SetDebugName(T handle, const std::string &name)
{
#ifdef CONFIG_SHADER_DEBUG_INFO
    DeviceContext::GetLogical().setDebugUtilsObjectNameEXT(
        vk::DebugUtilsObjectNameInfoEXT(
            T::objectType, reinterpret_cast<uint64_t>(static_cast<T::CType>(handle)), name.c_str()
        ),
        Application::GetDispatchLoader()
    );
#endif
}

/* Should only be used for debugging purposes */
#ifdef CONFIG_ASSERTS
inline void FullBarrier(vk::CommandBuffer commandBuffer)
{
    vk::MemoryBarrier2 memoryBarrier(
        vk::PipelineStageFlagBits2::eAllCommands,
        vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
        vk::PipelineStageFlagBits2::eAllCommands,
        vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite
    );

    vk::DependencyInfo dependencyInfo(vk::DependencyFlags(), memoryBarrier);

    commandBuffer.pipelineBarrier2(dependencyInfo);
}
#else
inline void FullBarrier(vk::CommandBuffer commandBuffer) = delete;
#endif

}

struct DebugLabel
{
    DebugLabel(vk::CommandBuffer commandBuffer, const std::string &name, std::array<float, 4> &&color)
        : CommandBuffer(commandBuffer)
    {
#ifdef CONFIG_SHADER_DEBUG_INFO
        commandBuffer.beginDebugUtilsLabelEXT(
            vk::DebugUtilsLabelEXT(name.c_str(), color), Application::GetDispatchLoader()
        );
#endif
    }

    ~DebugLabel()
    {
#ifdef CONFIG_SHADER_DEBUG_INFO
        CommandBuffer.endDebugUtilsLabelEXT(Application::GetDispatchLoader());
#endif
    }

    vk::CommandBuffer CommandBuffer;
};

}
