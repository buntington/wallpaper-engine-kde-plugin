#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Image.hpp"
#include "Interface/IImageParser.h"

namespace wallpaper
{

// Layout settings of a Wallpaper Engine text object.
struct TextStyle {
    std::vector<uint8_t> font; // font file data; system fallbacks are used when empty
    std::string          systemFont;
    float                pointsize { 32.0f };
    std::array<float, 2> size { 0.0f, 0.0f };
    float                padding { 0.0f };
    std::string          halign { "center" };
    std::string          valign { "center" };
    bool                 limitwidth { false };
    float                maxwidth { 0.0f };
    bool                 limitrows { false };
    int32_t              maxrows { 0 };
    bool                 opaquebackground { false };
    std::array<float, 3> backgroundcolor { 0.0f, 0.0f, 0.0f };
    float                backgroundbrightness { 1.0f };
};

// Text objects rendered to textures. The scene thread registers layers and
// changes their text; the render thread uploads the changed textures.
class TextLayers {
public:
    TextLayers();
    ~TextLayers();

    // Returns the texture name; size receives the layer size in scene units.
    std::string Add(int32_t id, TextStyle style, const std::string& text,
                    std::array<float, 2>& size);

    bool                   Contains(const std::string& tex) const;
    ImageHeader            Header(const std::string& tex) const;
    std::shared_ptr<Image> MakeImage(const std::string& tex) const;

    void SetText(int32_t id, const std::string& text);

    // Calls fn with the new image of every layer changed since the last call.
    void TakeDirty(const std::function<void(Image&)>& fn);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

// Serves text layer textures and forwards every other name to the base parser.
class TextImageParser : public IImageParser {
public:
    TextImageParser(std::unique_ptr<IImageParser> base, std::shared_ptr<TextLayers> texts)
        : m_base(std::move(base)), m_texts(std::move(texts)) {}

    std::shared_ptr<Image> Parse(const std::string& name) override {
        return m_texts->Contains(name) ? m_texts->MakeImage(name) : m_base->Parse(name);
    }
    ImageHeader ParseHeader(const std::string& name) override {
        return m_texts->Contains(name) ? m_texts->Header(name) : m_base->ParseHeader(name);
    }

private:
    std::unique_ptr<IImageParser> m_base;
    std::shared_ptr<TextLayers>   m_texts;
};

} // namespace wallpaper
