#include "TextLayers.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>

#include <fontconfig/fontconfig.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

#include "Utils/Logging.h"

using namespace wallpaper;

namespace
{

// Wallpaper Engine renders one point as four scene units.
constexpr float UnitsPerPoint  = 4.0f;
constexpr int   MaxTextureSide = 4096;

struct Font {
    std::shared_ptr<std::vector<uint8_t>> data;
    stbtt_fontinfo                        info {};
    bool                                  ok { false };

    static std::unique_ptr<Font> Load(std::shared_ptr<std::vector<uint8_t>> data, int index = 0) {
        auto font  = std::make_unique<Font>();
        font->data = std::move(data);
        if (! font->data || font->data->empty()) return font;
        const int offset = stbtt_GetFontOffsetForIndex(font->data->data(), index);
        font->ok         = offset >= 0 && stbtt_InitFont(&font->info, font->data->data(), offset);
        return font;
    }
    bool Has(uint32_t cp) const { return ok && stbtt_FindGlyphIndex(&info, (int)cp) != 0; }
};

std::shared_ptr<std::vector<uint8_t>> ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (! f) return nullptr;
    return std::make_shared<std::vector<uint8_t>>(std::istreambuf_iterator<char>(f),
                                                  std::istreambuf_iterator<char>());
}

// System fonts found through fontconfig, shared by all layers and loaded lazily.
class SystemFonts {
public:
    static SystemFonts& Get() {
        static SystemFonts instance;
        return instance;
    }

    // Font for a family name, such as the one in "systemfont_arial".
    const Font* Family(const std::string& family) {
        std::lock_guard lock(m_mutex);
        if (auto it = m_families.find(family); it != m_families.end()) return it->second.get();
        std::unique_ptr<Font> font;
        if (FcPattern* pat = FcNameParse((const FcChar8*)family.c_str())) {
            FcConfigSubstitute(nullptr, pat, FcMatchPattern);
            FcDefaultSubstitute(pat);
            FcResult res;
            if (FcPattern* match = FcFontMatch(nullptr, pat, &res)) {
                FcChar8* file  = nullptr;
                int      index = 0;
                if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch) {
                    FcPatternGetInteger(match, FC_INDEX, 0, &index);
                    font = Font::Load(ReadFile((const char*)file), index);
                }
                FcPatternDestroy(match);
            }
            FcPatternDestroy(pat);
        }
        return (m_families[family] = std::move(font)).get();
    }

    // First sans-serif font that has the character.
    const Font* Fallback(uint32_t cp) {
        std::lock_guard lock(m_mutex);
        if (auto it = m_by_char.find(cp); it != m_by_char.end()) return it->second;
        const Font* found = nullptr;
        for (auto& entry : m_fallbacks) {
            if (! entry.charset || ! FcCharSetHasChar(entry.charset, cp)) continue;
            if (! entry.font) entry.font = Font::Load(ReadFile(entry.file), entry.index);
            if (entry.font->Has(cp)) {
                found = entry.font.get();
                break;
            }
        }
        m_by_char[cp] = found;
        return found;
    }

private:
    struct Entry {
        std::string           file;
        int                   index { 0 };
        FcCharSet*            charset { nullptr };
        std::unique_ptr<Font> font;
    };

    SystemFonts() {
        FcInit();
        FcPattern* pat = FcNameParse((const FcChar8*)"sans-serif");
        if (! pat) return;
        FcConfigSubstitute(nullptr, pat, FcMatchPattern);
        FcDefaultSubstitute(pat);
        FcResult res;
        if (FcFontSet* set = FcFontSort(nullptr, pat, FcFalse, nullptr, &res)) {
            for (int i = 0; i < set->nfont && m_fallbacks.size() < 32; i++) {
                FcChar8*   file    = nullptr;
                FcCharSet* charset = nullptr;
                int        index   = 0;
                if (FcPatternGetString(set->fonts[i], FC_FILE, 0, &file) != FcResultMatch) continue;
                FcPatternGetInteger(set->fonts[i], FC_INDEX, 0, &index);
                FcPatternGetCharSet(set->fonts[i], FC_CHARSET, 0, &charset);
                m_fallbacks.push_back({ (const char*)file, index, FcCharSetCopy(charset), nullptr });
            }
            FcFontSetDestroy(set);
        }
        FcPatternDestroy(pat);
    }
    ~SystemFonts() {
        for (auto& entry : m_fallbacks)
            if (entry.charset) FcCharSetDestroy(entry.charset);
    }

    std::mutex                                   m_mutex;
    std::map<std::string, std::unique_ptr<Font>> m_families;
    std::vector<Entry>                           m_fallbacks;
    std::map<uint32_t, const Font*>              m_by_char;
};

