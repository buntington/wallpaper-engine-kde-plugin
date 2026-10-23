#include "SceneVertexArray.h"
#include <cstring>
#include <iostream>
#include <utility>
#include <algorithm>
#include <cassert>
#include <array>
#include <cmath>

using namespace wallpaper;

uint8_t SceneVertexArray::TypeCount(VertexType t) {
    switch (t) {
    case VertexType::FLOAT1:
    case VertexType::UINT1: return 1;
    case VertexType::FLOAT2:
    case VertexType::UINT2: return 2;
    case VertexType::FLOAT3:
    case VertexType::UINT3: return 3;
    case VertexType::FLOAT4:
    case VertexType::UINT4: return 4;
    // packed types: number of 32-bit words
    case VertexType::SNORM8x4:
    case VertexType::UNORM8x4:
    case VertexType::HALF2: return 1;
    case VertexType::HALF4: return 2;
    }
    return 1;
}

uint8_t SceneVertexArray::RealAttributeSize(const SceneVertexArray::SceneVertexAttribute& attr) {
    return attr.padding ? 4 : TypeCount(attr.type);
}

SceneVertexArray::SceneVertexArray(const std::vector<SceneVertexAttribute>& attrs,
                                   const usize                              count)
    : m_attributes(attrs) {
    for (const auto& el : m_attributes) {
        usize size = SceneVertexArray::RealAttributeSize(el);
        m_oneSize += size;
    }
    m_capacity = m_oneSize * count;
    m_pData    = new float[m_capacity];
    std::fill(m_pData, m_pData + m_capacity, 0.0f);
}

SceneVertexArray::~SceneVertexArray() {
    if (m_pData != nullptr) delete[] m_pData;
}
SceneVertexArray::SceneVertexArray(SceneVertexArray&& o) noexcept
    : m_attributes(o.m_attributes),
      m_pData(std::exchange(o.m_pData, nullptr)),
      m_oneSize(o.m_oneSize),
      m_size(o.m_size),
      m_capacity(o.m_capacity),
      m_id(o.m_id) {}

SceneVertexArray& SceneVertexArray::operator=(SceneVertexArray&& o) noexcept {
    m_attributes = o.m_attributes;
    m_pData      = std::exchange(o.m_pData, nullptr);
    m_oneSize    = o.m_oneSize;
    m_size       = o.m_size;
    m_capacity   = o.m_capacity;
    m_id         = o.m_id;
    return *this;
}

bool SceneVertexArray::AddVertex(const float* data) {
    if (m_size + m_oneSize >= m_capacity) return false;
    usize  pos   = 0;
    usize  mpos  = 0;
    float* mData = m_pData + m_size;
    for (const auto& el : m_attributes) {
        auto typeSize = SceneVertexArray::TypeCount(el.type);
        std::copy(data + pos, data + pos + typeSize, m_pData + mpos);
        pos += typeSize;
        mpos += SceneVertexArray::RealAttributeSize(el);
    }
    m_size += m_oneSize;
    return true;
}

bool SceneVertexArray::SetVertex(std::string_view name, std::span<const float> data) noexcept {
    u32 offset = 0;
    for (const auto& el : m_attributes) {
        if (el.name == name) {
            usize typeSize = SceneVertexArray::TypeCount(el.type);
            usize count    = data.size() / typeSize;
            if (! TrySetSize(count * m_oneSize)) return false;

            for (usize i = 0; i < data.size(); i += typeSize) {
                auto  start = data.begin() + (isize)i;
                usize num   = i / typeSize;
                std::copy(start, start + (isize)typeSize, m_pData + offset + num * m_oneSize);
            }
            return true;
        } else
            offset += RealAttributeSize(el);
    }
    return false;
}

bool SceneVertexArray::SetVertexs(usize index, std::span<const float> data) noexcept {
    usize start = index * m_oneSize;
    if (TrySetSize(start + data.size())) {
        std::copy(data.begin(), data.end(), m_pData + start);
        return true;
    }
    return false;
}

