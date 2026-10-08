# Fase 1.1a: medição fina dos maiores blocos do front-end — Plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Decompor `fe_streams` (~3,5 ms), o resto de `fe_end` fora da captura (~2,9 ms) e `fe_device` (~1,4 ms) do Vulkan em sub-partes, para escolher com dados as mudanças da dieta do front-end (Fase 1.1b).

**Architecture:** Mesma técnica da Fase 0.5: escopos `DetailScope` em nanossegundos nos `capture_timings` da thread do jogo, gravados no `OnSwap` quando `SR_FRAME_TIMELINE` e `SR_FRAME_TIMELINE_DETAIL=1` estão definidos. Quatro estágios novos, aninhados nos existentes: `fd_shaders` ⊂ `fe_device`, `fs_prep` e `fs_plan` ⊂ `fe_streams`, `fe_push` ⊂ `fe_end`.

**Tech Stack:** C++20, Python 3 + pytest, PowerShell.

## Global Constraints

- Desligado por padrão; sem as duas variáveis nada muda (nenhum relógio, lock ou efeito); nenhuma otimização nesta fase: só instrumento e diagnóstico.
- Contadores em nanossegundos; imagem bit a bit idêntica; `sr_renderer=native` nunca cai para Xenos.
- Política de texturas decidida em 2026-10-08: o padrão atual (monitor de escrita com backoff) vale no Vulkan; não planejar otimização de hash.
- Avise o progresso entre passos que levam minutos (build, bench).
- Commits só como parte desta execução; trailer exato `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`; nunca commitar `port/superman_returns_manifest.toml`, `logs/` nem `artifacts/`.

## Contexto medido

Médias das duas execuções Vulkan com detalhe (`logs/timeline_tl_fe{1,2}_vulkan.txt`, ms por quadro): `frontend` 17,05–18,16; `fe_streams` 3,45–3,68; `fe_end` 9,59–10,17 (dos quais `capture` 6,82–7,20); `fe_device` 1,37–1,48; `fe_index` 1,34–1,44; `fe_begin` 0,39–0,41; `fe_ring` 0,26–0,29. Já descartado por leitura de código: a alocação de vetor em `ReadVertexDeclaration` (já cacheada em `VertexDeclarationCache`) e o custo do `WorkCmd` em `fe_begin` (pequeno).

## Estágios novos no CSV

| Estágio | O que mede (nanossegundos somados por quadro) |
| --- | --- |
| `fd_shaders` | em `CaptureDevice`: `TryRegisterInlineShaders` + os dois `CaptureGuestShader` (três aquisições de `g_mutex`) |
| `fs_prep` | em `PlanStreams`: `DynamicVertexFetch` (duas consultas de shader com `g_mutex`) + `ReadVertexDeclaration` + a máscara de streams usados |
| `fs_plan` | em `PlanStreams`: o ramo lento de cada stream (`range->Resolve` + `PlanBuffer` + registro do cache); o ramo "limpo e em cache" fica fora |
| `fe_push` | em `EndCmd`: `batch_->cmds.push_back(std::move(cur_))` |

---

### Task 1: Estágios novos no cabeçalho e no relatório

**Files:**
- Modify: `port/src/graphics/frame_timeline.h`, `tests/native/test_frame_timeline.cpp`
- Modify: `tools/analysis/frame_timeline_report.py`, `tests/tools/test_frame_timeline_report.py`

**Interfaces:**
- Produces: `TimelineStage::{kFdShaders,kFsPrep,kFsPlan,kFePush}` com nomes de CSV `fd_shaders`, `fs_prep`, `fs_plan`, `fe_push`; o relatório imprime esses estágios (sem derivados novos) logo depois de `fe_flush`.

- [ ] **Step 1: Testes que falham**

Em `tests/native/test_frame_timeline.cpp` acrescente:

