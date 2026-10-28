#include "test_main.h"
#include "../../tools/rexglue-write-tracking/include/mnk_bind_match.h"
#include <array>
#include <set>
#include <string>
#include <string_view>

using namespace sr::mnk;
namespace {
// Keys held, by bind name ("W", "Shift", "Up").
struct Held {
  std::set<std::string, std::less<>> keys;
  // The driver parses both names to the same virtual key.
  bool operator()(std::string_view name) const { return keys.count(name == "Control" ? "Ctrl" : name) != 0; }
};
uint8_t Live(const Held& held) {
  return (held("Shift") ? kModShift : 0) | (held("Ctrl") ? kModCtrl : 0) | (held("Alt") ? kModAlt : 0);
}
bool Pressed(std::string_view bind, const Held& held, uint8_t bare_bound) {
  return BindPressed(bind, Live(held), bare_bound, held);
}
}  // namespace

SR_TEST(mnk_bare_modifier_bind_fires_on_its_own) {
  const std::array<std::string_view, 2> binds{"Shift", "W"};
  const uint8_t bare = BareModifierBinds(binds);
  SR_CHECK_EQ(bare, kModShift);
  SR_CHECK(Pressed("Shift", Held{{"Shift"}}, bare));
  SR_CHECK(Pressed("Q,Alt", Held{{"Alt"}}, BareModifierBinds(std::array<std::string_view, 1>{"Q,Alt"})));
  SR_CHECK(Pressed("Ctrl", Held{{"Ctrl"}}, kModCtrl));
  SR_CHECK(Pressed("Control", Held{{"Ctrl"}}, kModCtrl));  // alias of Ctrl
}

SR_TEST(mnk_bare_modifier_does_not_block_plain_binds) {
  // Sprint on Shift while walking on W: both must register.
  const uint8_t bare = kModShift;
  const Held held{{"Shift", "W"}};
  SR_CHECK(Pressed("W", held, bare));
  SR_CHECK(Pressed("Shift", held, bare));
  // A modifier that is not bound on its own still gates plain binds.
  SR_CHECK(!Pressed("W", Held{{"Ctrl", "W"}}, bare));
}

SR_TEST(mnk_default_binds_keep_exact_modifier_matching) {
  // No bare modifier binds: the D-pad shares the arrows through Shift.
  const std::array<std::string_view, 3> binds{"Up", "Shift+Up", "W"};
  const uint8_t bare = BareModifierBinds(binds);
  SR_CHECK_EQ(bare, 0u);
  SR_CHECK(Pressed("Up", Held{{"Up"}}, bare));
  SR_CHECK(!Pressed("Up", Held{{"Shift", "Up"}}, bare));
  SR_CHECK(Pressed("Shift+Up", Held{{"Shift", "Up"}}, bare));
  SR_CHECK(!Pressed("Shift+Up", Held{{"Up"}}, bare));
  SR_CHECK(!Pressed("W", Held{{"Shift", "W"}}, bare));
  SR_CHECK(!Pressed("", Held{{"W"}}, bare));
  SR_CHECK(Pressed(" Q , I ", Held{{"I"}}, bare));     // spaces around alternatives
}
