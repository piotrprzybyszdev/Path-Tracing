#pragma once

#include <map>
#include <memory>
#include <functional>

#include "Scene.h"

namespace PathTracing::ExampleScenes
{

std::shared_ptr<Scene> CreateTexturedCubesScene();
std::shared_ptr<Scene> CreateReuseMeshCubesScene();
std::shared_ptr<Scene> CreateSponzaScene();
std::shared_ptr<Scene> CreateChessGameScene();
std::shared_ptr<Scene> CreateVirtualCityScene();
std::shared_ptr<Scene> CreateCesiumManScene();
std::shared_ptr<Scene> CreateCesiumMilkTruckScene();
std::shared_ptr<Scene> CreateBrainStemScene();
std::shared_ptr<Scene> CreateBoxAnimatedScene();
std::shared_ptr<Scene> CreateLampLightScene();
std::shared_ptr<Scene> CreateBigSponzaScene();
std::shared_ptr<Scene> CreateRiggedSimpleScene();

static inline std::map<std::string, std::function<std::shared_ptr<Scene>()>> Scenes = {
    { "Textured Cubes", CreateTexturedCubesScene },
    { "Reuse Mesh", CreateReuseMeshCubesScene },
    { "Sponza", CreateSponzaScene },
    { "Chess Game", CreateChessGameScene },
    { "Virtual City", CreateVirtualCityScene },
    { "Cesium Man", CreateCesiumManScene },
    { "Cesium Milk Truck", CreateCesiumMilkTruckScene },
    { "Brain Stem", CreateBrainStemScene },
    { "Box Animated", CreateBoxAnimatedScene },
    { "Lamp", CreateLampLightScene },
    { "Big Sponza", CreateBigSponzaScene },
    { "Rigged Simple", CreateRiggedSimpleScene },
};

}
