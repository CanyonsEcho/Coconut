#include "unity/ClassIDs.h"

namespace coconut::unity {

std::string_view ClassIDName(i32 classId) {
    switch (static_cast<ClassID>(classId)) {
        case ClassID::Object:                  return "Object";
        case ClassID::GameObject:              return "GameObject";
        case ClassID::Component:               return "Component";
        case ClassID::LevelGameManager:        return "LevelGameManager";
        case ClassID::Transform:               return "Transform";
        case ClassID::TimeManager:             return "TimeManager";
        case ClassID::GlobalGameManager:       return "GlobalGameManager";
        case ClassID::Behaviour:               return "Behaviour";
        case ClassID::AudioManager:            return "AudioManager";
        case ClassID::InputManager:            return "InputManager";
        case ClassID::Camera:                  return "Camera";
        case ClassID::Material:                return "Material";
        case ClassID::MeshRenderer:            return "MeshRenderer";
        case ClassID::Renderer:                return "Renderer";
        case ClassID::Texture:                 return "Texture";
        case ClassID::Texture2D:               return "Texture2D";
        case ClassID::GraphicsSettings:        return "GraphicsSettings";
        case ClassID::MeshFilter:              return "MeshFilter";
        case ClassID::Mesh:                    return "Mesh";
        case ClassID::Shader:                  return "Shader";
        case ClassID::TextAsset:               return "TextAsset";
        case ClassID::Rigidbody2D:             return "Rigidbody2D";
        case ClassID::Rigidbody:               return "Rigidbody";
        case ClassID::Collider:                return "Collider";
        case ClassID::BoxCollider:             return "BoxCollider";
        case ClassID::AnimationClip:           return "AnimationClip";
        case ClassID::AudioSource:             return "AudioSource";
        case ClassID::AudioClip:               return "AudioClip";
        case ClassID::RenderTexture:           return "RenderTexture";
        case ClassID::Cubemap:                 return "Cubemap";
        case ClassID::Avatar:                  return "Avatar";
        case ClassID::AnimatorController:      return "AnimatorController";
        case ClassID::Animator:                return "Animator";
        case ClassID::Light:                   return "Light";
        case ClassID::Animation:               return "Animation";
        case ClassID::MonoBehaviour:           return "MonoBehaviour";
        case ClassID::MonoScript:              return "MonoScript";
        case ClassID::Font:                    return "Font";
        case ClassID::PlayerSettings:          return "PlayerSettings";
        case ClassID::SphereCollider:          return "SphereCollider";
        case ClassID::CapsuleCollider:         return "CapsuleCollider";
        case ClassID::SkinnedMeshRenderer:     return "SkinnedMeshRenderer";
        case ClassID::BuildSettings:           return "BuildSettings";
        case ClassID::AssetBundle:             return "AssetBundle";
        case ClassID::ResourceManager:         return "ResourceManager";
        case ClassID::TerrainData:             return "TerrainData";
        case ClassID::LightmapSettings:        return "LightmapSettings";
        case ClassID::ParticleSystem:          return "ParticleSystem";
        case ClassID::ParticleSystemRenderer:  return "ParticleSystemRenderer";
        case ClassID::SpriteRenderer:          return "SpriteRenderer";
        case ClassID::Sprite:                  return "Sprite";
        case ClassID::Terrain:                 return "Terrain";
        case ClassID::CanvasRenderer:          return "CanvasRenderer";
        case ClassID::Canvas:                  return "Canvas";
        case ClassID::RectTransform:           return "RectTransform";
        case ClassID::NavMeshData:             return "NavMeshData";
        case ClassID::PrefabInstance:          return "PrefabInstance";
        default:                               return "UnknownAsset";
    }
}

AssetCategory ClassifyClassID(i32 classId) {
    switch (static_cast<ClassID>(classId)) {
        case ClassID::GameObject:
        case ClassID::Transform:
        case ClassID::RectTransform:
        case ClassID::Camera:
        case ClassID::Light:
        case ClassID::MeshFilter:
        case ClassID::MeshRenderer:
        case ClassID::SkinnedMeshRenderer:
        case ClassID::BoxCollider:
        case ClassID::SphereCollider:
        case ClassID::CapsuleCollider:
        case ClassID::Rigidbody:
        case ClassID::Rigidbody2D:
        case ClassID::AudioSource:
        case ClassID::Animator:
        case ClassID::Canvas:
        case ClassID::CanvasRenderer:
        case ClassID::SpriteRenderer:
        case ClassID::MonoBehaviour:
            return AssetCategory::Component;
        case ClassID::PrefabInstance:
            return AssetCategory::Prefab;
        case ClassID::MonoScript:
            return AssetCategory::Script;
        case ClassID::Mesh:
            return AssetCategory::Mesh;
        case ClassID::Texture:
        case ClassID::Texture2D:
        case ClassID::RenderTexture:
        case ClassID::Cubemap:
        case ClassID::Sprite:
            return AssetCategory::Texture;
        case ClassID::Material:
            return AssetCategory::Material;
        case ClassID::Shader:
            return AssetCategory::Shader;
        case ClassID::AudioClip:
            return AssetCategory::Audio;
        case ClassID::TextAsset:
            return AssetCategory::Text;
        case ClassID::AnimationClip:
        case ClassID::AnimatorController:
        case ClassID::Avatar:
            return AssetCategory::Animation;
        default:
            return AssetCategory::Other;
    }
}

std::string_view AssetCategoryName(AssetCategory cat) {
    switch (cat) {
        case AssetCategory::Scene:     return "Scene";
        case AssetCategory::Prefab:    return "Prefab";
        case AssetCategory::Script:    return "Script";
        case AssetCategory::Mesh:      return "Mesh";
        case AssetCategory::Texture:   return "Texture";
        case AssetCategory::Material:  return "Material";
        case AssetCategory::Shader:    return "Shader";
        case AssetCategory::Audio:     return "Audio";
        case AssetCategory::Text:      return "Text";
        case AssetCategory::Animation: return "Animation";
        case AssetCategory::Component: return "Component";
        default:                       return "Other";
    }
}

bool IsComponentClassID(i32 classId) {
    return ClassifyClassID(classId) == AssetCategory::Component && classId != static_cast<i32>(ClassID::GameObject);
}

} // namespace coconut::unity
