#pragma once

#include <glm/glm.hpp>

struct Aabb {
    glm::vec3 start;
    glm::vec3 end;
};

struct Obb {
    glm::vec3 centre;
    glm::vec3 extent;
    glm::vec3 axis[3];
};

struct Plane {
    glm::vec3 normal;
    float distance;
};

struct FrustumPlanes {
    Plane planes[5]; // We don't include the far plane.
};

inline Obb ObbFromAabb(Aabb aabb, glm::mat4x4 transform)
{
    Obb obb;
    obb.centre = transform[3];
    obb.extent = aabb.end - aabb.start;
    obb.axis[0] = transform[0];
    obb.axis[1] = transform[1];
    obb.axis[2] = transform[2];
    return obb;
}

inline bool OutsidePlane(Plane plane, Obb obb)
{
    glm::vec3 temp(
        glm::dot(obb.axis[0], plane.normal), 
        glm::dot(obb.axis[1], plane.normal), 
        glm::dot(obb.axis[2], plane.normal)
    );
    float r = glm::dot(obb.extent, glm::abs(temp));
    float s = glm::dot(obb.centre, plane.normal) - plane.distance;
    return s > r;
}

inline bool OutsideFrustum(FrustumPlanes frustum, Obb obb)
{
    for (int i = 0; i < std::size(frustum.planes); i++) {
        if (OutsidePlane(frustum.planes[i], obb)) {
            return true;
        }
    }
    return false;
}

inline Plane NormalizeFrustum(Plane plane)
{
    float length = glm::length(plane.normal);
    plane.normal /= length;
    plane.distance /= length;
    return plane;
}

inline FrustumPlanes ExtractPlanesFromMatrix(glm::mat4x4 matrix)
{
    FrustumPlanes frustum;
    
    // Left.
    frustum.planes[0].normal = glm::vec3(-matrix[0][0] - matrix[0][3], -matrix[1][0] - matrix[1][3], -matrix[2][0] - matrix[2][3]);
    frustum.planes[0].distance = matrix[3][3] + matrix[3][0];
    frustum.planes[0] = NormalizeFrustum(frustum.planes[0]);
    
    // Right.
    frustum.planes[1].normal = glm::vec3(matrix[0][0] - matrix[0][3], matrix[1][0] - matrix[1][3], matrix[2][0] - matrix[2][3]);
    frustum.planes[1].distance = matrix[3][3] - matrix[3][0];
    frustum.planes[1] = NormalizeFrustum(frustum.planes[1]);
    
    // Bottom.
    frustum.planes[2].normal = glm::vec3(-matrix[0][1] - matrix[0][3], -matrix[1][1] - matrix[1][3], -matrix[2][1] - matrix[2][3]);
    frustum.planes[2].distance = matrix[3][3] + matrix[3][1];
    frustum.planes[2] = NormalizeFrustum(frustum.planes[2]);
    
    // Top.
    frustum.planes[3].normal = glm::vec3(matrix[0][1] - matrix[0][3], matrix[1][1] - matrix[1][3], matrix[2][1] - matrix[2][3]);
    frustum.planes[3].distance = matrix[3][3] - matrix[3][1];
    frustum.planes[3] = NormalizeFrustum(frustum.planes[3]);
    
    // Near.
    frustum.planes[4].normal = glm::vec3(-matrix[0][2] - matrix[0][3], -matrix[1][2] - matrix[1][3], -matrix[2][2] - matrix[2][3]);
    frustum.planes[4].distance = matrix[3][3] + matrix[3][2];
    frustum.planes[4] = NormalizeFrustum(frustum.planes[4]);
    
    return frustum;
}