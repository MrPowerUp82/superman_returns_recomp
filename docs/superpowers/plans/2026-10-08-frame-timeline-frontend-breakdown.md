# Fase 0.5: decomposição do front-end da thread do jogo — Plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Descobrir de que é feito o `game_other` (~28–30 ms) do Vulkan: quanto é trabalho do próprio renderer na thread do jogo (hooks de draw, captura de dispositivo, planejamento de streams, fila para o worker) e quanto é lógica do jogo (inclui o driver XDK recompilado) ou espera.

**Architecture:** Estende a linha do tempo da Fase 0 com estágios `frontend` (tempo total dentro dos hooks do renderer) e `fe_*` (partes: begin, ring, device, index, streams, end, flush), acumulados em nanossegundos por quadro nos mesmos `capture_timings` da thread do jogo e gravados no `OnSwap`. Os contadores finos são opt-in (`SR_FRAME_TIMELINE_DETAIL=1`, além de `SR_FRAME_TIMELINE`), porque somam alguns relógios por draw. O relatório deriva `game_guest = game − frontend` e `frontend_other`.

**Tech Stack:** C++20 (renderer nativo), Python 3 + pytest, PowerShell (`bench_api.ps1`).

## Global Constraints

- Desligado por padrão; sem `SR_FRAME_TIMELINE` e `SR_FRAME_TIMELINE_DETAIL` nada muda (nenhum relógio extra, nenhum lock, nenhuma alteração de imagem ou comportamento).
- Esta fase não faz nenhuma otimização: só instrumento e diagnóstico.
- Contadores finos em **nanossegundos** (a Fase de ciclo 3 mostrou que truncar para µs por chamada acumula erro de ms por quadro).
- Imagem bit a bit idêntica; texturas exatas a cada quadro; `sr_renderer=native` nunca cai para Xenos.
- Avise o progresso entre os passos que levam minutos (build do jogo, bench); o usuário interrompeu loops longos silenciosos.
- Commits só como parte da execução autorizada deste plano; trailer exato `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`; nunca commitar `port/superman_returns_manifest.toml`, `logs/` nem `artifacts/`.

## Contexto medido (por que isto existe)

Nos logs existentes com `SR_VULKAN_PROFILE=1` (builds de 08/10, `logs/bench_watch_scan_*_vulkan.log`), `native CPU frontend ... capture=` (tempo de `BeginCmd` até `EndCmd`, somado por quadro) vale **18–24 ms** no Vulkan. O estágio `capture` da Fase 0 mede só PM4 + texturas (6,6–7,2 ms). Portanto ~12–17 ms de trabalho do renderer na thread do jogo estão dentro do `game_other`, fora do estágio `capture`. Esses logs são de outras execuções e cenas: esta fase mede de novo no mesmo formato da Fase 0.

## Estrutura de arquivos

| Arquivo | Mudança |
| --- | --- |
| `port/src/graphics/frame_timeline.h` | Novos estágios `kFrontend`, `kFeBegin`, `kFeRing`, `kFeDevice`, `kFeIndex`, `kFeStreams`, `kFeEnd`, `kFeFlush` e `FrameTimeline::Detail()` |
| `tests/native/test_frame_timeline.cpp` | Testes dos nomes de estágio |
| `tools/analysis/frame_timeline_report.py` | Estágios novos e derivados `game_guest`, `frontend_other` |
| `tests/tools/test_frame_timeline_report.py` | Testes do detalhamento |
| `port/src/native_renderer/native_renderer.cpp` | Escopos de tempo nos hooks e nas etapas do front-end; gravação no `OnSwap` |
| `tools/bench/bench_api.ps1`, `tools/README.md` | Opção `-TimelineDetail` |
| `docs/native-renderer-timeline.md` | Resultado do detalhamento e conclusão |

## Estágios novos no CSV

