#pragma once
#include <nlohmann/json.hpp>

namespace wallpaper
{

// Prepares a scene.json before parsing:
// - resolves {"user": ..., "value": ...} bindings with the user's property values,
// - bakes parent transforms and visibility into child objects,
// - sets text object values (e.g. a clock script's first result), keyed by object id.
void PreprocessSceneJson(nlohmann::json& scene, const nlohmann::json& userProperties,
                         const nlohmann::json& textValues);

} // namespace wallpaper
