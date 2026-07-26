#include "AnimationPlayer.h"

void AnimationPlayer::Tick(Scene* scene, float delta_time) 
{
    if ((this->animation < scene->animations.size()) && (this->animation >= 0)) {
        if (this->playing) {
            this->playhead += delta_time;
        }
        if (scene->animations[animation].length < this->playhead) {
            if (loop) {
                if (scene->animations[animation].length != 0) {
                    this->playhead = std::fmod(this->playhead, scene->animations[animation].length);
                } else {
                    this->playhead = 0;
                }
            } else {
                playhead = scene->animations[animation].length;
                this->playing = false;
            }
        }
        scene->Animate(&scene->animations[animation], this->playhead);
    }
}