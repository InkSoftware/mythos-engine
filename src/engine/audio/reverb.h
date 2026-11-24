// A small stereo reverb (four damped comb filters and two all-pass filters
// per side, after Schroeder) for the mixer's room sound. Headless: it works
// on interleaved float blocks and knows nothing of devices. `Set` takes what
// `ReverbForRoom` (audio/spatial.h) computes for a room.
#ifndef MYTHOS_ENGINE_AUDIO_REVERB_H_
#define MYTHOS_ENGINE_AUDIO_REVERB_H_

#include <array>
#include <vector>

namespace mythos {
class Reverb {
 public:
  // Sets the sample rate and clears the tail. Call before Process.
  void Prepare(double sample_rate);

  // The wet level added to the dry sound (0 off, about 0.5 a big hall) and
  // the tail length: the time for the tail to fall by 60 dB, in seconds. Both
  // are eased, so changing rooms does not click.
  void Set(double send, double decay_seconds);

  // Adds the reverb of an interleaved stereo block to itself (`out` is the
  // dry mix and stays in place). With a send of 0 and no tail left, nothing
  // changes and the block is not touched.
  void Process(float* out, int frames);

  // As Process, but the reverb is fed from `send` (a stereo block of what each
  // sound sent to it, already scaled by its own send) and added to `out`; the
  // two may be the same block. A mixer collects the sends per sound.
  void ProcessSend(const float* send, float* out, int frames);

  [[nodiscard]] double Send() const { return send_; }

 private:
  struct Comb {
    std::vector<float> line;
    size_t pos = 0;
    float damp = 0;  // the low-pass state inside the loop
  };
  struct AllPass {
    std::vector<float> line;
    size_t pos = 0;
  };

  void UpdateFeedback();

  double rate_ = 44100;
  double send_ = 0, target_send_ = 0;
  double decay_ = 0.5, target_decay_ = 0.5;
  std::array<Comb, 4> comb_l_, comb_r_;
  std::array<AllPass, 2> all_l_, all_r_;
  std::array<float, 4> feedback_{};
  bool tail_ = false;
};
}

#endif
