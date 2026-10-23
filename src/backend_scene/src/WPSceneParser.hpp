#pragma once
#include <nlohmann/json.hpp>
#include "Interface/ISceneParser.h"
#include <random>

namespace wallpaper
{

class WPSceneParser : public ISceneParser {
public:
    WPSceneParser()  = default;
    ~WPSceneParser() = default;
    std::shared_ptr<Scene> Parse(std::string_view scene_id, const std::string&, fs::VFS&, audio::SoundManager&) override;

    // User property values, for materials that bind them to shader constants.
    void SetUserProperties(nlohmann::json props) { m_user_properties = std::move(props); }

private:
    nlohmann::json m_user_properties = nlohmann::json::object();
};
} // namespace wallpaper
