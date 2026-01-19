#include "AnimationPlayer.h"

void AnimationPlayer::Tick(Gltf* gltf, float delta_time) 
{
    if ((this->animation < gltf->animations.size()) && (this->animation >= 0)) {
        if (this->playing) {
            this->playhead += delta_time;
        }
        if (gltf->animations[animation].length < this->playhead) {
            if (loop) {
                if (gltf->animations[animation].length != 0) {
                    this->playhead = std::fmod(this->playhead, gltf->animations[animation].length);
                } else {
                    this->playhead = 0;
                }
            } else {
                playhead = gltf->animations[animation].length;
                this->playing = false;
            }
        }
        gltf->Animate(&gltf->animations[animation], this->playhead);
    }
}