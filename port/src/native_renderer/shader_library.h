// Pre-translated shader library ("pre-shaders"), the approach of
// StevensND/nfsmw-nx (app/src/nfsmw_shader_library.cpp, nfsmw_shader_hooks.cpp):
// one local file, made from the owner's game by tools/shaders/make_preshaders.py,
// that holds every ORIGINAL shader container next to its offline DXIL. The
// renderer recognises a shader by comparing the container the game hands to the
// XDK creator with those originals, instead of hashing whatever sits in guest
// memory at draw time: Direct3D rewrites containers after creation (vertex-fetch
// patching of vertex shaders, header fields restored by the outer loader), so a
// draw-time hash never matches the corpus.
//
// Pure code, no SDK or xxHash dependency: unit-tested in tests/native.
//
// File format (little-endian, private data derived from the game: never commit
// or embed it):
//   char[8] "SRSHLIB\0", u32 version (1), u32 count, u64 FNV-1a 64 of the body
//   count x { u64 container_hash, u32 stage (0 = vs, 1 = ps), u32 container_size,
//             u32 dxil_size, u32 reserved (0), container bytes, DXIL bytes }
// sorted by (container_hash, stage). container_hash is the corpus key
// (XXH3_64 of the container, artifacts/shaders/catalog.json); it is stored, not
// recomputed, so the reader needs no xxHash.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "shader_container.h"

namespace superman_returns::native {

inline uint64_t Fnv1a64(const uint8_t* data, size_t size, uint64_t h = 0xCBF29CE484222325ull) {
  for (size_t i = 0; i < size; ++i) {
    h ^= data[i];
    h *= 0x100000001B3ull;
  }
  return h;
}

struct PreShader {
  uint64_t container_hash = 0;
  bool vertex = false;
  std::vector<uint8_t> container;  // original big-endian container bytes
  std::vector<uint8_t> dxil;
  uint32_t virtual_size = 0;
};

// How a guest container was recognised (logged, and counted by the tests).
enum class PreShaderMatch {
  kNone,
  kExact,      // the whole container, header included
  kBody,       // everything but the size words (zeroed while the XDK builds it)
  kMicrocode,  // only the physical part (draw-time fallback, pixel shaders)
};

inline const char* PreShaderMatchName(PreShaderMatch m) {
  switch (m) {
    case PreShaderMatch::kExact: return "exact";
    case PreShaderMatch::kBody: return "body";
    case PreShaderMatch::kMicrocode: return "microcode";
    default: return "none";
  }
}

class ShaderLibrary {
 public:
  static constexpr char kMagic[8] = {'S', 'R', 'S', 'H', 'L', 'I', 'B', 0};
  static constexpr uint32_t kVersion = 1;
  static constexpr size_t kHeaderSize = 24;
  static constexpr size_t kEntryHeaderSize = 24;
  static constexpr size_t kMaxFile = 256u << 20;
  static constexpr size_t kMaxShaders = 16384;
  static constexpr size_t kMaxDxil = 16u << 20;
  // Bytes after the size words used as the lookup key of a body match (fewer
  // for containers shorter than kBodyStart + kBodyKeyBytes).
  static constexpr size_t kBodyKeyBytes = 64;
  // Smallest container accepted: header words plus a few dwords of microcode.
  static constexpr size_t kMinContainer = 24;
  // Header words that the XDK may leave zeroed while it builds a shader:
  // +4 virtualSize, +8 physicalSize.
  static constexpr size_t kBodyStart = 12;

  // Transactional: on failure the library keeps its previous contents and
  // `error` says why.
  bool Load(const uint8_t* data, size_t size, std::string* error = nullptr) {
    std::vector<PreShader> shaders;
    if (!Parse(data, size, shaders, error)) return false;
    shaders_ = std::move(shaders);
    BuildIndexes();
    return true;
  }

  size_t size() const { return shaders_.size(); }
  const std::vector<PreShader>& shaders() const { return shaders_; }

  // DXIL of a corpus shader (pipeline creation and precompilation).
  const PreShader* Find(uint64_t container_hash, bool vertex) const {
    auto it = std::lower_bound(shaders_.begin(), shaders_.end(), container_hash,
                               [](const PreShader& s, uint64_t h) { return s.container_hash < h; });
    for (; it != shaders_.end() && it->container_hash == container_hash; ++it) {
      if (it->vertex == vertex) return &*it;
    }
    return nullptr;
  }

