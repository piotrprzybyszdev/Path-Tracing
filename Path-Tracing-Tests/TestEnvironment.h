#pragma once

#include <gtest/gtest.h>

namespace PathTracingTests
{

class TestEnvironment : public ::testing::Environment
{
public:
    ~TestEnvironment() override = default;

    void SetUp() override;
    void TearDown() override;
};

}
