#pragma once

#include <map>
#include <memory>

#include "Scene.h"
#include "SceneManager.h"

namespace PathTracing::ExampleScenes
{

void AddScenes(std::map<std::string, SceneGroup> &sceneGroups);

}
