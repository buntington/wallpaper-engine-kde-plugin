#pragma once
#include <string>
#include <vector>
#include <Eigen/Dense>

namespace wallpaper
{
namespace fs
{
class VFS;
}

// Camera paths of 3D scenes (scene.json camera "paths", e.g. scripts/camera_00.json).
// Each path moves the camera through its keyframes over its duration; the paths of
// all files play one after another and loop.
class WPCameraPath {
public:
    struct Key {
        Eigen::Vector3d eye, center, up;
        double          time { 0 };
    };
    struct Path {
        double           duration { 0 };
        std::vector<Key> keys;
    };

    bool Load(const std::vector<std::string>& files, fs::VFS&);
    bool Empty() const { return m_paths.empty(); }

    // Camera at t seconds since the scene started.
    Key Sample(double t) const;
    // 1 at the cut between two paths, 0 more than `fade` seconds away from one.
    double FadeAlpha(double t, double fade) const;

private:
    // the path playing at t, and t within it
    const Path& Locate(double t, double& local) const;

    std::vector<Path> m_paths;
    double            m_total { 0 };
};

} // namespace wallpaper
