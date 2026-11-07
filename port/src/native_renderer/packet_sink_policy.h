#pragma once

#include "../graphics/guest/captured_batch.h"
#include <cstdint>
#include <cstdlib>

namespace superman_returns::native {

enum class PacketFailureStage : uint8_t { None = 0, Decode, Sink };

inline const char* PacketFailureStageToString(PacketFailureStage stage) {
  switch (stage) {
    case PacketFailureStage::None:
      return "None";
    case PacketFailureStage::Decode:
      return "Decode";
    case PacketFailureStage::Sink:
      return "Sink";
    default:
      return "Unknown";
  }
}

struct PacketSinkDisposition {
  bool skip_command = false;
  bool quarantine_sink = false;
  bool notify_cancel = false;
  bool fatal_abort = false;
  PacketFailureStage stage = PacketFailureStage::None;
};

inline bool StrictPacketDecodeEnabled() {
  const char* strict = std::getenv("SR_NATIVE_STRICT_PACKET_DECODE");
  return strict && *strict && *strict != '0';
}

inline bool AllowNonfatalSinkQuarantine() {
  const char* nonfatal = std::getenv("SR_NATIVE_NONFATAL_SINK_QUARANTINE");
  if (nonfatal && *nonfatal) return *nonfatal != '0';
  return !StrictPacketDecodeEnabled();
}

inline PacketSinkDisposition EvaluatePacketSinkFailure(
    bool decode_ok, bool sink_ok, graphics::guest::Op op, bool worker_stop,
    bool allow_nonfatal_sink_quarantine = true) {
  PacketSinkDisposition disposition{};

  if (decode_ok && sink_ok) {
    disposition.stage = PacketFailureStage::None;
    return disposition;
  }

  if (!decode_ok) {
    disposition.stage = PacketFailureStage::Decode;
    if (op != graphics::guest::Op::kSwap && allow_nonfatal_sink_quarantine) {
      disposition.skip_command = true;
      return disposition;
    }
    disposition.quarantine_sink = true;
    disposition.notify_cancel = true;
    disposition.fatal_abort = !worker_stop && !allow_nonfatal_sink_quarantine;
    return disposition;
  }

  disposition.stage = PacketFailureStage::Sink;
  disposition.quarantine_sink = true;
  disposition.notify_cancel = true;
  disposition.fatal_abort = !worker_stop && !allow_nonfatal_sink_quarantine;
  return disposition;
}

}  // namespace superman_returns::native
