#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace wallpaper
{
namespace fs
{
class VFS;
}
class SceneMesh;

// A 3D model (.mdl, "MDLV" container) used by scene "model" objects.
struct WPModel {
    // Vertex fields present, in this order (the header's format bits).
    enum Format : uint32_t
    {
        Position  = 1,  // 3 floats
        Normal    = 2,  // 3 floats
        Tangent   = 4,  // 4 floats
        TexCoord  = 8,  // 2 floats
        TexCoord4 = 32, // 4 floats: texture and lightmap coordinates
    };

    struct Mesh {
        std::vector<std::string> materials; // first one is used
        uint32_t                 format { 0 };
        std::vector<float>       vertices;  // as in the file, format-dependent stride
        std::vector<uint16_t>    indices;   // triangles
    };

    std::vector<Mesh> meshes;

    static uint32_t FloatsPerVertex(uint32_t format);
};

class WPModelParser {
public:
    static bool Parse(const std::string& path, fs::VFS&, WPModel&);
    // Builds a mesh with compact vertex formats; see the .cpp for the layout.
    static void GenMesh(SceneMesh&, const WPModel::Mesh&);
};

} // namespace wallpaper
