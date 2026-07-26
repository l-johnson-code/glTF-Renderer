#pragma once

#include "Scene.h"

class AnimationPlayer {

    public:

    int animation = -1;
    float playhead = 0;
    bool playing = false;
    bool loop = true;
    void Tick(Scene* Scene, float delta_time);
};