std::vector<uint32_t> DecodeUtf8(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        const auto c   = (unsigned char)s[i];
        int        len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        uint32_t   cp  = len == 1 ? c : len == 2 ? (c & 0x1Fu) : len == 3 ? (c & 0x0Fu) : (c & 0x07u);
        if (i + (size_t)len > s.size()) len = 1, cp = 0xFFFD;
        for (int k = 1; k < len; k++) cp = (cp << 6) | ((unsigned char)s[i + (size_t)k] & 0x3Fu);
        out.push_back(cp);
        i += (size_t)len;
    }
    return out;
}

using Line = std::vector<uint32_t>;

struct Layout {
    std::vector<Line>  lines;
    std::vector<float> widths;
    float              width { 0.0f };
    float              height { 0.0f };
    float              ascent { 0.0f };
    float              lineAdvance { 0.0f };
};

struct Layer {
    int32_t               id { 0 };
    std::string           tex;
    TextStyle             style;
    std::unique_ptr<Font> font;
    const Font*           systemFont { nullptr };

    std::string text;
    int32_t     width { 1 }; // texture size in pixels
    int32_t     height { 1 };
    float       pxPerUnit { 1.0f };
    float       padPx { 0.0f };
    float       emPx { 1.0f };

    std::vector<std::vector<uint8_t>> mips; // RGBA8, level 0 first
    bool                              dirty { false };

