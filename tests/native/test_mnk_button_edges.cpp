#include "test_main.h"
#include "../../tools/rexglue-write-tracking/include/mnk_bind_match.h"
#include "../../tools/rexglue-write-tracking/include/mnk_button_edges.h"
#include <array>
#include <set>
#include <string>
#include <string_view>

namespace {
constexpr uint16_t kStart = 0x10, kA = 0x1000, kB = 0x2000, kDpadUp = 1;
struct Keyboard {
  std::set<std::string, std::less<>> held;
  std::array<std::string_view, 4> binds{"X,Return", "Semicolon,Space", "Quote,Backspace", "Shift+Up"};
  uint16_t pending = 0;
  bool accepting = true;

  uint16_t Resolve() const {
    const uint8_t mods = (held.contains("Shift") ? sr::mnk::kModShift : 0) |
                         (held.contains("Ctrl") ? sr::mnk::kModCtrl : 0);
    const uint8_t bare = sr::mnk::BareModifierBinds(binds);
    constexpr uint16_t bits[] = {kStart, kA, kB, kDpadUp};
    uint16_t result = 0;
    for (size_t i = 0; i != binds.size(); ++i)
      if (sr::mnk::BindPressed(binds[i], mods, bare,
                              [&](std::string_view key) { return held.contains(key); }))
        result |= bits[i];
    return result;
  }
  void Down(std::string key) {
    held.insert(std::move(key));
    sr::mnk::ObserveButtonEdges(pending, Resolve(), accepting);
  }
  void Up(std::string_view key) { held.erase(std::string(key)); }
  uint16_t Poll() { return sr::mnk::ConsumeButtonEdges(pending, Resolve(), accepting); }
  void Reset() { held.clear(); pending = 0; }
};
}

SR_TEST(mnk_complete_tap_between_polls_publishes_start_once) {
  Keyboard k;
  k.Down("Return"); k.Up("Return");
  SR_CHECK_EQ(k.Poll(), kStart);
  SR_CHECK_EQ(k.Poll(), 0);
}

SR_TEST(mnk_held_buttons_continue_after_pending_consumed) {
  Keyboard k;
  k.Down("Space");
  SR_CHECK_EQ(k.Poll(), kA);
  SR_CHECK_EQ(k.Poll(), kA);
  k.Up("Space");
  SR_CHECK_EQ(k.Poll(), 0);
}

SR_TEST(mnk_separate_resolved_button_edges_share_one_poll) {
  Keyboard k;
  k.Down("Space"); k.Up("Space");
  k.Down("Backspace"); k.Up("Backspace");
  SR_CHECK_EQ(k.Poll(), kA | kB);
  SR_CHECK_EQ(k.Poll(), 0);
}

SR_TEST(mnk_true_modifier_chord_is_preserved_after_release) {
  Keyboard k;
  k.Down("Shift"); k.Down("Up");
  k.Up("Shift"); k.Up("Up");
  SR_CHECK_EQ(k.Poll(), kDpadUp);
  SR_CHECK_EQ(k.Poll(), 0);
}

SR_TEST(mnk_nonconcurrent_modifier_keys_cannot_invent_a_dpad_press) {
  Keyboard k;
  k.Down("Shift"); k.Up("Shift");
  k.Down("Up"); k.Up("Up");
  SR_CHECK_EQ(k.Poll(), 0);
  k.Down("Up"); k.Up("Up");
  k.Down("Shift"); k.Up("Shift");
  SR_CHECK_EQ(k.Poll(), 0);
}

SR_TEST(mnk_binding_lists_and_bare_modifiers_keep_matching_policy) {
  Keyboard k;
  k.binds[1] = "Q,Shift";
  k.Down("Shift"); k.Down("Return");
  k.Up("Shift"); k.Up("Return");
  SR_CHECK_EQ(k.Poll(), kA | kStart);
  k.Down("Ctrl"); k.Down("Return");
  k.Up("Ctrl"); k.Up("Return");
  SR_CHECK_EQ(k.Poll(), 0);
}

SR_TEST(mnk_inactive_poll_and_events_discard_pending_edges) {
  Keyboard k;
  k.Down("Return"); k.Up("Return");
  k.accepting = false;
  SR_CHECK_EQ(k.Poll(), 0);
  k.accepting = true;
  SR_CHECK_EQ(k.Poll(), 0);
  k.Down("Space"); k.Up("Space");
  k.accepting = false;
  k.Down("Return"); k.Up("Return");
  k.accepting = true;
  SR_CHECK_EQ(k.Poll(), 0);
}

SR_TEST(mnk_focus_shutdown_and_disabled_reset_do_not_replay_pending_input) {
  for (int transition = 0; transition != 3; ++transition) {
    Keyboard k;
    k.Down("Return"); k.Up("Return");
    k.Reset();
    SR_CHECK_EQ(k.Poll(), 0);
    k.Down("Space"); k.Up("Space");
    SR_CHECK_EQ(k.Poll(), kA);
  }
}
