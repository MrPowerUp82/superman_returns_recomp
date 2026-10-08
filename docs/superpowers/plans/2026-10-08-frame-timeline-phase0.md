# Fase 0: linha do tempo unificada por quadro — Plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Medir, quadro a quadro e nas duas APIs, quanto cada estágio (thread do jogo, captura, espera do worker, worker/replay, gravação Vulkan, GPU) trabalha e espera, e produzir a tabela "estágio limitante" que decide a ordem das fases seguintes.

**Architecture:** Um cabeçalho puro (`frame_timeline.h`) guarda spans por estágio num ring buffer por swap number (o identificador de quadro que todas as threads enxergam) e os despeja num CSV. Cada estágio grava só o seu span; `RAII` acumula o tempo bloqueado por thread. Um script Python lê o CSV e calcula FPS, 1% low, tempo ocupado/bloqueado por estágio e a fração de quadros em que cada estágio foi o limitante. Tudo fica desligado a menos que `SR_FRAME_TIMELINE=<arquivo.csv>` esteja definido.

**Tech Stack:** C++20 (renderer nativo, Windows/clang), Vulkan 1.x (timestamps), D3D12 (timestamps já existentes), PowerShell (`bench_api.ps1`), Python 3 + pytest.

## Global Constraints

Copiados do design (`docs/superpowers/specs/2026-10-08-native-renderer-cpu-gpu-bottlenecks-design.md`, seção 1) e dos limites do projeto:

- Instrumentação < 0,3 ms por quadro (ligada contra desligada).
- Desligada por padrão; ativada por variável de ambiente; **sem mudar imagem nem comportamento**.
- Formato comum às duas APIs; cada backend preenche os estágios que tem.
- Esta fase **não faz nenhuma otimização**: só instrumento e diagnóstico.
- Imagem bit a bit idêntica; texturas exatas a cada quadro; `sr_renderer=native` nunca cai para Xenos.
- O usuário interrompeu loops longos sem aviso: **avise o progresso entre os passos** que levam minutos (build do jogo, bench).
- Commits só quando o usuário autorizar a execução do plano. Mensagens terminam com `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.

## Fora do escopo desta fase (decisão registrada)

O design pedia "timestamps de GPU por grupo de passes". Na inspeção, os ids de passe de `port/src/native_renderer/game_profile.h` (`kPassOpaque`, `kPassHud` etc.) valem `-1` neste perfil, então não existe uma chave de agrupamento pronta. Escolher essa chave (por exemplo, a assinatura do render target) é uma decisão de design própria. Este plano entrega o **tempo de GPU por quadro inteiro** nas duas APIs. Se a tabela mostrar que a GPU é o limitante, o agrupamento por passe vira o plano seguinte.

## Estrutura de arquivos

| Arquivo | Responsabilidade |
| --- | --- |
| `port/src/graphics/frame_timeline.h` (novo) | Ring buffer de spans por estágio e quadro, escrita do CSV, `TimelineBlockScope`. Sem dependência do SDK, do D3D12 nem do Vulkan |
| `tests/native/test_frame_timeline.cpp` (novo) | Testes unitários do cabeçalho |
| `tests/native/CMakeLists.txt` | Registra o teste novo |
| `tools/analysis/frame_timeline_report.py` (novo) | Lê o CSV e imprime a tabela do estágio limitante |
| `tests/tools/test_frame_timeline_report.py` (novo) | Testes do script com CSV sintético |
| `port/src/native_renderer/native_renderer.h` / `.cpp` | Estágios `game`, `capture`, `front_wait`, `worker`; esperas e GPU do D3D12 |
| `port/src/graphics/vulkan/game_frame.h` / `.cpp`, `functions.inc` | Estágio `record` e GPU do Vulkan |
| `tools/bench/bench_api.ps1`, `tools/README.md` | Opção `-Timeline` |
| `docs/native-renderer-timeline.md` (novo) | Como usar e a tabela medida do estágio limitante |

## Formato do CSV

`frame,stage,begin_ns,end_ns,busy_ns,blocked_ns` — uma linha por quadro e estágio. `frame` é o swap number do jogo (começa em 1). `begin_ns`/`end_ns` usam o `steady_clock` (0 quando o estágio só tem duração, como a GPU). `busy_ns` já exclui `blocked_ns`.

| Estágio | Thread | begin → end | Bloqueado = |
| --- | --- | --- | --- |
| `game` | do jogo | saída do `OnSwap` anterior → entrada do `OnSwap` | 0 |
| `capture` | do jogo (aninhado em `game`) | só `busy_ns` (PM4 + texturas) | 0 |
| `front_wait` | do jogo | espera do worker dentro do `OnSwap` | 0 |
| `worker` | worker | início do primeiro batch → fim do batch do swap | espera de fences da GPU (D3D12) ou de `Enqueue` do swap (Vulkan) |
| `record` | gravação Vulkan | início de `RecordFrame` → fim | shaders + fence do slot + lock da fila |
| `gpu` | GPU | só `busy_ns` | 0 |

---

### Task 1: Núcleo `frame_timeline.h` com testes

**Files:**
- Create: `port/src/graphics/frame_timeline.h`
- Create: `tests/native/test_frame_timeline.cpp`
- Modify: `tests/native/CMakeLists.txt` (lista de fontes de `sr_native_tests`)

**Interfaces:**
- Produces (namespace `superman_returns::graphics`):
  - `enum class TimelineStage : uint8_t { kGame, kCapture, kFrontWait, kWorker, kRecord, kGpu, kCount }`
  - `const char* TimelineStageName(TimelineStage)`
  - `FrameTimeline(std::string path)` (caminho vazio = desligado), `static FrameTimeline& Global()`, `bool enabled() const`
  - `static uint64_t ToNs(std::chrono::steady_clock::time_point)`, `static uint64_t NowNs()`
  - `void Record(TimelineStage, uint64_t frame, uint64_t begin_ns, uint64_t end_ns, uint64_t busy_ns, uint64_t blocked_ns)`
  - `void RecordSpan(TimelineStage, uint64_t frame, uint64_t begin_ns, uint64_t end_ns, uint64_t blocked_ns)` (busy = duração − bloqueado, mínimo 0)
  - `void RecordBusy(TimelineStage, uint64_t frame, uint64_t busy_ns)`
  - `void WriteRows(std::ostream&, uint64_t first, uint64_t last) const`
  - `void Flush(uint64_t newest_frame)` (grava os quadros `<= newest_frame - 16`), `void FlushAll()`
  - `uint64_t& TimelineBlockedNs()`, `uint64_t TakeTimelineBlockedNs()`, `class TimelineBlockScope`

- [ ] **Step 1: Escrever os testes que falham**

Crie `tests/native/test_frame_timeline.cpp`:

```cpp
#include "../graphics/frame_timeline.h"
#include "test_main.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
using namespace superman_returns::graphics;

namespace {
std::string Rows(const FrameTimeline& timeline, uint64_t first, uint64_t last) {
  std::ostringstream out;
  timeline.WriteRows(out, first, last);
  return out.str();
}
std::string ReadAll(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}
size_t CountOf(const std::string& text, const std::string& needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) ++count;
  return count;
}
}  // namespace

SR_TEST(timeline_span_busy_excludes_blocked_time) {
  FrameTimeline timeline("unused.csv");
  timeline.RecordSpan(TimelineStage::kWorker, 5, 1000, 4000, 1000);
  SR_CHECK(Rows(timeline, 5, 5) == "5,worker,1000,4000,2000,1000\n");
}

SR_TEST(timeline_blocked_longer_than_span_gives_zero_busy) {
  FrameTimeline timeline("unused.csv");
  timeline.RecordSpan(TimelineStage::kRecord, 3, 100, 200, 500);
  SR_CHECK(Rows(timeline, 3, 3) == "3,record,100,200,0,500\n");
}

SR_TEST(timeline_gpu_rows_carry_only_busy_time) {
  FrameTimeline timeline("unused.csv");
  timeline.RecordBusy(TimelineStage::kGpu, 7, 5000000);
  SR_CHECK(Rows(timeline, 7, 7) == "7,gpu,0,0,5000000,0\n");
}

