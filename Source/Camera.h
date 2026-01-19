#pragma once

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/ext/matrix_clip_space.hpp>

class Camera {
    public:

    enum CameraType {
        CAMERA_TYPE_PERSPECTIVE,
        CAMERA_TYPE_ORTHOGRAPHIC,
    };

    float z_near;
    float z_far;

    Camera()
    {

    }

    void Perspective(float aspect_ratio, float y_fov, float z_near, float z_far)
    {
        this->type = CAMERA_TYPE_PERSPECTIVE;
        this->aspect_ratio = aspect_ratio;
        this->y_fov = y_fov;
        this->z_near = z_near;
        this->z_far = z_far;
    }
    
    void Orthographic(float x_mag, float y_mag, float z_near, float z_far)
    {
        this->type = CAMERA_TYPE_ORTHOGRAPHIC;
        this->x_mag = x_mag;
        this->y_mag = y_mag;
        this->z_near = z_near;
        this->z_far = z_far;
        this->y_fov = 0;
        this->aspect_ratio = x_mag / y_mag;
    }

    CameraType GetType()
    {
        return type;
    }

    float GetFov()
    {
        return this->y_fov;
    }

    void SetFov(float fov)
    {
        if (type == CAMERA_TYPE_ORTHOGRAPHIC) {
            return;
        }
        this->y_fov = fov;
    }

    float GetAspectRatio()
    {
        return this->aspect_ratio;
    }

    void SetAspectRatio(float aspect_ratio)
    {
        this->aspect_ratio = aspect_ratio;
    }

    void SetWorldToView(glm::mat4x4 world_to_view)
    {
        this->world_to_view = world_to_view;
    }

    glm::mat4x4 GetWorldToView()
    {
        return this->world_to_view;
    }

    glm::mat4x4 GetViewToClip()
    {
        if (type == CAMERA_TYPE_PERSPECTIVE) {
            if (z_far != 0.0) {
                return glm::perspectiveRH_ZO(y_fov, aspect_ratio, z_far, z_near);
            } else {
                // TODO: Implement an infinite perspective matrix that maps the near plane to 1 and infinity to 0.
                // For now we will just use a finite perspective matrix with a large far plane.
                return glm::perspectiveRH_ZO(y_fov, aspect_ratio, 100000.0f, z_near);
            }
        } else {
            return glm::orthoRH_ZO(-1/x_mag, 1/x_mag, -1/y_mag, 1/y_mag, z_far, z_near);
        }
    }

    private:

    float aspect_ratio;
    float y_fov;
    float x_mag;
    float y_mag;

    CameraType type;

    glm::mat4x4 world_to_view;
};