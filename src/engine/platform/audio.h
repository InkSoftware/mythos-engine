#pragma once
// A small SDL audio mixer: preloaded mono WAV clips mixed to stereo, fire-and-forget
// one-shots with per-play gain and pitch, one looping ambience that crossfades when it
// changes, one voice-over line at a time (a new line cuts the old one; the ambience ducks
// under it; a line can be placed in the world, like a voice from a radio), pausing (the game is
// frozen under a menu: the voice and the world's sounds hold where they are and carry on after;
// the ambience plays on), and positional sources: looping clips whose left and right gains the caller
// sets every frame from where they stand (the mixer eases towards them, so a source can
// fall silent as you come close without a click). No SDL_mixer - everything is mixed in
// the SDL audio callback.

#include <map>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

namespace mythos {

class AudioEngine {
public:
    ~AudioEngine() { shutdown(); }

    // Opens the audio device. Returns false (and stays silent) when audio is unavailable.
    bool init();
    void shutdown();

    // Loads <dir>/<name>.wav for every name. Missing or broken files are skipped;
    // playing an unloaded name does nothing. Returns how many were loaded.
    int loadDir(const std::string& dir, const std::vector<std::string>& names);

    // One-shot. pitch scales playback speed (1 = as recorded).
    void play(const std::string& name, float gain = 1.0f, float pitch = 1.0f);

    // Switches the looping ambience (crossfading over ~0.8s). An empty name stops it.
    void playLoop(const std::string& name, float gain = 1.0f);

    // Positional sources: a looping clip placed in the world. addSource starts one silent
    // and returns its id (-1 if the clip is not loaded); setSource sets the gains it eases
    // towards (scaled by the ambience volume); clearSources stops them all (a scene change).
    int addSource(const std::string& name);
    void setSource(int id, float left, float right);
    void clearSources();

    // Voice-over: starts `name`, quickly fading out any line still playing. Missing clips
    // just stop the old line. stopVoice fades the current line out. A `spatial` line is a sound in
    // the world: it is silent until setVoicePlacement says where it is heard (and it ducks the
    // ambience only as much as it is audible).
    void playVoice(const std::string& name, float gain = 1.0f, bool spatial = false);
    void stopVoice();
    // The left and right gains of the spatial voice line (0..1, eased like a source's), set every
    // frame from where the listener stands. No effect on a line that is not spatial.
    void setVoicePlacement(float left, float right);

    // Pausing: while paused the voice line, one-shots and positional sources stand still (they
    // continue from the same sample when resumed); the looping ambience keeps playing.
    void setPaused(bool paused);
    bool paused() const { return paused_; }
    // How far the ambience drops while a voice line plays (1 = not at all).
    void setVoiceDuck(float duck);
    // Length of a loaded clip in seconds, or 0 if it is not loaded.
    double clipSeconds(const std::string& name) const;

    // Volume settings, 0..1. Effects scale one-shots, ambience scales the loop (live).
    void setVolumes(float master, float ambience, float effects);

private:
    struct Clip {
        std::vector<float> samples;  // mono, device rate
    };
    struct Voice {
        const Clip* clip = nullptr;
        double pos = 0;
        double step = 1.0;
        float gain = 1.0f;
        bool loop = false;
        bool voice = false;     // the voice-over line
        float fade = 1.0f;      // current fade multiplier
        float fadeRate = 0.0f;  // per-sample change; a fading-out loop is removed at 0
        float panL = 0.7071f, panR = 0.7071f;  // stereo placement (one-shots sit centre: 1/sqrt(2) each)
        bool spatial = false;       // a voice line placed in the world: gains follow tgtL / tgtR
        bool placed = false;        // ...once setVoicePlacement has been called for it
        float curL = 0, curR = 0;   // current spatial gains, eased towards the targets
        float tgtL = 0, tgtR = 0;
    };
    struct Source {
        const Clip* clip = nullptr;
        double pos = 0;
        float left = 0, right = 0;       // current gains, eased towards the targets
        float targetL = 0, targetR = 0;  // what setSource last asked for
    };

    static void SDLCALL callback(void* userdata, SDL_AudioStream* stream, int additional, int total);
    void mix(float* out, int frames);

    // Pieces of mix(), in the order it runs them.
    float updateDuck(int frames);  // returns the duck level the chunk started at (duckLevel_ is its end)
    void mixVoices(float* out, int frames, float duckStart, float duckEnd);
    void mixSources(float* out, int frames);
    void removeFinishedVoices();
    // Adds a voice under the stream lock (ignored when the mixer is full).
    void startVoice(const Voice& voice);
    // The clip called `name`, or nullptr.
    const Clip* findClip(const std::string& name) const;

    SDL_AudioStream* stream_ = nullptr;
    int rate_ = 44100;
    std::map<std::string, Clip> clips_;
    std::vector<Voice> voices_;
    std::vector<Source> sources_;
    std::string currentLoop_;
    float effectScale_ = 1.0f;  // master * effects, applied when a one-shot starts
    float loopScale_ = 1.0f;    // master * ambience, applied live in the mixer
    float voiceDuck_ = 1.0f;    // ambience level while a voice line plays
    float duckLevel_ = 1.0f;    // current ambience duck, eased towards its target
    bool paused_ = false;
};

}  // namespace mythos