SR_TEST(timeline_disabled_records_nothing) {
  FrameTimeline timeline("");
  SR_CHECK(!timeline.enabled());
  timeline.RecordSpan(TimelineStage::kGame, 1, 0, 10, 0);
  SR_CHECK(Rows(timeline, 1, 1).empty());
}

SR_TEST(timeline_frame_zero_is_not_a_frame) {
  FrameTimeline timeline("unused.csv");
  timeline.RecordBusy(TimelineStage::kGame, 0, 10);
  SR_CHECK(Rows(timeline, 0, 0).empty());
}

SR_TEST(timeline_ring_slot_reuse_hides_the_older_frame) {
  FrameTimeline timeline("unused.csv");
  const uint64_t later = 1 + FrameTimeline::kRing;
  timeline.RecordBusy(TimelineStage::kGame, 1, 10);
  timeline.RecordBusy(TimelineStage::kGame, later, 20);
  SR_CHECK(Rows(timeline, 1, 1).empty());
  SR_CHECK(Rows(timeline, later, later) == std::to_string(later) + ",game,0,0,20,0\n");
}

SR_TEST(timeline_flush_writes_header_then_only_settled_frames) {
  const auto path = std::filesystem::temp_directory_path() / "sr_frame_timeline_flush.csv";
  std::filesystem::remove(path);
  FrameTimeline timeline(path.string());
  for (uint64_t frame = 1; frame <= 40; ++frame) timeline.RecordBusy(TimelineStage::kGame, frame, frame);
  timeline.Flush(40);  // settled frames: <= 40 - 16 = 24
  timeline.Flush(40);  // nothing new may be written twice
  std::string text = ReadAll(path);
  SR_CHECK(text.rfind("frame,stage,begin_ns,end_ns,busy_ns,blocked_ns\n", 0) == 0);
  SR_CHECK(text.find("\n24,game,0,0,24,0\n") != std::string::npos);
  SR_CHECK(text.find("\n25,game") == std::string::npos);
  timeline.Flush(50);  // frames 25..34
  text = ReadAll(path);
  SR_CHECK(text.find("\n34,game,0,0,34,0\n") != std::string::npos);
  SR_CHECK(text.find("\n35,game") == std::string::npos);
  SR_CHECK_EQ(CountOf(text, "\n24,game"), 1u);
  std::filesystem::remove(path);
}

SR_TEST(timeline_flush_all_writes_every_recorded_frame_once) {
  const auto path = std::filesystem::temp_directory_path() / "sr_frame_timeline_all.csv";
  std::filesystem::remove(path);
  FrameTimeline timeline(path.string());
  for (uint64_t frame = 1; frame <= 5; ++frame) timeline.RecordBusy(TimelineStage::kGpu, frame, 100 + frame);
  timeline.FlushAll();
  timeline.FlushAll();
  const std::string text = ReadAll(path);
  SR_CHECK(text.find("\n1,gpu,0,0,101,0\n") != std::string::npos);
  SR_CHECK(text.find("\n5,gpu,0,0,105,0\n") != std::string::npos);
  SR_CHECK_EQ(CountOf(text, ",gpu,"), 5u);
  std::filesystem::remove(path);
}

