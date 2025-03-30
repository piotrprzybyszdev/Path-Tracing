#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace PathTracing
{

class Camera
{
public:
	Camera(float verticalFOV, float nearClip, float farClip);
	~Camera();

	void OnUpdate(float timeStep);
	void OnResize(uint32_t width, uint32_t height);

	glm::mat4 GetInvViewMatrix() const;
	glm::mat4 GetInvProjectionMatrix() const;

	glm::vec3 GetPosition() const;
	glm::vec3 GetDirection() const;

private:
	static constexpr float CameraSpeed = 5.0f;
	static constexpr float MouseSensitivity = 0.05f;

	const float m_VerticalFOV;
	const float m_NearClip;
	const float m_FarClip;

	uint32_t m_Width = 0;
	uint32_t m_Height = 0;

	glm::vec2 m_PreviousMousePos;

	float m_Yaw;
	float m_Pitch;

	glm::vec3 m_Position;
	glm::vec3 m_Direction;

	glm::mat4 m_InvView;
	glm::mat4 m_InvProjection;
};

}