  // Recognises a container in guest memory. `available` bounds the readable
  // bytes from `p`. Never reads more than the candidate's own length, and every
  // match is confirmed byte by byte (a hash collision cannot pick another shader).
  const PreShader* Identify(const uint8_t* p, size_t available,
                            PreShaderMatch* how = nullptr) const {
    if (how) *how = PreShaderMatch::kNone;
    if (!p || available < kMinContainer) return nullptr;
    const uint32_t flags = LoadBigEndian32(p);
    if ((flags & 0xFFFFFF00u) != 0x102A1100u) return nullptr;
    const bool vertex = (flags & 1) != 0;

    // 1. Header sizes present: the whole container.
    ShaderContainerHeader header;
    if (ParseShaderContainerHeader(p, available, header)) {
      auto range = exact_.equal_range(Fnv1a64(p, header.total_size()));
      for (auto it = range.first; it != range.second; ++it) {
        const PreShader& s = shaders_[it->second];
        if (s.vertex == vertex && s.container.size() == header.total_size() &&
            std::memcmp(s.container.data(), p, s.container.size()) == 0) {
          if (how) *how = PreShaderMatch::kExact;
          return &s;
        }
      }
    }

    // 2. Size words zeroed or stale: key on the bytes that follow them and
    // compare the rest of each candidate.
    const PreShader* found = nullptr;
    for (size_t key_bytes : body_key_lengths_) {
      if (available < kBodyStart + key_bytes) continue;
      auto range = body_.equal_range(BodyKey(p + kBodyStart, key_bytes));
      for (auto it = range.first; it != range.second; ++it) {
        const PreShader& s = shaders_[it->second];
        if (s.vertex != vertex || s.container.size() > available) continue;
        if (std::memcmp(s.container.data() + kBodyStart, p + kBodyStart,
                        s.container.size() - kBodyStart) != 0) {
          continue;
        }
        // Two originals that differ only in their size words cannot both be
        // whole containers; prefer the longest (it contains the other).
        if (!found || s.container.size() > found->container.size()) found = &s;
      }
    }
    if (found && how) *how = PreShaderMatch::kBody;
    return found;
  }

  // Draw-time fallback: the physical part (microcode) of a container whose
  // header is intact but whose virtual part was patched in place. Only unique
  // matches are accepted. Vertex shaders rarely match: Direct3D rewrites their
  // vertex fetches when they are bound.
  const PreShader* IdentifyMicrocode(const uint8_t* p, size_t available, bool vertex,
                                     PreShaderMatch* how = nullptr) const {
    if (how) *how = PreShaderMatch::kNone;
    ShaderContainerHeader header;
    if (!ParseShaderContainerHeader(p, available, header) || header.is_vertex != vertex) {
      return nullptr;
    }
    const uint8_t* ucode = p + header.virtual_size;
    auto range = microcode_.equal_range(Fnv1a64(ucode, header.physical_size));
    const PreShader* found = nullptr;
    for (auto it = range.first; it != range.second; ++it) {
      const PreShader& s = shaders_[it->second];
      if (s.vertex != vertex || s.container.size() - s.virtual_size != header.physical_size ||
          std::memcmp(s.container.data() + s.virtual_size, ucode, header.physical_size) != 0) {
        continue;
      }
      // Same microcode, different constant layout: ambiguous, refuse.
      if (found && found->container_hash != s.container_hash) return nullptr;
      found = &s;
    }
    if (found && how) *how = PreShaderMatch::kMicrocode;
    return found;
  }

