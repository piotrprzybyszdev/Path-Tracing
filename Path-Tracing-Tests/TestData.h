#pragma once

#include <glm/glm.hpp>

#include <array>
#include <tuple>

namespace PathTracingTests
{

namespace Data
{

inline const std::array<glm::vec3, 3> EdgeCaseVec3s = {
    glm::normalize(glm::vec3(0.99f, 0.0f, 0.01f)),
    glm::normalize(glm::vec3(0.0f, 0.99f, 0.01f)),
    glm::normalize(glm::vec3(0.01f, 0.0f, 0.99f)),
};

inline const std::array<float, 2> EdgeCaseFloats = { 0.001f, 0.999f };

struct Vec3FloatGenerator
{
    std::tuple<glm::vec3, float> Next()
    {
        assert(Vec3Index < EdgeCaseVec3s.size() && FloatIndex < EdgeCaseFloats.size());

        auto ret = std::make_tuple(EdgeCaseVec3s[Vec3Index], EdgeCaseFloats[FloatIndex]);

        if (Vec3Index == EdgeCaseVec3s.size() - 1)
        {
            Vec3Index = 0;
            FloatIndex++;
        }
        else
            Vec3Index++;

        return ret;
    }

    static constexpr size_t GetSize()
    {
        return EdgeCaseVec3s.size() * EdgeCaseFloats.size();
    }

    size_t Vec3Index = 0, FloatIndex = 0;
};

struct FloatFloatGenerator
{
    std::tuple<float, float> Next()
    {
        assert(Float1Index < EdgeCaseFloats.size() && Float2Index < EdgeCaseFloats.size());

        auto ret = std::make_tuple(EdgeCaseFloats[Float1Index], EdgeCaseFloats[Float2Index]);

        if (Float1Index == EdgeCaseFloats.size() - 1)
        {
            Float1Index = 0;
            Float2Index++;
        }
        else
            Float1Index++;

        return ret;
    }

    static constexpr size_t GetSize()
    {
        return EdgeCaseFloats.size() * EdgeCaseFloats.size();
    }

    size_t Float1Index = 0, Float2Index = 0;
};

struct Vec3Vec3Generator
{
    std::tuple<glm::vec3, glm::vec3> Next()
    {
        assert(Vec31Index < EdgeCaseVec3s.size() && Vec32Index < EdgeCaseVec3s.size());

        auto ret = std::make_tuple(EdgeCaseVec3s[Vec31Index], EdgeCaseVec3s[Vec32Index]);

        if (Vec31Index == EdgeCaseVec3s.size() - 1)
        {
            Vec31Index = 0;
            Vec32Index++;
        }
        else
            Vec31Index++;

        return ret;
    }

    static constexpr size_t GetSize()
    {
        return EdgeCaseVec3s.size() * EdgeCaseVec3s.size();
    }

    size_t Vec31Index = 0, Vec32Index = 0;
};

}

}
