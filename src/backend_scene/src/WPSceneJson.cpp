#include "WPSceneJson.hpp"

#include <array>
#include <cmath>
#include <map>
#include <sstream>

using namespace wallpaper;
using json = nlohmann::json;

namespace
{

// Booleans compare as "1"/"0", whether stored as bool or as "true"/"false".
std::string ToStr(const json& v) {
    if (v.is_string()) {
        const auto s = v.get<std::string>();
        return s == "true" ? "1" : s == "false" ? "0" : s;
    }
    if (v.is_boolean()) return v.get<bool>() ? "1" : "0";
    return v.dump();
}

// Converts a user value to the type of the binding's default, so the parser reads it.
json CoerceLike(const json& value, const json& like) {
    try {
        if (like.is_boolean()) {
            if (value.is_string()) return value.get<std::string>() == "1" || value.get<std::string>() == "true";
            if (value.is_number()) return value.get<double>() != 0.0;
        } else if (like.is_number()) {
            if (value.is_string()) return std::stod(value.get<std::string>());
            if (value.is_boolean()) return value.get<bool>() ? 1 : 0;
        } else if (like.is_string() && ! value.is_string()) {
            return ToStr(value);
        }
    } catch (const std::exception&) {
        return like;
    }
    return value;
}

void ResolveUser(json& j, const json& props) {
    if (j.is_array()) {
        for (auto& el : j) ResolveUser(el, props);
        return;
    }
    if (! j.is_object()) return;

    if (j.contains("user")) {
        const json& user     = j.at("user");
        const json  def      = j.contains("value") ? j.at("value") : json();
        json        resolved = def;
        if (user.is_string()) {
            if (props.contains(user.get<std::string>()))
                resolved = CoerceLike(props.at(user.get<std::string>()), def);
        } else if (user.is_object() && user.contains("name") && user.at("name").is_string()) {
            const auto name = user.at("name").get<std::string>();
            if (props.contains(name)) {
                resolved = user.contains("condition")
                               ? json(ToStr(props.at(name)) == ToStr(user.at("condition")))
                               : CoerceLike(props.at(name), def);
            }
        }
        bool only_binding = true;
        for (auto& [key, _] : j.items())
            if (key != "user" && key != "value") only_binding = false;
        if (only_binding) {
            j = resolved;
        } else {
            j.erase("user");
            j["value"] = resolved;
        }
    }
    if (j.is_object() || j.is_array())
        for (auto& [_, v] : j.items()) ResolveUser(v, props);
}

// A property that may be plain or wrapped as {"value": ..., "script": ...}.
const json* PlainValue(const json& obj, const char* key) {
    if (! obj.contains(key)) return nullptr;
    const json& v = obj.at(key);
    if (v.is_object()) return v.contains("value") ? &v.at("value") : nullptr;
    return &v;
}

void SetPlainValue(json& obj, const char* key, json value) {
    if (obj.contains(key) && obj.at(key).is_object()) obj[key]["value"] = std::move(value);
    else obj[key] = std::move(value);
}

std::array<double, 3> ReadVec3(const json& obj, const char* key, std::array<double, 3> def) {
    const json* v = PlainValue(obj, key);
    if (! v) return def;
    if (v->is_number()) return { v->get<double>(), v->get<double>(), v->get<double>() };
    if (! v->is_string()) return def;
    std::istringstream in(v->get<std::string>());
    for (auto& d : def) {
        double x;
        if (in >> x) d = x;
    }
    return def;
}

std::string Vec3Str(const std::array<double, 3>& v) {
    std::ostringstream out;
    out.precision(9);
    out << v[0] << ' ' << v[1] << ' ' << v[2];
    return out.str();
}

bool ReadVisible(const json& obj) {
    const json* v = PlainValue(obj, "visible");
    return ! v || ! v->is_boolean() || v->get<bool>();
}

// 2D affine transform plus the parts that stay per axis.
struct Transform {
    double a { 1 }, b { 0 }, c { 0 }, d { 1 }, tx { 0 }, ty { 0 };
    double z { 0 }, sz { 1 };
    bool   visible { true };
};

class Flattener {
public:
    explicit Flattener(json& objects) {
        for (auto& obj : objects)
            if (obj.is_object() && obj.contains("id")) m_objects[obj.at("id").dump()] = &obj;
    }

    void Run() {
        std::map<json*, Transform> worlds;
        for (auto& [_, obj] : m_objects)
            if (obj->contains("parent")) worlds[obj] = World(*obj, 0);
        for (auto& [obj, w] : worlds) Write(*obj, w);
    }

private:
    static Transform Local(const json& obj) {
        const auto o = ReadVec3(obj, "origin", { 0, 0, 0 });
        const auto s = ReadVec3(obj, "scale", { 1, 1, 1 });
        const auto r = ReadVec3(obj, "angles", { 0, 0, 0 });
        Transform  t;
        const double cos = std::cos(r[2]), sin = std::sin(r[2]);
        t.a = cos * s[0], t.b = sin * s[0];
        t.c = -sin * s[1], t.d = cos * s[1];
        t.tx = o[0], t.ty = o[1], t.z = o[2], t.sz = s[2];
        t.visible = ReadVisible(obj);
        return t;
    }

    Transform World(const json& obj, int depth) {
        Transform local = Local(obj);
        if (depth > 64 || ! obj.contains("parent")) return local;
        auto it = m_objects.find(obj.at("parent").dump());
        if (it == m_objects.end()) return local;
        const Transform p = World(*it->second, depth + 1);
        Transform       w;
        w.a       = p.a * local.a + p.c * local.b;
        w.b       = p.b * local.a + p.d * local.b;
        w.c       = p.a * local.c + p.c * local.d;
        w.d       = p.b * local.c + p.d * local.d;
        w.tx      = p.a * local.tx + p.c * local.ty + p.tx;
        w.ty      = p.b * local.tx + p.d * local.ty + p.ty;
        w.z       = p.z + local.z;
        w.sz      = p.sz * local.sz;
        w.visible = p.visible && local.visible;
        return w;
    }

    static void Write(json& obj, const Transform& w) {
        const auto   angles = ReadVec3(obj, "angles", { 0, 0, 0 });
        const double sx     = std::hypot(w.a, w.b);
        const double sy     = sx > 0 ? (w.a * w.d - w.b * w.c) / sx : std::hypot(w.c, w.d);
        SetPlainValue(obj, "origin", Vec3Str({ w.tx, w.ty, w.z }));
        SetPlainValue(obj, "scale", Vec3Str({ sx, sy, w.sz }));
        SetPlainValue(obj, "angles", Vec3Str({ angles[0], angles[1], std::atan2(w.b, w.a) }));
        if (! w.visible) SetPlainValue(obj, "visible", false);
        obj.erase("parent");
    }

    std::map<std::string, json*> m_objects;
};

} // namespace

void wallpaper::PreprocessSceneJson(json& scene, const json& userProperties,
                                    const json& textValues) {
    if (! scene.is_object()) return;
    if (userProperties.is_object() && ! userProperties.empty()) ResolveUser(scene, userProperties);
    if (! scene.contains("objects") || ! scene.at("objects").is_array()) return;

    auto& objects = scene["objects"];
    Flattener(objects).Run();

    if (! textValues.is_object()) return;
    for (auto& obj : objects) {
        if (! obj.is_object() || ! obj.contains("text") || ! obj.contains("id")) continue;
        const std::string id = ToStr(obj.at("id"));
        if (! textValues.contains(id) || ! textValues.at(id).is_string()) continue;
        SetPlainValue(obj, "text", textValues.at(id));
    }
}
