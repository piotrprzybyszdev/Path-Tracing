#pragma once

#include <functional>
#include <memory>
#include <set>
#include <string>

#include "Scene.h"

namespace PathTracing
{

class SceneManager
{
public:
    static void Init();
    static void Shutdown();

    static const std::set<std::string> &GetSceneNames();
    static void SetActiveScene(std::string sceneName);
    static std::shared_ptr<Scene> GetActiveScene();

private:
    static std::set<std::string> s_SceneNames;
    static std::map<std::string, std::function<std::shared_ptr<Scene>()>> s_Scenes;
    static std::shared_ptr<Scene> s_ActiveScene;
};

}
