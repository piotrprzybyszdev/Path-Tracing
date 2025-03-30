#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Camera.h"
#include "Core.h"
#include "Input.h"

namespace PathTracing
{

static constexpr glm::vec3 UpDirection{ 0, 1, 0 };

Camera::Camera(float verticalFOV, float nearClip, float farClip)
	: m_VerticalFOV(verticalFOV), m_NearClip(nearClip), m_FarClip(farClip),
	m_Position(0.0f, 0.0f, 3.0f), m_Direction(0.0f, 0.0f, -1.0f),
	m_PreviousMousePos(0.0f, 0.0f), m_Yaw(-90.0f), m_Pitch(0.0f)
{
	glm::vec3 position(m_Position.x, m_Position.y, m_Position.z);
	glm::vec3 direction(m_Direction.x, m_Direction.y, m_Direction.z);
	m_InvView = glm::inverse(glm::lookAt(position, position + direction, UpDirection));
}

Camera::~Camera()
{
}

void Camera::OnUpdate(float timeStep)
{
	glm::vec2 previousMousePos(m_PreviousMousePos.x, m_PreviousMousePos.y);
	glm::vec3 direction(m_Direction.x, m_Direction.y, m_Direction.z);

	glm::vec2 mousePos = Input::GetMousePosition();
	glm::vec2 delta = (mousePos - previousMousePos) * MouseSensitivity;
	m_PreviousMousePos = mousePos;

	glm::vec3 rightDirection = glm::cross(direction, UpDirection);

	glm::vec3 prevPosition(m_Position.x, m_Position.y, m_Position.z);
	glm::vec3 prevDirection = direction;

	if (Input::IsKeyPressed(Key::W))
		m_Position += timeStep * CameraSpeed * direction;
	if (Input::IsKeyPressed(Key::S))
		m_Position -= timeStep * CameraSpeed * direction;
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
		Input::LockCursor();

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
		Input::UnlockCursor();

	Stats::AddStat("Camera position", "Camera position: ({:.1f} {:.1f} {:.1f})", m_Position.x, m_Position.y, m_Position.z);
	Stats::AddStat("Camera direction", "Camera direction: ({:.1f} {:.1f} {:.1f})", m_Direction.x, m_Direction.y, m_Direction.z);

	if (prevDirection != m_Direction || prevPosition != m_Position)
	{
		glm::vec3 position(m_Position.x, m_Position.y, m_Position.z);
		direction = glm::vec3(m_Direction.x, m_Direction.y, m_Direction.z);
		m_InvView = glm::inverse(glm::lookAt(position, position + direction, UpDirection));
	}
}

void Camera::OnResize(uint32_t width, uint32_t height)
{
	if (m_Width == width && m_Height == height)
		return;

	m_Width = width;
	m_Height = height;

	m_InvProjection = glm::inverse(glm::perspectiveFov(glm::radians(m_VerticalFOV), (float)m_Width, (float)m_Height, m_NearClip, m_FarClip));
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