```cpp
SR_TEST(timeline_front_end_sub_stages_use_their_csv_names) {
  FrameTimeline timeline("unused.csv");
  timeline.RecordBusy(TimelineStage::kFdShaders, 4, 11);
  timeline.RecordBusy(TimelineStage::kFsPrep, 4, 22);
  timeline.RecordBusy(TimelineStage::kFsPlan, 4, 33);
  timeline.RecordBusy(TimelineStage::kFePush, 4, 44);
  const std::string rows = Rows(timeline, 4, 4);
  SR_CHECK(rows.find("4,fd_shaders,0,0,11,0\n") != std::string::npos);
  SR_CHECK(rows.find("4,fs_prep,0,0,22,0\n") != std::string::npos);
  SR_CHECK(rows.find("4,fs_plan,0,0,33,0\n") != std::string::npos);
  SR_CHECK(rows.find("4,fe_push,0,0,44,0\n") != std::string::npos);
}
```

Em `tests/tools/test_frame_timeline_report.py` acrescente:

```python
def test_front_end_sub_stages_are_listed_after_fe_flush(tmp_path):
    rows = []
    for frame in range(1, 4):
        rows += detail_rows(frame)
        rows += [(frame, "fd_shaders", 0, 0, 1 * MS, 0), (frame, "fs_prep", 0, 0, 2 * MS, 0),
                 (frame, "fs_plan", 0, 0, 1 * MS, 0), (frame, "fe_push", 0, 0, 1 * MS, 0)]
    path = tmp_path / "sub.csv"
    write_csv(path, rows)
    result = ftr.analyze(ftr.load(path))
    for name in ("fd_shaders", "fs_prep", "fs_plan", "fe_push"):
        assert result["stages"][name]["samples"] == 3
    assert result["stages"]["fs_prep"]["mean"] == pytest.approx(2.0)
    lines = [line.split()[0] for line in ftr.render(result).splitlines()[5:] if line.strip()]
    order = [name for name in ftr.STAGES if name in lines]
    assert lines == order
    assert lines.index("fd_shaders") > lines.index("fe_flush")
    # sub-stages nest inside fe_device / fe_streams / fe_end: they must not change the derivations
    assert result["stages"]["frontend_other"]["mean"] == pytest.approx(1.0)
```

- [ ] **Step 2: Rodar e ver falhar**

```powershell
. .\tools\dev_env.ps1
cmake --build build/tests-native
python -m pytest tests/tools/test_frame_timeline_report.py -q
```

Expected: FAIL de compilação (`kFdShaders`) e o teste Python falha (`KeyError: 'fd_shaders'`).

- [ ] **Step 3: Implementar**

Em `frame_timeline.h`, no enum acrescente depois de `kFeFlush,`: `kFdShaders, kFsPrep, kFsPlan, kFePush,` e em `TimelineStageName`:

```cpp
    case TimelineStage::kFdShaders: return "fd_shaders";
    case TimelineStage::kFsPrep: return "fs_prep";
    case TimelineStage::kFsPlan: return "fs_plan";
    case TimelineStage::kFePush: return "fe_push";
```

Em `frame_timeline_report.py`: acrescente a constante `FRONT_END_SUB = ("fd_shaders", "fs_prep", "fs_plan", "fe_push")` com o comentário `# nest inside fe_device / fe_streams / fe_end: listed, never summed`, coloque `*FRONT_END_SUB` na tupla `STAGES` logo depois de `"fe_flush"` e no laço de estágios brutos de `analyze` (depois de `"fe_flush"`).

- [ ] **Step 4: Rodar e ver passar**

```powershell
cmake --build build/tests-native
ctest --test-dir build/tests-native --output-on-failure
python -m pytest tests/tools -q
```

Expected: tudo passando (nativo 2/2, pytest sem falhas).

- [ ] **Step 5: Commit**

