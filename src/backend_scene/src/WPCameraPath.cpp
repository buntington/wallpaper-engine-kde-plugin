#include "WPCameraPath.hpp"

#include <algorithm>
#include <cmath>

#include "Fs/VFS.h"
#include <nlohmann/json.hpp>
#include "WPJson.hpp"
#include "Utils/Logging.h"

using namespace wallpaper;
using Eigen::Vector3d;

namespace
{
Vector3d ToVec(const std::array<float, 3>& v) { return { v[0], v[1], v[2] }; }
} // namespace

bool WPCameraPath::Load(const std::vector<std::string>& files, fs::VFS& vfs) {
    for (const auto& file : files) {
        nlohmann::json json;
        if (! PARSE_JSON(fs::GetFileContent(vfs, "/assets/" + file), json) ||
            ! json.contains("paths")) {
            LOG_ERROR("can't load camera path %s", file.c_str());
            continue;
        }
        for (const auto& jPath : json.at("paths")) {
            Path path;
            GET_JSON_NAME_VALUE_NOWARN(jPath, "duration", path.duration);
            if (! jPath.contains("transforms")) continue;
            for (const auto& jKey : jPath.at("transforms")) {
                std::array<float, 3> eye { 0, 0, 1 }, center { 0, 0, 0 }, up { 0, 1, 0 };
                Key                  key;
                GET_JSON_NAME_VALUE_NOWARN(jKey, "eye", eye);
                GET_JSON_NAME_VALUE_NOWARN(jKey, "center", center);
                GET_JSON_NAME_VALUE_NOWARN(jKey, "up", up);
                GET_JSON_NAME_VALUE_NOWARN(jKey, "timestamp", key.time);
                key.eye    = ToVec(eye);
                key.center = ToVec(center);
                key.up     = ToVec(up);
                path.keys.push_back(key);
            }
            if (path.keys.empty()) continue;
            std::sort(path.keys.begin(), path.keys.end(), [](const Key& a, const Key& b) {
                return a.time < b.time;
            });
            path.duration = std::max(path.duration, path.keys.back().time);
            m_total += path.duration;
            m_paths.push_back(std::move(path));
        }
    }
    return ! m_paths.empty();
}

WPCameraPath::Key WPCameraPath::Sample(double t) const {
    // pick the path playing at t; paths of zero duration only show their first key
    const Path* path = &m_paths.front();
    if (m_total > 0) {
        t = std::fmod(std::max(t, 0.0), m_total);
        for (const auto& p : m_paths) {
            path = &p;
            if (t < p.duration) break;
            t -= p.duration;
        }
    }
    const auto& keys = path->keys;
    auto next = std::upper_bound(keys.begin(), keys.end(), t, [](double v, const Key& k) {
        return v < k.time;
    });
    if (next == keys.begin()) return keys.front();
    if (next == keys.end()) return keys.back();

    const Key&   a = *(next - 1);
    const Key&   b = *next;
    const double f = (t - a.time) / (b.time - a.time);
    Key          k;
    k.eye    = a.eye + (b.eye - a.eye) * f;
    k.center = a.center + (b.center - a.center) * f;
    k.up     = (a.up + (b.up - a.up) * f).normalized();
    k.time   = t;
    return k;
}
