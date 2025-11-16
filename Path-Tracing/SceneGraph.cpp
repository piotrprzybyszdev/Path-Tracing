#include "Core/Config.h"

#include "SceneGraph.h"

namespace PathTracing
{

void Animation::Update(float timeStep, std::span<SceneNode> nodes)
{
    CurrentTick += timeStep * TickPerSecond;
    if (CurrentTick >= Duration)
    {
        for (AnimationNode &node : Nodes)
        {
            node.Positions.Index = 0;
            node.Rotations.Index = 0;
            node.Scales.Index = 0;
        }
    }

    while (CurrentTick >= Duration)
        CurrentTick -= Duration;

    for (AnimationNode &node : Nodes)
    {
        glm::vec3 position = node.Positions.Update(CurrentTick);
        glm::quat rotation = node.Rotations.Update(CurrentTick);
        glm::vec3 scale = node.Scales.Update(CurrentTick);

        nodes[node.SceneNodeIndex].Transform =
            glm::transpose(glm::scale(glm::translate(glm::mat4(1.0f), position) * glm::mat4(rotation), scale)
            );
    }
}

void SceneGraph::UpdateTransforms()
{
#ifdef CONFIG_ASSERTS
    std::vector<bool> isUpdated(m_SceneNodes.size());
    isUpdated[0] = true;
#endif

    m_SceneNodes[0].CurrentTransform = m_SceneNodes[0].Transform;

    for (int i = 1; i < m_SceneNodes.size(); i++)
    {
        SceneNode &node = m_SceneNodes[i];
        SceneNode &parent = m_SceneNodes[node.Parent];
        assert(isUpdated[node.Parent] == true);  // Nodes are not in pre-order sequence
        assert(isUpdated[i] == false);  // Two animations have the same SceneNode or it's a DAG not a tree

        if (m_IsRelativeTransform[i])
            node.CurrentTransform = node.Transform * parent.CurrentTransform;
        else
            node.CurrentTransform = node.Transform;
#ifdef CONFIG_ASSERTS
        isUpdated[i] = true;
#endif
    }
}

SceneGraph::SceneGraph(
    std::vector<SceneNode> &&sceneNodes, std::vector<bool> &&isRelativeTransform,
    std::vector<Animation> &&animations
)
    : m_SceneNodes(std::move(sceneNodes)), m_IsRelativeTransform(std::move(isRelativeTransform)),
      m_Animations(std::move(animations))
{
}

SceneGraph::SceneGraph(SceneGraph &&sceneGraph) noexcept
    : m_SceneNodes(std::move(sceneGraph.m_SceneNodes)),
      m_IsRelativeTransform(std::move(sceneGraph.m_IsRelativeTransform)),
      m_Animations(std::move(sceneGraph.m_Animations))
{
}

SceneGraph &SceneGraph::operator=(SceneGraph &&sceneGraph) noexcept
{
    m_SceneNodes = std::move(sceneGraph.m_SceneNodes);
    m_IsRelativeTransform = std::move(sceneGraph.m_IsRelativeTransform);
    m_Animations = std::move(sceneGraph.m_Animations);
    return *this;
}

bool SceneGraph::Update(float timeStep)
{
    for (Animation &animation : m_Animations)
        animation.Update(timeStep, m_SceneNodes);

    UpdateTransforms();

    return !m_Animations.empty();
}

std::span<const SceneNode> SceneGraph::GetSceneNodes() const
{
    return m_SceneNodes;
}

bool SceneGraph::HasAnimations() const
{
    return !m_Animations.empty();
}

}