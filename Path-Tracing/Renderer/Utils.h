#pragma once

#include <array>
#include <string_view>

#include <vulkan/vulkan.hpp>

#include "Application.h"
#include "DeviceContext.h"

namespace PathTracing::Utils
{

template<typename T>
inline void SetDebugName(T handle, vk::ObjectType type, std::string_view name)
{
#ifndef NDEBUG
    DeviceContext::GetLogical().setDebugUtilsObjectNameEXT(
        vk::DebugUtilsObjectNameInfoEXT(
            type, reinterpret_cast<uint64_t>(static_cast<T::CType>(handle)), name.data()
        ),
        Application::GetDispatchLoader()
    );
#endif
}

struct DebugLabel
{
    inline DebugLabel(vk::CommandBuffer commandBuffer, std::string_view name, std::array<float, 4> color)
        : CommandBuffer(commandBuffer)
    {
#ifndef NDEBUG
        commandBuffer.beginDebugUtilsLabelEXT(
            vk::DebugUtilsLabelEXT(name.data(), color), Application::GetDispatchLoader()
        );
#endif
    }

    inline ~DebugLabel()
    {
#ifndef NDEBUG
        CommandBuffer.endDebugUtilsLabelEXT(Application::GetDispatchLoader());
#endif
    }

    vk::CommandBuffer CommandBuffer;
};

}
