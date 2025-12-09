#pragma once

#include <gtest/gtest.h>
#include <glm/glm.hpp>

namespace PathTracingTests
{

inline void AssertFloat(float value)
{
    EXPECT_FALSE(glm::isnan(value));
    EXPECT_FALSE(glm::isinf(value));
}

inline void AssertVec3(glm::vec3 value)
{
    EXPECT_FALSE(glm::any(glm::isnan(value)));
    EXPECT_FALSE(glm::any(glm::isinf(value)));
}

}
