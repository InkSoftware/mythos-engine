#pragma once
// Using things in a first-person world: what a prop is, what the player can do to it (read,
// inspect, listen, take), and what using it leaves behind (notebook lines, clues, flags).
// Nothing here knows a story: a game decides what each outcome means and sounds like.
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/world/level.h"
#include "engine/core/math.h"

namespace mythos {

// What a prop does when the player uses it.
//   Read: opens a document. Inspect: a line. Listen: hold E, then a line.
//   Take: into the hand; E on its empty place puts it back.
enum class Verb { Read, Inspect, Listen, Take };
// "read" / "inspect" / "listen" / "take" -> Verb; false for any other word.
bool parseVerb(const std::string& word, Verb& out);

// An object the player can use. Its texts are translation keys.
struct Prop {
    std::string id;
    std::string model;        // "" draws nothing (an empty place you can still look at)
    Vec2 at;                  // world x, z
    double lift = 0, yawDeg = 0;
    double aimHeight = 0, aimRadius = 0;  // the sphere the player's view must hit
    Verb verb = Verb::Inspect;
    double seconds = 0;       // how long its line stays up (not for Read)
    std::string loopClip;     // a looping sound it makes, placed in the world ("" silent)
    double hushRadius = 0;    // the loop falls silent this close (0 = never)
    std::string name, text, notebook;  // translation keys

    const std::string& nameKey() const { return name; }
    const std::string& textKey() const { return text; }
    const std::string& notebookKey() const { return notebook; }
};

// A change to a prop: only the fields given are set.
struct PropOverride {
    std::optional<std::string> model, name, text, notebook, loop;
    std::optional<Vec2> at;
    std::optional<double> lift, yawDeg, aimHeight, aimRadius, seconds;
    std::optional<Verb> verb;
    void applyTo(Prop& p) const;
    // The text keys it names (text, name, notebook).
    std::vector<std::string> textKeys() const;
};

// The non-solid level placement that draws a prop.
Placement placementOf(const Prop& p);

// What the player has done and gathered: it survives from one scene to the next and is what
// a game saves. A game's own state extends it with whatever else it tracks.
struct Progress {
    std::vector<std::string> clues;     // clues found
    std::vector<std::string> notebook;  // notebook entries (text keys), oldest first
    std::string held;                   // the prop in hand, or ""
    std::map<std::string, int> flags;   // story flags

    // Adds key to list unless it is already there; true if it was added.
    static bool addUnique(std::vector<std::string>& list, const std::string& key);
    static bool contains(const std::vector<std::string>& list, const std::string& key);
    bool addNote(const std::string& key) { return addUnique(notebook, key); }
    bool flag(const std::string& name) const {
        const auto it = flags.find(name);
        return it != flags.end() && it->second != 0;
    }
};

// What dealing with something leaves behind.
struct Effects {
    std::string sound;     // a sound event to emit ("" none); the game maps the name
    std::string notebook;  // a notebook entry ("" none)
    std::string clue;      // a clue gained ("" none)
    std::map<std::string, int> flags;  // flags set
};

// Writes the notebook line, the clue and the flags into progress. Returns true if the clue is new.
bool applyEffects(Progress& progress, const Effects& e);

// The outcome of one frame of using what the view rests on.
enum class Use {
    None,       // nothing happened
    Listening,  // E is held on something to listen to; not long enough yet
    Read,       // open the prop's document
    Inspect,    // show the prop's line
    Heard,      // listened long enough: show the prop's line
    Take,       // the prop goes into the hand (the game removes it from the level and sets held)
    HandsFull,  // tried to take something with a full hand
    PutBack,    // the held prop goes back to its place
};

// The verb mechanics, with the listening timer. The game feeds it what the view rests on each
// frame and carries out the result.
class Interactor {
public:
    // Call every frame with whether E is held: releasing E lets a heard prop be listened to again.
    void release(bool interactHeld) {
        if (!interactHeld) listenDone_ = false;
    }
    // Nothing in reach this frame.
    void idle() { listen_ = 0; }
    // `p` is what the view rests on; `home` means it is the held prop's empty place; `held` is the
    // id in hand ("" none).
    Use use(const Prop& p, bool home, const std::string& held, double dt, bool pressed, bool holding,
            double listenSeconds);
    double listenTime() const { return listen_; }

private:
    double listen_ = 0;
    bool listenDone_ = false;
};

}  // namespace mythos
