#include "shader_translator.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hash.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(sr_native_runtime_shaders, true, "Superman Returns Native",
                    "Translate the game's shaders on this machine when they are not in the "
                    "pre-shader library or the embedded pack (shader_tools/ next to the "
                    "executable), caching the DXIL in shader_cache/")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_INT32(sr_native_runtime_shader_threads, 2, "Superman Returns Native",
                     "Background threads translating shaders (each runs two child processes in turn)")
    .range(1, 8)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_INT32(sr_native_runtime_shader_wait_ms, 4000, "Superman Returns Native",
                     "A draw whose shader is still being translated waits this long for it "
                     "(0 = skip the draw until the shader is ready)")
    .range(0, 60000);
REXCVAR_DEFINE_STRING(sr_native_shader_tools_dir, "", "Superman Returns Native",
                      "Folder with sr_xenosrecomp.exe, dxc.exe, dxcompiler.dll, dxil.dll and "
                      "shader_common.h; default shader_tools/ next to the executable, then the "
                      "development tool folders under .tools/")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(sr_native_shader_cache_dir, "", "Superman Returns Native",
                      "Translated shader cache; default shader_cache/ next to the executable "
                      "(or %LOCALAPPDATA%\\superman_returns when that is not writable)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace superman_returns::native {
namespace {

namespace fs = std::filesystem;

struct Tools {
  bool ok = false;
  fs::path translator, dxc, common;
  fs::path cache;  // fingerprinted cache folder
  std::string include_text;
  std::string why;
};

bool WritableDir(const fs::path& dir) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  const fs::path probe = dir / ".write_test";
  std::ofstream f(probe, std::ios::binary);
  if (!f) return false;
  f << "x";
  f.close();
  fs::remove(probe, ec);
  return true;
}

bool ReadFile(const fs::path& p, std::vector<uint8_t>& out) {
  std::ifstream f(p, std::ios::binary | std::ios::ate);
  if (!f) return false;
  const auto n = f.tellg();
  if (n < 0) return false;
  out.resize(size_t(n));
  f.seekg(0);
  return n == 0 || bool(f.read(reinterpret_cast<char*>(out.data()), n));
}

fs::path FirstWith(const std::vector<fs::path>& dirs, const char* file) {
  for (const auto& d : dirs) {
    std::error_code ec;
    if (fs::exists(d / file, ec)) return d / file;
  }
  return {};
}

Tools FindTools() {
  Tools t;
  const fs::path exe = rex::filesystem::GetExecutableFolder();
  std::vector<fs::path> dirs;
  const std::string cvar = REXCVAR_GET(sr_native_shader_tools_dir);
  if (!cvar.empty()) {
    dirs.emplace_back(cvar);
  } else {
    dirs.push_back(exe / "shader_tools");
    // Development layout: port/out/build/<preset>/superman_returns.exe.
    const fs::path repo = (exe / ".." / ".." / ".." / "..").lexically_normal();
    dirs.push_back(repo / ".tools" / "xenosrecomp" / "build");
    dirs.push_back(repo / ".tools" / "dxc" / "bin" / "x64");
    dirs.push_back(repo / ".tools" / "xenosrecomp" / "src" / "XenosRecomp");
  }
  t.translator = FirstWith(dirs, "sr_xenosrecomp.exe");
  if (t.translator.empty()) t.translator = FirstWith(dirs, "XenosRecompCorpus.exe");
  t.dxc = FirstWith(dirs, "dxc.exe");
  t.common = FirstWith(dirs, "shader_common.h");
  if (t.translator.empty() || t.dxc.empty() || t.common.empty()) {
    t.why = fmt::format("missing in {}: {}{}{}", dirs.empty() ? "?" : dirs.front().string(),
                        t.translator.empty() ? "sr_xenosrecomp.exe " : "",
                        t.dxc.empty() ? "dxc.exe " : "", t.common.empty() ? "shader_common.h" : "");
    return t;
  }
  // The cache folder name fingerprints the tools: a new emitter, header or
  // compiler invalidates every cached DXIL.
  std::vector<uint8_t> common_bytes;
  ReadFile(t.common, common_bytes);
  std::error_code ec;
  uint64_t parts[4] = {XXH3_64bits(common_bytes.data(), common_bytes.size()),
                       uint64_t(fs::file_size(t.translator, ec)),
                       uint64_t(fs::file_size(t.dxc, ec)),
                       uint64_t(fs::file_size(t.dxc.parent_path() / "dxcompiler.dll", ec))};
  const std::string fingerprint = fmt::format("{:08x}", uint32_t(XXH3_64bits(parts, sizeof(parts))));

  fs::path root;
  const std::string cache_cvar = REXCVAR_GET(sr_native_shader_cache_dir);
  if (!cache_cvar.empty()) {
    root = cache_cvar;
  } else {
    root = exe / "shader_cache";
    if (!WritableDir(root)) {
      char* local = nullptr;
      size_t len = 0;
      if (_dupenv_s(&local, &len, "LOCALAPPDATA") == 0 && local) {
        root = fs::path(local) / "superman_returns" / "shader_cache";
        free(local);
      }
    }
  }
  t.cache = root / fingerprint;
  if (!WritableDir(t.cache) || !WritableDir(t.cache / "tmp")) {
    t.why = "cache folder not writable: " + t.cache.string();
    return t;
  }
  t.include_text = "#define CONAN_RECOMP 1\n";
  t.include_text.append(reinterpret_cast<const char*>(common_bytes.data()), common_bytes.size());
  t.ok = true;
  return t;
}

// Runs a child process hidden at below-normal priority, stdout and stderr
// going to `log`. Returns false on a launch failure or timeout.
bool RunProcess(const fs::path& exe, const std::wstring& args, const fs::path& log,
                DWORD timeout_ms, DWORD* exit_code) {
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE out = CreateFileW(log.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = INVALID_HANDLE_VALUE;
  si.hStdOutput = out;
  si.hStdError = out;
  std::wstring cmd = L"\"" + exe.wstring() + L"\" " + args;
  PROCESS_INFORMATION pi{};
  const BOOL created = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                                      CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr,
                                      exe.parent_path().wstring().c_str(), &si, &pi);
  if (!created) {
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    return false;
  }
  const DWORD wait = WaitForSingleObject(pi.hProcess, timeout_ms);
  bool ok = true;
  if (wait != WAIT_OBJECT_0) {
    TerminateProcess(pi.hProcess, 0xDEAD);
    WaitForSingleObject(pi.hProcess, 5000);
    ok = false;
  }
  DWORD code = 0xFFFFFFFF;
  GetExitCodeProcess(pi.hProcess, &code);
  *exit_code = code;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
  return ok;
}

std::string FirstErrorLine(const fs::path& log) {
  std::ifstream f(log);
  std::string line, last;
  while (std::getline(f, line)) {
    if (line.find("error") != std::string::npos || line.find("ssert") != std::string::npos) {
      return line.substr(0, 200);
    }
    if (!line.empty()) last = line;
  }
  return last.substr(0, 200);
}

struct Entry {
  bool vertex = false;
  std::vector<uint8_t> container;
  TranslateState state = TranslateState::kPending;
  std::shared_ptr<std::vector<uint8_t>> dxil;
  bool demanded = false;
};

class Impl {
 public:
  Impl() {
    if (!REXCVAR_GET(sr_native_runtime_shaders)) {
      REXLOG_INFO("runtime shaders: disabled (sr_native_runtime_shaders=false)");
      return;
    }
    tools_ = FindTools();
    if (!tools_.ok) {
      REXLOG_WARN("runtime shaders: unavailable ({}); only the pre-shader library and the "
                  "embedded pack can supply shaders", tools_.why);
      return;
    }
    REXLOG_INFO("runtime shaders: {} + {}, cache {}", tools_.translator.filename().string(),
                tools_.dxc.filename().string(), tools_.cache.string());
    const int n = std::clamp(REXCVAR_GET(sr_native_runtime_shader_threads), 1, 8);
    for (int i = 0; i < n; ++i) threads_.emplace_back([this] { Worker(); });
    enabled_ = true;
  }

  ~Impl() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    work_cv_.notify_all();
    for (auto& t : threads_) t.detach();  // a child process may still be running at exit
  }

  bool enabled() const { return enabled_; }

  void Offer(uint64_t hash, bool vertex, const uint8_t* data, size_t size) {
    if (!enabled_ || !size) return;
    const uint64_t key = Key(hash, vertex);
    std::lock_guard<std::mutex> lock(mutex_);
    if (entries_.count(key)) return;
    Entry& e = entries_[key];
    e.vertex = vertex;
    if (fs::exists(DxilPath(hash, vertex))) {
      e.state = TranslateState::kReady;  // loaded on first Get
      return;
    }
    if (fs::exists(FailedPath(hash, vertex))) {
      e.state = TranslateState::kFailed;
      return;
    }
    e.container.assign(data, data + size);
    e.state = TranslateState::kPending;
    low_.push_back(key);
    work_cv_.notify_one();
  }

  TranslateState Fetch(uint64_t hash, bool vertex, std::vector<uint8_t>& dxil) {
    if (!enabled_) return TranslateState::kUnknown;
    const uint64_t key = Key(hash, vertex);
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    if (it == entries_.end()) {
      // Cached by an earlier run but not offered yet (the shader was created
      // before this run's hooks ran, or by another route).
      std::vector<uint8_t> bytes;
      lock.unlock();
      if (ReadFile(DxilPath(hash, vertex), bytes) && !bytes.empty()) {
        dxil = std::move(bytes);
        return TranslateState::kReady;
      }
      return TranslateState::kUnknown;
    }
    Entry& e = it->second;
    if (e.state == TranslateState::kPending && !e.demanded) {
      e.demanded = true;
      high_.push_back(key);
      work_cv_.notify_one();
    }
    const int wait_ms = REXCVAR_GET(sr_native_runtime_shader_wait_ms);
    if (e.state == TranslateState::kPending && wait_ms > 0) {
      done_cv_.wait_for(lock, std::chrono::milliseconds(wait_ms), [&] {
        auto f = entries_.find(key);
        return f == entries_.end() || f->second.state != TranslateState::kPending;
      });
    }
    Entry& after = entries_[key];
    if (after.state == TranslateState::kReady && !after.dxil) {
      lock.unlock();
      std::vector<uint8_t> bytes;
      if (ReadFile(DxilPath(hash, vertex), bytes) && !bytes.empty()) {
        lock.lock();
        entries_[key].dxil = std::make_shared<std::vector<uint8_t>>(std::move(bytes));
      } else {
        lock.lock();
        entries_[key].state = TranslateState::kFailed;
      }
    }
    Entry& fin = entries_[key];
    if (fin.state == TranslateState::kReady && fin.dxil) dxil = *fin.dxil;
    return fin.state;
  }

  bool LoadCached(uint64_t hash, bool vertex, std::vector<uint8_t>& dxil) {
    if (!enabled_) return false;
    return ReadFile(DxilPath(hash, vertex), dxil) && !dxil.empty();
  }

 private:
  static uint64_t Key(uint64_t hash, bool vertex) { return hash ^ (vertex ? 0x9E3779B97F4A7C15ull : 0); }

  std::string Stem(uint64_t hash, bool vertex) const {
    return fmt::format("{:016X}.{}", hash, vertex ? "vs" : "ps");
  }
  fs::path DxilPath(uint64_t hash, bool vertex) const { return tools_.cache / (Stem(hash, vertex) + ".dxil"); }
  fs::path FailedPath(uint64_t hash, bool vertex) const { return tools_.cache / (Stem(hash, vertex) + ".failed"); }

  void Worker() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;) {
      uint64_t key = 0;
      std::vector<uint8_t> container;
      bool vertex = false;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        work_cv_.wait(lock, [&] { return stop_ || !high_.empty() || !low_.empty(); });
        if (stop_) return;
        auto& q = !high_.empty() ? high_ : low_;
        key = q.front();
        q.pop_front();
        Entry& e = entries_[key];
        if (e.state != TranslateState::kPending || e.container.empty()) continue;
        container = std::move(e.container);
        e.container.clear();
        vertex = e.vertex;
        ++active_;
      }
      const uint64_t hash = key ^ (vertex ? 0x9E3779B97F4A7C15ull : 0);
      const auto start = std::chrono::steady_clock::now();
      std::string reason;
      std::vector<uint8_t> dxil;
      const bool ok = Translate(hash, vertex, container, dxil, reason);
      const double ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - start).count();
      {
        std::lock_guard<std::mutex> lock(mutex_);
        Entry& e = entries_[key];
        --active_;
        if (ok) {
          e.state = TranslateState::kReady;
          e.dxil = std::make_shared<std::vector<uint8_t>>(std::move(dxil));
          ++translated_;
          total_ms_ += ms;
        } else {
          e.state = TranslateState::kFailed;
          ++failed_;
        }
        const uint32_t done = translated_ + failed_;
        if (!ok) {
          REXLOG_WARN("runtime shaders: {} failed: {}", Stem(hash, vertex), reason);
        }
        if (done <= 3 || (done % 25) == 0) {
          REXLOG_INFO("runtime shaders: {} translated, {} failed, {} queued, avg {:.0f} ms",
                      translated_, failed_, high_.size() + low_.size() + active_,
                      translated_ ? total_ms_ / translated_ : 0.0);
        }
      }
      done_cv_.notify_all();
    }
  }

  bool Translate(uint64_t hash, bool vertex, const std::vector<uint8_t>& container,
                 std::vector<uint8_t>& dxil, std::string& reason) {
    const std::string stem = Stem(hash, vertex);
    const fs::path tmp = tools_.cache / "tmp";
    const fs::path bin = tmp / (stem + ".bin");
    const fs::path hlsl = tmp / (stem + ".hlsl");
    const fs::path std_hlsl = tmp / (stem + ".std.hlsl");
    const fs::path dx = tmp / (stem + ".dxil");
    const fs::path log1 = tmp / (stem + ".translate.log");
    const fs::path log2 = tmp / (stem + ".dxc.log");
    std::error_code ec;
    auto cleanup = [&] {
      for (const auto& p : {bin, hlsl, std_hlsl, dx}) fs::remove(p, ec);
    };
    {
      std::ofstream f(bin, std::ios::binary);
      if (!f) {
        reason = "cannot write " + bin.string();
        return false;
      }
      f.write(reinterpret_cast<const char*>(container.data()), std::streamsize(container.size()));
    }
    DWORD code = 0;
    // 1. container -> HLSL (XenosRecomp emitter, includes written next to it).
    const std::wstring args1 = L"\"" + tools_.common.wstring() + L"\" \"" + tmp.wstring() +
                               L"\" \"" + bin.wstring() + L"\"";
    if (!RunProcess(tools_.translator, args1, log1, 60000, &code) || code != 0 ||
        !fs::exists(hlsl)) {
      reason = "translate: " + FirstErrorLine(log1) + fmt::format(" (exit {:#x})", code);
      WriteFailed(hash, vertex, reason);
      cleanup();
      return false;
    }
    std::vector<uint8_t> src;
    ReadFile(hlsl, src);
    {
      std::ofstream f(std_hlsl, std::ios::binary);
      f.write(reinterpret_cast<const char*>(src.data()), std::streamsize(src.size()));
      // The spec constants come from the shared constant buffer at runtime,
      // so one DXIL serves every variant (tools/shaders/build_catalog.py).
      static const char kSpec[] =
          "\n#ifndef __spirv__\n#ifdef CONAN_RECOMP\n"
          "uint g_SpecConstants() { return g_SpecConstantsRuntime; }\n#else\n"
          "uint g_SpecConstants() { return 0; }\n#endif\n#endif\n";
      f.write(kSpec, sizeof(kSpec) - 1);
    }
    // 2. HLSL -> DXIL (signed: dxil.dll sits next to dxcompiler.dll).
    const std::wstring args2 = std::wstring(L"-T ") + (vertex ? L"vs_6_0" : L"ps_6_0") +
                               L" -HV 2021 -all-resources-bound -Wno-ignored-attributes "
                               L"-Qstrip_debug \"" + std_hlsl.wstring() + L"\" -Fo \"" +
                               dx.wstring() + L"\"";
    if (!RunProcess(tools_.dxc, args2, log2, 120000, &code) || code != 0 || !fs::exists(dx)) {
      reason = "dxc: " + FirstErrorLine(log2) + fmt::format(" (exit {:#x})", code);
      WriteFailed(hash, vertex, reason);
      cleanup();
      return false;
    }
    if (!ReadFile(dx, dxil) || dxil.empty()) {
      reason = "dxc produced no output";
      cleanup();
      return false;
    }
    // Publish atomically so a crash never leaves a half-written cache file.
    const fs::path final_path = DxilPath(hash, vertex);
    fs::rename(dx, final_path, ec);
    if (ec) {
      fs::copy_file(dx, final_path, fs::copy_options::overwrite_existing, ec);
    }
    cleanup();
    return true;
  }

  void WriteFailed(uint64_t hash, bool vertex, const std::string& reason) {
    std::ofstream f(FailedPath(hash, vertex));
    f << reason << "\n";
  }

  Tools tools_;
  bool enabled_ = false;
  bool stop_ = false;
  std::mutex mutex_;
  std::condition_variable work_cv_, done_cv_;
  std::unordered_map<uint64_t, Entry> entries_;
  std::deque<uint64_t> high_, low_;
  std::vector<std::thread> threads_;
  uint32_t translated_ = 0, failed_ = 0, active_ = 0;
  double total_ms_ = 0.0;
};

Impl& Instance() {
  static Impl* impl = new Impl();  // never destroyed: workers may still be running at exit
  return *impl;
}

}  // namespace

ShaderTranslator& ShaderTranslator::Get() {
  static ShaderTranslator t;
  return t;
}

bool ShaderTranslator::Enabled() { return Instance().enabled(); }

void ShaderTranslator::Offer(uint64_t hash, bool vertex, const uint8_t* container, size_t size) {
  Instance().Offer(hash, vertex, container, size);
}

TranslateState ShaderTranslator::Fetch(uint64_t hash, bool vertex, std::vector<uint8_t>& dxil) {
  return Instance().Fetch(hash, vertex, dxil);
}

bool ShaderTranslator::LoadCached(uint64_t hash, bool vertex, std::vector<uint8_t>& dxil) {
  return Instance().LoadCached(hash, vertex, dxil);
}

}  // namespace superman_returns::native
