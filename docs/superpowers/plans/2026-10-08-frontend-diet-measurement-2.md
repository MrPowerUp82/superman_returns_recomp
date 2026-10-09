# Fase 1.1b: motivos do ramo lento e resíduo do EndCmd — Plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Responder as duas perguntas que a Fase 1.1a deixou abertas, antes de qualquer otimização: (1) por que e com que frequência o `PlanStreams` toma o ramo lento (`fs_plan`, ~3,1 ms), e quanto dele é `Resolve` e quanto é `PlanBuffer`; (2) o que são os ~2,8 ms de `EndCmd` fora da captura, testando a hipótese de que são truncamento dos cronômetros de PM4 e texturas, que somam microssegundos inteiros por chamada.

**Architecture:** Mesma técnica das fases anteriores: escopos `DetailScope` em nanossegundos e estágios novos no CSV, mais contadores de motivos do ramo lento registrados em uma linha de log a cada 120 quadros (contagens não cabem no formato de tempo do CSV). Tudo atrás de `SR_FRAME_TIMELINE` + `SR_FRAME_TIMELINE_DETAIL=1`.

**Tech Stack:** C++20, Python 3 + pytest, PowerShell.

## Global Constraints

- Desligado por padrão; sem as duas variáveis nada muda (nenhum relógio, contador ou log novo); só instrumento e diagnóstico, nenhuma otimização.
- Nanossegundos nos contadores novos; imagem bit a bit idêntica; `sr_renderer=native` nunca cai para Xenos.
- Política de texturas decidida: watch + backoff continua; nada sobre hash.
- Avise o progresso entre passos que levam minutos (build, bench).
- Commits só como parte desta execução; trailer exato `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`; nunca commitar `port/superman_returns_manifest.toml`, `logs/` nem `artifacts/`.
- Ao editar docs com caminhos do Windows, não use sequências de escape em heredoc de shell (já corrompeu `\b` em `\x08`); use a ferramenta Edit e confira com `grep -cP '\x08'`.

## Contexto medido

Fase 1.1a (Vulkan, ~2630 draws/quadro): `fe_streams` 3,79 ms = `fs_plan` 3,08 + `fs_prep` 0,38 + resto 0,34; `fe_end − capture` = 3,07 ms, dos quais `fe_push` 0,28 e ~2,79 sem explicação. Hipótese a testar: os cronômetros `pm4_us` e `textures_us` do `EndCmd` somam `duration_cast<microseconds>` por chamada (perda média ~0,5 µs); com ≈5260 intervalos cronometrados por quadro isso dá ≈2,6 ms, perto do resíduo. Nos logs de captura (74 amostras) a captura faz em média 488 leituras `ReadProcessMemory` e copia 5,6 MB por quadro (máx. 42 MB), o que também pode pesar dentro de `fe_pm4`/`fe_textures`.

## Estágios novos no CSV

| Estágio | O que mede (ns somados por quadro, thread do jogo) |
| --- | --- |
| `fe_pm4` | em `EndCmd`: a chamada `CapturePm4Dependencies` (mesmo intervalo do `pm4_us`, mas em ns) |
| `fe_textures` | em `EndCmd`: a chamada `CaptureTextures` (mesmo intervalo do `textures_us`, em ns) |
| `fs_resolve` | em `PlanStreams`, ramo lento: `range->Resolve()` |
| `fs_buffer` | em `PlanStreams`, ramo lento: a chamada `PlanBuffer` |

Aninhamento: `fe_pm4` e `fe_textures` ⊂ `fe_end`; `fs_resolve` e `fs_buffer` ⊂ `fs_plan`. O relatório deriva `fe_end_rest = fe_end − fe_pm4 − fe_textures − fe_push` (≥ 0).

---

### Task 1: Estágios novos no cabeçalho e no relatório

**Files:**
- Modify: `port/src/graphics/frame_timeline.h`, `tests/native/test_frame_timeline.cpp`
- Modify: `tools/analysis/frame_timeline_report.py`, `tests/tools/test_frame_timeline_report.py`

