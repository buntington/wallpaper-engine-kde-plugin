// Build: g++ -std=c++20 -Isrc/backend_scene/src -Isrc/backend_scene/third_party/nlohmann/single_include \
//          devtest/json_test.cpp src/backend_scene/src/WPSceneJson.cpp -o /tmp/json_test && /tmp/json_test
#include "WPSceneJson.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <sstream>
using json = nlohmann::json;
static std::array<double,3> vec(const json& v) { std::istringstream in(v.is_object() ? v["value"].get<std::string>() : v.get<std::string>()); std::array<double,3> a{}; in >> a[0] >> a[1] >> a[2]; return a; }
static bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }
int main() {
    json scene = json::parse(R"({
      "objects": [
        {"id": 1, "name": "parent", "origin": "100 50 0", "scale": "2 3 1", "angles": "0 0 1.5707963267948966",
         "visible": {"user": "showparent", "value": true}},
        {"id": 2, "name": "child", "parent": 1, "origin": "10 0 0", "scale": "0.5 0.5 1", "angles": "0 0 0",
         "text": {"value": "12:34", "script": "x"}},
        {"id": 3, "name": "cond", "visible": {"user": {"name": "style", "condition": "1"}, "value": false}},
        {"id": 4, "name": "boolcond", "visible": {"user": {"name": "flag", "condition": "true"}, "value": false}},
        {"id": 5, "name": "coerce", "alpha": {"user": "amount", "value": 0.5},
         "color": {"user": "tint", "value": "1 1 1"}, "keep": {"user": "missing", "value": 7}},
        {"id": 6, "name": "scripted-origin", "parent": 1, "origin": {"script": "y", "value": "0 10 0"}}
      ]})");
    json props = {{"showparent", false}, {"style", "1"}, {"flag", true}, {"amount", "0.25"}, {"tint", "0.1 0.2 0.3"}};
    wallpaper::PreprocessSceneJson(scene, props, {{"2", "-21:30-"}});
    auto& o = scene["objects"];
    // parent at (100,50), scale (2,3), rotated 90 degrees: child local x=10 lands at (100, 50 + 2*10)
    auto c = vec(o[1]["origin"]);
    assert(near(c[0], 100) && near(c[1], 70));
    auto s = vec(o[1]["scale"]);
    assert(near(s[0], 1.0) && near(s[1], 1.5));
    assert(near(vec(o[1]["angles"])[2], 1.5707963267948966));
    assert(! o[1].contains("parent"));
    assert(o[0]["visible"] == false);             // user binding resolved
    assert(o[1]["visible"] == false);             // hidden with its parent
    assert(o[1]["text"]["value"] == "-21:30-");   // text value set, script kept
    assert(o[1]["text"]["script"] == "x");
    assert(o[2]["visible"] == true);              // combo condition met
    assert(o[3]["visible"] == true);              // "true" condition matches a bool property
    assert(near(o[4]["alpha"].get<double>(), 0.25)); // string coerced to the default's type
    assert(o[4]["color"] == "0.1 0.2 0.3");
    assert(o[4]["keep"] == 7);                    // unknown property keeps its default
    auto so = vec(o[5]["origin"]);                // scripted value is flattened in place
    assert(near(so[0], 100 - 3 * 10) && near(so[1], 50) && o[5]["origin"]["script"] == "y");
    std::puts("all preprocessing checks passed");
}
