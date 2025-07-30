#include "Input.h"
#include "UserInterface.h"

namespace PathTracing
{

GLFWwindow *Input::s_Window = nullptr;

static void glfwKeyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    if (action == GLFW_RELEASE)
        UserInterface::OnKeyRelease(static_cast<Key>(key));
}

void Input::SetWindow(GLFWwindow *window)
{
    s_Window = window;
    glfwSetKeyCallback(s_Window, glfwKeyCallback);
}

void Input::LockCursor()
{
    glfwSetInputMode(s_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void Input::UnlockCursor()
{
    glfwSetInputMode(s_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

bool Input::IsKeyPressed(Key key)
{
    if (UserInterface::GetIsFocused())
        return false;

    return glfwGetKey(s_Window, key) == GLFW_PRESS;
}

bool Input::IsMouseButtonPressed(MouseButton mouseButton)
{
    if (UserInterface::GetIsFocused())
        return false;

    return glfwGetMouseButton(s_Window, mouseButton) == GLFW_PRESS;
}

glm::vec2 Input::GetMousePosition()
{
    double x, y;
    glfwGetCursorPos(s_Window, &x, &y);
    return glm::vec2(x, y);
}

}
