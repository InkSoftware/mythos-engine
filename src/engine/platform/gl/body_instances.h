#ifndef MYTHOS_ENGINE_PLATFORM_GL_BODY_INSTANCES_H_
#define MYTHOS_ENGINE_PLATFORM_GL_BODY_INSTANCES_H_
// The renderer side of engine/world/body.h: the body parts, the torch and the
// lighter as uploaded models, and the model instances of a frame's pose.

#include <vector>

#include "engine/gfx/model.h"
#include "engine/platform/gl/scene_renderer.h"
#include "engine/world/body.h"
#include "engine/world/mirror.h"

namespace mythos {

// The ids of the models a body draws (-1: the library has no such model, and
// that part is not drawn).
struct BodyModels {
  int torso = -1, head = -1, leg = -1, arm = -1,
      forearm = -1;  // body_torso, body_head, body_leg, body_arm, body_forearm
  int torch = -1, lighter = -1, flame = -1;  // torch, lighter, flame
};

// Uploads those models (by those names) from the library into the renderer.
BodyModels UploadBodyModels(SceneRenderer& scene, const ModelLibrary& models);

// Appends the instances of a pose: the body shows only in the mirror; the light
// in the hand (the torch, or the lighter and its flame) shows in the view and
// in the mirror, when `lightHeld`.
void AddBodyInstances(std::vector<ModelInstance>& out, const BodyPose& pose,
                      const BodyModels& ids, bool light_held, bool lighter);

// Hangs the renderer's mirror where `p` says (leaves its alpha and tint alone);
// `p.on` false switches it off.
inline void SetMirror(MirrorState& out, const MirrorPlacement& p) {
  out.on = p.on;
  if (!p.on) return;
  out.x = p.x;
  out.y = p.y;
  out.z = p.z;
  out.nx = p.nx;
  out.nz = p.nz;
  out.half_w = p.half_w;
  out.half_h = p.half_h;
}

}  // namespace mythos

#endif  // MYTHOS_ENGINE_PLATFORM_GL_BODY_INSTANCES_H_
