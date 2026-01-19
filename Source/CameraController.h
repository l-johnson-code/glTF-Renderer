#pragma once

#include <glm/glm.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/vector_relational.hpp>
#include <SDL3/SDL.h>

class OrbitController {
    public:
    
    OrbitController(glm::vec3 centre, float radius, float azimuth, float inclination)
    {
        this->centre = centre;
        this->radius = radius;
        this->azimuth = azimuth;
        this->inclination = inclination;
    }
    
    void Rotate(float azimuth, float inclination)
    {
        this->azimuth += azimuth;
        this->inclination += inclination;
        this->inclination = glm::clamp(this->inclination, this->min_inclination, this->max_inclination);
    }

    void Zoom(float zoom)
    {
        this->radius += zoom;
        this->radius = std::max(radius, 0.0f);
    }

    void Pan(float x, float y)
    {
        glm::mat4x4 rotation = glm::eulerAngleXZ(inclination, azimuth);
        glm::vec3 right = rotation * glm::vec4(1., 0., 0., 1.);
        glm::vec3 forward = rotation * glm::vec4(0., 1., 0., 1.);
        glm::vec3 up = glm::cross(right, forward);
        this->centre = this->centre + this->radius * x * right + this->radius * y * up;
    }

    glm::mat4x4 GetTransform()
    {
        glm::mat4x4 transform = glm::mat4x4(glm::mat3x3(glm::vec3(1, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0)));
        transform *= glm::translate(glm::vec3(0., radius, 0.));
        transform *= glm::eulerAngleXZ(-inclination, -azimuth);
        transform *= glm::translate(-centre);
        return transform;
    }

    float GetRadius()
    {
        return radius;
    }

    void SetIncliationLimits(float min, float max)
    {
        this->min_inclination = min;
        this->max_inclination = max;
    }


    bool ProcessEvent(const SDL_Event* event)
    {
        // TODO: Check if focus loss has to be handled.
        switch (event->type) {
            case SDL_EVENT_MOUSE_WHEEL: {
                Zoom(-zoom_sensitivity * event->wheel.y);
            } break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                switch (event->button.button) {
                    case SDL_BUTTON_RIGHT: {
                        SDL_CaptureMouse(true);
                        is_panning = true;
                    } break;
                    case SDL_BUTTON_LEFT: {
                        SDL_CaptureMouse(true);
                        is_rotating = true;
                    } break;
                    default: break;
                }
            } break;
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                switch (event->button.button) {
                    case SDL_BUTTON_RIGHT: {
                        SDL_CaptureMouse(false);
                        is_panning = false;
                    } break;
                    case SDL_BUTTON_LEFT: {
                        SDL_CaptureMouse(false);
                        is_rotating = false;
                    } break;
                    default: break;
                }
            } break;
            case SDL_EVENT_MOUSE_MOTION: {
                if (is_rotating) {
                    Rotate(-rotation_sensitivity * (float)event->motion.xrel, -rotation_sensitivity * (float)event->motion.yrel);
                }
                if (is_panning) {
                    Pan(-panning_sensitivity * (float)event->motion.xrel, panning_sensitivity * (float)event->motion.yrel);
                }
            } break;
        }
        return false;
    }

    private:

    glm::vec3 centre;
    float radius = 1.;
    float azimuth = 0.;
    float inclination = 0.;
    float min_inclination = -.25 * glm::two_pi<float>();
    float max_inclination = .25 * glm::two_pi<float>();

    bool is_rotating = false;
    bool is_panning = false;
    float zoom_sensitivity = .1;
    float rotation_sensitivity = .001;
    float panning_sensitivity = .001;
};

class FreeController {
    public:
    
    FreeController(glm::vec3 position, float yaw, float pitch)
    {
        this->position = position;
        this->yaw = yaw;
        this->pitch = pitch;
    }
    
    void Rotate(float yaw, float pitch)
    {
        this->yaw += yaw;
        this->pitch += pitch;
        this->pitch = glm::clamp(this->pitch, this->min_pitch, this->max_pitch);
    }

    void Move(glm::vec3 xyz)
    {
        glm::mat3x3 rotation = glm::eulerAngleZX(yaw, pitch);
        xyz = rotation * xyz;
        position += xyz;
    }

    void SetPosition(glm::vec3 position)
    {
        this->position = position;
    }

    void IncreaseSpeed(float increase)
    {
        this->movement_speed += increase;
        this->movement_speed = std::max(movement_speed, 0.0f);
    }

    glm::mat4x4 GetTransform()
    {
        glm::mat4x4 transform = glm::mat4x4(glm::mat3x3(glm::vec3(1, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0)));
        transform *= glm::eulerAngleXZ(-pitch, -yaw);
        transform *= glm::translate(-position);
        return transform;
    }

    bool ProcessEvent(const SDL_Event* event, SDL_Window* window)
    {
        // TODO: Check if focus loss has to be handled.
        switch (event->type) {
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                switch (event->button.button) {
                    case SDL_BUTTON_RIGHT: {
                        SDL_SetWindowRelativeMouseMode(window, true);
                        this->is_enabled = true;
                    } break;
                    default: break;
                }
            } break;
            case SDL_EVENT_MOUSE_WHEEL: {
                IncreaseSpeed(0.3f * event->wheel.y);
            } break;
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                switch (event->button.button) {
                    case SDL_BUTTON_RIGHT: {
                        SDL_SetWindowRelativeMouseMode(window, false);
                        this->is_enabled = false;
                    } break;
                    default: break;
                }
            } break;
            case SDL_EVENT_MOUSE_MOTION: {
                if (is_enabled) {
                    Rotate(-rotation_sensitivity * (float)event->motion.xrel, -rotation_sensitivity * (float)event->motion.yrel);
                }
            } break;
        }
        return false;
    }

    void Tick(float delta)
    {
        if (is_enabled) {
            const bool *key_states = SDL_GetKeyboardState(nullptr);
            glm::vec3 direction = glm::vec3(0.0f);
            if (key_states[SDL_SCANCODE_W]) {
                direction += glm::vec3(0.0f, 1.0f, 0.0f);
            }
            if (key_states[SDL_SCANCODE_A]) {
                direction += glm::vec3(-1.0f, 0.0f, 0.0f);
            }
            if (key_states[SDL_SCANCODE_S]) {
                direction += glm::vec3(0.0f, -1.0f, 0.0f);
            }
            if (key_states[SDL_SCANCODE_D]) {
                direction += glm::vec3(1.0f, 0.0f, 0.0f);
            }
            if (key_states[SDL_SCANCODE_Q]) {
                direction += glm::vec3(0.0f, 0.0f, -1.0f);
            }
            if (key_states[SDL_SCANCODE_E]) {
                direction += glm::vec3(0.0f, 0.0f, 1.0f);
            }
            direction = glm::any(glm::greaterThan(direction, glm::vec3(0.0f))) ? glm::normalize(direction) : direction;
            bool fast = key_states[SDL_SCANCODE_LSHIFT];
            float speed = fast ? fast_movement_factor * movement_speed : movement_speed;
            Move(delta * speed * direction);
        }
    }

    private:

    glm::vec3 position;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float min_pitch = -0.25f * glm::two_pi<float>();
    float max_pitch = 0.25f * glm::two_pi<float>();

    bool is_enabled = false;
    float rotation_sensitivity = 0.001f;
    float movement_speed = 1.0f;
    float fast_movement_factor = 2.0f;
};