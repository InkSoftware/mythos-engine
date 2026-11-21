#ifndef MYTHOS_ENGINE_PLATFORM_GL_BODY_INSTANCES_H_
#define MYTHOS_ENGINE_PLATFORM_GL_BODY_INSTANCES_H_

#include <vector>

#include "engine/gfx/model.h"
#include "engine/platform/gl/scene_renderer.h"
#include "engine/world/body.h"
#include "engine/world/mirror.h"

namespace mythos {
// Renderer ids of the models that make up the player's body and held light; -1
// for one that is missing.
struct BodyModels {
  int torso = -1, head = -1, leg = -1, arm = -1, forearm = -1;
  int torch = -1, lighter = -1, flame = -1;
};

// Uploads the body, torch, lighter and flame models found in `models` by name
// (`body_torso`, `body_head`, `body_leg`, `body_arm`, `body_forearm`, `torch`,
// `lighter`, `flame`).
BodyModels UploadBodyModels(SceneRenderer& scene, const ModelLibrary& models);

// Appends the instances to draw for `pose`. The figure appears only in the
// mirror; the held light (a lighter and its flame, or the torch when `lighter`
// is false) is drawn in both views. Missing models are skipped.
void AddBodyInstances(std::vector<ModelInstance>& out, const BodyPose& pose,
                      const BodyModels& ids, bool light_held, bool lighter);

// Copies a mirror placement into the camera's mirror state; the geometry is
// left untouched when the mirror is off.
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
}

#endif
