// observed_instance_lifetimes.h
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>

namespace superman_returns::graphics {

struct ObservedInstanceToken {
    uint32_t address = 0;
    uint64_t generation = 0;
    bool operator==(const ObservedInstanceToken&) const = default;
};

struct ObservedPartSelection {
    uint32_t model = 0, sections = 0, origin = 0, lod_slice = 0, actor_palette = 0;
    uint32_t group = 0, attachment = 0, stream_offset = 0, submesh = 0;
    bool source_route_observed = false;
};

// Immutable frontend observation carried with a draw to the worker. This is
// provenance for diagnostics, not a GeometryHistoryProof or actor class.
struct ObservedRenderInvocation {
    ObservedInstanceToken token;
    uint32_t render = 0, vtable = 0;
    uint32_t packet = 0, packet_word16 = 0; // Observed only; no lifetime/part identity.
    ObservedPartSelection part;
};

// Observations only: Create follows a normal constructor return; Destroy
// precedes cleanup. The caller synchronizes access and handles allocation
// failure without changing guest execution. Tokens prove no actor class or
// draw association on their own; registry instances are separate domains.
class ObservedInstanceLifetimes {
public:
    explicit ObservedInstanceLifetimes(size_t max_capacity = 65536, uint64_t initial_serial = 0)
        : max_capacity_(max_capacity), current_serial_(initial_serial) {}

    std::optional<ObservedInstanceToken> Create(uint32_t address) {
        if (exhausted_ || address < 0x10000 || (address & 0x3) != 0) return std::nullopt;
        auto it = instances_.find(address);
        if (it == instances_.end() && instances_.size() >= max_capacity_) return std::nullopt;
        if (current_serial_ == std::numeric_limits<uint64_t>::max()) {
            exhausted_ = true;
            instances_.clear();
            return std::nullopt;
        }
        const uint64_t next_gen = ++current_serial_;
        if (it != instances_.end()) {
            it->second = next_gen;
        } else {
            instances_.emplace(address, next_gen);
        }
        return ObservedInstanceToken{address, next_gen};
    }

    void Destroy(uint32_t address) {
        if (!exhausted_) instances_.erase(address);
    }

    std::optional<ObservedInstanceToken> Find(uint32_t address) const {
        if (exhausted_) return std::nullopt;
        if (auto it = instances_.find(address); it != instances_.end()) {
            return ObservedInstanceToken{address, it->second};
        }
        return std::nullopt;
    }

    bool IsAlive(ObservedInstanceToken token) const {
        if (exhausted_ || token.address < 0x10000 || token.generation == 0) return false;
        auto it = instances_.find(token.address);
        return it != instances_.end() && it->second == token.generation;
    }

    bool exhausted() const { return exhausted_; }

private:
    size_t max_capacity_;
    uint64_t current_serial_;
    bool exhausted_{false};
    std::unordered_map<uint32_t, uint64_t> instances_;
};

} // namespace superman_returns::graphics
