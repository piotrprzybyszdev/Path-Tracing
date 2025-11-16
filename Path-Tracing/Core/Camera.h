#pragma once

#include <glm/glm.hpp>

namespace PathTracing
{

class Camera
{
public:
    Camera(
        float verticalFOV, float nearClip, float farClip, glm::vec3 position, glm::vec3 direction,
        glm::vec3 up
    );
    virtual ~Camera() = default;

    virtual bool OnUpdate(float timeStep) = 0;
    void OnResize(uint32_t width, uint32_t height);

    [[nodiscard]] std::pair<uint32_t, uint32_t> GetExtent() const;

    [[nodiscard]] glm::mat4 GetInvViewMatrix() const;
    [[nodiscard]] glm::mat4 GetInvProjectionMatrix() const;

protected:
    void UpdateInvView();
    void UpdateInvProjection();

    glm::vec3 m_UpDirection;
    glm::vec3 m_Position;
    glm::vec3 m_Direction;

private:
    const float m_VerticalFOV;
    const float m_NearClip;
    const float m_FarClip;

    uint32_t m_Width, m_Height;

    glm::mat4 m_InvView;
    glm::mat4 m_InvProjection;
};

class InputCamera : public Camera
{
public:
    InputCamera(float verticalFOV, float nearClip, float farClip, glm::vec3 position, glm::vec3 direction);
    ~InputCamera() override = default;

    bool OnUpdate(float timeStep) override;

private:
    static constexpr float CameraSpeed = 5.0f;
    static constexpr float MouseSensitivity = 0.05f;

    bool m_WasPreviousPressed = false;
    glm::vec2 m_PreviousMousePos = { 0.0f, 0.0f };

    float m_Yaw;
    float m_Pitch;
};

class AnimatedCamera : public Camera
{
public:
    AnimatedCamera(
        float verticalFOV, float nearClip, float farClip, glm::vec3 position, glm::vec3 direction,
        glm::vec3 up, const glm::mat4 &transform
    );
    ~AnimatedCamera() override = default;

    bool OnUpdate(float timeStep) override;

private:
    const glm::vec3 m_RelativePosition;
    const glm::vec3 m_RelativeDirection;
    const glm::vec3 m_RelativeUpDirection;

    const glm::mat4 &m_Transform;
};

}