    const Font* Primary() const {
        if (font && font->ok) return font.get();
        return systemFont && systemFont->ok ? systemFont : nullptr;
    }
    const Font* FontFor(uint32_t cp) const {
        const Font* primary = Primary();
        if (primary && (primary->Has(cp) || cp < 0x20)) return primary;
        if (const Font* fb = SystemFonts::Get().Fallback(cp)) return fb;
        return primary;
    }
    float Advance(uint32_t cp, uint32_t next, float em) const {
        const Font* f = FontFor(cp);
        if (! f) return 0.0f;
        const float scale = stbtt_ScaleForMappingEmToPixels(&f->info, em);
        int         adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&f->info, (int)cp, &adv, &lsb);
        float w = (float)adv * scale;
        if (next && FontFor(next) == f) w += (float)stbtt_GetCodepointKernAdvance(&f->info, (int)cp, (int)next) * scale;
        return w;
    }
    float Measure(const Line& line, float em) const {
        float w = 0.0f;
        for (size_t i = 0; i < line.size(); i++)
            w += Advance(line[i], i + 1 < line.size() ? line[i + 1] : 0, em);
        return w;
    }

    Layout DoLayout(const std::string& str, float em) const {
        Layout l;
        if (const Font* f = Primary()) {
            int ascent = 0, descent = 0, gap = 0;
            stbtt_GetFontVMetrics(&f->info, &ascent, &descent, &gap);
            const float scale = stbtt_ScaleForMappingEmToPixels(&f->info, em);
            l.ascent          = (float)ascent * scale;
            l.lineAdvance     = (float)(ascent - descent + gap) * scale;
        } else {
            l.ascent = em * 0.8f, l.lineAdvance = em * 1.2f;
        }

        const float maxWidth = style.limitwidth && style.maxwidth > 0 ? style.maxwidth * pxPerUnit
                                                                       : 1e9f;
        Line para;
        auto flush = [&](Line& p) {
            // Greedy word wrap; a word wider than the limit is broken by characters.
            Line cur;
            for (size_t i = 0; i < p.size(); i++) {
                Line next = cur;
                next.push_back(p[i]);
                if (! cur.empty() && Measure(next, em) > maxWidth) {
                    size_t space = cur.size();
                    while (space > 0 && cur[space - 1] != ' ') space--;
                    if (space > 0 && p[i] != ' ') {
                        l.lines.emplace_back(cur.begin(), cur.begin() + (long)space - 1);
                        cur.erase(cur.begin(), cur.begin() + (long)space);
                    } else {
                        l.lines.push_back(cur);
                        cur.clear();
                    }
                    if (p[i] == ' ' && cur.empty()) continue;
                }
                cur.push_back(p[i]);
            }
            l.lines.push_back(cur);
            p.clear();
        };
        for (uint32_t cp : DecodeUtf8(str)) {
            if (cp == '\r') continue;
            if (cp == '\n') flush(para);
            else para.push_back(cp == '\t' ? (uint32_t)' ' : cp);
        }
        flush(para);
        if (style.limitrows && style.maxrows > 0 && l.lines.size() > (size_t)style.maxrows)
            l.lines.resize((size_t)style.maxrows);

        for (const auto& line : l.lines) {
            l.widths.push_back(Measure(line, em));
            l.width = std::max(l.width, l.widths.back());
        }
        l.height = (float)l.lines.size() * l.lineAdvance;
        return l;
    }

    void Raster(const Layout& l, float em) {
        std::vector<uint8_t> cov((size_t)width * (size_t)height, 0);
        std::vector<uint8_t> glyph;

        const float boxW = (float)width - 2.0f * padPx;
        const float boxH = (float)height - 2.0f * padPx;
        float       y    = padPx;
        if (style.valign == "bottom") y += boxH - l.height;
        else if (style.valign != "top") y += (boxH - l.height) * 0.5f;

        for (size_t li = 0; li < l.lines.size(); li++, y += l.lineAdvance) {
            const Line& line = l.lines[li];
            float       x    = padPx;
            if (style.halign == "right") x += boxW - l.widths[li];
            else if (style.halign != "left") x += (boxW - l.widths[li]) * 0.5f;
            const float baseline = std::round(y + l.ascent);

            for (size_t i = 0; i < line.size(); i++) {
                const uint32_t cp   = line[i];
                const uint32_t next = i + 1 < line.size() ? line[i + 1] : 0;
                const Font*    f    = FontFor(cp);
                if (f && cp > ' ') {
                    const float scale = stbtt_ScaleForMappingEmToPixels(&f->info, em);
                    const float ix    = std::floor(x);
                    int         x0, y0, x1, y1;
                    stbtt_GetCodepointBitmapBoxSubpixel(&f->info, (int)cp, scale, scale, x - ix, 0.0f,
                                                        &x0, &y0, &x1, &y1);
                    const int gw = x1 - x0, gh = y1 - y0;
                    if (gw > 0 && gh > 0) {
                        glyph.assign((size_t)gw * (size_t)gh, 0);
                        stbtt_MakeCodepointBitmapSubpixel(&f->info, glyph.data(), gw, gh, gw, scale,
                                                          scale, x - ix, 0.0f, (int)cp);
                        for (int gy = 0; gy < gh; gy++) {
                            const int py = (int)baseline + y0 + gy;
                            if (py < 0 || py >= height) continue;
                            for (int gx = 0; gx < gw; gx++) {
                                const int px = (int)ix + x0 + gx;
                                if (px < 0 || px >= width) continue;
                                auto& dst = cov[(size_t)py * (size_t)width + (size_t)px];
                                dst = std::max(dst, glyph[(size_t)gy * (size_t)gw + (size_t)gx]);
                            }
                        }
                    }
                }
                x += Advance(cp, next, em);
            }
        }

        std::vector<uint8_t> rgba(cov.size() * 4);
        std::array<float, 3> bg {};
        for (size_t c = 0; c < 3; c++)
            bg[c] = std::clamp(style.backgroundcolor[c] * style.backgroundbrightness, 0.0f, 1.0f) * 255.0f;
        for (size_t i = 0; i < cov.size(); i++) {
            const float a = (float)cov[i] / 255.0f;
            for (size_t c = 0; c < 3; c++)
                rgba[i * 4 + c] = style.opaquebackground ? (uint8_t)std::lround(bg[c] + (255.0f - bg[c]) * a)
                                                         : (uint8_t)255;
            rgba[i * 4 + 3] = style.opaquebackground ? (uint8_t)255 : cov[i];
        }

        mips.clear();
        mips.push_back(std::move(rgba));
        // Box-filtered mip chain so small on-screen text stays smooth.
        int w = width, h = height;
        while (w > 1 || h > 1) {
            const int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
            const auto&          src = mips.back();
            std::vector<uint8_t> dst((size_t)nw * (size_t)nh * 4);
            for (int yy = 0; yy < nh; yy++)
                for (int xx = 0; xx < nw; xx++)
                    for (int c = 0; c < 4; c++) {
                        int sum = 0, n = 0;
                        for (int dy = 0; dy < 2; dy++)
                            for (int dx = 0; dx < 2; dx++) {
                                const int sx = std::min(xx * 2 + dx, w - 1), sy = std::min(yy * 2 + dy, h - 1);
                                sum += src[((size_t)sy * (size_t)w + (size_t)sx) * 4 + (size_t)c];
                                n++;
                            }
                        dst[((size_t)yy * (size_t)nw + (size_t)xx) * 4 + (size_t)c] = (uint8_t)(sum / n);
                    }
            mips.push_back(std::move(dst));
            w = nw, h = nh;
        }
    }

    // Lays out and draws the text, shrinking it if it outgrows the texture.
    void Render(const std::string& str) {
        text      = str;
        float  em = emPx;
        Layout l  = DoLayout(str, em);
        const float availW = (float)width - 2.0f * padPx, availH = (float)height - 2.0f * padPx;
        if (l.width > availW || l.height > availH) {
            const float fit = std::min(availW / std::max(l.width, 1.0f), availH / std::max(l.height, 1.0f));
            em *= std::max(fit, 0.05f);
            l = DoLayout(str, em);
        }
        Raster(l, em);
        dirty = true;
    }

    ImageHeader Header() const {
        ImageHeader h;
        h.width = h.mapWidth = width;
        h.height = h.mapHeight = height;
        h.format               = TextureFormat::RGBA8;
        h.count                = 1;
        h.sample.wrapS = h.sample.wrapT = TextureWrap::CLAMP_TO_EDGE;
        h.sample.minFilter = h.sample.magFilter = TextureFilter::LINEAR;
        return h;
    }

    std::shared_ptr<Image> MakeImage() const {
        auto img    = std::make_shared<Image>();
        img->key    = tex;
        img->header = Header();
        img->slots.resize(1);
        auto& slot  = img->slots[0];
        slot.width  = width;
        slot.height = height;
        int w = width, h = height;
        for (const auto& level : mips) {
            ImageData data;
            data.width  = w;
            data.height = h;
            data.size   = (isize)level.size();
            data.data   = ImageDataPtr(new uint8_t[level.size()], [](uint8_t* p) {
                delete[] p;
            });
            std::memcpy(data.data.get(), level.data(), level.size());
            slot.mipmaps.push_back(std::move(data));
            w = std::max(1, w / 2), h = std::max(1, h / 2);
        }
        return img;
    }
};

} // namespace