SR_TEST(timeline_block_scope_accumulates_wait_only_when_enabled) {
  FrameTimeline on("unused.csv"), off("");
  TakeTimelineBlockedNs();
  {
    TimelineBlockScope scope(off);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  SR_CHECK_EQ(TakeTimelineBlockedNs(), 0u);
  {
    TimelineBlockScope scope(on);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  SR_CHECK(TakeTimelineBlockedNs() >= 1000000u);
  SR_CHECK_EQ(TakeTimelineBlockedNs(), 0u);
}

SR_TEST(timeline_recording_a_frame_costs_microseconds) {
  // Budget from the design: < 0.3 ms per frame. 100k frames of every stage must
  // take far less than 100k * 0.3 ms; the bound only catches a pathological regression.
  FrameTimeline timeline("unused.csv");
  const auto start = std::chrono::steady_clock::now();
  for (uint64_t frame = 1; frame <= 100000; ++frame)
    for (size_t stage = 0; stage < size_t(TimelineStage::kCount); ++stage)
      timeline.RecordSpan(TimelineStage(stage), frame, frame, frame + 10, 1);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  SR_CHECK(elapsed.count() < 500);
}
```

Em `tests/native/CMakeLists.txt`, na lista de fontes de `sr_native_tests`, logo depois de `test_resource_unlock_audit.cpp`, acrescente a linha:

```cmake
    test_frame_timeline.cpp
```

- [ ] **Step 2: Rodar e ver falhar**

```powershell
. .\tools\dev_env.ps1
cmake -S tests/native -B build/tests-native -DCMAKE_BUILD_TYPE=Release
cmake --build build/tests-native
```

Expected: FAIL de compilação, `'../graphics/frame_timeline.h' file not found`.

- [ ] **Step 3: Implementar o cabeçalho**

Crie `port/src/graphics/frame_timeline.h`:

```cpp
#pragma once
// Per-frame stage timeline shared by the D3D12 and Vulkan native renderers.
// SR_FRAME_TIMELINE=<file.csv> turns it on; without it every call returns at
// once. A frame is identified by the guest swap number, which every stage sees.
// Each stage writes only its own slot, so threads never share a field; the CSV
// is written from complete frames only (Flush keeps a margin behind the newest).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>

namespace superman_returns::graphics {

enum class TimelineStage : uint8_t { kGame, kCapture, kFrontWait, kWorker, kRecord, kGpu, kCount };

inline const char* TimelineStageName(TimelineStage stage) {
  switch (stage) {
    case TimelineStage::kGame: return "game";
    case TimelineStage::kCapture: return "capture";
    case TimelineStage::kFrontWait: return "front_wait";
    case TimelineStage::kWorker: return "worker";
    case TimelineStage::kRecord: return "record";
    case TimelineStage::kGpu: return "gpu";
    default: return "unknown";
  }
}

class FrameTimeline {
 public:
  static constexpr uint64_t kRing = 4096;
  static constexpr uint64_t kFlushMargin = 16;

  explicit FrameTimeline(std::string path) : path_(std::move(path)), enabled_(!path_.empty()) {
    if (enabled_) spans_ = std::make_unique<Span[]>(size_t(TimelineStage::kCount) * kRing);
  }
  FrameTimeline(const FrameTimeline&) = delete;
  FrameTimeline& operator=(const FrameTimeline&) = delete;

  static FrameTimeline& Global() {
    static FrameTimeline instance(PathFromEnvironment());
    return instance;
  }
  static std::string PathFromEnvironment() {
    const char* value = std::getenv("SR_FRAME_TIMELINE");
    if (!value || !*value || std::string(value) == "0") return {};
    return value;
  }

  bool enabled() const { return enabled_; }
  static uint64_t ToNs(std::chrono::steady_clock::time_point time) {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count());
  }
  static uint64_t NowNs() { return ToNs(std::chrono::steady_clock::now()); }

  void Record(TimelineStage stage, uint64_t frame, uint64_t begin_ns, uint64_t end_ns, uint64_t busy_ns,
              uint64_t blocked_ns) {
    if (!enabled_ || frame == 0) return;
    Span& span = Slot(size_t(stage), frame);
    span.begin_ns = begin_ns;
    span.end_ns = end_ns;
    span.busy_ns = busy_ns;
    span.blocked_ns = blocked_ns;
    span.frame.store(frame, std::memory_order_release);
    uint64_t seen = max_frame_.load(std::memory_order_relaxed);
    while (frame > seen && !max_frame_.compare_exchange_weak(seen, frame, std::memory_order_relaxed)) {}
  }
  void RecordSpan(TimelineStage stage, uint64_t frame, uint64_t begin_ns, uint64_t end_ns, uint64_t blocked_ns) {
    const uint64_t wall = end_ns > begin_ns ? end_ns - begin_ns : 0;
    Record(stage, frame, begin_ns, end_ns, wall > blocked_ns ? wall - blocked_ns : 0, blocked_ns);
  }
  void RecordBusy(TimelineStage stage, uint64_t frame, uint64_t busy_ns) { Record(stage, frame, 0, 0, busy_ns, 0); }

  void WriteRows(std::ostream& out, uint64_t first, uint64_t last) const {
    if (!enabled_) return;
    for (uint64_t frame = std::max<uint64_t>(first, 1); frame <= last; ++frame) {
      for (size_t stage = 0; stage < size_t(TimelineStage::kCount); ++stage) {
        const Span& span = Slot(stage, frame);
        if (span.frame.load(std::memory_order_acquire) != frame) continue;
        out << frame << ',' << TimelineStageName(TimelineStage(stage)) << ',' << span.begin_ns << ','
            << span.end_ns << ',' << span.busy_ns << ',' << span.blocked_ns << '\n';
      }
    }
  }

  // Writes every frame up to newest_frame - kFlushMargin that was not written yet.
  void Flush(uint64_t newest_frame) {
    if (!enabled_ || newest_frame <= kFlushMargin) return;
    FlushUpTo(newest_frame - kFlushMargin);
  }
  // Writes everything recorded so far (shutdown, tests).
  void FlushAll() {
    if (!enabled_) return;
    FlushUpTo(max_frame_.load(std::memory_order_relaxed));
  }

 private:
  struct Span {
    std::atomic<uint64_t> frame{0};  // written last; 0 = empty
    uint64_t begin_ns = 0, end_ns = 0, busy_ns = 0, blocked_ns = 0;
  };
  Span& Slot(size_t stage, uint64_t frame) const { return spans_[stage * kRing + frame % kRing]; }
  void FlushUpTo(uint64_t last) {
    std::lock_guard<std::mutex> lock(flush_mutex_);
    if (last <= flushed_) return;
    std::ofstream out(path_, std::ios::out | (header_written_ ? std::ios::app : std::ios::trunc));
    if (!out) return;
    if (!header_written_) {
      out << "frame,stage,begin_ns,end_ns,busy_ns,blocked_ns\n";
      header_written_ = true;
    }
    WriteRows(out, flushed_ + 1, last);
    flushed_ = last;
  }

  std::string path_;
  bool enabled_;
  std::unique_ptr<Span[]> spans_;
  std::atomic<uint64_t> max_frame_{0};
  std::mutex flush_mutex_;
  uint64_t flushed_ = 0;
  bool header_written_ = false;
};

// Time this thread spent waiting on another stage since the last Take. The
// stage that owns the thread reads it when it closes its span.
inline uint64_t& TimelineBlockedNs() {
  thread_local uint64_t value = 0;
  return value;
}
inline uint64_t TakeTimelineBlockedNs() {
  uint64_t& value = TimelineBlockedNs();
  const uint64_t taken = value;
  value = 0;
  return taken;
}
class TimelineBlockScope {
 public:
  explicit TimelineBlockScope(const FrameTimeline& timeline = FrameTimeline::Global())
      : active_(timeline.enabled()), start_(active_ ? FrameTimeline::NowNs() : 0) {}
  ~TimelineBlockScope() {
    if (active_) TimelineBlockedNs() += FrameTimeline::NowNs() - start_;
  }
  TimelineBlockScope(const TimelineBlockScope&) = delete;
  TimelineBlockScope& operator=(const TimelineBlockScope&) = delete;

 private:
  bool active_;
  uint64_t start_;
};

}  // namespace superman_returns::graphics
```

- [ ] **Step 4: Rodar e ver passar**

```powershell
cmake --build build/tests-native
ctest --test-dir build/tests-native --output-on-failure
.\build\tests-native\sr_native_tests.exe | Select-String "timeline"
```

Expected: `ctest` com 100% passando; dez linhas `PASS timeline_...` (os dez `SR_TEST` acima) e nenhum `FAIL`.

- [ ] **Step 5: Commit**

```powershell
git add port/src/graphics/frame_timeline.h tests/native/test_frame_timeline.cpp tests/native/CMakeLists.txt
git commit -m "feat(native): add the per-frame stage timeline core" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Relatório do caminho crítico (Python)

**Files:**
- Create: `tools/analysis/frame_timeline_report.py`
- Create: `tests/tools/test_frame_timeline_report.py`

**Interfaces:**
- Consumes: o CSV da Task 1 (`frame,stage,begin_ns,end_ns,busy_ns,blocked_ns`).
- Produces: `percentile(values, p)`, `load(path) -> dict[int, dict[str, dict]]`, `analyze(frames, last=1100, budget_ms=33.33) -> dict`, `render(result) -> str`, `main(argv) -> int`. As chaves de `analyze`: `frames`, `dropped`, `budget_ms`, `interval` (`samples`, `mean_ms`, `mean_fps`, `p99_ms`, `low1_fps`, `min_fps` ou `None`), `stages` (por nome: `mean`, `p50`, `p99`, `blocked_mean`, `samples`, `util`, `headroom_p99_ms`), `limiting_share` (fração de quadros por estágio limitante).

- [ ] **Step 1: Escrever os testes que falham**

Crie `tests/tools/test_frame_timeline_report.py`:

```python
"""tools/analysis/frame_timeline_report.py on synthetic timeline CSVs."""
import json

import pytest

import frame_timeline_report as ftr

HEADER = "frame,stage,begin_ns,end_ns,busy_ns,blocked_ns\n"
MS = 1_000_000


def write_csv(path, rows):
    path.write_text(HEADER + "".join(f"{f},{s},{b},{e},{busy},{blk}\n" for f, s, b, e, busy, blk in rows))


def frame_rows(frame, interval_ms=33.3, skip=()):
    end = 1_000 * MS + round(frame * interval_ms * MS)
    rows = [
        (frame, "game", end - 20 * MS, end, 20 * MS, 0),
        (frame, "capture", 0, 0, 6 * MS, 0),
        (frame, "front_wait", end, end + 2 * MS, 2 * MS, 0),
        (frame, "worker", end, end + 28 * MS, 25 * MS, 3 * MS),
        (frame, "record", end, end + 33 * MS, 30 * MS, 3 * MS),
        (frame, "gpu", 0, 0, 18 * MS, 0),
    ]
    return [row for row in rows if row[1] not in skip]


def make(tmp_path, frames=10, skip_by_frame=None):
    skip_by_frame = skip_by_frame or {}
    rows = []
    for frame in range(1, frames + 1):
        rows += frame_rows(frame, skip=skip_by_frame.get(frame, ()))
    path = tmp_path / "timeline.csv"
    write_csv(path, rows)
    return path


def test_interval_and_fps(tmp_path):
    result = ftr.analyze(ftr.load(make(tmp_path)))
    assert result["frames"] == 10
    assert result["dropped"] == 0
    assert result["interval"]["samples"] == 9
    assert result["interval"]["mean_ms"] == pytest.approx(33.3, abs=0.01)
    assert result["interval"]["mean_fps"] == pytest.approx(30.03, rel=0.01)
    assert result["interval"]["low1_fps"] == pytest.approx(30.03, rel=0.01)


def test_stage_stats(tmp_path):
    stages = ftr.analyze(ftr.load(make(tmp_path)))["stages"]
    assert stages["worker"]["mean"] == pytest.approx(25.0)
    assert stages["worker"]["blocked_mean"] == pytest.approx(3.0)
    assert stages["worker"]["util"] == pytest.approx(25.0 / 33.3, rel=0.01)
    assert stages["worker"]["headroom_p99_ms"] == pytest.approx(1000 / 30 - 25.0, rel=0.01)
    assert stages["game_other"]["mean"] == pytest.approx(14.0)  # game 20 - capture 6


def test_limiting_stage_is_the_largest_busy_time(tmp_path):
    share = ftr.analyze(ftr.load(make(tmp_path)))["limiting_share"]
    assert share == {"game": 0.0, "worker": 0.0, "record": 1.0, "gpu": 0.0}


def test_frames_missing_a_required_stage_are_dropped(tmp_path):
    path = make(tmp_path, skip_by_frame={5: ("worker",)})
    result = ftr.analyze(ftr.load(path))
    assert result["frames"] == 9
    assert result["dropped"] == 1
    # pairs (1,2) (2,3) (3,4) and (6,7) (7,8) (8,9) (9,10): the gap breaks two pairs
    assert result["interval"]["samples"] == 7


def test_last_limits_the_window(tmp_path):
    result = ftr.analyze(ftr.load(make(tmp_path)), last=4)
    assert result["frames"] == 4


def test_api_without_record_and_capture_stages(tmp_path):
    rows = []
    for frame in range(1, 6):
        rows += frame_rows(frame, skip=("record", "capture"))
    path = tmp_path / "d3d12.csv"
    write_csv(path, rows)
    result = ftr.analyze(ftr.load(path))
    assert "record" not in result["stages"]
    assert "game_other" not in result["stages"]
    assert set(result["limiting_share"]) == {"game", "worker", "gpu"}
    assert result["limiting_share"]["worker"] == 1.0


def test_percentile_nearest_rank():
    assert ftr.percentile(list(range(1, 101)), 99) == 99
    assert ftr.percentile([1, 2, 3, 4], 50) == 2
    assert ftr.percentile([], 99) == 0.0


def test_cli_prints_the_table_and_json(tmp_path, capsys):
    path = make(tmp_path)
    assert ftr.main([str(path)]) == 0
    text = capsys.readouterr().out
    assert "limitante" in text and "record" in text and "1% low" in text
    assert ftr.main([str(path), "--json"]) == 0
    data = json.loads(capsys.readouterr().out)
    assert "stages" in data and data["frames"] == 10


def test_cli_fails_when_no_frame_is_complete(tmp_path):
    path = tmp_path / "empty.csv"
    write_csv(path, [])
    assert ftr.main([str(path)]) == 1
```

- [ ] **Step 2: Rodar e ver falhar**

```powershell
python -m pytest tests/tools/test_frame_timeline_report.py -q
```

Expected: FAIL com `ModuleNotFoundError: No module named 'frame_timeline_report'`.

- [ ] **Step 3: Implementar o script**

Crie `tools/analysis/frame_timeline_report.py`:

```python
#!/usr/bin/env python3
"""Critical-path report for the timeline CSV written with SR_FRAME_TIMELINE=<file>.

Columns: frame,stage,begin_ns,end_ns,busy_ns,blocked_ns (port/src/graphics/frame_timeline.h).
frame is the guest swap number, shared by every stage. busy_ns excludes the time the stage
waited on another one; blocked_ns is that waiting time.
"""
import argparse
import csv
import json
import math
import sys
from collections import defaultdict

STAGES = ("game", "game_other", "capture", "front_wait", "worker", "record", "gpu")
LIMITERS = ("game", "worker", "record", "gpu")  # stages that can bound the frame rate
REQUIRED = ("game", "worker")  # a frame without these has unusable ids or a lost stage


def percentile(values, p):
    """Nearest-rank percentile; 0.0 for an empty list."""
    if not values:
        return 0.0
    ordered = sorted(values)
    rank = max(1, math.ceil(p / 100.0 * len(ordered)))
    return float(ordered[rank - 1])


def load(path):
    frames = defaultdict(dict)
    with open(path, newline="") as handle:
        for row in csv.DictReader(handle):
            frames[int(row["frame"])][row["stage"]] = {
                key: int(row[key]) for key in ("begin_ns", "end_ns", "busy_ns", "blocked_ns")
            }
    return frames


def _summary(busy_ms, blocked_ms, mean_interval, budget_ms):
    p99 = percentile(busy_ms, 99)
    mean = sum(busy_ms) / len(busy_ms)
    return {
        "mean": mean,
        "p50": percentile(busy_ms, 50),
        "p99": p99,
        "blocked_mean": sum(blocked_ms) / len(blocked_ms),
        "samples": len(busy_ms),
        "util": mean / mean_interval if mean_interval else None,
        "headroom_p99_ms": budget_ms - p99,
    }


def analyze(frames, last=1100, budget_ms=1000.0 / 30.0):
    ids = sorted(frames)
    if last > 0:
        ids = ids[-last:]
    complete = [i for i in ids if all(stage in frames[i] for stage in REQUIRED)]
    result = {"frames": len(complete), "dropped": len(ids) - len(complete), "budget_ms": budget_ms,
              "interval": None, "stages": {}, "limiting_share": {}}
    if not complete:
        return result

    done = set(complete)
    intervals = [(frames[i + 1]["game"]["end_ns"] - frames[i]["game"]["end_ns"]) / 1e6
                 for i in complete if i + 1 in done]
    mean_interval = 0.0
    if intervals:
        mean_interval = sum(intervals) / len(intervals)
        p99 = percentile(intervals, 99)
        result["interval"] = {
            "samples": len(intervals),
            "mean_ms": mean_interval,
            "mean_fps": 1000.0 / mean_interval,
            "p99_ms": p99,
            "low1_fps": 1000.0 / p99,
            "min_fps": 1000.0 / max(intervals),
        }

    stages = {}
    for name in ("game", "capture", "front_wait", "worker", "record", "gpu"):
        rows = [frames[i][name] for i in complete if name in frames[i]]
        if rows:
            stages[name] = _summary([r["busy_ns"] / 1e6 for r in rows], [r["blocked_ns"] / 1e6 for r in rows],
                                    mean_interval, budget_ms)
    with_capture = [i for i in complete if "capture" in frames[i]]
    if with_capture:  # the game thread's own work: game minus the capture hooks nested in it
        other = [(frames[i]["game"]["busy_ns"] - frames[i]["capture"]["busy_ns"]) / 1e6 for i in with_capture]
        stages["game_other"] = _summary(other, [0.0] * len(other), mean_interval, budget_ms)
    result["stages"] = stages

    present = [name for name in LIMITERS if name in stages]
    counts = {name: 0 for name in present}
    for i in complete:
        busy = {name: frames[i][name]["busy_ns"] for name in present if name in frames[i]}
        counts[max(busy, key=busy.get)] += 1
    result["limiting_share"] = {name: count / len(complete) for name, count in counts.items()}
    return result


def render(result):
    lines = [f"Quadros analisados: {result['frames']} (descartados por falta de estágio: {result['dropped']})"]
    interval = result["interval"]
    if interval:
        lines.append(f"Intervalo médio {interval['mean_ms']:.1f} ms = {interval['mean_fps']:.1f} FPS"
                     f" | 1% low {interval['low1_fps']:.1f} FPS | mínimo {interval['min_fps']:.1f} FPS")
    lines.append(f"Orçamento por quadro: {result['budget_ms']:.1f} ms (tempos em ms)")
    lines.append("")
    lines.append(f"{'estágio':<12}{'média':>8}{'p50':>8}{'p99':>8}{'bloq.':>8}{'util.':>8}{'folga p99':>11}{'limitante':>11}")
    share = result["limiting_share"]
    for name in STAGES:
        stage = result["stages"].get(name)
        if not stage:
            continue
        util = f"{stage['util'] * 100:.0f}%" if stage["util"] is not None else "-"
        limiting = f"{share[name] * 100:.0f}%" if name in share else "-"
        lines.append(f"{name:<12}{stage['mean']:>8.2f}{stage['p50']:>8.2f}{stage['p99']:>8.2f}"
                     f"{stage['blocked_mean']:>8.2f}{util:>8}{stage['headroom_p99_ms']:>11.2f}{limiting:>11}")
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("csv", help="file written with SR_FRAME_TIMELINE")
    parser.add_argument("--last", type=int, default=1100, help="analyze only the last N frames (0 = all)")
    parser.add_argument("--budget-ms", type=float, default=1000.0 / 30.0, help="frame budget (default 30 FPS)")
    parser.add_argument("--json", action="store_true", help="print the raw result as JSON")
    args = parser.parse_args(argv)
    result = analyze(load(args.csv), args.last, args.budget_ms)
    print(json.dumps(result, indent=2) if args.json else render(result))
    return 0 if result["frames"] else 1


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Rodar e ver passar**

```powershell
python -m pytest tests/tools/test_frame_timeline_report.py -q
```

Expected: `9 passed`.

- [ ] **Step 5: Commit**

```powershell
git add tools/analysis/frame_timeline_report.py tests/tools/test_frame_timeline_report.py
git commit -m "feat(tools): report the limiting stage from the frame timeline" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Estágios compartilhados: `game`, `capture`, `front_wait`, `worker`

**Files:**
- Modify: `port/src/native_renderer/native_renderer.h` (membros de estado do timeline, junto a `std::mutex front_mutex_;`)
- Modify: `port/src/native_renderer/native_renderer.cpp` (`OnSwap`, `WorkerMain`, `Execute`, include)

**Interfaces:**
- Consumes: `FrameTimeline::Global()`, `RecordSpan`, `RecordBusy`, `Record`, `NowNs`, `Flush`, `TimelineBlockScope`, `TakeTimelineBlockedNs` (Task 1).
- Produces: linhas `game`, `capture` (só Vulkan: o D3D12 não captura no front end), `front_wait` e `worker` no CSV. A thread do worker fecha o span `worker` quando o batch termina com um `Op::kSwap`.

- [ ] **Step 1: Membros no header**

Em `port/src/native_renderer/native_renderer.h`, logo depois da linha `std::mutex front_mutex_;`, acrescente:

```cpp
  // SR_FRAME_TIMELINE (graphics/frame_timeline.h). Game thread: timeline_game_exit_ns_
  // (under front_mutex_). Worker thread: the three worker fields.
  uint64_t timeline_game_exit_ns_ = 0;
  uint64_t timeline_worker_begin_ns_ = 0, timeline_worker_exec_ns_ = 0, timeline_worker_blocked_ns_ = 0;
```

- [ ] **Step 2: Include**

Em `port/src/native_renderer/native_renderer.cpp`, depois de `#include "../graphics/guest/pm4_capture.h"` (linha 24), acrescente:

```cpp
#include "../graphics/frame_timeline.h"
```

- [ ] **Step 3: `OnSwap` (game, capture, front_wait, flush)**

Em `Renderer::OnSwap`, troque as duas primeiras linhas do corpo:

```cpp
  ++front_frame_;
  std::lock_guard<std::mutex> lock(front_mutex_);
```

por:

```cpp
  ++front_frame_;
  auto& timeline = graphics::FrameTimeline::Global();
  const uint64_t swap_entry_ns = timeline.enabled() ? graphics::FrameTimeline::NowNs() : 0;
  std::lock_guard<std::mutex> lock(front_mutex_);
```

Troque a linha `  capture_timings={};` (a que vem logo depois do `REXLOG_INFO("native Vulkan capture frame=...` e antes de `if(front_frame_%120==0) {`) por:

```cpp
  if (timeline.enabled()) {
    timeline.RecordSpan(graphics::TimelineStage::kGame, swap_number,
                        timeline_game_exit_ns_ ? timeline_game_exit_ns_ : swap_entry_ns, swap_entry_ns, 0);
    timeline.RecordBusy(graphics::TimelineStage::kCapture, swap_number,
                        (capture_timings.pm4_us + capture_timings.textures_us) * 1000);
  }
  capture_timings={};
```

Troque o bloco da espera do worker:

```cpp
    {
      CpuTimer cpu_timer(front_cpu_timings.wait_ns);
      WaitWorkerIdle(REXCVAR_GET(sr_native_worker_lag) ? prev_swap_batches_ : submitted);
    }
```

por:

```cpp
    const uint64_t wait_begin_ns = timeline.enabled() ? graphics::FrameTimeline::NowNs() : 0;
    {
      CpuTimer cpu_timer(front_cpu_timings.wait_ns);
      WaitWorkerIdle(REXCVAR_GET(sr_native_worker_lag) ? prev_swap_batches_ : submitted);
    }
    if (timeline.enabled())
      timeline.RecordSpan(graphics::TimelineStage::kFrontWait, swap_number, wait_begin_ns,
                          graphics::FrameTimeline::NowNs(), 0);
```

No fim de `OnSwap`, depois do bloco `if(CpuProfiling() && front_frame_%120==0) { ... front_cpu_timings.capture_ns=front_cpu_timings.wait_ns=0;\n  }`, e antes do `}` que fecha a função, acrescente:

```cpp
  if (timeline.enabled()) {
    // The write happens before the exit stamp, so it is not charged to the next frame's game time.
    if (swap_number % 60 == 0) timeline.Flush(swap_number);
    timeline_game_exit_ns_ = graphics::FrameTimeline::NowNs();
  }
```

- [ ] **Step 4: `WorkerMain` (worker)**

No início de `Renderer::WorkerMain`, depois de `compat::RegisterSampledThread(4, "sr_native_worker");`, acrescente:

```cpp
  auto& timeline = graphics::FrameTimeline::Global();
```

Troque:

```cpp
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      for (const WorkCmd& cmd : batch->cmds) {
```

por:

```cpp
    const uint64_t timeline_begin_ns = timeline.enabled() ? graphics::FrameTimeline::NowNs() : 0;
    graphics::TakeTimelineBlockedNs();  // drop waits recorded outside a batch
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      for (const WorkCmd& cmd : batch->cmds) {
```

E logo antes de `    batch->Clear();` (a linha que vem depois do bloco do `lock_guard` do loop, dentro de `WorkerMain`), acrescente:

```cpp
    if (timeline.enabled()) {
      // OnSwap flushes the batch right after the swap command, so a swap always ends its batch.
      const uint64_t batch_end_ns = graphics::FrameTimeline::NowNs();
      if (!timeline_worker_begin_ns_) timeline_worker_begin_ns_ = timeline_begin_ns;
      timeline_worker_exec_ns_ += batch_end_ns - timeline_begin_ns;
      timeline_worker_blocked_ns_ += graphics::TakeTimelineBlockedNs();
      if (!batch->cmds.empty() && batch->cmds.back().op == Op::kSwap) {
        const uint64_t blocked = std::min(timeline_worker_blocked_ns_, timeline_worker_exec_ns_);
        timeline.Record(graphics::TimelineStage::kWorker, batch->cmds.back().u64, timeline_worker_begin_ns_,
                        batch_end_ns, timeline_worker_exec_ns_ - blocked, blocked);
        timeline_worker_begin_ns_ = timeline_worker_exec_ns_ = timeline_worker_blocked_ns_ = 0;
      }
    }
```

- [ ] **Step 5: `Execute` (espera do swap no Vulkan)**

Em `Renderer::Execute`, no ramo `if(packet_sink_) {`, troque a linha:

```cpp
    if(ok) ok=packet_sink_(std::move(packet),error);
```

por:

```cpp
    if(ok) {
      // The swap hand-off waits for the previous frame's recording; count it as blocked, not busy.
      if(swap) {graphics::TimelineBlockScope blocked;ok=packet_sink_(std::move(packet),error);}
      else ok=packet_sink_(std::move(packet),error);
    }
```

- [ ] **Step 6: Compilar o jogo**

```powershell
$env:SR_BUILD_LAUNCHER = 'OFF'
cmd /c build.cmd 2>&1 | Select-Object -Last 15
Remove-Item env:SR_BUILD_LAUNCHER
```

Expected: saída sem `error:` e `exit 0` (a última linha de `ninja` mostra o link de `superman_returns.exe`). Se falhar por `Op` ou `std::min` não encontrado, confira os `#include <algorithm>` (já presente na linha 28) e que o trecho está dentro de `Renderer::WorkerMain`.

- [ ] **Step 7: Verificação em jogo (D3D12)**

Avise o usuário que o bench leva alguns minutos. Depois:

```powershell
$env:SR_FRAME_TIMELINE = "$PWD\logs\timeline_smoke_d3d12.csv"
powershell -NoProfile -File tools\bench\bench_api.ps1 -Api d3d12 -Name tl_smoke_d3d12 -Exe "$PWD\port\out\build\win-amd64-release\superman_returns.exe"
Remove-Item env:SR_FRAME_TIMELINE
python tools\analysis\frame_timeline_report.py logs\timeline_smoke_d3d12.csv
```

Expected: o bench termina com FPS normais (≈ 26–30). A tabela mostra as linhas `game`, `front_wait` e `worker`, e **`descartados` ≤ 5% dos quadros**. Se os descartados passarem de 5%, **pare**: os ids de quadro não estão alinhados entre as threads; confira `cmd.u64` do `kSwap`.

- [ ] **Step 8: Commit**

```powershell
git add port/src/native_renderer/native_renderer.h port/src/native_renderer/native_renderer.cpp
git commit -m "feat(native): record the game, capture, front wait and worker stages" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: D3D12: esperas de GPU e tempo de GPU por quadro

**Files:**
- Modify: `port/src/native_renderer/native_renderer.h` (`ts_frame_`)
- Modify: `port/src/native_renderer/native_renderer.cpp` (`BeginFrame`, `EndFrameTimestamp`, `BeginFrameTimestamp`)

**Interfaces:**
- Consumes: `TimelineBlockScope`, `FrameTimeline::RecordBusy`, o membro `swap_number_` (o swap em execução no worker) e os timestamps D3D12 que já são escritos todo quadro.
- Produces: linhas `gpu` no CSV do D3D12 e `blocked_ns` do `worker` com a espera pelos fences.

- [ ] **Step 1: Membro**

Em `native_renderer.h`, logo depois da linha `bool ts_pending_[3] = {};`, acrescente:

```cpp
  uint64_t ts_frame_[3] = {};  // swap number of the frame measured in each slot (SR_FRAME_TIMELINE)
```

- [ ] **Step 2: Esperas em `BeginFrame`**

Em `Renderer::BeginFrame`, troque:

```cpp
  if (wait_value && fence_->GetCompletedValue() < wait_value) {
    fence_->SetEventOnCompletion(wait_value, fence_event_);
```

por:

```cpp
  if (wait_value && fence_->GetCompletedValue() < wait_value) {
    graphics::TimelineBlockScope gpu_blocked;
    fence_->SetEventOnCompletion(wait_value, fence_event_);
```

E troque:

```cpp
    present_fence_->SetEventOnCompletion(present_fence_values_[frame_index_], nullptr);
```

por:

```cpp
    graphics::TimelineBlockScope present_blocked;
    present_fence_->SetEventOnCompletion(present_fence_values_[frame_index_], nullptr);
```

- [ ] **Step 3: Tempo de GPU por quadro**

Em `Renderer::EndFrameTimestamp`, troque:

```cpp
  ts_pending_[frame_index_] = true;
```

por:

```cpp
  ts_frame_[frame_index_] = swap_number_;
  ts_pending_[frame_index_] = true;
```

Em `Renderer::BeginFrameTimestamp`, dentro de `if (n >= 2 && t[n - 1] > t[0]) {`, logo depois de `ts_accum_ms_ += double(t[n - 1] - t[0]) * 1000.0 / double(ts_frequency_);`, acrescente:

```cpp
      graphics::FrameTimeline::Global().RecordBusy(
          graphics::TimelineStage::kGpu, ts_frame_[frame_index_],
          uint64_t(double(t[n - 1] - t[0]) * 1e9 / double(ts_frequency_)));
```

- [ ] **Step 4: Compilar e verificar**

```powershell
$env:SR_BUILD_LAUNCHER = 'OFF'
cmd /c build.cmd 2>&1 | Select-Object -Last 15
Remove-Item env:SR_BUILD_LAUNCHER
$env:SR_FRAME_TIMELINE = "$PWD\logs\timeline_smoke_d3d12.csv"
powershell -NoProfile -File tools\bench\bench_api.ps1 -Api d3d12 -Name tl_smoke_d3d12 -Exe "$PWD\port\out\build\win-amd64-release\superman_returns.exe"
Remove-Item env:SR_FRAME_TIMELINE
python tools\analysis\frame_timeline_report.py logs\timeline_smoke_d3d12.csv
```

Expected: build sem `error:`; a tabela agora inclui a linha `gpu` (média de poucos ms a ~30 ms) e a coluna `bloq.` do `worker` deixa de ser 0 quando a GPU atrasa o worker.

- [ ] **Step 5: Commit**

```powershell
git add port/src/native_renderer/native_renderer.h port/src/native_renderer/native_renderer.cpp
git commit -m "feat(native): record D3D12 GPU time and fence waits in the timeline" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Vulkan: estágio `record` e tempo de GPU com timestamps

**Files:**
- Modify: `port/src/graphics/vulkan/functions.inc` (cinco funções de device)
- Modify: `port/src/graphics/vulkan/game_frame.h`
- Modify: `port/src/graphics/vulkan/game_frame.cpp`

**Interfaces:**
- Consumes: `FrameTimeline` (Task 1), `SwapPacket::guest_swap` (igual ao swap number: `render_packet.cpp` copia `cmd.u64`), `Context::properties.limits.timestampPeriod`, `Context::graphics_family`.
- Produces: linhas `record` e `gpu` no CSV do Vulkan; `FlushAll` ao destruir o `GameFrame`.

- [ ] **Step 1: Funções do Vulkan**

Em `port/src/graphics/vulkan/functions.inc`, logo depois de `VK_DEVICE(vkResetFences)`, acrescente:

```
VK_DEVICE(vkCreateQueryPool)
VK_DEVICE(vkDestroyQueryPool)
VK_DEVICE(vkCmdResetQueryPool)
VK_DEVICE(vkCmdWriteTimestamp)
VK_DEVICE(vkGetQueryPoolResults)
```

- [ ] **Step 2: Membros em `game_frame.h`**

Em `port/src/graphics/vulkan/game_frame.h`, na seção `private:`, logo depois de `bool WaitSlot(size_t slot,Error&);`, acrescente:

```cpp
  void HarvestTimestamps(size_t slot);
```

E logo depois da linha `std::array<uint64_t,kSlots> slot_serial_{};std::array<bool,kSlots> slot_submitted_{};` acrescente:

```cpp
  // SR_FRAME_TIMELINE: two timestamps per slot bracket the frame's GPU work.
  VkQueryPool timestamps_=VK_NULL_HANDLE;std::array<uint64_t,kSlots> slot_swap_{};uint64_t timestamp_mask_=0;double timestamp_period_ns_=0;
```

- [ ] **Step 3: `game_frame.cpp` — include e criação do pool**

Depois de `#include <unordered_map>`, acrescente:

```cpp
#include "../frame_timeline.h"
```

Em `GameFrame::Initialize`, troque o `return true;` final (o que vem depois do laço `for(size_t slot=0;slot<kSlots;++slot) {...}`) por:

```cpp
  if(FrameTimeline::Global().enabled()) {
    uint32_t count=0;c_.f.vkGetPhysicalDeviceQueueFamilyProperties(c_.physical,&count,nullptr);
    std::vector<VkQueueFamilyProperties> families(count);c_.f.vkGetPhysicalDeviceQueueFamilyProperties(c_.physical,&count,families.data());
    const uint32_t bits=c_.graphics_family<count?families[c_.graphics_family].timestampValidBits:0;
    if(bits) {
      VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};info.queryType=VK_QUERY_TYPE_TIMESTAMP;info.queryCount=uint32_t(kSlots*2);
      if(!Check(c_.f.vkCreateQueryPool(c_.device,&info,nullptr,&timestamps_),"Game timestamp pool",e)) return false;
      timestamp_mask_=bits>=64?~0ull:(1ull<<bits)-1;timestamp_period_ns_=c_.properties.limits.timestampPeriod;
    }
  }
  return true;
```

- [ ] **Step 4: Destrutor e `HarvestTimestamps`**

Em `GameFrame::~GameFrame`, depois da linha `if(slot_submitted_[0] || slot_submitted_[1]) {c_.f.vkDeviceWaitIdle(c_.device);renderer_.Retire(serial_);}`, acrescente:

```cpp
  if(timestamps_) {for(size_t slot=0;slot<kSlots;++slot) HarvestTimestamps(slot);c_.f.vkDestroyQueryPool(c_.device,timestamps_,nullptr);}
  FrameTimeline::Global().FlushAll();
```

Depois de `GameFrame::WaitSlot` (antes de `void GameFrame::Cancel()`), acrescente:

```cpp
// The slot's frame has completed (fence waited or device idle): convert its two timestamps to GPU time.
void GameFrame::HarvestTimestamps(size_t slot) {
  if(!timestamps_ || !slot_swap_[slot]) return;
  uint64_t ticks[2]{};
  if(c_.f.vkGetQueryPoolResults(c_.device,timestamps_,uint32_t(slot*2),2,sizeof(ticks),ticks,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT)==VK_SUCCESS)
    FrameTimeline::Global().RecordBusy(TimelineStage::kGpu,slot_swap_[slot],uint64_t(double((ticks[1]-ticks[0])&timestamp_mask_)*timestamp_period_ns_));
  slot_swap_[slot]=0;
}
```

- [ ] **Step 5: `RecordFrame` — leitura, escrita dos timestamps e span `record`**

Em `RecordFrame`, logo depois de `if(!WaitSlot(slot,e)) {failed_=true;return false;}`, acrescente:

```cpp
  HarvestTimestamps(slot);
```

Logo depois da linha que começa com `if(!Check(c_.f.vkBeginCommandBuffer(upload_,&begin),"Begin game uploads",e) || ...` (a que termina em `return fail();`), acrescente:

```cpp
  if(timestamps_) {c_.f.vkCmdResetQueryPool(upload_,timestamps_,uint32_t(slot*2),2);c_.f.vkCmdWriteTimestamp(upload_,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,timestamps_,uint32_t(slot*2));}
```

Logo antes de `if(!Check(c_.f.vkEndCommandBuffer(upload_),"End game uploads",e) || ...`, acrescente:

```cpp
  if(timestamps_) {c_.f.vkCmdWriteTimestamp(command_,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timestamps_,uint32_t(slot*2+1));slot_swap_[slot]=swap.guest_swap;}
```

Logo depois de `const auto finished=std::chrono::steady_clock::now();`, acrescente:

```cpp
  if(auto& timeline=FrameTimeline::Global();timeline.enabled()) {
    // Waiting for shaders, the slot's fence and the queue lock is time spent on someone else.
    const uint64_t blocked=FrameTimeline::ToNs(fence_ready)-FrameTimeline::ToNs(started)+FrameTimeline::ToNs(queue_ready)-FrameTimeline::ToNs(submit_started);
    timeline.RecordSpan(TimelineStage::kRecord,swap.guest_swap,FrameTimeline::ToNs(started),FrameTimeline::ToNs(finished),blocked);
  }
```

- [ ] **Step 6: Compilar os testes e o jogo**

```powershell
. .\tools\dev_env.ps1
cmake --build build/tests-vulkan
.\build\tests-vulkan\sr_vulkan_tests.exe | Select-Object -Last 3
$env:SR_BUILD_LAUNCHER = 'OFF'
cmd /c build.cmd 2>&1 | Select-Object -Last 15
Remove-Item env:SR_BUILD_LAUNCHER
```

Expected: `sr_vulkan_tests` sem falhas (o número de casos não muda); o build do jogo sem `error:`. Se `build/tests-vulkan` não existir ou estiver desconfigurado, rode antes `cmake -S tests/vulkan -B build/tests-vulkan` (veja `docs/checkpoints/checkpoint7.md` para o `SR_VULKAN_DXC`).

- [ ] **Step 7: Verificação com GPU real (fixture)**

```powershell
cmake --build build/tests-vulkan --target sr_vulkan_resources_test
$env:SR_FRAME_TIMELINE = "$PWD\build\timeline_fixture.csv"
.\build\tests-vulkan\sr_vulkan_resources_test.exe
Remove-Item env:SR_FRAME_TIMELINE
Select-String -Path build\timeline_fixture.csv -Pattern ",record,|,gpu,"
```

Expected: o fixture termina com exit 0 e o `Select-String` mostra pelo menos uma linha `,record,` e uma `,gpu,` (o destrutor do `GameFrame` despeja tudo). Se não houver linha `gpu`, o `timestampValidBits` da fila é 0 ou o `HarvestTimestamps` não foi chamado no destrutor.

- [ ] **Step 8: Verificação em jogo (Vulkan)**

Avise o usuário que o bench leva alguns minutos.

```powershell
$env:SR_FRAME_TIMELINE = "$PWD\logs\timeline_smoke_vulkan.csv"
powershell -NoProfile -File tools\bench\bench_api.ps1 -Api vulkan -Name tl_smoke_vulkan -Exe "$PWD\port\out\build\win-amd64-release\superman_returns.exe"
Remove-Item env:SR_FRAME_TIMELINE
python tools\analysis\frame_timeline_report.py logs\timeline_smoke_vulkan.csv
```

Expected: a tabela mostra `game`, `game_other`, `capture`, `front_wait`, `worker`, `record` e `gpu`, com descartados ≤ 5%.

- [ ] **Step 9: Commit**

```powershell
git add port/src/graphics/vulkan/functions.inc port/src/graphics/vulkan/game_frame.h port/src/graphics/vulkan/game_frame.cpp
git commit -m "feat(vulkan): record the recording stage and GPU time in the timeline" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 6: `bench_api.ps1 -Timeline` e índice das ferramentas

**Files:**
- Modify: `tools/bench/bench_api.ps1`
- Modify: `tools/README.md`

**Interfaces:**
- Consumes: `SR_FRAME_TIMELINE` (Tasks 1–5) e `tools/analysis/frame_timeline_report.py` (Task 2).
- Produces: `logs/timeline_<Name>.csv` e `logs/timeline_<Name>.txt` (a tabela), com `bench_api.ps1 -Timeline`.

- [ ] **Step 1: Editar `bench_api.ps1`**

No comentário de uso, troque a linha:

```powershell
Uso: tools\bench\bench_api.ps1 -Api vulkan|d3d12 -Name <rótulo> [-Profile] [-Gate record|check] [-Exe <caminho>]
```

por:

```powershell
Uso: tools\bench\bench_api.ps1 -Api vulkan|d3d12 -Name <rótulo> [-Profile] [-Timeline] [-Gate record|check] [-Exe <caminho>]
```

e depois da linha que descreve `-Profile` acrescente:

```powershell
  -Timeline liga SR_FRAME_TIMELINE, grava logs\timeline_<Name>.csv e imprime a tabela do estágio limitante
            (tools\analysis\frame_timeline_report.py), também salva em logs\timeline_<Name>.txt.
```

No `param(...)`, troque `[switch]$Profile,` por:

```powershell
  [switch]$Profile,
  [switch]$Timeline,
```

Troque a linha `$prevProfile = $env:SR_VULKAN_PROFILE` por:

```powershell
$prevProfile = $env:SR_VULKAN_PROFILE
$prevTimeline = $env:SR_FRAME_TIMELINE
$timelineCsv = "$root\logs\timeline_$Name.csv"
```

Dentro do `try {`, logo depois de `if ($Profile) { $env:SR_VULKAN_PROFILE = '1' }`, acrescente:

```powershell
  if ($Timeline) {
    Remove-Item $timelineCsv -ErrorAction SilentlyContinue
    $env:SR_FRAME_TIMELINE = $timelineCsv
  }
```

No `finally {`, antes da restauração das variáveis, acrescente:

```powershell
  if ($Timeline -and (Test-Path $timelineCsv)) {
    python "$root\tools\analysis\frame_timeline_report.py" $timelineCsv | Tee-Object "$root\logs\timeline_$Name.txt"
  }
  if ($null -ne $prevTimeline) { $env:SR_FRAME_TIMELINE = $prevTimeline } else { Remove-Item env:SR_FRAME_TIMELINE -ErrorAction SilentlyContinue }
```

- [ ] **Step 2: Índice**

Em `tools/README.md`, na tabela de `bench/`, troque a linha do `bench_api.ps1` por:

```markdown
| `bench_api.ps1` | Roda o `bench.ps1` com os argumentos que o launcher passa para uma API (`-Api vulkan` ou `d3d12`); `-Profile` liga o perfil do Vulkan e `-Timeline` grava a linha do tempo por quadro e imprime o estágio limitante. É o ponto de partida. |
```

e, na tabela de `analysis/`, acrescente a linha:

```markdown
| `frame_timeline_report.py` | Lê o CSV de `SR_FRAME_TIMELINE` e imprime FPS, 1% low, tempo ocupado/bloqueado por estágio e a fração de quadros em que cada estágio foi o limitante. Teste em `tests/tools/test_frame_timeline_report.py`. |
```

- [ ] **Step 3: Verificar**

```powershell
powershell -NoProfile -File tools\bench\bench_api.ps1 -Api d3d12 -Name tl_check -Timeline -Exe "$PWD\port\out\build\win-amd64-release\superman_returns.exe"
Test-Path logs\timeline_tl_check.csv, logs\timeline_tl_check.txt
```

Expected: a tabela é impressa ao fim do bench e os dois arquivos existem (`True True`). Depois confirme que a variável não vazou: `$env:SR_FRAME_TIMELINE` deve estar vazia.

- [ ] **Step 4: Commit**

```powershell
git add tools/bench/bench_api.ps1 tools/README.md
git commit -m "feat(bench): add -Timeline to bench_api.ps1" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Medir, documentar a tabela do estágio limitante e fechar a Fase 0

**Files:**
- Create: `docs/native-renderer-timeline.md`
- Modify: `docs/superpowers/specs/2026-10-08-native-renderer-cpu-gpu-bottlenecks-design.md` (responder as perguntas em aberto)

**Interfaces:**
- Consumes: tudo acima. Produces: a tabela medida que decide a ordem da Fase 1.

- [ ] **Step 1: Sanidade do custo (ligado contra desligado)**

Avise o usuário que são quatro execuções de bench por API (alguns minutos cada). Rode em ordem OFF, ON, ON, OFF no Vulkan e repita no D3D12, com a mesma build:

```powershell
$exe = "$PWD\port\out\build\win-amd64-release\superman_returns.exe"
foreach ($api in 'vulkan','d3d12') {
  powershell -NoProfile -File tools\bench\bench_api.ps1 -Api $api -Name "tl_off1_$api" -Exe $exe
  powershell -NoProfile -File tools\bench\bench_api.ps1 -Api $api -Name "tl_on1_$api"  -Exe $exe -Timeline
  powershell -NoProfile -File tools\bench\bench_api.ps1 -Api $api -Name "tl_on2_$api"  -Exe $exe -Timeline
  powershell -NoProfile -File tools\bench\bench_api.ps1 -Api $api -Name "tl_off2_$api" -Exe $exe
}
Get-Content logs\bench_results.csv | Select-String "tl_o"
```

Expected: os FPS ligados ficam na mesma faixa dos desligados (a variação de cena entre execuções é maior que o custo; o limite de custo é o teste unitário `timeline_recording_a_frame_costs_microseconds`). Se o FPS ligado ficar claramente abaixo nas duas execuções, investigue antes de seguir.

- [ ] **Step 2: Tabela do estágio limitante**

Use as duas execuções `-Timeline` de cada API; os `.txt` já têm a tabela:

```powershell
Get-Content logs\timeline_tl_on1_vulkan.txt, logs\timeline_tl_on2_vulkan.txt, logs\timeline_tl_on1_d3d12.txt, logs\timeline_tl_on2_d3d12.txt
```

- [ ] **Step 3: Escrever `docs/native-renderer-timeline.md`**

Estrutura (copie os números **das tabelas impressas no passo anterior**, uma seção por API e execução):

```markdown
# Linha do tempo por quadro do renderer nativo

Ferramenta da Fase 0 do ciclo 4 (design: `superpowers/specs/2026-10-08-native-renderer-cpu-gpu-bottlenecks-design.md`).

## Como usar

- `SR_FRAME_TIMELINE=<arquivo.csv>` liga a gravação; sem ela nada é registrado.
- `tools\bench\bench_api.ps1 -Api vulkan|d3d12 -Name <nome> -Timeline` roda o bench e imprime a tabela.
- `python tools\analysis\frame_timeline_report.py <arquivo.csv> [--last N] [--json]` analisa um CSV.
- Colunas, estágios e o significado de `busy_ns`/`blocked_ns`: `docs/superpowers/plans/2026-10-08-frame-timeline-phase0.md`.

## Como ler a tabela

`média/p50/p99` são ms de trabalho do estágio por quadro (sem a espera); `bloq.` é a espera média por outro estágio; `util.` é média ÷ intervalo médio do quadro; `folga p99` é o orçamento de 33,3 ms menos o p99; `limitante` é a fração dos quadros em que o estágio teve o maior tempo ocupado entre `game`, `worker`, `record` e `gpu`. `capture` está aninhado em `game` (só existe no Vulkan, onde a captura roda na thread do jogo).

## Resultados (Intel UHD, i5-13420H, 1280x720, vsync, limite de 30 FPS, 2026-10-08)

### Vulkan
<cole aqui a tabela de tl_on1_vulkan e a de tl_on2_vulkan>

### D3D12
<cole aqui a tabela de tl_on1_d3d12 e a de tl_on2_d3d12>

## Conclusão: estágio limitante de cada API

<uma frase por API: qual estágio tem o maior `util.`/`limitante` e qual a folga p99; e a ordem recomendada para a Fase 1 segundo a tabela de decisão do design (passos 1 a 4)>
```

Substitua os dois marcadores `<...>` pelos números e pela conclusão reais antes de commitar. A conclusão precisa citar os valores medidos (por exemplo, "no Vulkan o `game` tem util. X% e é o limitante em Y% dos quadros").

- [ ] **Step 4: Responder as perguntas em aberto do design**

Em `docs/superpowers/specs/2026-10-08-native-renderer-cpu-gpu-bottlenecks-design.md`, na seção "Perguntas em aberto (a Fase 0 responde)", acrescente sob cada pergunta uma linha `Resposta (Fase 0, 2026-10-08): ...` com o dado medido. A pergunta "O Vulkan já tem timestamps de GPU por grupo de passes?" tem resposta definitiva: **não tinha**; esta fase adicionou só o tempo de GPU por quadro, e o agrupamento por passe ficou como decisão pendente (ver "Fora do escopo desta fase" neste plano).

- [ ] **Step 5: Suites completas e grafo**

```powershell
ctest --test-dir build/tests-native --output-on-failure
.\build\tests-vulkan\sr_vulkan_tests.exe | Select-Object -Last 3
python -m pytest tests/tools -q
graphify update .
```

Expected: todos passam (nativo: 100% no `ctest`; Vulkan: sem falhas; pytest: sem falhas novas); `graphify update .` termina sem erro.

- [ ] **Step 6: Commit**

```powershell
git add docs/native-renderer-timeline.md docs/superpowers/specs/2026-10-08-native-renderer-cpu-gpu-bottlenecks-design.md
git commit -m "docs: record the Phase 0 limiting-stage table for D3D12 and Vulkan" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Self-review

**Cobertura do design (seção 1):**
- Ring buffer por quadro, sem alocação nem lock → Task 1 (`Record`, `Slot`; o `Flush` usa mutex só no despejo, fora do caminho quente).
- Estágios jogo/captura/replay-worker/gravação/GPU/present → Tasks 3–5. O **present** não ganhou estágio próprio: o intervalo entre `game.end_ns` consecutivos já dá o ritmo de apresentação e o FPS; está registrado aqui como decisão.
- Formato comum às duas APIs → CSV único; `capture` e `record` aparecem só onde existem (Task 2 testa a ausência).
- Cálculo do caminho crítico, média/mínimo/1% low e percentual limitante → Task 2.
- Timestamps de GPU → tempo por quadro inteiro nas duas APIs (Tasks 4–5); o agrupamento por passe foi adiado de forma explícita (seção "Fora do escopo").
- Instrumentação < 0,3 ms, desligada por padrão, sem mudar imagem → Task 1 (teste de custo), Task 7 (A/B de FPS), `enabled()` curto-circuita tudo.
- Saída em `docs/` com a tabela por API → Task 7.

**Placeholders:** os dois marcadores `<...>` da Task 7, Step 3 são dados que só existem depois da medição (o passo manda substituí-los antes do commit); nenhum passo de código tem trecho vago.

**Consistência de tipos:** `TimelineStage` (`kGame`, `kCapture`, `kFrontWait`, `kWorker`, `kRecord`, `kGpu`), `RecordSpan(stage, frame, begin, end, blocked)`, `RecordBusy(stage, frame, busy)`, `Record(stage, frame, begin, end, busy, blocked)`, `Flush(newest)`, `FlushAll()`, `TimelineBlockScope`, `TakeTimelineBlockedNs()` têm a mesma assinatura em todas as tasks. Os nomes de estágio no CSV (`game`, `capture`, `front_wait`, `worker`, `record`, `gpu`) são os mesmos que o script espera.

**Riscos conhecidos:**
- Os ids de quadro precisam coincidir entre threads (swap number); a Task 3 tem uma checagem explícita de descartados ≤ 5%.
- `SwapPacket::guest_swap` vem de `cmd.u64`, coberto por `tests/native/test_render_packets.cpp:237`.
- O estágio `game` inclui o trabalho do próprio jogo (lógica), não só o do renderer; `game_other` mostra a parte que não é captura.
- Sem validation layers, a verificação do Vulkan é o fixture com GPU real (Task 5, Step 7).
