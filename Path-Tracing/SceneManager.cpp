#include <ranges>

#include "ExampleScenes.h"
#include "SceneManager.h"

namespace PathTracing
{

std::set<std::string> SceneManager::s_SceneNames = {};
std::map<std::string, std::unique_ptr<SceneLoader>> SceneManager::s_Scenes = {};
std::shared_ptr<Scene> SceneManager::s_ActiveScene = nullptr;  // TODO: Always have an active scene (maybe some menu)

void SceneManager::Init()
{
    ExampleScenes::AddScenes(s_Scenes);

    auto keys = s_Scenes | std::views::keys;
    s_SceneNames.insert(keys.begin(), keys.end());
}

void SceneManager::Shutdown()
{
    s_ActiveScene.reset();
    s_Scenes.clear();
    s_SceneNames.clear();
}

const std::set<std::string> &SceneManager::GetSceneNames()
{
    return s_SceneNames;
}

void SceneManager::SetActiveScene(std::string sceneName)
{
    SceneBuilder sceneBuilder;
    s_Scenes.at(sceneName)->Load(sceneBuilder);
    s_ActiveScene = std::move(sceneBuilder.CreateSceneShared());
}

std::shared_ptr<Scene> SceneManager::GetActiveScene()
{
    assert(s_ActiveScene != nullptr);
    return s_ActiveScene;
}

}
