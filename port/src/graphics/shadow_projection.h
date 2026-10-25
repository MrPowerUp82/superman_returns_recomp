#pragma once
#include <array>
#include <cmath>
namespace superman_returns::graphics {
struct ShadowProjection {std::array<float,4> u{},v{},z{};};
struct ShadowDirection {std::array<float,3> xyz{}; bool valid=false;};
// Audited affine shadow contract: caster w=1; receiver depth minus stored
// depth determines occlusion. A ray towards the light preserves U/V and
// decreases Z. This establishes neither scene coverage nor light intensity.
inline ShadowDirection ShadowRayDirection(const ShadowProjection& p) {
  for(const auto& row:{p.u,p.v,p.z}) for(float f:row) if(!std::isfinite(f)) return {};
  std::array<double,3> d{double(p.u[1])*p.v[2]-double(p.u[2])*p.v[1],
      double(p.u[2])*p.v[0]-double(p.u[0])*p.v[2],double(p.u[0])*p.v[1]-double(p.u[1])*p.v[0]};
  double length2=0,z2=0,along_z=0;
  for(size_t i=0;i<3;++i) {length2+=d[i]*d[i]; z2+=double(p.z[i])*p.z[i]; along_z+=d[i]*p.z[i];}
  if(length2<=1e-40 || z2<=1e-40 || std::abs(along_z)<=std::sqrt(length2*z2)*1e-6) return {};
  const double scale=(along_z>0 ? -1.0 : 1.0)/std::sqrt(length2);
  ShadowDirection result;
  for(size_t i=0;i<3;++i) result.xyz[i]=float(d[i]*scale);
  result.valid=true; return result;
}
inline bool ShadowProjectionsMatch(const ShadowProjection& a,const ShadowProjection& b) {
  if(!ShadowRayDirection(a).valid || !ShadowRayDirection(b).valid) return false;
  const std::array<std::array<float,4>,3> rows_a{a.u,a.v,a.z},rows_b{b.u,b.v,b.z};
  for(size_t row=0;row<3;++row) for(size_t i=0;i<4;++i)
    if(std::abs(rows_a[row][i]-rows_b[row][i])>1e-5f*(1+std::abs(rows_a[row][i]))) return false;
  return true;
}
}
