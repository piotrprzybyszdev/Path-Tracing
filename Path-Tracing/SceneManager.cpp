#include "Core/Core.h"

#include "ExampleScenes.h"
#include "SceneImporter.h"
#include "SceneManager.h"

namespace PathTracing
{

std::map<std::string, SceneGroup> SceneManager::s_SceneGroups = {};
std::atomic<std::shared_ptr<Scene>> SceneManager::s_ActiveScene = nullptr;
std::jthread SceneManager::s_LoadingThread = {};

void SceneManager::Init()
{
    ExampleScenes::AddScenes(s_SceneGroups);
    SceneManager::SetActiveScene("Test Scenes", "Default");
    WaitLoadFinish();
}

void SceneManager::Shutdown()
{
    WaitLoadFinish();
    s_ActiveScene.load().reset();
    s_SceneGroups.clear();
}

void SceneManager::DiscoverScenes()
{
    WaitLoadFinish();
    ExampleScenes::AddScenes(s_SceneGroups);
}

void SceneManager::SetActiveScene(const std::filesystem::path &path)
{
    WaitLoadFinish();

    s_LoadingThread = std::jthread([path](std::stop_token stopToken) {
        try
        {
            SceneBuilder sceneBuilder;
            SceneImporter::AddFile(sceneBuilder, path);
            s_ActiveScene = sceneBuilder.CreateSceneShared();
        }
        catch (const error &error)
        {
            logger::error("Scene {} could not be loaded", path.string());
        }
    });
}

void SceneManager::SetActiveScene(const std::string &groupName, const std::string &sceneName)
{
    WaitLoadFinish();

    auto &loader = s_SceneGroups.at(groupName).at(sceneName);

    s_LoadingThread = std::jthread([&loader, sceneName](std::stop_token stopToken) {
        try
        {
            SceneBuilder sceneBuilder;
            loader->Load(sceneBuilder);
            s_ActiveScene = sceneBuilder.CreateSceneShared();
        }
        catch (const error &error)
        {
            logger::error("Scene {} could not be loaded", sceneName);
        }
    });
}

std::shared_ptr<Scene> SceneManager::GetActiveScene()
{
    auto scene = s_ActiveScene.load();
    assert(scene != nullptr);
    return scene;
}

void SceneManager::WaitLoadFinish()
{
    if (s_LoadingThread.joinable())
        s_LoadingThread.join();
}

}
