#pragma once
// --sr_costume: "0" Superman Returns, "1" Pod Suit, "2" Golden Age Superman
// (text.AST Costume_Name_0..2); anything else leaves the game's choice alone.
#include <cstdint>
#include <optional>
#include <string_view>

namespace superman_returns::costume {
inline std::optional<int> ParseCostume(std::string_view value) {
  if (value.size() == 1 && value[0] >= '0' && value[0] <= '2') return value[0] - '0';
  return std::nullopt;
}

// The game puts the film suit back on whenever it recreates Superman (cinematics,
// level loads), and a costume written before Superman exists is consumed for
// nothing. So the costume is written a moment after the film suit body texture
// is decoded, and written again a few times until the wanted suit's texture is
// decoded. Each write that lands reloads the model (a short hitch), so there is
// no periodic re-apply.
constexpr int kReapplyDelayFrames = 30;   // ~0.5 s at 60 FPS
constexpr int kReapplyRetryFrames = 180;  // ~3 s
constexpr int kReapplyTries = 3;

struct ReapplyState {
  uint32_t film_seen = 0, worn_seen = 0;
  int countdown = 0;  // frames to the next write; 0 = none pending
  int tries = 0;
};

// One call per frame with the decode counts of the film suit and of the wanted
// suit (track_worn = false when the wanted suit has no known texture). True
// means: write the costume now.
inline bool StepReapply(ReapplyState& state, uint32_t film, uint32_t worn, bool track_worn) {
  if (track_worn && worn != state.worn_seen) {
    state.worn_seen = worn;
    state.countdown = 0;
  }
  if (film != state.film_seen) {
    state.film_seen = film;
    state.tries = 0;
    state.countdown = kReapplyDelayFrames;
  }
  if (state.countdown == 0 || --state.countdown > 0) return false;
  if (track_worn && ++state.tries < kReapplyTries) state.countdown = kReapplyRetryFrames;
  return true;
}
}  // namespace superman_returns::costume