bool SceneVertexArray::SetAttribute(usize vertex, usize word_offset, VertexType type,
                                    std::span<const float> data) noexcept {
    const usize start = vertex * m_oneSize;
    if (! TrySetSize(start + m_oneSize)) return false;
    PackAttribute(type, data, m_pData + start + word_offset);
    return true;
}

void SceneVertexArray::PackAttribute(VertexType type, std::span<const float> data,
                                     float* dst) noexcept {
    const auto at = [&data](usize i) {
        return i < data.size() ? data[i] : 0.0f;
    };
    switch (type) {
    case VertexType::SNORM8x4:
    case VertexType::UNORM8x4: {
        std::array<uint8_t, 4> v {};
        for (usize i = 0; i < 4; i++) {
            if (type == VertexType::SNORM8x4)
                v[i] = (uint8_t)(int8_t)std::lround(std::clamp(at(i), -1.0f, 1.0f) * 127.0f);
            else
                v[i] = (uint8_t)std::lround(std::clamp(at(i), 0.0f, 1.0f) * 255.0f);
        }
        std::memcpy(dst, v.data(), 4);
        break;
    }
    case VertexType::HALF2:
    case VertexType::HALF4: {
        std::array<uint16_t, 4> v {};
        const usize             n = type == VertexType::HALF2 ? 2 : 4;
        for (usize i = 0; i < n; i++) v[i] = ToHalf(at(i));
        std::memcpy(dst, v.data(), n * sizeof(uint16_t));
        break;
    }
    default: std::copy_n(data.begin(), std::min<usize>(data.size(), TypeCount(type)), dst);
    }
}

// IEEE 754 binary16, round to nearest even; out-of-range values become infinity.
uint16_t SceneVertexArray::ToHalf(float value) noexcept {
    uint32_t f;
    std::memcpy(&f, &value, sizeof(f));
    const uint32_t sign = (f >> 16) & 0x8000u;
    const uint32_t exp  = (f >> 23) & 0xFFu;
    uint32_t       mant = f & 0x7FFFFFu;
    if (exp == 0xFFu) return (uint16_t)(sign | 0x7C00u | (mant ? 0x200u : 0u));
    const int32_t e = (int32_t)exp - 127 + 15;
    if (e >= 31) return (uint16_t)(sign | 0x7C00u);
    if (e <= 0) {
        if (e < -10) return (uint16_t)sign;
        mant |= 0x800000u;
        const uint32_t shift = (uint32_t)(14 - e);
        uint32_t       half  = mant >> shift;
        const uint32_t rest  = mant & ((1u << shift) - 1u);
        const uint32_t mid   = 1u << (shift - 1u);
        if (rest > mid || (rest == mid && (half & 1u))) half++;
        return (uint16_t)(sign | half);
    }
    uint32_t half = sign | ((uint32_t)e << 10) | (mant >> 13);
    const uint32_t rest = mant & 0x1FFFu;
    if (rest > 0x1000u || (rest == 0x1000u && (half & 1u))) half++;
    return (uint16_t)half;
}

bool SceneVertexArray::TrySetSize(usize new_size) noexcept {
    assert(new_size <= m_capacity);
    if (new_size > m_capacity) {
        return false;
    }
    if (new_size > m_size) m_size = new_size;
    return true;
}

Map<std::string, SceneVertexArray::SceneVertexAttributeOffset>
SceneVertexArray::GetAttrOffsetMap() const {
    Map<std::string, SceneVertexArray::SceneVertexAttributeOffset> result;
    usize                                                          offset { 0 };
    for (const auto& attr : m_attributes) {
        result[attr.name] = (SceneVertexAttributeOffset { .attr = attr, .offset = offset });
        offset += SceneVertexArray::RealAttributeSize(attr) * sizeof(float);
    }
    return result;
}

bool SceneVertexArray::GetOption(std::string_view name) const {
    return exists(m_options, name) && m_options.at(std::string(name));
}
void SceneVertexArray::SetOption(std::string_view name, bool value) {
    m_options[std::string(name)] = value;
}
