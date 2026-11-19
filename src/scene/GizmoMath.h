#pragma once

#include <glm/glm.hpp>
#include <optional>

namespace coconut::scene {

enum class GizmoMode {
    Translate,
    Rotate,
    Scale
};

enum class GizmoAxis {
    None = 0,
    X = 1,
    Y = 2,
    Z = 3
};

struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 dir{0.0f, 0.0f, -1.0f};
};

struct OrbitCamera {
    glm::vec3 target{0.0f, 1.2f, 0.0f};
    float yawDeg = -38.0f;
    float pitchDeg = 24.0f;
    float distance = 12.5f;
    float fovDeg = 48.0f;
    float nearClip = 0.1f;
    float farClip = 500.0f;

    glm::vec3 Position() const;
    glm::mat4 ViewMatrix() const;
    glm::mat4 ProjectionMatrix(float aspect) const;
    Ray ScreenPointToRay(float normX, float normY, float aspect) const;

    void Orbit(float deltaYawDeg, float deltaPitchDeg);
    void Pan(float deltaRight, float deltaUp);
    void Zoom(float factor);
    void Fly(float forward, float right, float up);
    void Focus(glm::vec3 center, float radius = 2.5f);
};

class GizmoMath {
public:
    // Returns hit distance >= 0 if ray intersects AABB, or nullopt.
    static std::optional<float> IntersectRayAABB(const Ray& ray, glm::vec3 bmin, glm::vec3 bmax);

    // Returns closest distance between ray and line segment [a, b], plus parameter t along segment [0,1].
    static float DistanceRaySegment(const Ray& ray, glm::vec3 a, glm::vec3 b, float& outSegT);

    // Pick X/Y/Z axis handle on a gizmo centered at origin with handle length `size`.
    static GizmoAxis PickAxisHandle(const Ray& ray, glm::vec3 gizmoOrigin, float size);

    // Project a ray onto an axis line (for smooth 1D gizmo translation/scaling drag).
    static float ProjectRayOntoAxis(const Ray& ray, glm::vec3 axisOrigin, glm::vec3 axisDir);
};

} // namespace coconut::scene
