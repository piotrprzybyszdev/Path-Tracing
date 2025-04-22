#include "Input.h"

namespace PathTracing
{

GLFWwindow *Input::s_Window = nullptr;
bool Input::s_IsCursorLocked = false;

void Input::SetWindow(GLFWwindow *window)
{
    s_Window = window;
}

void Input::LockCursor()
{
    if (s_IsCursorLocked)
        return;

    glfwSetInputMode(s_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    s_IsCursorLocked = true;
}

void Input::UnlockCursor()
{
    if (!s_IsCursorLocked)
        return;

    glfwSetInputMode(s_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    s_IsCursorLocked = false;
}

bool Input::IsKeyPressed(Key key)
{
    return glfwGetKey(s_Window, key) == GLFW_PRESS;
}

bool Input::IsMouseButtonPressed(MouseButton mouseButton)
{
    return glfwGetMouseButton(s_Window, mouseButton) == GLFW_PRESS;
}

glm::vec2 Input::GetMousePosition()
{
    double x, y;
    glfwGetCursorPos(s_Window, &x, &y);
    return glm::vec2(x, y);
}

}