| Estágio | O que mede (thread do jogo, nanossegundos somados por quadro) |
| --- | --- |
| `frontend` | tempo total dentro dos hooks `DrawVertices`, `DrawIndexedVertices`, `DrawInlineVertices`, `Resolve`, `BeginTiling`, `Clear`, `EndTiling`, `OnPassEnd` (inclui a espera do `front_mutex_`) |
| `fe_begin` | `BeginCmd` (inclui `cur_ = WorkCmd{}`) |
| `fe_ring` | `CaptureRing` |
| `fe_device` | `CaptureDevice` |
| `fe_index` | bloco do buffer de índices no `DrawIndexedVertices` |
| `fe_streams` | `PlanStreams` |
| `fe_end` | `EndCmd` inteiro (aninha PM4, texturas e `fe_flush`) |
| `fe_flush` | `FlushBatch` |

Nota: o comando do próprio `OnSwap` também passa por `BeginCmd`, `CaptureRing` e `EndCmd`; seu custo (µs) entra em `fe_begin/ring/end` mas não em `frontend`.

---

### Task 1: Estágios novos e `Detail()` em `frame_timeline.h`

**Files:**
- Modify: `port/src/graphics/frame_timeline.h`
- Modify: `tests/native/test_frame_timeline.cpp`

**Interfaces:**
- Produces: `TimelineStage::{kFrontend,kFeBegin,kFeRing,kFeDevice,kFeIndex,kFeStreams,kFeEnd,kFeFlush}` (nomes no CSV: `frontend`, `fe_begin`, `fe_ring`, `fe_device`, `fe_index`, `fe_streams`, `fe_end`, `fe_flush`), `static bool FrameTimeline::Detail()`.

- [ ] **Step 1: Escrever os testes que falham**

Acrescente em `tests/native/test_frame_timeline.cpp` (e `#include <set>` junto dos outros includes):

```cpp
SR_TEST(timeline_every_stage_has_a_distinct_name) {
  std::set<std::string> names;
  for (size_t stage = 0; stage < size_t(TimelineStage::kCount); ++stage) {
    const std::string name = TimelineStageName(TimelineStage(stage));
    SR_CHECK(name != "unknown");
    SR_CHECK(names.insert(name).second);
  }
}

SR_TEST(timeline_front_end_detail_stages_use_their_csv_names) {
  FrameTimeline timeline("unused.csv");
  timeline.RecordBusy(TimelineStage::kFrontend, 9, 100);
  timeline.RecordBusy(TimelineStage::kFeStreams, 9, 123);
  timeline.RecordBusy(TimelineStage::kFeFlush, 9, 7);
  const std::string rows = Rows(timeline, 9, 9);
  SR_CHECK(rows.find("9,frontend,0,0,100,0\n") != std::string::npos);
  SR_CHECK(rows.find("9,fe_streams,0,0,123,0\n") != std::string::npos);
  SR_CHECK(rows.find("9,fe_flush,0,0,7,0\n") != std::string::npos);
}

SR_TEST(timeline_detail_is_off_without_the_environment) {
  // The test process sets neither SR_FRAME_TIMELINE nor SR_FRAME_TIMELINE_DETAIL.
  SR_CHECK(!FrameTimeline::Detail());
}
```

- [ ] **Step 2: Rodar e ver falhar**

```powershell
. .\tools\dev_env.ps1
cmake --build build/tests-native
```

Expected: FAIL de compilação (`kFrontend` / `Detail` não existem).

- [ ] **Step 3: Implementar**

Em `port/src/graphics/frame_timeline.h`: troque o enum por

```cpp
enum class TimelineStage : uint8_t {
  kGame, kCapture, kFrontWait, kWorker, kRecord, kGpu,
  // Front-end detail on the game thread (SR_FRAME_TIMELINE_DETAIL=1).
  kFrontend, kFeBegin, kFeRing, kFeDevice, kFeIndex, kFeStreams, kFeEnd, kFeFlush,
  kCount
};
```

e em `TimelineStageName` acrescente, antes do `default:`:

```cpp
    case TimelineStage::kFrontend: return "frontend";
    case TimelineStage::kFeBegin: return "fe_begin";
    case TimelineStage::kFeRing: return "fe_ring";
    case TimelineStage::kFeDevice: return "fe_device";
    case TimelineStage::kFeIndex: return "fe_index";
    case TimelineStage::kFeStreams: return "fe_streams";
    case TimelineStage::kFeEnd: return "fe_end";
    case TimelineStage::kFeFlush: return "fe_flush";
```

