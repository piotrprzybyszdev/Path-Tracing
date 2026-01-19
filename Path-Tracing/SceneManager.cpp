#include "Core/Core.h"

#include "Application.h"
#include "ExampleScenes.h"
#include "SceneManager.h"
#include "TextureImporter.h"

namespace PathTracing
{

void CombinedSceneLoader::AddTextureMapping(TextureMapping mapping)
{
    m_TextureMapping = mapping;
}

void CombinedSceneLoader::AddComponent(const std::filesystem::path &path)
{
    m_ComponentPaths.push_back(path);
}

void CombinedSceneLoader::AddComponents(std::span<const std::filesystem::path> paths)
{
    for (const auto &path : paths)
        m_ComponentPaths.push_back(path);
}

void CombinedSceneLoader::AddSkybox2D(const std::filesystem::path &path)
{
    m_SkyboxPath = path;
}

void CombinedSceneLoader::SetDxNormalTextures()
{
    m_HasDxNormalTextures = true;
}

void CombinedSceneLoader::ForceFullTextureSize()
{
    m_ForceFullTextureSize = true;
}

bool CombinedSceneLoader::HasContent() const
{
    return m_SkyboxPath.has_value() || !m_ComponentPaths.empty();
}

void CombinedSceneLoader::Load(SceneBuilder &sceneBuilder)
{
    for (const auto &path : m_ComponentPaths)
        SceneImporter::AddFile(sceneBuilder, path, m_TextureMapping);

    if (m_SkyboxPath.has_value())
    {
        const TextureInfo info =
            TextureImporter::GetTextureInfo(m_SkyboxPath.value(), TextureType::Skybox, "Skybox");
        sceneBuilder.SetSkybox(Skybox2D(info));
    }

    if (m_HasDxNormalTextures)
        sceneBuilder.SetDxNormalTextures();

    if (m_ForceFullTextureSize)
        sceneBuilder.ForceFullTextureSize();
}

std::map<std::string, SceneGroup> SceneManager::s_SceneGroups = {};
std::shared_ptr<Scene> SceneManager::s_ActiveScene = nullptr;
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
    s_ActiveScene.reset();
    s_SceneGroups.clear();
}

void SceneManager::DiscoverScenes()
{
    WaitLoadFinish();
    ExampleScenes::AddScenes(s_SceneGroups);
}

void SceneManager::SetActiveScene(std::unique_ptr<SceneLoader> loader, const std::string &sceneName)
{
    WaitLoadFinish();

    s_LoadingThread = std::jthread([loader = std::move(loader), sceneName](std::stop_token stopToken) {
        try
        {
            SceneBuilder sceneBuilder;
            loader->Load(sceneBuilder); 
            s_ActiveScene = sceneBuilder.CreateSceneShared(sceneName);
        }
        catch (const error &error)
        {
            logger::error("Scene `{}` could not be loaded", sceneName);
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
            s_ActiveScene = sceneBuilder.CreateSceneShared(sceneName);
        }
        catch (const error &error)
        {
            logger::error("Scene `{}` could not be loaded", sceneName);
            Application::ResetBackgroundTask(BackgroundTaskType::SceneImport);
        }
    });
}

std::shared_ptr<Scene> SceneManager::GetActiveScene()
{
    auto scene = s_ActiveScene;
    assert(scene != nullptr);
    return scene;
}

void SceneManager::WaitLoadFinish()
{
    if (s_LoadingThread.joinable())
        s_LoadingThread.join();
}

}
