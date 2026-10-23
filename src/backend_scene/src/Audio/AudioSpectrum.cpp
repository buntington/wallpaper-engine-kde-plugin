#include "AudioSpectrum.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "Utils/Logging.h"
#include "miniaudio-config.h"

using namespace wallpaper::audio;

namespace
{

constexpr int    FftSize    = 2048;
constexpr int    Hop        = 512;
constexpr int    Bands      = 64;
constexpr double SampleRate = 48000.0;
constexpr double MinFreq    = 20.0;
constexpr double MaxFreq    = 16000.0;
// Per analysis step (about 11 ms); values fall to a fifth in roughly 100 ms.
constexpr float Decay = 0.86f;

void Fft(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float               ang = -2.0f * (float)M_PI / (float)len;
        const std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f);
            for (size_t j = 0; j < len / 2; j++) {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j]           = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

} // namespace

struct AudioSpectrum::Impl {
    std::mutex user_mutex;
    int        users { 0 };
    ma_context context {};
    ma_device  device {};
    bool       context_ok { false };
    bool       device_ok { false };

    // Capture thread state.
    std::array<std::vector<float>, 2> ring { std::vector<float>(FftSize), std::vector<float>(FftSize) };
    size_t                            pos { 0 };
    int                               since_fft { 0 };
    std::vector<float>                window = MakeWindow();
    std::vector<std::complex<float>>  fft = std::vector<std::complex<float>>(FftSize);

    mutable std::mutex                       data_mutex;
    std::array<std::array<float, Bands>, 2> bands {};

    static std::vector<float> MakeWindow() {
        std::vector<float> w(FftSize);
        for (int i = 0; i < FftSize; i++)
            w[(size_t)i] = 0.5f - 0.5f * std::cos(2.0f * (float)M_PI * (float)i / (FftSize - 1));
        return w;
    }

    static void Callback(ma_device* dev, void*, const void* input, ma_uint32 frames) {
        static_cast<Impl*>(dev->pUserData)->OnFrames(static_cast<const float*>(input), frames);
    }

    void OnFrames(const float* in, ma_uint32 frames) {
        if (! in) return;
        for (ma_uint32 f = 0; f < frames; f++) {
            ring[0][pos] = in[f * 2];
            ring[1][pos] = in[f * 2 + 1];
            pos          = (pos + 1) % FftSize;
            if (++since_fft >= Hop) {
                since_fft = 0;
                Analyze();
            }
        }
    }

    void Analyze() {
        std::array<std::array<float, Bands>, 2> next {};
        const double bin_hz = SampleRate / FftSize;
        for (size_t ch = 0; ch < 2; ch++) {
            for (size_t i = 0; i < (size_t)FftSize; i++)
                fft[i] = { ring[ch][(pos + i) % FftSize] * window[i], 0.0f };
            Fft(fft);
            for (int b = 0; b < Bands; b++) {
                const double lo = MinFreq * std::pow(MaxFreq / MinFreq, (double)b / Bands);
                const double hi = MinFreq * std::pow(MaxFreq / MinFreq, (double)(b + 1) / Bands);
                const int    k0 = std::max(1, (int)(lo / bin_hz));
                const int    k1 = std::max(k0 + 1, (int)(hi / bin_hz));
                float        peak = 0.0f;
                for (int k = k0; k < k1 && k < FftSize / 2; k++)
                    peak = std::max(peak, std::abs(fft[(size_t)k]) / (FftSize / 4.0f));
                // -60 dB .. 0 dB mapped to 0 .. 1
                const float db = 20.0f * std::log10(peak + 1e-9f);
                next[ch][(size_t)b] = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
            }
        }
        std::lock_guard lock(data_mutex);
        for (size_t ch = 0; ch < 2; ch++)
            for (size_t b = 0; b < (size_t)Bands; b++)
                bands[ch][b] = std::max(next[ch][b], bands[ch][b] * Decay);
    }

    // Monitor source of the default output, or WEKDE_AUDIO_SOURCE when set.
    bool FindMonitor(ma_device_id& id) {
        ma_device_info *playback = nullptr, *capture = nullptr;
        ma_uint32       nplayback = 0, ncapture = 0;
        if (ma_context_get_devices(&context, &playback, &nplayback, &capture, &ncapture) != MA_SUCCESS)
            return false;
        std::string wanted;
        if (const char* env = std::getenv("WEKDE_AUDIO_SOURCE")) wanted = env;
        for (ma_uint32 i = 0; wanted.empty() && i < nplayback; i++)
            if (playback[i].isDefault) wanted = std::string(playback[i].id.pulse) + ".monitor";
        const ma_device_info* found = nullptr;
        for (ma_uint32 i = 0; i < ncapture; i++) {
            const std::string name = capture[i].id.pulse;
            if (name == wanted) found = &capture[i];
            else if (! found && name.size() > 8 && name.ends_with(".monitor")) found = &capture[i];
        }
        if (! found) return false;
        LOG_INFO("audio spectrum source: %s", found->id.pulse);
        id = found->id;
        return true;
    }

    void Start() {
        const ma_backend backends[] = { ma_backend_pulseaudio };
        context_ok = ma_context_init(backends, 1, nullptr, &context) == MA_SUCCESS;
        ma_device_id id {};
        if (! context_ok || ! FindMonitor(id)) {
            LOG_ERROR("audio spectrum: no output monitor found");
            return;
        }
        ma_device_config config         = ma_device_config_init(ma_device_type_capture);
        config.capture.pDeviceID        = &id;
        config.capture.format           = ma_format_f32;
        config.capture.channels         = 2;
        config.sampleRate               = (ma_uint32)SampleRate;
        config.periodSizeInFrames       = Hop;
        config.dataCallback             = Callback;
        config.pUserData                = this;
        device_ok = ma_device_init(&context, &config, &device) == MA_SUCCESS;
        if (! device_ok || ma_device_start(&device) != MA_SUCCESS)
            LOG_ERROR("audio spectrum: can't start capture");
    }

    void Stop() {
        if (device_ok) ma_device_uninit(&device);
        if (context_ok) ma_context_uninit(&context);
        device_ok = context_ok = false;
        std::lock_guard lock(data_mutex);
        bands = {};
    }
};

AudioSpectrum& AudioSpectrum::Get() {
    static AudioSpectrum instance;
    return instance;
}

AudioSpectrum::AudioSpectrum(): pImpl(std::make_unique<Impl>()) {}
AudioSpectrum::~AudioSpectrum() {
    std::lock_guard lock(pImpl->user_mutex);
    if (pImpl->users > 0) pImpl->Stop();
}

void AudioSpectrum::Acquire() {
    std::lock_guard lock(pImpl->user_mutex);
    if (pImpl->users++ == 0) pImpl->Start();
}

void AudioSpectrum::Release() {
    std::lock_guard lock(pImpl->user_mutex);
    if (pImpl->users > 0 && --pImpl->users == 0) pImpl->Stop();
}

void AudioSpectrum::Read(int bands, float* left, float* right) const {
    const int group = bands >= Bands ? 1 : bands >= 32 ? 2 : 4;
    const int count = Bands / group;
    std::lock_guard lock(pImpl->data_mutex);
    for (int i = 0; i < count; i++) {
        // Peak of the merged bands, so a narrow tone is not diluted.
        float l = 0.0f, r = 0.0f;
        for (int g = 0; g < group; g++) {
            l = std::max(l, pImpl->bands[0][(size_t)(i * group + g)]);
            r = std::max(r, pImpl->bands[1][(size_t)(i * group + g)]);
        }
        if (left) left[i] = l;
        if (right) right[i] = r;
    }
}
