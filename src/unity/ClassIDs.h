#pragma once

#include "core/Types.h"
#include <string_view>

namespace coconut::unity {

enum class ClassID : i32 {
    Unknown                 = -1,
    Object                  = 0,
    GameObject              = 1,
    Component               = 2,
    LevelGameManager        = 3,
    Transform               = 4,
    TimeManager             = 5,
    GlobalGameManager       = 6,
    Behaviour               = 8,
    GameManager             = 9,
    AudioManager            = 11,
    InputManager            = 13,
    Camera                  = 20,
    Material                = 21,
    MeshRenderer            = 23,
    Renderer                = 25,
    Texture                 = 27,
    Texture2D               = 28,
    OcclusionCullingSettings = 29,
    GraphicsSettings        = 30,
    MeshFilter              = 33,
    Mesh                    = 43,
    Shader                  = 48,
    TextAsset               = 49,
    Rigidbody2D             = 50,
    Rigidbody               = 54,
    Collider                = 56,
    BoxCollider             = 65,
    AnimationClip           = 74,
    AudioBehaviour          = 81,
    AudioSource             = 82,
    AudioClip               = 83,
    RenderTexture           = 84,
    Cubemap                 = 89,
    Avatar                  = 90,
    AnimatorController      = 91,
    Animator                = 95,
    Light                   = 108,
    Animation               = 111,
    MonoBehaviour           = 114,
    MonoScript              = 115,
    Font                    = 128,
    PlayerSettings          = 129,
    SphereCollider          = 135,
    CapsuleCollider         = 136,
    SkinnedMeshRenderer     = 137,
    AssetBundle             = 142,
    BuildSettings           = 141,
    ResourceManager         = 147,
    TerrainData             = 156,
    LightmapSettings        = 157,
    ParticleSystem          = 198,
    ParticleSystemRenderer  = 199,
    SpriteRenderer          = 212,
    Sprite                  = 213,
    Terrain                 = 218,
    CanvasRenderer          = 222,
    Canvas                  = 223,
    RectTransform           = 224,
    NavMeshData             = 238,
    PrefabInstance          = 1001
};

enum class AssetCategory {
    Scene,
    Prefab,
    Script,
    Mesh,
    Texture,
    Material,
    Shader,
    Audio,
    Text,
    Animation,
    Component,
    Other
};

std::string_view ClassIDName(i32 classId);
AssetCategory ClassifyClassID(i32 classId);
std::string_view AssetCategoryName(AssetCategory cat);
bool IsComponentClassID(i32 classId);

} // namespace coconut::unity
