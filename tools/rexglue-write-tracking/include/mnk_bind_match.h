#pragma once
// Keyboard bind matching for the SDK's MnK input driver (mnk_input_driver.cpp).
// Lives with the project's shared SDK sources so tests/native covers it.
//
// A bind is comma-separated alternatives, each with optional modifier prefixes:
// "Q,I" or "Shift+W". Modifier matching is exact, so "Up" stays silent while
// Shift is held and "Shift+Up" needs it: the D-pad shares the arrow keys.
// A modifier bound on its own ("Shift", "Q,Alt") is a button instead: it
// fires by itself and no longer blocks plain binds (sprint on Shift while
// walking on W). The SDK rejected those binds because holding the modifier
// made the live mask differ from the empty mask the bind asked for.
#include <cstdint>
#include <span>
#include <string_view>

namespace sr::mnk {
constexpr uint8_t kModShift = 1u << 0;
constexpr uint8_t kModCtrl = 1u << 1;
constexpr uint8_t kModAlt = 1u << 2;

inline uint8_t ModifierOfKeyName(std::string_view name) {
  if (name == "Shift") return kModShift;
  if (name == "Ctrl" || name == "Control") return kModCtrl;
  if (name == "Alt") return kModAlt;
  return 0;
}

// Strips leading modifier prefixes off `token`, leaving the bare key name, and
// returns the mask they require.
inline uint8_t TakeModifiers(std::string_view& token) {
  uint8_t mods = 0;
  for (;;) {
    const size_t plus = token.find('+');
    if (plus == std::string_view::npos || plus == 0) break;
    const uint8_t bit = ModifierOfKeyName(token.substr(0, plus));
    if (!bit) break;
    mods |= bit;
    token.remove_prefix(plus + 1);
  }
  return mods;
}

inline std::string_view TrimSpaces(std::string_view s) {
  while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
  while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
  return s;
}

// Calls `visit(token)` for each trimmed, non-empty alternative of `bind`;
// stops early when it returns true.
template <class Visit>
bool ForEachToken(std::string_view bind, Visit&& visit) {
  while (!bind.empty()) {
    const size_t comma = bind.find(',');
    const std::string_view token = TrimSpaces(bind.substr(0, comma));
    bind = comma == std::string_view::npos ? std::string_view() : bind.substr(comma + 1);
    if (!token.empty() && visit(token)) return true;
  }
  return false;
}

// Modifiers bound on their own in any of `binds`.
inline uint8_t BareModifierBinds(std::span<const std::string_view> binds) {
  uint8_t mask = 0;
  for (const auto bind : binds)
    ForEachToken(bind, [&](std::string_view token) {
      if (!TakeModifiers(token)) mask |= ModifierOfKeyName(token);
      return false;
    });
  return mask;
}

// `key_down(name)` reports whether the bare key named `name` is held.
// `live_mods` are the modifiers held; `bare_bound` comes from BareModifierBinds.
template <class KeyDown>
bool BindPressed(std::string_view bind, uint8_t live_mods, uint8_t bare_bound, KeyDown&& key_down) {
  return ForEachToken(bind, [&](std::string_view token) {
    const uint8_t want = TakeModifiers(token);
    const bool modifiers_match =
        want == 0 && ModifierOfKeyName(token) ? true                      // the modifier itself
        : want == 0                           ? (live_mods & ~bare_bound) == 0
                                              : want == live_mods;
    return modifiers_match && key_down(token);
  });
}
}  // namespace sr::mnk
