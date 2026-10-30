#pragma once
#include "shader_capture.h"
namespace superman_returns::graphics::guest {
inline ShaderConstantUsage ConstantUsageForShaders(const ShaderCapture* first,const ShaderCapture* second,
                                                   bool vertex,bool all_bound_resolved,bool enabled=true) {
  if(!enabled || !all_bound_resolved) return {};
  const ShaderCapture *vs=nullptr,*ps=nullptr;
  for(const auto* shader:{first,second}) {
    if(!shader) continue;auto*& stage=shader->vertex?vs:ps;
    if(stage) return {};stage=shader;
  }
  const auto* stage=vertex?vs:ps;return stage?stage->constant_usage:ShaderConstantUsage{};
}
}
