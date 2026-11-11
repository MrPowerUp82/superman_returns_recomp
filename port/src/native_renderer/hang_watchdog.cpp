// Ported from crazyriddler/rexglue-native-kit @136bc6c4,
// reference/conan/port/src/native/hang_watchdog.cpp (Conan native renderer).
// The kit ships no license file at that revision; parts derived from Xenia /
// ReXGlue keep their BSD license. Changes for Superman Returns are listed in
// docs/native-port-plan.md section 2.
//
// Hang watchdog: when guest swaps stop for sr_native_hang_watchdog_s seconds, logs
// the native call stacks of every process thread once (dbghelp, superman_returns.pdb).
// Diagnoses freezes without an attached debugger.
#include "hang_watchdog.h"

#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>

// generated/default/superman_returns_init.cpp, terminated by { 0, nullptr }.
extern PPCFuncMapping PPCFuncMappings[];

REXCVAR_DEFINE_INT32(sr_native_hang_watchdog_s, 6, "Superman Returns Native",
                     "Log all thread stacks when no swap happens for this many seconds (0 = off)");

namespace superman_returns::native {
namespace {

std::atomic<int64_t> g_last_beat_ms{0};
std::atomic<uint32_t> g_beat_thread{0};

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Recompiled guest code has no symbols: name an executable address after the
// recompiled function starting closest below it (PPCFuncMappings, generated).
std::string GuestFunctionName(uint64_t address) {
  static const std::vector<std::pair<uint64_t, uint32_t>> starts = [] {
    std::vector<std::pair<uint64_t, uint32_t>> v;
    for (const PPCFuncMapping* m = PPCFuncMappings; m->host; ++m)
      v.emplace_back(reinterpret_cast<uint64_t>(m->host), uint32_t(m->guest));
    std::sort(v.begin(), v.end());
    return v;
  }();
  auto it = std::upper_bound(starts.begin(), starts.end(), std::pair{address, ~0u});
  if (it == starts.begin() || address - std::prev(it)->first > (1u << 20)) return {};
  --it;
  return fmt::format("sub_{:08X}+0x{:X}", it->second, address - it->first);
}

// Copy of the walked thread's stack. While a thread is suspended nothing may
// allocate or take a lock it could hold (heap, dbghelp, logging): a thread
// suspended inside the heap lock deadlocked the watchdog mid-dump and, never
// resumed, froze the game during loading (about 1 start in 25). Only its
// context and stack are copied; the walk runs on the copy after it resumes.
alignas(16) uint8_t g_stack_copy[256 * 1024];
uint64_t g_stack_copy_base = 0, g_stack_copy_size = 0;

BOOL CALLBACK ReadStackCopy(HANDLE process, DWORD64 address, PVOID buffer, DWORD size,
                            LPDWORD read) {
  if (address >= g_stack_copy_base && address + size <= g_stack_copy_base + g_stack_copy_size) {
    std::memcpy(buffer, g_stack_copy + (address - g_stack_copy_base), size);
    *read = size;
    return TRUE;
  }
  SIZE_T done = 0;
  BOOL ok = ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), buffer, size, &done);
  *read = DWORD(done);
  return ok;
}

// Returns the thread's call stack, one indented line per frame.
std::string WalkThread(HANDLE process, DWORD tid, int max_frames) {
  HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                             FALSE, tid);
  if (!thread) return {};
  if (SuspendThread(thread) == DWORD(-1)) {
    CloseHandle(thread);
    return {};
  }
  CONTEXT ctx{};
  ctx.ContextFlags = CONTEXT_FULL;
  bool have_context = GetThreadContext(thread, &ctx);
  g_stack_copy_base = g_stack_copy_size = 0;
  MEMORY_BASIC_INFORMATION region{};
  if (have_context && VirtualQuery(reinterpret_cast<LPCVOID>(ctx.Rsp), &region, sizeof(region))) {
    uint64_t end = uint64_t(region.BaseAddress) + region.RegionSize;
    uint64_t size = std::min<uint64_t>(sizeof(g_stack_copy), end - ctx.Rsp);
    SIZE_T done = 0;
    if (ReadProcessMemory(process, reinterpret_cast<LPCVOID>(ctx.Rsp), g_stack_copy, size, &done)) {
      g_stack_copy_base = ctx.Rsp;
      g_stack_copy_size = done;
    }
  }
  ResumeThread(thread);
  std::string out;
  if (have_context) {
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < max_frames; ++i) {
      if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &ctx, ReadStackCopy,
                       SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
          !frame.AddrPC.Offset) {
        break;
      }
      alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256];
      auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
      sym->SizeOfStruct = sizeof(SYMBOL_INFO);
      sym->MaxNameLen = 255;
      DWORD64 disp = 0;
      if (SymFromAddr(process, frame.AddrPC.Offset, &disp, sym)) {
        out += fmt::format("\n    {}+0x{:X}", sym->Name, disp);
      } else if (auto name = GuestFunctionName(frame.AddrPC.Offset); !name.empty()) {
        // The executable offset resolves renderer frames against a linker map.
        static const uint64_t exe = reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
        out += fmt::format("\n    {} [exe+0x{:X}]", name, frame.AddrPC.Offset - exe);
      } else {
        out += fmt::format("\n    0x{:X}", frame.AddrPC.Offset);
      }
    }
  }
  CloseHandle(thread);
  return out;
}

void DumpThread(HANDLE process, DWORD tid, bool is_render) {
  REXLOG_WARN("native watchdog: thread {}{}:{}", tid, is_render ? " (swap thread)" : "",
              WalkThread(process, tid, 24));
}

void DumpAllThreads() {
  HANDLE process = GetCurrentProcess();
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  SymInitialize(process, nullptr, TRUE);
  DWORD self = GetCurrentThreadId();
  DWORD pid = GetCurrentProcessId();
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snap == INVALID_HANDLE_VALUE) return;
  THREADENTRY32 te{};
  te.dwSize = sizeof(te);
  for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
    if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
    DumpThread(process, te.th32ThreadID, te.th32ThreadID == g_beat_thread.load());
  }
  CloseHandle(snap);
  SymCleanup(process);
  rex::FlushLogging();
}

void WatchdogMain() {
  // A stall is dumped when it reaches the limit and again at 5x and 25x of it:
  // the first dump of a long load shows loading, not what it later hung on.
  int64_t next_dump_ms = 0;
  int dumps = 0;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    int32_t limit_s = REXCVAR_GET(sr_native_hang_watchdog_s);
    int64_t last = g_last_beat_ms.load();
    if (limit_s <= 0 || !last) continue;
    int64_t idle = NowMs() - last;
    if (idle < int64_t(limit_s) * 1000) {
      next_dump_ms = int64_t(limit_s) * 1000;
      dumps = 0;
      continue;
    }
    if (!next_dump_ms) next_dump_ms = int64_t(limit_s) * 1000;
    if (dumps < 3 && idle >= next_dump_ms) {
      ++dumps;
      next_dump_ms *= 5;
      REXLOG_WARN("native watchdog: no swap for {} ms, dumping thread stacks", idle);
      DumpAllThreads();
    }
  }
}

}  // namespace

void HangWatchdogBeat() {
  static std::once_flag started;
  std::call_once(started, [] { std::thread(WatchdogMain).detach(); });
  g_beat_thread.store(GetCurrentThreadId());
  g_last_beat_ms.store(NowMs());
}

}  // namespace superman_returns::native