struct TextLayers::Impl {
    mutable std::mutex                       mutex;
    std::map<int32_t, std::unique_ptr<Layer>> layers;
    std::map<std::string, Layer*>            by_tex;
};

TextLayers::TextLayers(): pImpl(std::make_unique<Impl>()) {}
TextLayers::~TextLayers() = default;

std::string TextLayers::Add(int32_t id, TextStyle style, const std::string& text,
                            std::array<float, 2>& size) {
    auto layer   = std::make_unique<Layer>();
    layer->id    = id;
    // A serial keeps names unique across scene reloads, so the texture cache never
    // returns a previous scene's texture.
    static std::atomic<uint64_t> serial { 0 };
    layer->tex   = "_wetext_" + std::to_string(++serial) + "_" + std::to_string(id);
    layer->style = std::move(style);
    layer->font  = Font::Load(std::make_shared<std::vector<uint8_t>>(std::move(layer->style.font)));
    if (! layer->font->ok)
        layer->systemFont = SystemFonts::Get().Family(
            layer->style.systemFont.empty() ? "sans-serif" : layer->style.systemFont);
    if (! layer->Primary()) LOG_ERROR("text layer %d: no usable font", id);

    // Measure in scene units first, then pick the texture resolution. The layer
    // is exactly as large as Wallpaper Engine makes it (effect masks map onto it);
    // text that outgrows it later is shrunk to fit.
    const auto& st     = layer->style;
    const float em     = std::max(st.pointsize, 1.0f) * UnitsPerPoint;
    Layout      l      = layer->DoLayout(text, em);
    const float unitsW = std::max(st.size[0], l.width + 2.0f * st.padding);
    const float unitsH = std::max(st.size[1], l.height + 2.0f * st.padding);
    size               = { std::max(unitsW, 1.0f), std::max(unitsH, 1.0f) };

    layer->pxPerUnit = std::min(1.0f, (float)MaxTextureSide / std::max(size[0], size[1]));
    layer->width     = std::max(1, (int32_t)std::ceil(size[0] * layer->pxPerUnit));
    layer->height    = std::max(1, (int32_t)std::ceil(size[1] * layer->pxPerUnit));
    layer->padPx     = st.padding * layer->pxPerUnit;
    layer->emPx      = em * layer->pxPerUnit;
    layer->Render(text);
    layer->dirty = false;

    std::lock_guard lock(pImpl->mutex);
    const std::string tex      = layer->tex;
    pImpl->by_tex[tex]         = layer.get();
    pImpl->layers[id]          = std::move(layer);
    return tex;
}

