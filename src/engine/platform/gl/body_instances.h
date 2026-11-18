#pragma once
// The renderer side of engine/world/body.h: the body parts, the torch and the lighter as uploaded models, and the
// model instances of a frame's pose.

#include <vector>

#include "engine/gfx/model.h"
#include "engine/platform/gl/scene_renderer.h"
#include "engine/world/body.h"

namespace mythos {

// The ids of the models a body draws (-1: the library has no such model, and that part is not drawn).
struct BodyModels {
    int torso = -1, head = -1, leg = -1, arm = -1, forearm = -1;  // body_torso, body_head, body_leg, body_arm, body_forearm
    int torch = -1, lighter = -1, flame = -1;                     // torch, lighter, flame
};

// Uploads those models (by those names) from the library into the renderer.
BodyModels uploadBodyModels(SceneRenderer& scene, const ModelLibrary& models);

// Appends the instances of a pose: the body shows only in the mirror; the light in the hand (the torch, or the
// lighter and its flame) shows in the view and in the mirror, when `lightHeld`.
void addBodyInstances(std::vector<ModelInstance>& out, const BodyPose& pose, const BodyModels& ids, bool lightHeld,
                      bool lighter);

}  // namespace mythos
