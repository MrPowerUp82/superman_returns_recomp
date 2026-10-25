#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace superman_returns::graphics {
// Audited translated shader contracts. All values are canonical world XYZ,
// after each shader's own transforms/skinning and before projection/fixups.
// See docs/post-vertex-world-contracts-2026-10-24.md for per-family evidence.
inline uint32_t WorldPositionTexcoord(uint64_t shader) {
  switch(shader) {
    case 0x2C45BFFE76D77398ull: return 2;
    case 0x4F17209D364D5B51ull: case 0x18B4F69400B7F0AFull:
    case 0x9A62E7C65051447Aull: case 0x008A68F78E2E3502ull:
    case 0x32EAA2870BB32915ull: case 0x406390546D9691FAull:
    case 0x770469DDF5AB09DCull: case 0x9B2F97272E1F510Bull:
    case 0xC687EB65D635C5D7ull: case 0xC20921D0DBBD2F1Full:
    case 0xE57FB2C0DC954BAAull: case 0xEC4D2CD998957026ull: return 1;
    default: return UINT32_MAX;
  }
}
using Matrix4 = std::array<float, 16>;

inline std::array<float,4> TransformColumn(const Matrix4& m, const std::array<float,4>& v) {
  std::array<float,4> result{};
  for (int r=0;r<4;++r) for(int c=0;c<4;++c) result[r]+=m[r*4+c]*v[c];
  return result;
}

inline bool InvertProjection(const Matrix4& m, Matrix4& inverse) {
  double a[4][8]{};
  for(int r=0;r<4;++r) {
    for(int c=0;c<4;++c) {
      if(!std::isfinite(m[r*4+c])) return false;
      a[r][c]=m[r*4+c];
    }
    a[r][r+4]=1;
  }
  for(int c=0;c<4;++c) {
    int pivot=c;
    for(int r=c+1;r<4;++r) if(std::abs(a[r][c])>std::abs(a[pivot][c])) pivot=r;
    if(std::abs(a[pivot][c])<1e-14) return false;
    if(pivot!=c) for(int k=0;k<8;++k) std::swap(a[pivot][k],a[c][k]);
    const double scale=a[c][c];
    for(int k=0;k<8;++k) a[c][k]/=scale;
    for(int r=0;r<4;++r) if(r!=c) {
      const double f=a[r][c];
      for(int k=0;k<8;++k) a[r][k]-=f*a[c][k];
    }
  }
  Matrix4 result{};
  for(int r=0;r<4;++r) for(int c=0;c<4;++c) {
    result[r*4+c]=float(a[r][c+4]);
    if(!std::isfinite(result[r*4+c])) return false;
  }
  inverse=result;
  return true;
}

struct RecoveredPosition { std::array<float,3> xyz{}; bool valid=false; };

// Applies the inverse of the exact row-dot projection and undoes the two
// final operations of CONAN_RECOMP translated VSs (screen transform, pixel
// center offset). A caller must establish the semantic VP contract by shader;
// invertibility alone does not prove that these are world coordinates.
inline RecoveredPosition RecoverWorldPosition(const Matrix4& inverse,
    std::array<float,4> clip, const std::array<float,4>& screen,
    const std::array<float,2>& pixel) {
  for(float f:clip) if(!std::isfinite(f)) return {};
  for(float f:screen) if(!std::isfinite(f)) return {};
  for(float f:pixel) if(!std::isfinite(f)) return {};
  if(std::abs(screen[0])<1e-12f || std::abs(screen[1])<1e-12f) return {};
  for(int i=0;i<2;++i) clip[i]=(clip[i]-(screen[i+2]+pixel[i])*clip[3])/screen[i];
  const auto world=TransformColumn(inverse,clip);
  if(!std::isfinite(world[3]) || std::abs(world[3])<1e-8f) return {};
  RecoveredPosition result{};
  for(int i=0;i<3;++i) {
    result.xyz[i]=world[i]/world[3];
    if(!std::isfinite(result.xyz[i])) return {};
  }
  result.valid=true;
  return result;
}

inline uint32_t PostVertexCapacity(uint32_t elements, bool triangle_strip) {
  const uint64_t triangles=triangle_strip ? (elements>=3 ? uint64_t(elements)-2 : 0)
                                          : elements/3;
  // BufferFilledSize is a uint32 byte counter, with 12 bytes per float3.
  return triangles <= UINT32_MAX/36 ? uint32_t(triangles*3) : 0;
}
} // namespace superman_returns::graphics
