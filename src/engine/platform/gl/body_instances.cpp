#include "engine/platform/gl/body_instances.h"

#include <algorithm>

namespace mythos {
BodyModels UploadBodyModels(SceneRenderer& scene, const ModelLibrary& models) {
  const auto upload = [&](const char* const name) {
    const Model* m = models.Find(name);
    return m ? scene.UploadModel(m->batches) : -1;
  };
  BodyModels ids;
  ids.torso = upload("body_torso");
  ids.head = upload("body_head");
  ids.leg = upload("body_leg");
  ids.arm = upload("body_arm");
  ids.forearm = upload("body_forearm");
  ids.torch = upload("torch");
  ids.lighter = upload("lighter");
  ids.flame = upload("flame");
  return ids;
}

void AddBodyInstances(std::vector<ModelInstance>& out, const BodyPose& pose,
                      const BodyModels& ids, const bool light_held,
                      const bool lighter, const bool casts_shadow) {
  const auto add = [&](const int model, const PartMatrix& m,
                       const unsigned views) {
    if (model < 0) return;
    ModelInstance inst;
    inst.model = model;
    std::ranges::copy(m.m, inst.matrix);
    inst.views = views;
    out.push_back(inst);
  };
  const unsigned kMirror =
      ModelInstance::kMirror | (casts_shadow ? ModelInstance::kShadow : 0u);
  constexpr unsigned kBoth = ModelInstance::kMain | ModelInstance::kMirror;
  add(ids.torso, pose.torso, kMirror);
  add(ids.head, pose.head, kMirror);
  add(ids.leg, pose.leg_l, kMirror);
  add(ids.leg, pose.leg_r, kMirror);
  add(ids.arm, pose.upper_arm_l, kMirror);
  add(ids.forearm, pose.fore_arm_l, kMirror);
  add(ids.arm, pose.upper_arm_r, kMirror);
  add(ids.forearm, pose.fore_arm_r, kMirror);
  if (light_held) {
    add(lighter ? ids.lighter : ids.torch, pose.held, kBoth);
    if (lighter) add(ids.flame, pose.flame, kBoth);
  }
}
}
