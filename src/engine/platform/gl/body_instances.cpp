#include "engine/platform/gl/body_instances.h"

#include <algorithm>
#include <iterator>

namespace mythos {

BodyModels uploadBodyModels(SceneRenderer& scene, const ModelLibrary& models) {
    const auto upload = [&](const char* name) {
        const Model* m = models.find(name);
        return m ? scene.uploadModel(m->batches) : -1;
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

void addBodyInstances(std::vector<ModelInstance>& out, const BodyPose& pose, const BodyModels& ids, bool lightHeld,
                      bool lighter) {
    const auto add = [&](int model, const PartMatrix& m, unsigned views) {
        if (model < 0) return;
        ModelInstance inst;
        inst.model = model;
        std::copy(std::begin(m.m), std::end(m.m), inst.matrix);
        inst.views = views;
        out.push_back(inst);
    };
    constexpr unsigned mirror = ModelInstance::kMirror, both = ModelInstance::kMain | ModelInstance::kMirror;
    add(ids.torso, pose.torso, mirror);
    add(ids.head, pose.head, mirror);
    add(ids.leg, pose.legL, mirror);
    add(ids.leg, pose.legR, mirror);
    add(ids.arm, pose.upperArmL, mirror);
    add(ids.forearm, pose.foreArmL, mirror);
    add(ids.arm, pose.upperArmR, mirror);
    add(ids.forearm, pose.foreArmR, mirror);
    if (lightHeld) {
        add(lighter ? ids.lighter : ids.torch, pose.held, both);
        if (lighter) add(ids.flame, pose.flame, both);
    }
}

}  // namespace mythos
