#pragma once

#include <array>
#include <string>

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "Application.h"
#include "DeviceContext.h"

namespace PathTracing::Utils
{

template<typename T>
concept uploadable = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>;

namespace
{

inline uint32_t AlignTo(uint32_t size, uint32_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

template<typename T> requires vk::isVulkanHandleType<T>::value
inline void SetDebugName(T handle, const std::string &name)
{
#ifndef NDEBUG
    DeviceContext::GetLogical().setDebugUtilsObjectNameEXT(
        vk::DebugUtilsObjectNameInfoEXT(
            T::objectType, reinterpret_cast<uint64_t>(static_cast<T::CType>(handle)), name.c_str()
        ),
        Application::GetDispatchLoader()
    );
#endif
}

}

struct DebugLabel
{
    DebugLabel(vk::CommandBuffer commandBuffer, const std::string &name, std::array<float, 4> &&color)
        : CommandBuffer(commandBuffer)
    {
#ifndef NDEBUG
        commandBuffer.beginDebugUtilsLabelEXT(
            vk::DebugUtilsLabelEXT(name.c_str(), color), Application::GetDispatchLoader()
        );
#endif
    }

    ~DebugLabel()
    {
#ifndef NDEBUG
        CommandBuffer.endDebugUtilsLabelEXT(Application::GetDispatchLoader());
#endif
    }

    vk::CommandBuffer CommandBuffer;
};

}
