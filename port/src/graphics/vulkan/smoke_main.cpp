#include "frame_loop.h"
#include "platform/win32_surface.h"
#include "smoke_options.h"
#include "triangle.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace superman_returns::graphics::vulkan;
namespace {
int Run(const SmokeOptions &o,
        const std::function<void(const std::string &)> &log) {
  Win32Window window;
  Context context;
  context.logger = log;
  Error e;
  auto fail = [&]() {
    log("ERROR " + e.operation + ": " + e.message);
    return 1;
  };
  const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                              VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
  if (!context.CreateInstance(extensions, o.validation, e))
    return fail();
  if (o.list) {
    std::vector<DeviceCandidate> devices;
    if (!context.EnumerateCandidates(VK_NULL_HANDLE, devices, e))
      return fail();
    for (auto &d : devices)
      log("GPU " + d.uuid + " | " + d.name +
          " | API=" + std::to_string(VK_VERSION_MAJOR(d.api_version)) + "." +
          std::to_string(VK_VERSION_MINOR(d.api_version)) +
          " swapchain=" + (d.swapchain ? "yes" : "no"));
    return 0;
  }
  if (!window.Open(1280, 720, e))
    return fail();
  auto surface = window.CreateSurface(context, e);
  if (!surface)
    return fail();
  if (!context.OpenDevice(surface, o.uuid, e))
    return fail();
  wchar_t module[32768];
  auto length = GetModuleFileNameW(nullptr, module, 32768);
  if (!length || length >= 32768) {
    e = {"Executable path", VK_ERROR_INITIALIZATION_FAILED,
         "Unable to resolve shader directory"};
    return fail();
  }
  auto dir = std::filesystem::path(module).parent_path() / "shaders";
  std::vector<uint32_t> vs, ps;
  if (!ReadSpirv(dir / "triangle.vert.spv", vs, e) ||
      !ReadSpirv(dir / "triangle.frag.spv", ps, e))
    return fail();
  Swapchain chain;
  Triangle triangle;
  FrameLoop loop;
  VkExtent2D last{};
  auto rebuild = [&](VkExtent2D extent) {
    if (!loop.Retire(context, e))
      return false;
    triangle.Destroy();
    if (!chain.Recreate(context, extent, true, e))
      return false;
    last = extent;
    if (!chain.handle)
      return true;
    return triangle.Initialize(context, chain.render_pass, vs, ps, e) &&
           loop.Initialize(context, chain, e);
  };
  auto started = std::chrono::steady_clock::now();
  int phase = 0;
  bool minimized = false;
  auto restore_at = started;
  bool recreate = true;
  while (true) {
    auto events = window.PumpEvents();
    if (events.close_requested) {
      if (o.frames && loop.presented < o.frames) {
        e = {"Smoke test", VK_ERROR_INITIALIZATION_FAILED,
             "Window closed before requested frames"};
        return fail();
      }
      break;
    }
    auto now = std::chrono::steady_clock::now();
    if (o.self_test && now - started > std::chrono::seconds(30)) {
      e = {"Smoke test", VK_TIMEOUT, "Self-test timed out"};
      return fail();
    }
    if (o.self_test) {
      if (phase == 0 && loop.presented >= 20) {
        log("self-test phase initial passed");
        window.Resize(960, 540);
        phase = 1;
        continue;
      }
      if (phase == 1 && loop.presented >= 40) {
        log("self-test phase resize-960 passed");
        window.Resize(640, 480);
        phase = 2;
        continue;
      }
      if (phase == 2 && loop.presented >= 60) {
        log("self-test phase resize-640 passed");
        window.Minimize();
        restore_at = now + std::chrono::milliseconds(250);
        phase = 3;
        continue;
      }
      if (phase == 3 && now >= restore_at) {
        if (!minimized) {
          e = {"Self-test minimize", VK_ERROR_INITIALIZATION_FAILED,
               "Zero extent not observed"};
          return fail();
        }
        window.Restore();
        log("self-test phase minimize passed");
        phase = 4;
        continue;
      }
    }
    if (recreate || events.extent.width != last.width ||
        events.extent.height != last.height) {
      if (!rebuild(events.extent))
        return fail();
      recreate = false;
    }
    if (!events.extent.width || !events.extent.height) {
      minimized = true;
      window.WaitEvents();
      continue;
    }
    auto outcome = loop.Draw(
        context, chain,
        [&](auto command, auto) {
          triangle.Record(command, chain.choice.extent);
        },
        e);
    if (outcome == FrameOutcome::kFailed)
      return fail();
    if (outcome == FrameOutcome::kRecreate)
      recreate = true;
    if (outcome == FrameOutcome::kSuspended)
      window.WaitEvents();
    if (o.frames && loop.presented >= o.frames) {
      if (o.self_test && phase != 4) {
        e = {"Self-test phases", VK_ERROR_INITIALIZATION_FAILED,
             "Not all phases completed"};
        return fail();
      }
      log("presented=" + std::to_string(loop.presented) +
          " generations=" + std::to_string(chain.generation));
      if (o.self_test)
        log("self-test phase restore/close passed");
      break;
    }
  }
  bool retired = loop.Retire(context, e);
  if (!retired)
    return fail();
  log(context.validation_active
          ? "Validation active"
          : "Validation not active; no validation-layer claim");
  return 0;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    int count = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0,
                                    nullptr, nullptr);
    std::string s(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, s.data(), count, nullptr,
                        nullptr);
    s.pop_back();
    args.push_back(std::move(s));
  }
  SmokeOptions options;
  std::string error;
  if (!ParseOptions(args, options, error)) {
    std::cerr << error
              << "\nOptions: --list-gpus --gpu-uuid=<32hex> --frames=N "
                 "--validation --self-test --log-file=<path>\n";
    return 2;
  }
  std::ofstream file;
  if (!options.log_file.empty()) {
    auto path = std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t *>(options.log_file.data()),
        options.log_file.size()));
    std::error_code ec;
    if (path.has_parent_path())
      std::filesystem::create_directories(path.parent_path(), ec);
    file.open(path);
    if (ec || !file) {
      std::cerr << "Cannot open log\n";
      return 2;
    }
  }
  uint32_t validation_errors = 0;
  auto log = [&](const std::string &s) {
    std::cout << s << std::endl;
    if (file)
      file << s << std::endl;
    if (s.starts_with("validation ERROR:"))
      ++validation_errors;
  };
  int result = 1;
  try {
    result = Run(options, log);
  } catch (const std::exception &e) {
    log(std::string("ERROR exception: ") + e.what());
  }
  log("validation_errors=" + std::to_string(validation_errors) +
      " exit=" + std::to_string(result));
  return validation_errors ? 1 : result;
}
