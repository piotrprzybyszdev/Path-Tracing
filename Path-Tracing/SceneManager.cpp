#include <ranges>

#include "ExampleScenes.h"
#include "SceneManager.h"

namespace PathTracing
{

std::set<std::string> SceneManager::s_SceneNames = {};
std::map<std::string, std::function<std::shared_ptr<Scene>()>> SceneManager::s_Scenes = {};
std::shared_ptr<Scene> SceneManager::s_ActiveScene = nullptr;  // TODO: Always have an active scene (maybe some menu)

void SceneManager::Init()
{
    // TODO: Find example scenes by searching the assets folder

    s_Scenes.insert(ExampleScenes::Scenes.begin(), ExampleScenes::Scenes.end());
    
    auto keys = s_Scenes | std::views::keys;
    s_SceneNames.insert(keys.begin(), keys.end());
}

void SceneManager::Shutdown()
{
    s_ActiveScene.reset();
}

const std::set<std::string> &SceneManager::GetSceneNames()
{
    return s_SceneNames;
}

void SceneManager::SetActiveScene(std::string sceneName)
{
    if (s_ActiveScene != nullptr && s_ActiveScene->GetName() == sceneName)
        return;

    s_ActiveScene = std::move(s_Scenes.at(sceneName))();
}

std::shared_ptr<Scene> SceneManager::GetActiveScene()
{
    assert(s_ActiveScene != nullptr);
    return s_ActiveScene;
}

}
