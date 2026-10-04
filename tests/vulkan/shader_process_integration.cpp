#include "platform/shader_process.h"
#include <fstream>
#include <iostream>
#include <thread>
using namespace superman_returns::graphics::vulkan;
namespace shaders=superman_returns::graphics::shaders;
int main(int argc,char** argv) {
  try {
    if(argc==6 && std::string(argv[2])=="--runtime") {
      std::filesystem::path root=std::filesystem::absolute(argv[3]);
      shaders::VulkanShaderConfig config{argv[1],root/"tools/shaders/runtime_vulkan_shader.py",root/"build/vulkan-m3-runtime",root/"build/vulkan-m2/emitter-build/XenosRecompCorpus.exe",root/"build/vulkan-m2/emitter-tree/src/XenosRecomp/shader_common.h",root/".tools/dxc/bin/x64/dxc.exe"};
      shaders::VulkanShaderService service(config,WindowsShaderProcess());
      for(uint32_t i=0;i<2;++i) {
        std::ifstream input(argv[4+i],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
        auto stage=i?shaders::ShaderStage::kPixel:shaders::ShaderStage::kVertex;auto key=service.Request(bytes,stage);
        if(!input || !key || service.Request(bytes,stage)!=key) throw std::runtime_error("Missing input or runtime request failed/did not deduplicate");
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(220);shaders::ShaderResult result;
        do {result=service.Poll(key);if(result.status!=shaders::ShaderPoll::pending) break;std::this_thread::sleep_for(std::chrono::milliseconds(25));} while(std::chrono::steady_clock::now()<deadline);
        if(result.status!=shaders::ShaderPoll::ready || !result.artifact) throw std::runtime_error("Runtime shader compilation failed: "+result.diagnostic);
        std::cout<<"Owned runtime "<<(i?"PS":"VS")<<" compiled: "<<result.artifact->words.size()<<" SPIR-V words, "<<result.artifact->inputs.size()<<" inputs, "<<result.artifact->outputs.size()<<" outputs\n";
      }
      return 0;
    }
    if(argc!=2) throw std::runtime_error("Provide absolute Python executable path, optionally --runtime root VS-container PS-container");
    auto runner=WindowsShaderProcess();std::string error;
    auto directory=std::filesystem::temp_directory_path()/(L"sr-vulkan-Métrô-日本-"+std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);auto output=directory/L"arquivo espaço 日本.txt";
    std::vector<std::filesystem::path> args{argv[1],L"-c",L"import sys;from pathlib import Path;Path(sys.argv[1]).write_text(sys.argv[2],encoding='utf-8')",output,L"quotes \" and trailing slash\\"};
    if(!runner(args,std::chrono::seconds(10),{},error)) throw std::runtime_error(error);
    std::ifstream file(output);std::string content((std::istreambuf_iterator<char>(file)),{});file.close();
    if(content!="quotes \" and trailing slash\\") throw std::runtime_error("Unicode/quoted argument roundtrip failed");
    args={argv[1],L"-c",L"import time;time.sleep(10)"};
    if(runner(args,std::chrono::milliseconds(80),{},error) || error.find("timeout")==std::string::npos) throw std::runtime_error("Compiler timeout not enforced");
    std::stop_source cancel;std::jthread stop([&]{std::this_thread::sleep_for(std::chrono::milliseconds(80));cancel.request_stop();});
    if(runner(args,std::chrono::seconds(10),cancel.get_token(),error) || error.find("cancelled")==std::string::npos) throw std::runtime_error("Compiler cancellation not enforced");
    std::filesystem::remove(output);std::filesystem::remove(directory);
    std::cout<<"Windows shader process Unicode/quoted arguments, timeout and cancellation passed\n";return 0;
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