Na classe `FrameTimeline`, logo depois de `PathFromEnvironment()`, acrescente:

```cpp
  // Fine-grained front-end timers add a few clock reads per draw, so they are
  // opt-in on top of SR_FRAME_TIMELINE.
  static bool Detail() {
    static const bool on = [] {
      if (!Global().enabled()) return false;
      const char* value = std::getenv("SR_FRAME_TIMELINE_DETAIL");
      return value && *value && std::string(value) != "0";
    }();
    return on;
  }
```

- [ ] **Step 4: Rodar e ver passar**

```powershell
cmake --build build/tests-native
ctest --test-dir build/tests-native --output-on-failure
.\build\tests-native\sr_native_tests.exe | Select-String "timeline"
```

Expected: `ctest` 100% passando; treze `PASS timeline_...` (os dez da Fase 0 mais os três novos).

- [ ] **Step 5: Commit**

```powershell
git add port/src/graphics/frame_timeline.h tests/native/test_frame_timeline.cpp
git commit -m "feat(native): add front-end detail stages to the frame timeline" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Relatório com o detalhamento do front-end

**Files:**
- Modify: `tools/analysis/frame_timeline_report.py`
- Modify: `tests/tools/test_frame_timeline_report.py`

**Interfaces:**
- Consumes: os estágios novos do CSV (Task 1).
- Produces: em `analyze(...)["stages"]`: os estágios brutos `frontend`, `fe_begin`, `fe_ring`, `fe_device`, `fe_index`, `fe_streams`, `fe_end`, `fe_flush` quando presentes, e os derivados `game_guest` (= `max(0, game − frontend)`) e `frontend_other` (= `max(0, frontend − (fe_begin+fe_ring+fe_device+fe_index+fe_streams+fe_end))`), só quando `frontend` existe no quadro.

- [ ] **Step 1: Escrever o teste que falha**

Acrescente em `tests/tools/test_frame_timeline_report.py`:

```python
def detail_rows(frame):
    rows = frame_rows(frame, skip=("record",))
    rows += [
        (frame, "frontend", 0, 0, 15 * MS, 0),
        (frame, "fe_begin", 0, 0, 1 * MS, 0),
        (frame, "fe_ring", 0, 0, 2 * MS, 0),
        (frame, "fe_device", 0, 0, 4 * MS, 0),
        (frame, "fe_index", 0, 0, 1 * MS, 0),
        (frame, "fe_streams", 0, 0, 3 * MS, 0),
        (frame, "fe_end", 0, 0, 3 * MS, 0),
        (frame, "fe_flush", 0, 0, 1 * MS, 0),
    ]
    return rows


def test_front_end_detail_derives_game_guest_and_frontend_other(tmp_path):
    rows = []
    for frame in range(1, 6):
        rows += detail_rows(frame)
    path = tmp_path / "detail.csv"
    write_csv(path, rows)
    stages = ftr.analyze(ftr.load(path))["stages"]
    assert stages["frontend"]["mean"] == pytest.approx(15.0)
    assert stages["fe_device"]["mean"] == pytest.approx(4.0)
    assert stages["game_guest"]["mean"] == pytest.approx(5.0)       # game 20 - frontend 15
    assert stages["frontend_other"]["mean"] == pytest.approx(1.0)   # 15 - (1+2+4+1+3+3)


def test_front_end_derivations_are_clamped_at_zero(tmp_path):
    rows = []
    for frame in range(1, 4):
        rows += frame_rows(frame, skip=("record",))
        rows += [(frame, "frontend", 0, 0, 25 * MS, 0), (frame, "fe_device", 0, 0, 30 * MS, 0)]
    path = tmp_path / "clamp.csv"
    write_csv(path, rows)
    stages = ftr.analyze(ftr.load(path))["stages"]
    assert stages["game_guest"]["mean"] == 0.0       # game 20 < frontend 25
    assert stages["frontend_other"]["mean"] == 0.0   # frontend 25 < parts 30