```powershell
git add port/src/graphics/frame_timeline.h tests/native/test_frame_timeline.cpp tools/analysis/frame_timeline_report.py tests/tools/test_frame_timeline_report.py
git commit -m "feat(native): add front-end sub-stages to the frame timeline and report" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Escopos finos no renderer

**Files:**
- Modify: `port/src/native_renderer/native_renderer.cpp`

**Interfaces:**
- Consumes: `DetailScope` (Fase 0.5), os novos `TimelineStage` (Task 1).
- Produces: linhas `fd_shaders`, `fs_prep`, `fs_plan`, `fe_push` no CSV com `SR_FRAME_TIMELINE` + `SR_FRAME_TIMELINE_DETAIL=1`.

- [ ] **Step 1: Campos**

Em `CaptureTimings` acrescente uma linha de campos: `uint64_t fd_shaders_ns=0,fs_prep_ns=0,fs_plan_ns=0,fe_push_ns=0;`

- [ ] **Step 2: `CaptureDevice` → `fd_shaders`**

No bloco `if (cur_.packet_check) {` de `Renderer::CaptureDevice`, troque

```cpp
    TryRegisterInlineShaders(base, vs, ps);
    cur_.vertex_shader = CaptureGuestShader(vs);
    cur_.pixel_shader = CaptureGuestShader(ps);
```

por

```cpp
    {
      DetailScope shader_scope(capture_timings.fd_shaders_ns);
      TryRegisterInlineShaders(base, vs, ps);
      cur_.vertex_shader = CaptureGuestShader(vs);
      cur_.pixel_shader = CaptureGuestShader(ps);
    }
```

(O trecho `cur_.vertex_shader = CaptureGuestShader(vs);` aparece uma vez em `CaptureDevice`; confira que a edição não atinge outra função.)

- [ ] **Step 3: `PlanStreams` → `fs_prep` e `fs_plan`**

Em `Renderer::PlanStreams`, depois de `if (!decl) return false;` acrescente `DetailScope prep_scope(capture_timings.fs_prep_ns);` e, imediatamente depois do `for (const auto& attribute : ReadVertexDeclaration(base,decl)) if(attribute.stream<16) streams_used|=1u<<attribute.stream;` (a linha seguinte é `for (uint32_t s = 0; s < 16; ++s) {`), acrescente `prep_scope.Stop();`.

No ramo lento do cache de streams (o `} else {` que contém `uint32_t need_begin = 0, need_end = ~0u;`), acrescente como **primeira linha dentro do `else`**: `DetailScope plan_scope(capture_timings.fs_plan_ns);`.

- [ ] **Step 4: `EndCmd` → `fe_push`**

Em `Renderer::EndCmd`, troque

```cpp
  batch_->cmds.push_back(std::move(cur_));
```

por

```cpp
  {
    DetailScope push_scope(capture_timings.fe_push_ns);
    batch_->cmds.push_back(std::move(cur_));
  }
```

(Há uma única linha `batch_->cmds.push_back(std::move(cur_));` em `EndCmd`; se o Edit apontar mais de uma ocorrência, inclua o comentário anterior "The completed command belongs to the batch." para desambiguar.)

- [ ] **Step 5: Gravação no `OnSwap`**

No bloco `if (graphics::FrameTimeline::Detail()) {` do `OnSwap`, depois do `RecordBusy(... kFeFlush ...)`, acrescente:

```cpp
      timeline.RecordBusy(graphics::TimelineStage::kFdShaders, swap_number, t.fd_shaders_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFsPrep, swap_number, t.fs_prep_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFsPlan, swap_number, t.fs_plan_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFePush, swap_number, t.fe_push_ns);
```

- [ ] **Step 6: Compilar e verificar (Vulkan, um bench)**

Rode o build em background (minutos), depois um bench; avise o usuário.

```powershell
$env:SR_BUILD_LAUNCHER = 'OFF'
cmd /c ".\build.cmd" 2>&1 | Select-Object -Last 15
Remove-Item env:SR_BUILD_LAUNCHER
powershell -NoProfile -File tools\bench\bench_api.ps1 -Api vulkan -Name tl_fd_smoke_vulkan -Exe "$PWD\port\out\build\win-amd64-release\superman_returns.exe" -Timeline -TimelineDetail
```

Expected: sem `error:`; a tabela impressa tem `fd_shaders`, `fs_prep`, `fs_plan`, `fe_push` com ≤ 5% descartados. Regras de sanidade a conferir (médias): `fd_shaders` ≤ `fe_device`; `fs_prep + fs_plan` ≤ `fe_streams`; `fe_push` ≤ `fe_end − capture`. Se alguma falhar, **pare** e reporte com os valores.

- [ ] **Step 7: Commit**

```powershell
git add port/src/native_renderer/native_renderer.cpp
git commit -m "feat(native): time the shader lookups, stream planning and command push in the front end" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Medir, documentar e escolher as mudanças da Fase 1.1b

**Files:**
- Modify: `docs/native-renderer-timeline.md`

- [ ] **Step 1: Medições**

Quatro benches Vulkan com detalhe (avise o usuário; um por vez, sem builds nem carga em paralelo): duas com `-Timeline -TimelineDetail` e duas com `-Timeline -TimelineDetail -Profile` (o perfil acrescenta a linha `Vulkan profile` com `draws=` por quadro, necessário para µs por draw).

```powershell
$exe = "$PWD\port\out\build\win-amd64-release\superman_returns.exe"
foreach ($n in 1,2) { powershell -NoProfile -File tools\bench\bench_api.ps1 -Api vulkan -Name "tl_fd$n`_vulkan" -Exe $exe -Timeline -TimelineDetail }
foreach ($n in 3,4) { powershell -NoProfile -File tools\bench\bench_api.ps1 -Api vulkan -Name "tl_fd$n`_vulkan" -Exe $exe -Timeline -TimelineDetail -Profile }
```

Repita uma vez em caso de falha de infraestrutura; se falhar de novo, pare e reporte.

- [ ] **Step 2: Seção no documento**

Acrescente "Decomposição fina do front-end (Fase 1.1a)" a `docs/native-renderer-timeline.md` com: as tabelas reais dos relatórios; os draws por quadro das execuções com `-Profile` (campo `draws=` da linha `Vulkan profile` em `logs/bench_tl_fd3_vulkan.log` e `..._fd4_...`); uma tabela de µs por draw (ms do estágio × 1000 ÷ draws) para `fe_device`, `fd_shaders`, `fe_streams`, `fs_prep`, `fs_plan`, `fe_index`, `fe_push` e `fe_end − capture`; a ressalva de que `fe_streams − fs_prep − fs_plan` é o ramo "limpo e em cache" mais as leituras de registradores por stream; a ressalva do custo do modo detalhado (compare o `game` médio com `tl_fe1/2`); e uma **lista ordenada de candidatos da Fase 1.1b**, cada um com o estágio que ele ataca, o teto de ganho (o próprio estágio) e o risco. Nenhuma afirmação mais forte que os dados; sem marcadores `<...>`; releia a seção contra o texto da Fase 0.5.

- [ ] **Step 3: Suítes e commit**

```powershell
python -m pytest tests/tools -q
git add docs/native-renderer-timeline.md
git commit -m "docs: break down the biggest Vulkan front-end blocks per draw" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Self-review

**Cobertura:** decompõe os três maiores blocos sem decomposição (`fe_streams`, `fe_end` fora da captura, `fe_device`) e produz a lista de candidatos para o plano 1.1b. `fe_index` não ganha sub-estágio (1,3–1,4 ms; contém `PlanBuffer`, que `fs_plan` cobre no outro lado).

**Consistência:** `fd_shaders_ns`, `fs_prep_ns`, `fs_plan_ns`, `fe_push_ns` (campos), `kFdShaders`, `kFsPrep`, `kFsPlan`, `kFePush` (enum) e `fd_shaders`, `fs_prep`, `fs_plan`, `fe_push` (CSV e script) coincidem.

**Riscos:** os `DetailScope` aninhados somam alguns relógios por draw (o custo do modo detalhado é remedido no Task 3); `fs_plan` só cobre o ramo lento; `fe_push` mede só o `push_back` (a movimentação do `WorkCmd`), não o resto do `EndCmd`, que fica como resíduo explícito.
