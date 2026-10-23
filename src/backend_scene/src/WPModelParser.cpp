#include "WPModelParser.hpp"

#include <algorithm>
#include <cstring>

#include "Fs/VFS.h"
#include "Scene/SceneMesh.h"
#include "SpecTexs.hpp"
#include "Utils/Logging.h"

using namespace wallpaper;

/*
    MDLV layout (versions 4, 14 and 23 seen):
    "MDLVxxxx", u8 pad, u32 format bits, u16 materials per mesh, u16 flags, u32 mesh count
    each mesh:
        material paths (null-terminated, materials-per-mesh of them), u32
        [v23+: 6 floats bounding box, u32 format bits]
        u32 vertex bytes, vertices, u32 index bytes, u16 indices
        [v23+: 6 bytes]
    one zero byte at the end
*/

namespace
{

class Reader {
public:
    explicit Reader(const std::string& data): m_data(data) {}

    bool Has(size_t n) const { return m_pos + n <= m_data.size(); }
    void Skip(size_t n) { m_pos = std::min(m_pos + n, m_data.size()); }

    template<typename T>
    bool Read(T& out) {
        if (! Has(sizeof(T))) return false;
        std::memcpy(&out, m_data.data() + m_pos, sizeof(T));
        m_pos += sizeof(T);
        return true;
    }
    bool ReadString(std::string& out) {
        const auto end = m_data.find('\0', m_pos);
        if (end == std::string::npos) return false;
        out   = m_data.substr(m_pos, end - m_pos);
        m_pos = end + 1;
        return true;
    }
    bool ReadBytes(void* dst, size_t n) {
        if (! Has(n)) return false;
        std::memcpy(dst, m_data.data() + m_pos, n);
        m_pos += n;
        return true;
    }

private:
    const std::string& m_data;
    size_t             m_pos { 0 };
};

bool InUnitRange(const std::vector<float>& v, size_t stride, size_t offset, size_t count) {
    for (size_t i = offset; i < v.size(); i += stride)
        for (size_t c = 0; c < count; c++)
            if (v[i + c] < 0.0f || v[i + c] > 1.0f) return false;
    return true;
}

} // namespace

uint32_t WPModel::FloatsPerVertex(uint32_t format) {
    uint32_t n = 0;
    if (format & Position) n += 3;
    if (format & Normal) n += 3;
    if (format & Tangent) n += 4;
    if (format & TexCoord) n += 2;
    if (format & TexCoord4) n += 4;
    return n;
}

bool WPModelParser::Parse(const std::string& path, fs::VFS& vfs, WPModel& model) {
    const std::string data = fs::GetFileContent(vfs, "/assets/" + path);
    Reader            r(data);

    char magic[8] {};
    if (! r.ReadBytes(magic, 8) || std::memcmp(magic, "MDLV", 4) != 0) {
        LOG_ERROR("not a model: %s", path.c_str());
        return false;
    }
    const int version = std::atoi(std::string(magic + 4, 4).c_str());

    uint32_t format { 0 }, mesh_count { 0 };
    uint16_t materials_per_mesh { 0 }, flags { 0 };
    r.Skip(1);
    if (! (r.Read(format) && r.Read(materials_per_mesh) && r.Read(flags) && r.Read(mesh_count)))
        return false;
    if (mesh_count > 4096 || materials_per_mesh > 16) {
        LOG_ERROR("model %s: unexpected header", path.c_str());
        return false;
    }

    for (uint32_t m = 0; m < mesh_count; m++) {
        WPModel::Mesh mesh;
        mesh.materials.resize(std::max<uint16_t>(materials_per_mesh, 1));
        for (auto& mat : mesh.materials)
            if (! r.ReadString(mat)) return false;
        r.Skip(4);
        mesh.format = format;
        if (version >= 23) {
            r.Skip(24); // bounding box
            if (! r.Read(mesh.format)) return false;
        }

        uint32_t vertex_bytes { 0 }, index_bytes { 0 };
        const uint32_t floats = WPModel::FloatsPerVertex(mesh.format);
        if (! r.Read(vertex_bytes) || floats == 0 || ! (mesh.format & WPModel::Position) ||
            vertex_bytes % (floats * 4) != 0 || ! r.Has(vertex_bytes)) {
            LOG_ERROR("model %s: unsupported vertex format %u", path.c_str(), mesh.format);
            return false;
        }
        mesh.vertices.resize(vertex_bytes / 4);
        r.ReadBytes(mesh.vertices.data(), vertex_bytes);

        if (! r.Read(index_bytes) || index_bytes % 6 != 0 || ! r.Has(index_bytes)) return false;
        mesh.indices.resize(index_bytes / 2);
        r.ReadBytes(mesh.indices.data(), index_bytes);
        if (version >= 23) r.Skip(6);

        const size_t vertex_count = mesh.vertices.size() / floats;
        if (std::any_of(mesh.indices.begin(), mesh.indices.end(), [vertex_count](uint16_t i) {
                return i >= vertex_count;
            })) {
            LOG_ERROR("model %s: index out of range", path.c_str());
            return false;
        }
        model.meshes.push_back(std::move(mesh));
    }
    return true;
}

