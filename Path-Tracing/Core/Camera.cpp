#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Camera.h"
#include "Core.h"
#include "Input.h"

namespace PathTracing
{

Camera::Camera(
    float verticalFOV, float nearClip, float farClip, glm::vec3 position, glm::vec3 direction, glm::vec3 up
)
    : m_VerticalFOV(verticalFOV), m_NearClip(nearClip), m_FarClip(farClip), m_Position(position),
      m_Direction(direction), m_UpDirection(up), m_Width(0), m_Height(0), m_InvProjection(glm::mat4(1.0f)),
      m_InvView(glm::mat4(1.0f))
{
    UpdateInvView();
}

void Camera::OnResize(uint32_t width, uint32_t height)
{
    m_Width = width;
    m_Height = height;

    UpdateInvProjection();
}

std::pair<uint32_t, uint32_t> Camera::GetExtent() const
{
    return { m_Width, m_Height };
}

glm::mat4 Camera::GetInvViewMatrix() const
{
    return m_InvView;
}

glm::mat4 Camera::GetInvProjectionMatrix() const
{
    assert(m_Width != 0 && m_Height != 0);
    return m_InvProjection;
}

void Camera::UpdateInvView()
{
    Stats::AddStat(
        "Camera position", "Camera position: ({:.1f} {:.1f} {:.1f})", m_Position.x, m_Position.y, m_Position.z
    );
    Stats::AddStat(
        "Camera direction", "Camera direction: ({:.1f} {:.1f} {:.1f})", m_Direction.x, m_Direction.y,
        m_Direction.z
    );

    m_InvView = glm::inverse(glm::lookAt(m_Position, m_Position + m_Direction, m_UpDirection));
}

void Camera::UpdateInvProjection()
{
    m_InvProjection = glm::inverse(glm::perspectiveFov(
        glm::radians(m_VerticalFOV), static_cast<float>(m_Width), static_cast<float>(m_Height), m_NearClip,
        m_FarClip
    ));
}

InputCamera::InputCamera(
    float verticalFOV, float nearClip, float farClip, glm::vec3 position, glm::vec3 direction
)
    : Camera(verticalFOV, nearClip, farClip, position, direction, glm::vec3(0.0f, -1.0f, 0.0f)),
      m_Yaw(glm::degrees(std::atan2(m_Direction.x, m_Direction.z) + glm::pi<float>() / 2)),
      m_Pitch(glm::degrees(glm::asin(m_Direction.y)))
{
}

void InputCamera::OnUpdate(float timeStep)
{
    glm::vec3 prevPosition = m_Position;
    glm::vec3 prevDirection = m_Direction;

    glm::vec3 rightDirection = glm::normalize(glm::cross(m_Direction, m_UpDirection));

    if (Input::IsKeyPressed(Key::W))
        m_Position += timeStep * CameraSpeed * m_Direction;
    if (Input::IsKeyPressed(Key::S))
        m_Position -= timeStep * CameraSpeed * m_Direction;
    if (Input::IsKeyPressed(Key::A))
        m_Position -= timeStep * CameraSpeed * rightDirection;
    if (Input::IsKeyPressed(Key::D))
        m_Position += timeStep * CameraSpeed * rightDirection;
    if (Input::IsKeyPressed(Key::E))
        m_Position -= timeStep * CameraSpeed * m_UpDirection;
    if (Input::IsKeyPressed(Key::Q))
        m_Position += timeStep * CameraSpeed * m_UpDirection;

    if (Input::IsMouseButtonPressed(MouseButton::Right))
    {
        glm::vec2 mousePos = Input::GetMousePosition();
        glm::vec2 delta = (mousePos - m_PreviousMousePos) * MouseSensitivity;
        m_PreviousMousePos = mousePos;

        if (!m_WasPreviousPressed)
        {
            Input::LockCursor();
            m_WasPreviousPressed = true;
            delta = glm::vec2();
        }

        if (delta.x != 0.0f || delta.y != 0.0f)
        {
            m_Yaw -= delta.x;
            m_Pitch = glm::clamp(m_Pitch - delta.y, -89.0f, 89.0f);

            m_Direction = glm::normalize(glm::vec3(
                glm::cos(glm::radians(m_Yaw)) * glm::cos(glm::radians(m_Pitch)),
                glm::sin(glm::radians(m_Pitch)),
                glm::sin(glm::radians(m_Yaw)) * glm::cos(glm::radians(m_Pitch))
            ));
        }
    }
    else
    {
        if (m_WasPreviousPressed)
            Input::UnlockCursor();
        m_WasPreviousPressed = false;
    }

    if (prevDirection != m_Direction || prevPosition != m_Position)
        UpdateInvView();
}

AnimatedCamera::AnimatedCamera(
    float verticalFOV, float nearClip, float farClip, glm::vec3 position, glm::vec3 direction, glm::vec3 up,
    const glm::mat4 &transform
)
    : Camera(verticalFOV, nearClip, farClip, position, direction, up), m_RelativePosition(position),
      m_RelativeDirection(direction), m_RelativeUpDirection(up), m_Transform(transform)
{
}

void AnimatedCamera::OnUpdate(float timeStep)
{
    m_Position = glm::vec4(m_RelativePosition, 1.0f) * m_Transform;
    m_Direction = glm::vec4(m_RelativeDirection, 0.0f) * m_Transform;
    m_UpDirection = glm::vec4(m_RelativeUpDirection, 0.0f) * m_Transform;

    UpdateInvView();
}

}