bool TextLayers::Contains(const std::string& tex) const {
    std::lock_guard lock(pImpl->mutex);
    return pImpl->by_tex.count(tex) > 0;
}

ImageHeader TextLayers::Header(const std::string& tex) const {
    std::lock_guard lock(pImpl->mutex);
    auto            it = pImpl->by_tex.find(tex);
    return it == pImpl->by_tex.end() ? ImageHeader {} : it->second->Header();
}

std::shared_ptr<Image> TextLayers::MakeImage(const std::string& tex) const {
    std::lock_guard lock(pImpl->mutex);
    auto            it = pImpl->by_tex.find(tex);
    return it == pImpl->by_tex.end() ? nullptr : it->second->MakeImage();
}

void TextLayers::SetText(int32_t id, const std::string& text) {
    std::lock_guard lock(pImpl->mutex);
    auto            it = pImpl->layers.find(id);
    if (it == pImpl->layers.end() || it->second->text == text) return;
    it->second->Render(text);
}

void TextLayers::TakeDirty(const std::function<void(Image&)>& fn) {
    std::vector<std::shared_ptr<Image>> images;
    {
        std::lock_guard lock(pImpl->mutex);
        for (auto& [id, layer] : pImpl->layers) {
            if (! layer->dirty) continue;
            layer->dirty = false;
            images.push_back(layer->MakeImage());
        }
    }
    for (auto& img : images) fn(*img);
}
