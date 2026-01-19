#pragma once

#include "Gltf.h"

class AnimationPlayer {

    public:

    int animation = -1;
    float playhead = 0;
    bool playing = false;
    bool loop = true;
    void Tick(Gltf* gltf, float delta_time);
};