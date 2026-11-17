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

void PropOverride::applyTo(Prop& p) const {
    if (model) p.model = *model;
    if (loop) p.loopClip = *loop;
    if (name) p.name = *name;
    if (text) p.text = *text;
    if (notebook) p.notebook = *notebook;
    if (at) p.at = *at;
    if (lift) p.lift = *lift;
    if (yawDeg) p.yawDeg = *yawDeg;
    if (aimHeight) p.aimHeight = *aimHeight;
    if (aimRadius) p.aimRadius = *aimRadius;
    if (seconds) p.seconds = *seconds;
    if (verb) p.verb = *verb;
}

std::vector<std::string> PropOverride::textKeys() const {
    std::vector<std::string> out;
    for (const auto* k : {&text, &name, &notebook})
        if (*k) out.push_back(**k);
    return out;
}

Placement placementOf(const Prop& p) {
    Placement out;
    out.x = static_cast<int>(std::floor(p.at.x));
    out.y = static_cast<int>(std::floor(p.at.y));
    out.offX = p.at.x - (out.x + 0.5);
    out.offZ = p.at.y - (out.y + 0.5);
    out.lift = p.lift;
    out.yawDeg = p.yawDeg;
    out.model = p.model;
    out.solid = false;
    out.id = p.id;
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

bool applyEffects(Progress& progress, const Effects& e) {
    if (!e.notebook.empty()) progress.addNote(e.notebook);
    const bool newClue = !e.clue.empty() && Progress::addUnique(progress.clues, e.clue);
    for (const auto& [k, v] : e.flags) progress.flags[k] = v;
    return newClue;
}

Use Interactor::use(const Prop& p, bool home, const std::string& held, double dt, bool pressed, bool holding,
                    double listenSeconds) {
    if (home) {  // the empty place of the prop in hand
        listen_ = 0;
        return pressed ? Use::PutBack : Use::None;
    }
    if (p.verb != Verb::Listen) listen_ = 0;
    switch (p.verb) {
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