// Vertex layout, packed: position float3, normal and tangent snorm16x4, texture
// coordinates unorm16 when within [0, 1] (otherwise float). 32 bytes for a
// typical vertex instead of 64 with padded floats.
void WPModelParser::GenMesh(SceneMesh& mesh, const WPModel::Mesh& src) {
    const uint32_t floats = WPModel::FloatsPerVertex(src.format);
    const size_t   count  = src.vertices.size() / floats;

    struct Field {
        std::string_view name;
        size_t           offset; // in the file's vertex, floats
        size_t           size;   // floats
        VertexType       type;
    };
    std::vector<Field> fields;
    size_t             offset = 0;
    fields.push_back({ WE_IN_POSITION, offset, 3, VertexType::FLOAT3 });
    offset += 3;
    if (src.format & WPModel::Normal) {
        fields.push_back({ WE_IN_NORMAL, offset, 3, VertexType::SNORM16x4 });
        offset += 3;
    }
    if (src.format & WPModel::Tangent) {
        fields.push_back({ WE_IN_TANGENT4, offset, 4, VertexType::SNORM16x4 });
        offset += 4;
    }
    if (src.format & WPModel::TexCoord) {
        const bool unit = InUnitRange(src.vertices, floats, offset, 2);
        fields.push_back(
            { WE_IN_TEXCOORD, offset, 2, unit ? VertexType::UNORM16x2 : VertexType::FLOAT2 });
        offset += 2;
    }
    if (src.format & WPModel::TexCoord4) {
        const bool unit = InUnitRange(src.vertices, floats, offset, 4);
        fields.push_back(
            { WE_IN_TEXCOORDVEC4, offset, 4, unit ? VertexType::UNORM16x4 : VertexType::FLOAT4 });
        // shaders without a lightmap read the first pair as a_TexCoord
        if (! (src.format & WPModel::TexCoord))
            fields.push_back(
                { WE_IN_TEXCOORD, offset, 2, unit ? VertexType::UNORM16x2 : VertexType::FLOAT2 });
        offset += 4;
    }

    std::vector<SceneVertexArray::SceneVertexAttribute> attrs;
    for (const auto& f : fields) attrs.push_back({ std::string(f.name), f.type, false });
    SceneVertexArray vertex(attrs, count);

    const auto offsets = vertex.GetAttrOffsetMap();
    for (size_t v = 0; v < count; v++) {
        const float* in = src.vertices.data() + v * floats;
        for (const auto& f : fields) {
            const auto& o = offsets.at(std::string(f.name));
            vertex.SetAttribute(v, o.offset / sizeof(float), f.type, { in + f.offset, f.size });
        }
    }

    // Index arrays hold pairs of u16 in u32 words.
    std::vector<uint32_t> indices((src.indices.size() + 1) / 2, 0);
    std::memcpy(indices.data(), src.indices.data(), src.indices.size() * sizeof(uint16_t));

    mesh.AddVertexArray(std::move(vertex));
    mesh.AddIndexArray(SceneIndexArray(indices));
}