**Interfaces:**
- Produces: `TimelineStage::{kFePm4,kFeTextures,kFsResolve,kFsBuffer}` (CSV: `fe_pm4`, `fe_textures`, `fs_resolve`, `fs_buffer`); no relatório, a lista `FRONT_END_SUB` ganha os quatro nomes e o derivado `fe_end_rest`.

- [ ] **Step 1: Testes que falham**

Em `tests/native/test_frame_timeline.cpp`:

```cpp
SR_TEST(timeline_end_cmd_and_stream_plan_stages_use_their_csv_names) {
  FrameTimeline timeline("unused.csv");
  timeline.RecordBusy(TimelineStage::kFePm4, 6, 11);
  timeline.RecordBusy(TimelineStage::kFeTextures, 6, 22);
  timeline.RecordBusy(TimelineStage::kFsResolve, 6, 33);
  timeline.RecordBusy(TimelineStage::kFsBuffer, 6, 44);
  const std::string rows = Rows(timeline, 6, 6);
  SR_CHECK(rows.find("6,fe_pm4,0,0,11,0\n") != std::string::npos);
  SR_CHECK(rows.find("6,fe_textures,0,0,22,0\n") != std::string::npos);
  SR_CHECK(rows.find("6,fs_resolve,0,0,33,0\n") != std::string::npos);
  SR_CHECK(rows.find("6,fs_buffer,0,0,44,0\n") != std::string::npos);
}
```

Em `tests/tools/test_frame_timeline_report.py` (reutilize `detail_rows`, `write_csv`, `MS`, `ftr`):

```python
def test_end_cmd_rest_is_derived_from_the_nanosecond_parts(tmp_path):
    rows = []
    for frame in range(1, 4):
        rows += detail_rows(frame)   # fe_end = 3 ms
        rows += [(frame, "fe_pm4", 0, 0, 1 * MS, 0), (frame, "fe_textures", 0, 0, 1 * MS, 0),
                 (frame, "fe_push", 0, 0, 0, 0), (frame, "fs_resolve", 0, 0, 1 * MS, 0),
                 (frame, "fs_buffer", 0, 0, 2 * MS, 0)]
    path = tmp_path / "rest.csv"
    write_csv(path, rows)
    stages = ftr.analyze(ftr.load(path))["stages"]
    assert stages["fe_pm4"]["mean"] == pytest.approx(1.0)
    assert stages["fs_buffer"]["mean"] == pytest.approx(2.0)
    assert stages["fe_end_rest"]["mean"] == pytest.approx(1.0)   # 3 - 1 - 1 - 0


def test_end_cmd_rest_is_clamped_and_absent_without_the_parts(tmp_path):
    rows = []
    for frame in range(1, 4):
        rows += detail_rows(frame)
        rows += [(frame, "fe_pm4", 0, 0, 4 * MS, 0)]   # more than fe_end (3 ms)
    path = tmp_path / "clamp2.csv"
    write_csv(path, rows)
    assert ftr.analyze(ftr.load(path))["stages"]["fe_end_rest"]["mean"] == 0.0
    plain = tmp_path / "plain.csv"
    write_csv(plain, [r for f in range(1, 4) for r in detail_rows(f)])
    assert "fe_end_rest" not in ftr.analyze(ftr.load(plain))["stages"]
```

- [ ] **Step 2: Rodar e ver falhar**

```powershell
. .\tools\dev_env.ps1
cmake --build build/tests-native
python -m pytest tests/tools/test_frame_timeline_report.py -q
```

Expected: FAIL (`kFePm4` não existe; `KeyError: 'fe_pm4'`).

- [ ] **Step 3: Implementar**

Em `frame_timeline.h`: no enum, depois de `kFePush,` acrescente `kFePm4, kFeTextures, kFsResolve, kFsBuffer,` (com o comentário `// nest inside fe_end / fs_plan; listed, never summed`) e, em `TimelineStageName`:

```cpp
    case TimelineStage::kFePm4: return "fe_pm4";
    case TimelineStage::kFeTextures: return "fe_textures";
    case TimelineStage::kFsResolve: return "fs_resolve";
    case TimelineStage::kFsBuffer: return "fs_buffer";
```

