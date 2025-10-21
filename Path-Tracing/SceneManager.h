#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <ranges>
#include <map>
#include <string>
#include <thread>

#include "Scene.h"

namespace PathTracing
{

class SceneLoader
{
public:
    virtual ~SceneLoader() = default;

    virtual void Load(SceneBuilder &sceneBuilder) = 0;
};

using SceneGroup = std::map<std::string, std::unique_ptr<SceneLoader>>;

class SceneManager
{
public:
    static void Init();
    static void Shutdown();

    static void DiscoverScenes();
    static void SetActiveScene(const std::filesystem::path &path);
    static void SetActiveScene(const std::string &groupName, const std::string &sceneName);
    static std::shared_ptr<Scene> GetActiveScene();

    static auto GetSceneGroupNames()
    {
        return s_SceneGroups | std::views::keys;
    }

    static auto GetSceneNames(const std::string &groupName)
    {
        return s_SceneGroups.at(groupName) | std::views::keys;
    }

private:
    static std::map<std::string, SceneGroup> s_SceneGroups;
    static std::atomic<std::shared_ptr<Scene>> s_ActiveScene;
    static std::jthread s_LoadingThread;

private:
    static void WaitLoadFinish();
};

}