  // Writer (tests; tools/shaders/make_preshaders.py writes the same format).
  static std::vector<uint8_t> Serialize(std::vector<PreShader> shaders) {
    std::sort(shaders.begin(), shaders.end(), Less);
    std::vector<uint8_t> body;
    for (const PreShader& s : shaders) {
      Put64(body, s.container_hash);
      Put32(body, s.vertex ? 0 : 1);
      Put32(body, uint32_t(s.container.size()));
      Put32(body, uint32_t(s.dxil.size()));
      Put32(body, 0);
      body.insert(body.end(), s.container.begin(), s.container.end());
      body.insert(body.end(), s.dxil.begin(), s.dxil.end());
    }
    std::vector<uint8_t> out(kMagic, kMagic + 8);
    Put32(out, kVersion);
    Put32(out, uint32_t(shaders.size()));
    Put64(out, Fnv1a64(body.data(), body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
  }

 private:
  static bool Less(const PreShader& a, const PreShader& b) {
    if (a.container_hash != b.container_hash) return a.container_hash < b.container_hash;
    return a.vertex > b.vertex;  // stage 0 (vs) first
  }
  static uint32_t Get32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;  // the formats here are little-endian, like the hosts that run them
  }
  static uint64_t Get64(const uint8_t* p) { return Get32(p) | uint64_t(Get32(p + 4)) << 32; }
  static void Put32(std::vector<uint8_t>& d, uint32_t v) {
    for (int i = 0; i < 4; ++i) d.push_back(uint8_t(v >> (8 * i)));
  }
  static void Put64(std::vector<uint8_t>& d, uint64_t v) {
    Put32(d, uint32_t(v));
    Put32(d, uint32_t(v >> 32));
  }

  static bool Fail(std::string* error, const char* why) {
    if (error) *error = why;
    return false;
  }

  static bool Parse(const uint8_t* data, size_t size, std::vector<PreShader>& out,
                    std::string* error) {
    if (!data || size < kHeaderSize || size > kMaxFile) return Fail(error, "bad file size");
    if (std::memcmp(data, kMagic, 8) != 0) return Fail(error, "not a shader library (magic)");
    if (Get32(data + 8) != kVersion) return Fail(error, "unsupported library version");
    const uint32_t count = Get32(data + 12);
    if (!count || count > kMaxShaders) return Fail(error, "bad shader count");
    if (Fnv1a64(data + kHeaderSize, size - kHeaderSize) != Get64(data + 16)) {
      return Fail(error, "checksum mismatch (truncated or altered file)");
    }
    size_t pos = kHeaderSize;
    out.clear();
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
      if (size - pos < kEntryHeaderSize) return Fail(error, "truncated entry");
      PreShader s;
      s.container_hash = Get64(data + pos);
      const uint32_t stage = Get32(data + pos + 8);
      const uint32_t csize = Get32(data + pos + 12);
      const uint32_t dsize = Get32(data + pos + 16);
      pos += kEntryHeaderSize;
      if (stage > 1 || dsize == 0 || dsize > kMaxDxil || csize < kMinContainer ||
          csize > kMaxShaderContainerBytes) {
        return Fail(error, "entry out of bounds");
      }
      if (size - pos < uint64_t(csize) + dsize) return Fail(error, "truncated entry data");
      ShaderContainerHeader h;
      if (!ParseShaderContainerHeader(data + pos, csize, h) || h.total_size() != csize ||
          h.is_vertex != (stage == 0)) {
        return Fail(error, "entry is not a whole container of its stage");
      }
      s.vertex = stage == 0;
      s.virtual_size = h.virtual_size;
      s.container.assign(data + pos, data + pos + csize);
      pos += csize;
      s.dxil.assign(data + pos, data + pos + dsize);
      pos += dsize;
      if (!out.empty() && !Less(out.back(), s)) return Fail(error, "entries repeated or unsorted");
      out.push_back(std::move(s));
    }
    if (pos != size) return Fail(error, "trailing data");
    return true;
  }

  // The key length is part of the key: a short container cannot collide with
  // the first bytes of a longer one under another length.
  static uint64_t BodyKey(const uint8_t* body, size_t key_bytes) {
    return Fnv1a64(body, key_bytes) ^ (uint64_t(key_bytes) << 56);
  }

  void BuildIndexes() {
    exact_.clear();
    body_.clear();
    microcode_.clear();
    body_key_lengths_.clear();
    for (size_t i = 0; i < shaders_.size(); ++i) {
      const PreShader& s = shaders_[i];
      const size_t key_bytes = std::min(kBodyKeyBytes, s.container.size() - kBodyStart);
      exact_.emplace(Fnv1a64(s.container.data(), s.container.size()), i);
      body_.emplace(BodyKey(s.container.data() + kBodyStart, key_bytes), i);
      if (std::find(body_key_lengths_.begin(), body_key_lengths_.end(), key_bytes) ==
          body_key_lengths_.end()) {
        body_key_lengths_.push_back(key_bytes);
      }
      microcode_.emplace(Fnv1a64(s.container.data() + s.virtual_size,
                                 s.container.size() - s.virtual_size),
                         i);
    }
  }

  std::vector<PreShader> shaders_;
  std::unordered_multimap<uint64_t, size_t> exact_, body_, microcode_;
  std::vector<size_t> body_key_lengths_;
};

}  // namespace superman_returns::native
