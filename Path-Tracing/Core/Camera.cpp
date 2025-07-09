#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Camera.h"
#include "Core.h"
#include "Input.h"

namespace PathTracing
{

static inline constexpr glm::vec3 UpDirection { 0, 1, 0 };

Camera::Camera(float verticalFOV, float nearClip, float farClip)
    : m_VerticalFOV(verticalFOV), m_NearClip(nearClip), m_FarClip(farClip), m_Position(0.0f, 0.0f, 3.0f),
      m_Direction(0.0f, 0.0f, -1.0f), m_PreviousMousePos(0.0f, 0.0f), m_Yaw(-90.0f), m_Pitch(0.0f),
      m_WasPreviousPressed(false)
{
    glm::vec3 position(m_Position.x, m_Position.y, m_Position.z);
    glm::vec3 direction(m_Direction.x, m_Direction.y, m_Direction.z);
    m_InvView = glm::inverse(glm::lookAt(position, position + direction, UpDirection));
    m_InvProjection = glm::mat4();
}

Camera::~Camera()
{
}

void Camera::OnUpdate(float timeStep)
{
    glm::vec3 prevPosition = m_Position;
    glm::vec3 prevDirection = m_Direction;

    glm::vec3 rightDirection = glm::cross(m_Direction, UpDirection);

    if (Input::IsKeyPressed(Key::W))
        m_Position += timeStep * CameraSpeed * m_Direction;
    if (Input::IsKeyPressed(Key::S))
        m_Position -= timeStep * CameraSpeed * m_Direction;
    if (Input::IsKeyPressed(Key::A))
        m_Position -= timeStep * CameraSpeed * rightDirection;
    if (Input::IsKeyPressed(Key::D))
        m_Position += timeStep * CameraSpeed * rightDirection;
    if (Input::IsKeyPressed(Key::E))
        m_Position -= timeStep * CameraSpeed * UpDirection;
    if (Input::IsKeyPressed(Key::Q))
        m_Position += timeStep * CameraSpeed * UpDirection;

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
            m_Yaw += delta.x;
            m_Pitch += delta.y;

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

    Stats::AddStat(
        "Camera position", "Camera position: ({:.1f} {:.1f} {:.1f})", m_Position.x, m_Position.y, m_Position.z
    );
    Stats::AddStat(
        "Camera direction", "Camera direction: ({:.1f} {:.1f} {:.1f})", m_Direction.x, m_Direction.y,
        m_Direction.z
    );

    if (prevDirection != m_Direction || prevPosition != m_Position)
        m_InvView = glm::inverse(glm::lookAt(m_Position, m_Position + m_Direction, UpDirection));
}

void Camera::OnResize(uint32_t width, uint32_t height)
{
    m_InvProjection = glm::inverse(glm::perspectiveFov(
        glm::radians(m_VerticalFOV), static_cast<float>(width), static_cast<float>(height), m_NearClip,
        m_FarClip
    ));
}

glm::mat4 Camera::GetInvProjectionMatrix() const
{
    return m_InvProjection;
}

glm::mat4 Camera::GetInvViewMatrix() const
{
    return m_InvView;
}

glm::vec3 Camera::GetPosition() const
{
    return m_Position;
}

glm::vec3 Camera::GetDirection() const
{
    return m_Direction;
}

}