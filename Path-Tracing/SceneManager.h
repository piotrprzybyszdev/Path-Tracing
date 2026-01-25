#pragma once

#include <filesystem>
#include <memory>
#include <ranges>
#include <map>
#include <string>
#include <span>
#include <thread>

#include "Scene.h"
#include "SceneImporter.h"

namespace PathTracing
{

class SceneLoader
{
public:
    virtual ~SceneLoader() = default;

    virtual void Load(SceneBuilder &sceneBuilder) = 0;
};

class CombinedSceneLoader : public SceneLoader
{
public:
    ~CombinedSceneLoader() override = default;

    void AddTextureMapping(TextureMapping mapping);
    void AddComponent(const std::filesystem::path &path);
    void AddComponents(std::span<const std::filesystem::path> paths);
    void AddSkybox2D(const std::filesystem::path &path);
    void SetDxNormalTextures();
    void ForceFullTextureSize();

    [[nodiscard]] bool HasContent() const;
    void Load(SceneBuilder &sceneBuilder) override;

private:
    TextureMapping m_TextureMapping;
    std::vector<std::filesystem::path> m_ComponentPaths;
    std::optional<std::filesystem::path> m_SkyboxPath;
    bool m_HasDxNormalTextures = false;
    bool m_ForceFullTextureSize = false;
};

struct SceneDescription
{
    std::vector<std::filesystem::path> ComponentPaths;
    std::optional<std::filesystem::path> SkyboxPath;
    TextureMapping Mapping;
    bool HasDxNormalTextures = false;
    bool ForceFullTextureSize = false;

    [[nodiscard]] std::unique_ptr<CombinedSceneLoader> ToLoader() const;
};

using SceneGroup = std::map<std::string, std::unique_ptr<SceneLoader>>;

class SceneManager
{
public:
    static void Init();
    static void Shutdown();

    static void DiscoverScenes();
    static void SetActiveScene(std::unique_ptr<SceneLoader> loader, const std::string &sceneName);
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
    static std::shared_ptr<Scene> s_ActiveScene;
    static std::jthread s_LoadingThread;

private:
    static void WaitLoadFinish();
};

}
