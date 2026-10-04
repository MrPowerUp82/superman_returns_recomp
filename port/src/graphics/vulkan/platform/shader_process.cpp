#include "shader_process.h"
#include <windows.h>
namespace superman_returns::graphics::vulkan {
namespace {
struct Handle {HANDLE value=nullptr;~Handle(){if(value) CloseHandle(value);}};
std::wstring Quote(std::wstring_view argument) {
  std::wstring out=L"\"";size_t slashes=0;
  for(auto ch:argument) {
    if(ch==L'\\') {++slashes;continue;}
    out.append(slashes*(ch==L'"'?2:1),L'\\');slashes=0;
    if(ch==L'"') out.push_back(L'\\');out.push_back(ch);
  }
  out.append(slashes*2,L'\\');out.push_back(L'"');return out;
}
}
shaders::ShaderProcess WindowsShaderProcess() {
  return [](std::span<const std::filesystem::path> args,std::chrono::milliseconds timeout,std::stop_token cancel,std::string& error) {
    if(args.empty() || args[0].empty() || timeout.count()<=0) {error="Invalid shader process arguments/timeout";return false;}
    std::wstring command;for(auto& arg:args) {if(!command.empty()) command.push_back(L' ');command+=Quote(arg.wstring());}
    if(command.size()>32766) {error="Shader process argument limit exceeded";return false;}
    Handle job{CreateJobObjectW(nullptr,nullptr)};if(!job.value) {error="Cannot create shader process job";return false;}
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) {error="Cannot configure shader process job";return false;}
    STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION info{};
    if(!CreateProcessW(args[0].c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&start,&info)) {error="Shader compiler launch failed Win32="+std::to_string(GetLastError());return false;}
    Handle process{info.hProcess},thread{info.hThread};
    if(!AssignProcessToJobObject(job.value,process.value)) {TerminateProcess(process.value,1);WaitForSingleObject(process.value,5000);error="Cannot attach shader compiler to cancellation job";return false;}
    if(ResumeThread(thread.value)==DWORD(-1)) {TerminateJobObject(job.value,1);WaitForSingleObject(process.value,5000);error="Cannot resume shader compiler";return false;}
    auto deadline=std::chrono::steady_clock::now()+timeout;
    for(;;) {
      auto wait=WaitForSingleObject(process.value,25);
      if(wait==WAIT_OBJECT_0) {DWORD code=1;if(!GetExitCodeProcess(process.value,&code) || code) {error="Shader compiler exited with code "+std::to_string(code);return false;}error.clear();return true;}
      if(wait==WAIT_FAILED || cancel.stop_requested() || std::chrono::steady_clock::now()>=deadline) {
        TerminateJobObject(job.value,1);WaitForSingleObject(process.value,5000);
        error=cancel.stop_requested()?"Shader compiler cancelled":wait==WAIT_FAILED?"Shader compiler wait failed":"Shader compiler timeout";return false;
      }
    }
  };
}
} // namespace superman_returns::graphics::vulkan
