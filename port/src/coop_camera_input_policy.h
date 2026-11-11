#pragma once
#include <cstdint>
#include <optional>

namespace superman_returns::coop {
struct CameraInputBinding {
  uint32_t primary = 0, secondary = 0, primary_actor = 0, secondary_actor = 0;
  uint64_t generation = 0;
};
inline uint32_t CameraForInput(const CameraInputBinding& b, uint64_t session,
                              uint32_t native, uint32_t port, uint32_t actor) {
  if (!(session & 1) || (session >> 1) != b.generation ||
      !b.primary || !b.secondary || b.primary == b.secondary ||
      !b.primary_actor || b.primary_actor == 255 ||
      !b.secondary_actor || b.secondary_actor == 255 || b.primary_actor == b.secondary_actor ||
      native != b.primary || port != 1 || actor != b.secondary_actor) return native;
  return b.secondary;
}
// Camera singleton to expose while one actor runs its own update (sub_821BE6C8).
// Only the live, generation-matched P2 actor sees its own manager, and only
// after that manager has produced an aim point this generation. 0 = unchanged.
inline uint32_t CameraForActorUpdate(const CameraInputBinding& b, uint64_t session,
                                     uint32_t singleton, uint32_t actor, bool aim_ready) {
  if (!aim_ready || !actor || actor == 255) return 0;
  const auto routed = CameraForInput(b, session, singleton, 1, actor);
  return routed != singleton ? routed : 0;
}
inline std::optional<uint32_t> CameraModeForLocomotion(uint32_t type) {
  if (type == 0x00464C59) return 0;  // FLY / FlightCamDefsXML (+544)
  if (type == 0x4C4F434F) return 1;  // LOCO / OnFootCamDefsXML (+2032)
  return std::nullopt;
}
// Force sub_82174758 only for a plain FLY(0)/LOCO(1) mismatch with no camera
// transition in progress (manager +16 == 0). Native transition modes (e.g. 8)
// belong to the game.
inline bool ShouldForceLocomotionCamera(std::optional<uint32_t> desired, uint32_t mode, uint32_t state) {
  return desired && state == 0 && (mode == 0 || mode == 1) && mode != *desired;
}
}  // namespace superman_returns::coop