def test_report_without_detail_has_no_front_end_rows(tmp_path):
    result = ftr.analyze(ftr.load(make(tmp_path)))
    for name in ("frontend", "game_guest", "frontend_other", "fe_device"):
        assert name not in result["stages"]
```

- [ ] **Step 2: Rodar e ver falhar**

```powershell
python -m pytest tests/tools/test_frame_timeline_report.py -q
```

Expected: FAIL (`KeyError: 'frontend'` ou `game_guest`).

- [ ] **Step 3: Implementar**

Em `tools/analysis/frame_timeline_report.py`:

Troque a constante `STAGES` por:

```python
FRONT_END_PARTS = ("fe_begin", "fe_ring", "fe_device", "fe_index", "fe_streams", "fe_end")
STAGES = ("game", "game_guest", "game_other", "capture", "frontend", *FRONT_END_PARTS, "fe_flush",
          "frontend_other", "front_wait", "worker", "record", "gpu")
```

No laço de estágios brutos de `analyze`, troque `for name in ("game", "capture", "front_wait", "worker", "record", "gpu"):` por:

```python
    for name in ("game", "capture", "front_wait", "worker", "record", "gpu", "frontend", *FRONT_END_PARTS, "fe_flush"):
```

Logo depois do bloco que cria `stages["game_other"]` e antes de `result["stages"] = stages`, acrescente:

```python
    with_frontend = [i for i in complete if "frontend" in frames[i]]
    if with_frontend:  # game thread time outside the renderer hooks, and the hooks' unattributed rest
        guest = [max(0, frames[i]["game"]["busy_ns"] - frames[i]["frontend"]["busy_ns"]) / 1e6 for i in with_frontend]
        stages["game_guest"] = _summary(guest, [0.0] * len(guest), mean_interval, budget_ms)
        rest = [max(0, frames[i]["frontend"]["busy_ns"]
                    - sum(frames[i][part]["busy_ns"] for part in FRONT_END_PARTS if part in frames[i])) / 1e6
                for i in with_frontend]
        stages["frontend_other"] = _summary(rest, [0.0] * len(rest), mean_interval, budget_ms)
```

- [ ] **Step 4: Rodar e ver passar**

```powershell
python -m pytest tests/tools/test_frame_timeline_report.py -q
python -m pytest tests/tools -q
```

Expected: `12 passed` no arquivo (9 antigos + 3 novos) e a suíte inteira sem falhas.

- [ ] **Step 5: Commit**

```powershell
git add tools/analysis/frame_timeline_report.py tests/tools/test_frame_timeline_report.py
git commit -m "feat(tools): break game time into front-end parts in the timeline report" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Escopos de tempo no front-end do renderer

**Files:**
- Modify: `port/src/native_renderer/native_renderer.cpp`

**Interfaces:**
- Consumes: `FrameTimeline::Detail()`, `FrameTimeline::NowNs()`, `RecordBusy`, os novos `TimelineStage` (Task 1).
- Produces: linhas `frontend` e `fe_*` no CSV quando `SR_FRAME_TIMELINE` e `SR_FRAME_TIMELINE_DETAIL=1` estão definidos. Com a variável de detalhe ausente, nenhum relógio novo é lido.

- [ ] **Step 1: Campos e o escopo**

Em `CaptureTimings` (próximo à linha 371), acrescente uma linha de campos depois de `uint64_t watch_scan_hits=0,watch_scan_misses=0;`:

```cpp
  uint64_t fe_hook_ns=0,fe_begin_ns=0,fe_ring_ns=0,fe_device_ns=0,fe_index_ns=0,fe_streams_ns=0,fe_end_ns=0,fe_flush_ns=0;
```

Logo depois da linha `thread_local CaptureTimings capture_timings;` acrescente:

