#include "../port/src/graphics/scene_resolution_shader.h"
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc,char** argv) {
  if(argc!=3)return 2;
  std::ifstream in(argv[1],std::ios::binary);
  std::string source{std::istreambuf_iterator<char>(in),{}},out,error;
  if(!in || !superman_returns::graphics::SpecializeSceneResolutionShader(source,out,error)){std::cerr<<error;return 1;}
  std::ofstream f(argv[2],std::ios::binary);
  f<<out<<"\nuint g_SpecConstants() { return g_SpecConstantsRuntime; }\n";
  return f?0:1;
}
