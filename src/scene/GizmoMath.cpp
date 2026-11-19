#include "scene/GizmoMath.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace coconut::scene {

glm::vec3 OrbitCamera::Position() const {
    float yaw = glm::radians(yawDeg);
    float pitch = glm::radians(pitchDeg);
    float cp = std::cos(pitch);
    glm::vec3 offset{
        std::sin(yaw) * cp * distance,
        std::sin(pitch) * distance,
        std::cos(yaw) * cp * distance
    };
    return target + offset;
}

glm::mat4 OrbitCamera::ViewMatrix() const {
    return glm::lookAt(Position(), target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 OrbitCamera::ProjectionMatrix(float aspect) const {
    return glm::perspective(glm::radians(fovDeg), std::max(0.1f, aspect), nearClip, farClip);
}

Ray OrbitCamera::ScreenPointToRay(float normX, float normY, float aspect) const {
    // normX, normY in [0, 1] top-left origin
    float ndcX = normX * 2.0f - 1.0f;
    float ndcY = 1.0f - normY * 2.0f;
    glm::mat4 invVP = glm::inverse(ProjectionMatrix(aspect) * ViewMatrix());
    glm::vec4 nearP = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farP  = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    nearP /= nearP.w;
    farP  /= farP.w;
    return Ray{Position(), glm::normalize(glm::vec3(farP - nearP))};
}

void OrbitCamera::Orbit(float deltaYawDeg, float deltaPitchDeg) {
    yawDeg += deltaYawDeg;
    pitchDeg = std::clamp(pitchDeg + deltaPitchDeg, -85.0f, 85.0f);
}

void OrbitCamera::Pan(float deltaRight, float deltaUp) {
    glm::vec3 pos = Position();
    glm::vec3 fwd = glm::normalize(target - pos);
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 up = glm::normalize(glm::cross(right, fwd));
    float scale = distance * 0.0022f;
    target += (-right * deltaRight + up * deltaUp) * scale;
}

void OrbitCamera::Zoom(float factor) {
    distance = std::clamp(distance * factor, 1.2f, 200.0f);
}

void OrbitCamera::Fly(float forward, float rightAmt, float upAmt) {
    glm::vec3 pos = Position();
    glm::vec3 fwd = glm::normalize(target - pos);
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
    target += fwd * forward + right * rightAmt + glm::vec3(0.0f, 1.0f, 0.0f) * upAmt;
}

void OrbitCamera::Focus(glm::vec3 center, float radius) {
    target = center;
    distance = std::clamp(radius * 3.0f, 2.5f, 80.0f);
}

std::optional<float> GizmoMath::IntersectRayAABB(const Ray& ray, glm::vec3 bmin, glm::vec3 bmax) {
    float tmin = 0.0f;
    float tmax = 1e9f;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(ray.dir[i]) < 1e-7f) {
            if (ray.origin[i] < bmin[i] || ray.origin[i] > bmax[i]) return std::nullopt;
        } else {
            float invD = 1.0f / ray.dir[i];
            float t1 = (bmin[i] - ray.origin[i]) * invD;
            float t2 = (bmax[i] - ray.origin[i]) * invD;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return std::nullopt;
        }
    }
    return tmin;
}

float GizmoMath::DistanceRaySegment(const Ray& ray, glm::vec3 a, glm::vec3 b, float& outSegT) {
    glm::vec3 u = ray.dir;
    glm::vec3 v = b - a;
    glm::vec3 w = ray.origin - a;
    float aa = glm::dot(u, u);
    float bb = glm::dot(u, v);
    float cc = glm::dot(v, v);
    float dd = glm::dot(u, w);
    float ee = glm::dot(v, w);
    float denom = aa * cc - bb * bb;

    float sc = 0.0f, tc = 0.0f;
    if (denom < 1e-6f) {
        tc = (cc > 1e-6f) ? std::clamp(ee / cc, 0.0f, 1.0f) : 0.0f;
        sc = std::max(0.0f, (bb * tc - dd) / std::max(1e-6f, aa));
    } else {
        sc = std::max(0.0f, (bb * ee - cc * dd) / denom);
        tc = std::clamp((aa * ee - bb * dd) / denom, 0.0f, 1.0f);
        sc = std::max(0.0f, (bb * tc - dd) / aa);
    }

    outSegT = tc;
    glm::vec3 diff = (ray.origin + u * sc) - (a + v * tc);
    return glm::length(diff);
}

GizmoAxis GizmoMath::PickAxisHandle(const Ray& ray, glm::vec3 gizmoOrigin, float size) {
    float threshold = size * 0.16f;
    float bestDist = threshold;
    GizmoAxis best = GizmoAxis::None;

    const glm::vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const GizmoAxis ids[3]  = {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};
    for (int i = 0; i < 3; ++i) {
        float segT = 0.0f;
        float dist = DistanceRaySegment(ray, gizmoOrigin, gizmoOrigin + axes[i] * size, segT);
        if (dist < bestDist && segT > 0.05f) {
            bestDist = dist;
            best = ids[i];
        }
    }
    return best;
}

float GizmoMath::ProjectRayOntoAxis(const Ray& ray, glm::vec3 axisOrigin, glm::vec3 axisDir) {
    glm::vec3 u = ray.dir;
    glm::vec3 v = glm::normalize(axisDir);
    glm::vec3 w = ray.origin - axisOrigin;
    float b = glm::dot(u, v);
    float d = glm::dot(u, w);
    float e = glm::dot(v, w);
    float denom = 1.0f - b * b;
    if (std::fabs(denom) < 1e-6f) return 0.0f;
    return (e - b * d) / denom;
}

} // namespace coconut::scene