```cpp
// SR_FRAME_TIMELINE_DETAIL=1: nanosecond accumulator for one front-end region of the game thread.
// Reads no clock unless the detail mode is on.
struct DetailScope {
  uint64_t& total;
  bool active = graphics::FrameTimeline::Detail();
  uint64_t start = active ? graphics::FrameTimeline::NowNs() : 0;
  explicit DetailScope(uint64_t& value) : total(value) {}
  void Stop() {
    if (active) {total += graphics::FrameTimeline::NowNs() - start;active = false;}
  }
  ~DetailScope() {Stop();}
  DetailScope(const DetailScope&) = delete;
  DetailScope& operator=(const DetailScope&) = delete;
};
```

- [ ] **Step 2: Hooks (frontend)**

Acrescente como **primeira linha do corpo** de cada uma destas funções (antes de qualquer `return` antecipado ou `lock_guard`):

```cpp
  DetailScope hook_scope(capture_timings.fe_hook_ns);
```

em `Renderer::DrawVertices`, `Renderer::DrawIndexedVertices` (antes do bloco `if (int32_t spin = ...)`), `Renderer::DrawInlineVertices`, `Renderer::Resolve`, `Renderer::BeginTiling`, `Renderer::Clear`, `Renderer::EndTiling` e `Renderer::OnPassEnd`.

- [ ] **Step 3: Partes**

- `Renderer::BeginCmd`: primeira linha do corpo → `DetailScope scope(capture_timings.fe_begin_ns);`
- `Renderer::CaptureRing`: primeira linha → `DetailScope scope(capture_timings.fe_ring_ns);`
- `Renderer::CaptureDevice`: primeira linha → `DetailScope scope(capture_timings.fe_device_ns);`
- `Renderer::PlanStreams`: primeira linha → `DetailScope scope(capture_timings.fe_streams_ns);`
- `Renderer::EndCmd`: primeira linha → `DetailScope scope(capture_timings.fe_end_ns);`
- `Renderer::FlushBatch`: primeira linha → `DetailScope scope(capture_timings.fe_flush_ns);`
- `Renderer::DrawIndexedVertices`, bloco do índice: imediatamente antes de `VertexRange draw_range;` que precede `if (uint32_t ib_object = Load32(base, dev + kDev.index_buffer)) {` acrescente `DetailScope index_scope(capture_timings.fe_index_ns);` e, imediatamente depois do `}` que fecha esse `if` (a linha seguinte é `cur_.streams_ok = PlanStreams(base, dev, Load32(base, dev + kDevVertexDecl), &draw_range);`), acrescente `index_scope.Stop();`.

Cada função tem uma única ocorrência da assinatura; se um âncora não for único, acrescente contexto e anote no relatório.

- [ ] **Step 4: Gravação no `OnSwap`**

No bloco que já existe em `Renderer::OnSwap`:

```cpp
  if (timeline.enabled()) {
    timeline.RecordSpan(graphics::TimelineStage::kGame, swap_number, ...
    timeline.RecordBusy(graphics::TimelineStage::kCapture, ...
  }
```

acrescente, **dentro** do mesmo `if (timeline.enabled()) {`, depois do `RecordBusy(kCapture...)`:

```cpp
    if (graphics::FrameTimeline::Detail()) {
      const auto& t = capture_timings;
      timeline.RecordBusy(graphics::TimelineStage::kFrontend, swap_number, t.fe_hook_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFeBegin, swap_number, t.fe_begin_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFeRing, swap_number, t.fe_ring_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFeDevice, swap_number, t.fe_device_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFeIndex, swap_number, t.fe_index_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFeStreams, swap_number, t.fe_streams_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFeEnd, swap_number, t.fe_end_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFeFlush, swap_number, t.fe_flush_ns);
    }
```

(O `capture_timings={};` que já vem logo depois zera os novos campos.)

- [ ] **Step 5: Compilar**

```powershell
$env:SR_BUILD_LAUNCHER = 'OFF'
cmd /c ".\build.cmd" 2>&1 | Select-Object -Last 15
Remove-Item env:SR_BUILD_LAUNCHER
```

Rode em background e aguarde (leva minutos). Expected: sem `error:`, `exit 0`, `superman_returns.exe` linkado. Confirme que `git status` só mostra `native_renderer.cpp` (e o `port/superman_returns_manifest.toml` modificado pelo build, que **não** se commita).

