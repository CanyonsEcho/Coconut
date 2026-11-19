#include "assets/MaterialAsset.h"

namespace coconut::assets {

glm::vec4 MaterialData::PrimaryColor() const {
    for (const auto& c : colors) {
        if (c.name == "_Color" || c.name == "_BaseColor") return c.value;
    }
    if (!colors.empty()) return colors.front().value;
    return {0.72f, 0.75f, 0.80f, 1.0f};
}

void MaterialData::SetPrimaryColor(const glm::vec4& col) {
    for (auto& c : colors) {
        if (c.name == "_Color" || c.name == "_BaseColor") {
            c.value = col;
            return;
        }
    }
    colors.push_back({"_Color", col});
}

f32 MaterialData::GetFloat(std::string_view propName, f32 fallback) const {
    for (const auto& f : floats) {
        if (f.name == propName) return f.value;
    }
    return fallback;
}

void MaterialData::SetFloat(const std::string& propName, f32 val) {
    for (auto& f : floats) {
        if (f.name == propName) {
            f.value = val;
            return;
        }
    }
    floats.push_back({propName, val});
}

Result<MaterialData> MaterialCodec::ParseFromObject(const unity::SerializedFile& sf, const unity::ObjectInfo& obj) {
    auto valRes = sf.ReadObjectValue(obj);
    if (!valRes) return Result<MaterialData>::Err(valRes.Error());
    const auto& root = valRes.Value();

    MaterialData mat;
    mat.name = obj.cachedName;
    if (auto* f = root.FindField("m_Name")) mat.name = f->AsString(mat.name);
    if (auto* s = root.FindField("m_Shader")) {
        if (auto* pid = s->FindField("m_PathID")) mat.shaderPathId = pid->AsInt();
    }
    if (auto* sn = root.FindField("m_ShaderName")) mat.shaderName = sn->AsString(mat.shaderName);

    if (auto* sp = root.FindField("m_SavedProperties")) {
        if (auto* fl = sp->FindField("m_Floats")) {
            if (auto* list = std::get_if<std::vector<unity::TypeValue>>(&fl->value)) {
                for (const auto& pair : *list) {
                    auto* k = pair.FindField("first");
                    auto* v = pair.FindField("second");
                    if (k && v) mat.floats.push_back({k->AsString(), static_cast<f32>(v->AsFloat())});
                }
            }
        }
        if (auto* cl = sp->FindField("m_Colors")) {
            if (auto* list = std::get_if<std::vector<unity::TypeValue>>(&cl->value)) {
                for (const auto& pair : *list) {
                    auto* k = pair.FindField("first");
                    auto* v = pair.FindField("second");
                    if (k && v) {
                        glm::vec4 col{
                            static_cast<f32>(v->FindField("r") ? v->FindField("r")->AsFloat(1.0) : 1.0),
                            static_cast<f32>(v->FindField("g") ? v->FindField("g")->AsFloat(1.0) : 1.0),
                            static_cast<f32>(v->FindField("b") ? v->FindField("b")->AsFloat(1.0) : 1.0),
                            static_cast<f32>(v->FindField("a") ? v->FindField("a")->AsFloat(1.0) : 1.0)
                        };
                        mat.colors.push_back({k->AsString(), col});
                    }
                }
            }
        }
        if (auto* tx = sp->FindField("m_TexEnvs")) {
            if (auto* list = std::get_if<std::vector<unity::TypeValue>>(&tx->value)) {
                for (const auto& pair : *list) {
                    auto* k = pair.FindField("first");
                    auto* v = pair.FindField("second");
                    if (k && v) {
                        auto* tex = v->FindField("m_Texture");
                        i32 fid = tex && tex->FindField("m_FileID") ? static_cast<i32>(tex->FindField("m_FileID")->AsInt()) : 0;
                        i64 pid = tex && tex->FindField("m_PathID") ? tex->FindField("m_PathID")->AsInt() : 0;
                        mat.textures.push_back({k->AsString(), fid, pid});
                    }
                }
            }
        }
    }

    return Result<MaterialData>::Ok(std::move(mat));
}

Result<void> MaterialCodec::WriteBackToObject(unity::SerializedFile& sf, unity::ObjectInfo& obj, const MaterialData& mat) {
    auto valRes = sf.ReadObjectValue(obj);
    if (!valRes) return Result<void>::Err(valRes.Error());
    auto& root = valRes.Value();

    if (auto* f = root.FindField("m_Name")) f->value = mat.name;
    if (auto* sp = root.FindField("m_SavedProperties")) {
        if (auto* cl = sp->FindField("m_Colors")) {
            if (auto* list = std::get_if<std::vector<unity::TypeValue>>(&cl->value)) {
                for (auto& pair : *list) {
                    auto* k = pair.FindField("first");
                    auto* v = pair.FindField("second");
                    if (!k || !v) continue;
                    for (const auto& c : mat.colors) {
                        if (c.name == k->AsString()) {
                            if (auto* r = v->FindField("r")) r->value = static_cast<f64>(c.value.r);
                            if (auto* g = v->FindField("g")) g->value = static_cast<f64>(c.value.g);
                            if (auto* b = v->FindField("b")) b->value = static_cast<f64>(c.value.b);
                            if (auto* a = v->FindField("a")) a->value = static_cast<f64>(c.value.a);
                        }
                    }
                }
            }
        }
        if (auto* fl = sp->FindField("m_Floats")) {
            if (auto* list = std::get_if<std::vector<unity::TypeValue>>(&fl->value)) {
                for (auto& pair : *list) {
                    auto* k = pair.FindField("first");
                    auto* v = pair.FindField("second");
                    if (!k || !v) continue;
                    for (const auto& fp : mat.floats) {
                        if (fp.name == k->AsString()) {
                            v->value = static_cast<f64>(fp.value);
                        }
                    }
                }
            }
        }
    }
    return sf.WriteObjectValue(obj, root);
}

} // namespace coconut::assets
