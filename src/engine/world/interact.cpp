#include "engine/world/interact.h"

#include <algorithm>
#include <cmath>

namespace mythos {

bool parseVerb(const std::string& word, Verb& out) {
    if (word == "read") out = Verb::Read;
    else if (word == "inspect") out = Verb::Inspect;
    else if (word == "listen") out = Verb::Listen;
    else if (word == "take") out = Verb::Take;
    else return false;
    return true;
}

void PropOverride::applyTo(Prop& prop) const {
    if (model) prop.model = *model;
    if (loop) prop.loopClip = *loop;
    if (name) prop.name = *name;
    if (text) prop.text = *text;
    if (notebook) prop.notebook = *notebook;
    if (at) prop.at = *at;
    if (lift) prop.lift = *lift;
    if (yawDeg) prop.yawDeg = *yawDeg;
    if (aimHeight) prop.aimHeight = *aimHeight;
    if (aimRadius) prop.aimRadius = *aimRadius;
    if (seconds) prop.seconds = *seconds;
    if (verb) prop.verb = *verb;
}

std::vector<std::string> PropOverride::textKeys() const {
    std::vector<std::string> out;
    for (const auto* key : {&text, &name, &notebook})
        if (*key) out.push_back(**key);
    return out;
}

Placement placementOf(const Prop& prop) {
    Placement out;
    out.x = static_cast<int>(std::floor(prop.at.x));
    out.y = static_cast<int>(std::floor(prop.at.y));
    out.offX = prop.at.x - (out.x + 0.5);
    out.offZ = prop.at.y - (out.y + 0.5);
    out.lift = prop.lift;
    out.yawDeg = prop.yawDeg;
    out.model = prop.model;
    out.solid = false;
    out.id = prop.id;
    return out;
}

bool Progress::addUnique(std::vector<std::string>& list, const std::string& key) {
    if (contains(list, key)) return false;
    list.push_back(key);
    return true;
}

bool Progress::contains(const std::vector<std::string>& list, const std::string& key) {
    return std::find(list.begin(), list.end(), key) != list.end();
}

bool applyEffects(Progress& progress, const Effects& effects) {
    if (!effects.notebook.empty()) progress.addNote(effects.notebook);
    const bool newClue = !effects.clue.empty() && Progress::addUnique(progress.clues, effects.clue);
    for (const auto& [flag, value] : effects.flags) progress.flags[flag] = value;
    return newClue;
}

Use Interactor::use(const Prop& prop, bool home, const std::string& held, double dt, bool pressed, bool holding,
                    double listenSeconds) {
    if (home) {  // the empty place of the prop in hand
        listen_ = 0;
        return pressed ? Use::PutBack : Use::None;
    }
    if (prop.verb != Verb::Listen) listen_ = 0;
    switch (prop.verb) {
        case Verb::Read: return pressed ? Use::Read : Use::None;
        case Verb::Inspect: return pressed ? Use::Inspect : Use::None;
        case Verb::Listen:
            if (!holding || listenDone_) {
                listen_ = 0;
                return Use::None;
            }
            listen_ += dt;
            if (listen_ < listenSeconds) return Use::Listening;
            listen_ = 0;
            listenDone_ = true;
            return Use::Heard;
        case Verb::Take:
            if (!pressed) return Use::None;
            return held.empty() ? Use::Take : Use::HandsFull;
    }
    return Use::None;
}

}  // namespace mythos