- [ ] **Step 6: Verificação rápida de sanidade (um bench, D3D12 não precisa)**

Avise o usuário que o bench leva alguns minutos.

```powershell
$env:SR_FRAME_TIMELINE = "$PWD\logs\timeline_fe_smoke_vulkan.csv"
$env:SR_FRAME_TIMELINE_DETAIL = '1'
powershell -NoProfile -File tools\bench\bench_api.ps1 -Api vulkan -Name tl_fe_smoke_vulkan -Exe "$PWD\port\out\build\win-amd64-release\superman_returns.exe"
Remove-Item env:SR_FRAME_TIMELINE, env:SR_FRAME_TIMELINE_DETAIL
python tools\analysis\frame_timeline_report.py logs\timeline_fe_smoke_vulkan.csv
```

Expected: a tabela tem `frontend`, `fe_begin`, `fe_ring`, `fe_device`, `fe_index`, `fe_streams`, `fe_end`, `fe_flush`, `game_guest` e `frontend_other`, com descartados ≤ 5%. Sanidade dos números: `frontend` ≤ `game`; `fe_flush` ≤ `fe_end`; `capture` ≤ `fe_end`. Se alguma regra falhar, **pare** e reporte com os valores (não faça commit de palpite).

- [ ] **Step 7: Commit**

```powershell
git add port/src/native_renderer/native_renderer.cpp
git commit -m "feat(native): time the front-end hooks and their parts on the game thread" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Medir as duas APIs, documentar e apontar o alvo

**Files:**
- Modify: `tools/bench/bench_api.ps1`, `tools/README.md`
- Modify: `docs/native-renderer-timeline.md`

**Interfaces:**
- Consumes: tudo acima. Produces: a decomposição medida do `game` e a recomendação do primeiro alvo da Fase 1.

- [ ] **Step 1: Opção `-TimelineDetail`**

Em `tools/bench/bench_api.ps1`, siga o padrão de `-Timeline`: acrescente `[switch]$TimelineDetail,` ao `param(...)`; salve o valor anterior de `$env:SR_FRAME_TIMELINE_DETAIL`; quando `$Timeline -and $TimelineDetail` defina `$env:SR_FRAME_TIMELINE_DETAIL = '1'` junto da definição de `SR_FRAME_TIMELINE`; e restaure o valor anterior **no início do `finally`**, ao lado das outras restaurações (as restaurações vêm antes do passo do relatório). Documente a opção na linha de uso e na descrição do cabeçalho do script (comentários em português, ASCII nos avisos). Em `tools/README.md` acrescente à linha do `bench_api.ps1` que `-Timeline -TimelineDetail` liga também os contadores finos do front-end. Verifique com a análise sintática do PowerShell (0 erros em `powershell -NoProfile` e `pwsh`).

- [ ] **Step 2: Medições**

Avise o usuário que são quatro execuções de bench (alguns minutos cada). Não rode builds nem outra carga durante o bench.

```powershell
$exe = "$PWD\port\out\build\win-amd64-release\superman_returns.exe"
foreach ($api in 'vulkan','d3d12') {
  powershell -NoProfile -File tools\bench\bench_api.ps1 -Api $api -Name "tl_fe1_$api" -Exe $exe -Timeline -TimelineDetail
  powershell -NoProfile -File tools\bench\bench_api.ps1 -Api $api -Name "tl_fe2_$api" -Exe $exe -Timeline -TimelineDetail
}
Get-Content logs\bench_results.csv | Select-String "tl_fe"
```

Se uma execução falhar por infraestrutura (janela não encontrada, "No guest FPS samples"), repita uma vez; se falhar de novo, pare e reporte.

- [ ] **Step 3: Conferir o custo dos contadores finos**

Compare o `game` médio das execuções com detalhe (`tl_fe1/2`) com o das execuções sem detalhe da Fase 0 (`tl_on1/2`, mesma API) e o FPS de `bench_results.csv`. Registre a diferença como custo do modo detalhado (esperado: sub-milissegundo por quadro; se passar de ~1,5 ms por quadro, registre como ressalva forte, porque os números finos passam a incluir a própria sonda).

- [ ] **Step 4: Atualizar `docs/native-renderer-timeline.md`**

Acrescente a seção "Decomposição do front-end da thread do jogo (Fase 0.5)" com:

1. Como ligar: `-Timeline -TimelineDetail` (variável `SR_FRAME_TIMELINE_DETAIL=1`), e o que cada estágio `fe_*` mede (a tabela deste plano).
2. As tabelas impressas pelo relatório de `tl_fe1_*` e `tl_fe2_*` das duas APIs (cole a saída real).
3. Uma tabela-resumo por API e execução: `game` | `frontend` | `game_guest` | `capture` | `fe_device` | `fe_streams` | `fe_end` | `fe_begin` | `fe_ring` | `fe_index` | `frontend_other` (ms médios), com a proporção de cada parte em `game`.
4. Ressalvas honestas: (a) `game_guest` é "game menos hooks": mistura a lógica do jogo, o driver XDK recompilado que monta o PM4 e as esperas do kernel (vblank etc.) — o detalhamento não as separa; (b) as partes aninham (`capture` ⊂ `fe_end`, `fe_flush` ⊂ `fe_end`) e não se somam; (c) o custo do modo detalhado medido no Step 3; (d) uma máquina, cena que varia.
5. **Conclusão**, citando os números: quanto do `game_other` é trabalho do renderer na thread do jogo e qual parte domina; qual deve ser o primeiro alvo da Fase 1 (por exemplo, se `fe_device` ou `fe_streams` dominam, a captura de estado por draw; se `game_guest` domina, o driver/jogo e o pipeline não ajudam); e o que os dados ainda não decidem. Siga o rigor do documento existente: nenhuma afirmação mais forte que a tabela.

Remova qualquer marcador de pendência antes de commitar: não pode sobrar texto `<...>`.

- [ ] **Step 5: Suítes e grafo**

```powershell
. .\tools\dev_env.ps1
cmake --build build/tests-native; ctest --test-dir build/tests-native --output-on-failure
python -m pytest tests/tools -q
graphify update .
```

Expected: tudo passando (nativo 2/2, pytest sem falhas).

- [ ] **Step 6: Commit**

```powershell
git add tools/bench/bench_api.ps1 tools/README.md docs/native-renderer-timeline.md
git commit -m "docs: measure what the Vulkan game thread spends its time on" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Self-review

