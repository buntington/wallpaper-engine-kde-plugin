#pragma once
#include <memory>

namespace wallpaper
{
namespace audio
{

// Spectrum of what the default output device is playing, for audio-reactive
// wallpapers. Capture runs only while at least one user holds it.
class AudioSpectrum {
public:
    static AudioSpectrum& Get();

    void Acquire();
    void Release();

    // Fills `bands` (16, 32 or 64) values per channel, each from 0 to 1.
    void Read(int bands, float* left, float* right) const;

    ~AudioSpectrum();

private:
    AudioSpectrum();
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace audio
} // namespace wallpaper