Em `frame_timeline_report.py`: acrescente os quatro nomes ao final de `FRONT_END_SUB`, acrescente `"fe_end_rest"` à tupla `STAGES` logo depois de `"frontend_other"`, e, logo depois do bloco que cria `frontend_other`, acrescente (dentro do mesmo `if with_frontend:` ou em um bloco próprio com a mesma lista de quadros):

```python
        has_parts = [i for i in with_frontend if "fe_end" in frames[i] and ("fe_pm4" in frames[i] or "fe_textures" in frames[i])]
        if has_parts:  # EndCmd minus its nanosecond-timed parts
            rest = [max(0, frames[i]["fe_end"]["busy_ns"]
                        - sum(frames[i][p]["busy_ns"] for p in ("fe_pm4", "fe_textures", "fe_push") if p in frames[i])) / 1e6
                    for i in has_parts]
            stages["fe_end_rest"] = _summary(rest, [0.0] * len(rest), mean_interval, budget_ms)
```

- [ ] **Step 4: Rodar e ver passar**

```powershell
cmake --build build/tests-native
ctest --test-dir build/tests-native --output-on-failure
python -m pytest tests/tools -q
```

Expected: tudo passando.

- [ ] **Step 5: Commit**

```powershell
git add port/src/graphics/frame_timeline.h tests/native/test_frame_timeline.cpp tools/analysis/frame_timeline_report.py tests/tools/test_frame_timeline_report.py
git commit -m "feat(native): add EndCmd and stream-plan sub-stages to the timeline and report" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Escopos em ns, contadores de motivos e linha de log

**Files:**
- Modify: `port/src/native_renderer/native_renderer.cpp`

**Interfaces:**
- Consumes: `DetailScope`, os estágios da Task 1.
- Produces: linhas `fe_pm4`, `fe_textures`, `fs_resolve`, `fs_buffer` no CSV e uma linha de log `native front-end stream plan (per frame over 120): ...` a cada 120 quadros, ambas só com `SR_FRAME_TIMELINE` + `SR_FRAME_TIMELINE_DETAIL=1`.

- [ ] **Step 1: Campos e contadores**

Em `CaptureTimings` acrescente: `uint64_t fe_pm4_ns=0,fe_textures_ns=0,fs_resolve_ns=0,fs_buffer_ns=0;`

Logo depois da definição de `DetailScope` (anonymous namespace) acrescente:

```cpp
// SR_FRAME_TIMELINE_DETAIL=1: why PlanStreams takes its slow path (first failing reason per stream),
// accumulated on the guest thread and logged every 120 frames. Counts only; no timing.
struct StreamPlanCounters {
  uint64_t evaluated=0,fast=0,slow=0;
  uint64_t untracked=0,address=0,size=0,decl=0,stride=0,phase=0,dirty=0;
};
thread_local StreamPlanCounters stream_plan_counters;
```

- [ ] **Step 2: `EndCmd` → `fe_pm4` e `fe_textures`**

No bloco PM4 de `Renderer::EndCmd`, imediatamente antes da linha `cur_.pm4_capture_ok = graphics::guest::CapturePm4Dependencies(*batch_, cur_,`, acrescente `DetailScope pm4_scope(capture_timings.fe_pm4_ns);` e imediatamente depois da linha `capture_timings.pm4_us+=...;` acrescente `pm4_scope.Stop();`. (Os cronômetros antigos em µs ficam como estão.)

No bloco de texturas, imediatamente antes de `CaptureTextures(base);` acrescente `DetailScope textures_scope(capture_timings.fe_textures_ns);` e imediatamente depois da linha `capture_timings.textures_us+=...;` acrescente `textures_scope.Stop();`.

- [ ] **Step 3: `PlanStreams` → `fs_resolve`, `fs_buffer`, contadores**

Em `Renderer::PlanStreams`, no laço por stream:

(a) logo depois da linha `FrontStreamCache& sc = front_stream_cache_[s];` acrescente `if (graphics::FrameTimeline::Detail()) ++stream_plan_counters.evaluated;`

(b) no ramo rápido (o `if (!REXCVAR_GET(sr_native_debug_buffers_always_dirty) && sc.tracked && ...) {`), como primeira linha dentro do bloco: `if (graphics::FrameTimeline::Detail()) ++stream_plan_counters.fast;`

(c) no ramo lento, logo depois de `DetailScope plan_scope(capture_timings.fs_plan_ns);`, acrescente a classificação (antes de qualquer campo de `sc` ser sobrescrito):

```cpp
      if (graphics::FrameTimeline::Detail()) {
        auto& c = stream_plan_counters;
        ++c.slow;
        if (!sc.tracked) ++c.untracked;
        else if (sc.address != buffer_base) ++c.address;
        else if (sc.size != buffer_size) ++c.size;
        else if (sc.decl != decl) ++c.decl;
        else if (sc.stride != stride) ++c.stride;
        else if (sc.phase != phase) ++c.phase;
        else ++c.dirty;  // every cached field matched: RefreshTrackedBuffer reported a dirty buffer
      }
```

(d) em volta de `range->Resolve();` (dentro de `if (range) { ... }`): troque `range->Resolve();` por `{DetailScope resolve_scope(capture_timings.fs_resolve_ns);range->Resolve();}`. Se a chamada aparecer mais de uma vez na função, edite só a do ramo lento (a que está logo antes do cálculo de `need_begin`).

(e) em volta de `PlanBuffer`: troque

```cpp
      sp.buffer = PlanBuffer(base, buffer_base, buffer_size, decl, stride, s << 8, phase,
                             need_begin, need_end, ok);
```

por

```cpp
      {
        DetailScope buffer_scope(capture_timings.fs_buffer_ns);
        sp.buffer = PlanBuffer(base, buffer_base, buffer_size, decl, stride, s << 8, phase,
                               need_begin, need_end, ok);
      }
```

- [ ] **Step 4: Gravação e log no `OnSwap`**

No bloco `if (graphics::FrameTimeline::Detail()) {` do `OnSwap`, depois dos `RecordBusy` existentes, acrescente:

```cpp
      timeline.RecordBusy(graphics::TimelineStage::kFePm4, swap_number, t.fe_pm4_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFeTextures, swap_number, t.fe_textures_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFsResolve, swap_number, t.fs_resolve_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFsBuffer, swap_number, t.fs_buffer_ns);
      if (swap_number % 120 == 0) {
        auto& c = stream_plan_counters;
        REXLOG_INFO("native front-end stream plan (per frame over 120): evaluated={} fast={} slow={} | slow reasons: "
                    "untracked={} address={} size={} decl={} stride={} phase={} dirty={}",
                    c.evaluated/120,c.fast/120,c.slow/120,c.untracked/120,c.address/120,c.size/120,c.decl/120,
                    c.stride/120,c.phase/120,c.dirty/120);
        c={};
      }
```

(Este bloco está dentro do `if (timeline.enabled())` e antes de `capture_timings={};`; `t` é `capture_timings`.)

- [ ] **Step 5: Compilar e verificar (um bench Vulkan, com `-Profile` para copiar o log)**

Rode o build em background e depois o bench; avise o usuário.

```powershell
$env:SR_BUILD_LAUNCHER = 'OFF'
cmd /c ".\build.cmd" 2>&1 | Select-Object -Last 15
Remove-Item env:SR_BUILD_LAUNCHER
powershell -NoProfile -File tools\bench\bench_api.ps1 -Api vulkan -Name tl_fp_smoke_vulkan -Exe "$PWD\port\out\build\win-amd64-release\superman_returns.exe" -Timeline -TimelineDetail -Profile
Select-String -Path logs\bench_tl_fp_smoke_vulkan.log -Pattern "stream plan" | Select-Object -Last 3
```

Expected: sem `error:`; a tabela tem `fe_pm4`, `fe_textures`, `fs_resolve`, `fs_buffer`, `fe_end_rest` com ≤ 5% descartados; a linha de log aparece (uma por 120 quadros). Sanidade: `fe_pm4 + fe_textures + fe_push ≤ fe_end`; `fs_resolve + fs_buffer ≤ fs_plan`; `fast + slow = evaluated`; a soma dos motivos = `slow`. Se alguma falhar, **pare** e reporte.

- [ ] **Step 6: Commit**

```powershell
git add port/src/native_renderer/native_renderer.cpp
git commit -m "feat(native): time PM4/texture capture in ns and count why the stream plan takes its slow path" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Medir e documentar

**Files:**
- Modify: `docs/native-renderer-timeline.md`

- [ ] **Step 1: Medições**

Quatro benches Vulkan com `-Timeline -TimelineDetail -Profile` (um por vez, sem builds nem carga em paralelo; avise o usuário que levam minutos; repita uma vez em falha de infraestrutura):

```powershell
$exe = "$PWD\port\out\build\win-amd64-release\superman_returns.exe"
foreach ($n in 1..4) { powershell -NoProfile -File tools\bench\bench_api.ps1 -Api vulkan -Name "tl_fp$n`_vulkan" -Exe $exe -Timeline -TimelineDetail -Profile }
```

- [ ] **Step 2: Seção no documento**

Acrescente "Motivos do ramo lento e resíduo do EndCmd (Fase 1.1b)" a `docs/native-renderer-timeline.md` com:

1. As quatro tabelas reais do relatório e a tabela de FPS.
2. **Pergunta 1:** divisão de `fs_plan` em `fs_resolve`, `fs_buffer` e o resto (cadastro do cache, `tracked_.find`); e a tabela de motivos da linha `native front-end stream plan` de cada execução (últimas janelas): `evaluated`, `fast`, `slow` por quadro, a fração `slow/evaluated` e a fração de cada motivo entre os lentos. Diga qual motivo domina, com os números, e o que isso sugere (por exemplo, `dirty` dominando significa que buffers que ficaram sujos permanecem sujos: ligar ao fato, já descrito, de `dirty` nunca voltar a falso), sem ir além do que a tabela mostra.
3. **Pergunta 2:** compare `capture` (em µs truncados) com `fe_pm4 + fe_textures` (em ns) por execução e mostre o que sobra em `fe_end_rest`; conclua se o truncamento explica o resíduo de ~2,8 ms (reporte o resultado qualquer que seja, inclusive "não explica"). Se `capture` real for maior, corrija os números derivados que dependem dele (quanto do `game` é captura) e diga que as tabelas anteriores usavam o valor truncado, sem reescrever o histórico.
4. A lista de candidatos da próxima fase (Fase 1.1c) reordenada com os novos dados: cada item com o estágio atacado, o teto (a média do próprio estágio), risco e verificação; itens "medir mais" onde os dados não decidem. Nada sobre hash/texturas além do que a política já decidiu.
5. Ressalvas: o custo do modo detalhado (compare `game` com `tl_fd1/2`), a variação de cena e os draws por quadro (use `draws=` das linhas `Vulkan profile`).

Sem marcadores `<...>`; afirmações não mais fortes que os dados; releia contra as seções anteriores. Confira `grep -cP '\x08'` = 0.

- [ ] **Step 3: Suítes e commit**

```powershell
python -m pytest tests/tools -q
git add docs/native-renderer-timeline.md
git commit -m "docs: record why the stream plan goes slow and what is left in EndCmd" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Self-review

**Cobertura:** a pergunta 1 (motivo e frequência do ramo lento, `Resolve` versus `PlanBuffer`) é coberta pelos contadores e por `fs_resolve`/`fs_buffer`; a pergunta 2 (resíduo do `EndCmd`) pelo `fe_pm4`/`fe_textures` em ns e pelo derivado `fe_end_rest`.

**Consistência:** nomes no C++ (`kFePm4`, `kFeTextures`, `kFsResolve`, `kFsBuffer`; campos `fe_pm4_ns`, `fe_textures_ns`, `fs_resolve_ns`, `fs_buffer_ns`), no CSV e no script coincidem; `StreamPlanCounters` é usada nos passos 3 e 4.

**Riscos:** a classificação do motivo toma o primeiro campo diferente na ordem testada, então não é mutuamente exclusiva no sentido de "causa única", e o motivo `dirty` inclui o desvio de depuração `sr_native_debug_buffers_always_dirty` (desligado por padrão); os escopos em ns do PM4 e das texturas somam dois relógios por intervalo, o que aumenta o custo do modo detalhado (remedido no Task 3); `range->Resolve()` pode ser chamado de mais de um lugar, e só a do ramo lento entra em `fs_resolve`.
