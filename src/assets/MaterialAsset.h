#pragma once

#include "core/Types.h"
#include "unity/SerializedFile.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace coconut::assets {

struct MaterialFloatProp {
    std::string name;
    f32 value = 0.0f;
};

struct MaterialColorProp {
    std::string name;
    glm::vec4 value{1.0f, 1.0f, 1.0f, 1.0f};
};

struct MaterialTexProp {
    std::string name;
    i32 fileId = 0;
    i64 pathId = 0;
};

struct MaterialData {
    std::string name;
    std::string shaderName = "Standard";
    i64 shaderPathId = 0;
    std::vector<MaterialFloatProp> floats;
    std::vector<MaterialColorProp> colors;
    std::vector<MaterialTexProp> textures;

    glm::vec4 PrimaryColor() const;
    void SetPrimaryColor(const glm::vec4& c);
    f32 GetFloat(std::string_view propName, f32 fallback = 0.0f) const;
    void SetFloat(const std::string& propName, f32 val);
};

class MaterialCodec {
public:
    static Result<MaterialData> ParseFromObject(const unity::SerializedFile& sf, const unity::ObjectInfo& obj);
    static Result<void> WriteBackToObject(unity::SerializedFile& sf, unity::ObjectInfo& obj, const MaterialData& mat);
};

} // namespace coconut::assets
