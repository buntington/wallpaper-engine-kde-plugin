#pragma once
#include <memory>
#include <string_view>
#include <functional>
#include "Type.hpp"
#include "Swapchain/ExSwapchain.hpp"

namespace wallpaper
{

using FirstFrameCallback = std::function<void()>;

constexpr std::string_view PROPERTY_SOURCE               = "source";
constexpr std::string_view PROPERTY_ASSETS               = "assets";
constexpr std::string_view PROPERTY_FPS                  = "fps";
constexpr std::string_view PROPERTY_FILLMODE             = "fillmode";
constexpr std::string_view PROPERTY_SPEED                = "speed";
constexpr std::string_view PROPERTY_GRAPHIVZ             = "graphivz";
constexpr std::string_view PROPERTY_VOLUME               = "volume";
constexpr std::string_view PROPERTY_MUTED                = "muted";
constexpr std::string_view PROPERTY_CACHE_PATH           = "cache_path";
constexpr std::string_view PROPERTY_FIRST_FRAME_CALLBACK = "first_frame_callback";
// JSON object of Wallpaper Engine user property values; reloads the scene.
constexpr std::string_view PROPERTY_USER_PROPERTIES      = "user_properties";
// JSON object of text object id -> initial text, used on the next load.
constexpr std::string_view PROPERTY_TEXT_VALUES          = "text_values";
// "<text object id>\x1f<text>": changes a text object's text while running.
constexpr std::string_view PROPERTY_LAYER_TEXT           = "layer_text";

#include "Core/NoCopyMove.hpp"
class MainHandler;
struct RenderInitInfo;

class SceneWallpaper : NoCopy {
public:
    SceneWallpaper();
    ~SceneWallpaper();
    bool init();
    bool inited() const;

    void initVulkan(const RenderInitInfo&);

    void play();
    void pause();
    void mouseInput(double x, double y);

    void setPropertyBool(std::string_view, bool);
    void setPropertyInt32(std::string_view, int32_t);
    void setPropertyFloat(std::string_view, float);
    void setPropertyString(std::string_view, std::string);
    void setPropertyObject(std::string_view, std::shared_ptr<void>);

    ExSwapchain* exSwapchain() const;

    // Spectrum of the default output for scripts; capture runs while acquired.
    static void AcquireAudioSpectrum();
    static void ReleaseAudioSpectrum();
    static void ReadAudioSpectrum(int bands, float* left, float* right);

private:
    bool m_inited { false };

private:
    friend class MainHandler;

    bool                         m_offscreen { false };
    std::shared_ptr<MainHandler> m_main_handler;
};
} // namespace wallpaper