**Cobertura do objetivo:** decompor `game_other` em trabalho do renderer (Tasks 1–3) e lógica/espera (derivado `game_guest`, Task 2), medir nas duas APIs (Task 4) e decidir o alvo (Task 4, Step 4).

**Placeholders:** nenhum passo de código tem trecho vago; os dados da Task 4 só existem após a medição e o passo manda substituí-los.

**Consistência:** nomes de estágio no C++ (`kFrontend`, `kFeBegin`, `kFeRing`, `kFeDevice`, `kFeIndex`, `kFeStreams`, `kFeEnd`, `kFeFlush`), no CSV (`frontend`, `fe_begin`, ... `fe_flush`) e no script (`FRONT_END_PARTS`, `"fe_flush"`, `"frontend"`) coincidem. Os campos de `CaptureTimings` (`fe_hook_ns`, `fe_begin_ns`, ...) são usados nos escopos da Task 3 e lidos no `OnSwap`.

**Riscos:**
- Os contadores finos perturbam a medição: ~10 relógios por draw (≈0,5–1 ms por quadro); medido no Task 4, Step 3.
- `FlushBatch` chamado do `OnSwap` depois do `capture_timings = {}` cai no quadro seguinte (um flush entre ~100 por quadro; desprezível, documentado).
- O `DrawIndexedVertices` tem `return` antecipado antes do bloco do índice; o RAII cobre.
- `fe_*` ⊂ `frontend` só vale para os hooks listados; trabalho do front-end fora deles (por exemplo, o `OnSwap`) fica de fora de `frontend`.
