#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

#include <span>

namespace PathTracing
{

struct SceneNode
{
    const uint32_t Parent;
    glm::mat4 Transform;
    glm::mat4 CurrentTransform;
};

struct AnimationNode
{
    template<typename T> struct Sequence
    {
        struct Key
        {
            T Value;
            float Tick;
        };

        std::vector<Key> Keys;
        uint32_t Index = 0;

        T Update(float currentTick);
        T Interpolate(float ratio);
    };

    uint32_t SceneNodeIndex;

    Sequence<glm::vec3> Positions;
    Sequence<glm::quat> Rotations;
    Sequence<glm::vec3> Scales;
};

template<typename T> inline T AnimationNode::Sequence<T>::Update(float currentTick)
{
    if (currentTick < Keys[0].Tick)
        return Keys[0].Value;

    while (Index + 1 < Keys.size() && currentTick > Keys[Index + 1].Tick)
        Index++;

    if (Index + 1 == Keys.size())
        return Keys.back().Value;

    const float total = Keys[Index + 1].Tick - Keys[Index].Tick;
    const float current = currentTick - Keys[Index].Tick;

    return Interpolate(current / total);
}

template<typename T> inline T AnimationNode::Sequence<T>::Interpolate(float ratio)
{
    return glm::mix(Keys[Index].Value, Keys[Index + 1].Value, ratio);
}

template<> inline glm::quat AnimationNode::Sequence<glm::quat>::Interpolate(float ratio)
{
    return glm::slerp(Keys[Index].Value, Keys[Index + 1].Value, ratio);
}

struct Animation
{
    std::vector<AnimationNode> Nodes;
    const float TickPerSecond;
    const float Duration;
    float CurrentTick = 0;

    void Update(float timeStep, std::span<SceneNode> nodes);
};

class SceneGraph
{
public:
    SceneGraph(
        std::vector<SceneNode> &&sceneNodes, std::vector<bool> &&isRelativeTransform,
        std::vector<Animation> &&animations
    );

    SceneGraph(const SceneGraph &) = delete;
    SceneGraph &operator=(const SceneGraph &) = delete;

    SceneGraph(SceneGraph &&sceneGraph) noexcept;
    SceneGraph &operator=(SceneGraph &&sceneGraph) noexcept;

    bool Update(float timeStep);

    [[nodiscard]] std::span<const SceneNode> GetSceneNodes() const;
    [[nodiscard]] bool HasAnimations() const;

private:
    std::vector<SceneNode> m_SceneNodes;
    std::vector<bool> m_IsRelativeTransform;
    std::vector<Animation> m_Animations;

private:
    void UpdateTransforms();
};

}