# Fase 1.1c: `Resolve` preguiçoso no planejamento de streams — Plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Primeira otimização real da dieta do front-end do Vulkan: parar de calcular o intervalo de vértices de um draw indexado (`VertexRange::Resolve()`, uma varredura dos índices) quando o buffer planejado está limpo ou ainda não existe, que é quando o resultado nunca é lido. Teto de ganho medido: `fs_resolve` ≈ 0,95 ms por quadro (≈ 0,35 µs por draw). Em paralelo, medir quanto de `fs_buffer` (≈ 2,3 ms) é `RefreshTrackedBuffer` e quanto disso é hash de conteúdo, para escolher a próxima otimização.

**Architecture:** `PlanBuffer` ganha dois parâmetros opcionais (`lazy_range`, `lazy_offset`) e resolve o intervalo só no ramo "buffer sujo". `PlanStreams` deixa de resolver no ramo lento e passa o intervalo adiante. Um desligador por ambiente (`SR_NATIVE_LAZY_RESOLVE=0`) restaura o comportamento antigo no mesmo binário, para A/B. O resultado de `Resolve` só alimenta `need_begin/need_end`, que `PlanBuffer` lê apenas depois de `RefreshTrackedBuffer` e de `if (!t.dirty) return plan;`; o ramo "buffer novo" captura o buffer inteiro e ignora o intervalo (verificado no código na Fase 1.1b).

**Tech Stack:** C++20 (renderer nativo), Python 3 + pytest, PowerShell.

## Global Constraints

- Imagem bit a bit idêntica: o plano de cada stream (ação, intervalo, bytes capturados) deve ser o mesmo de antes; a otimização só adia um cálculo cujo resultado era descartado.
- A mudança é aceita só se o custo do **seu estágio** cair por draw (`fs_resolve` e `fs_plan`); caso contrário é revertida.
- Desligador por ambiente `SR_NATIVE_LAZY_RESOLVE=0`; padrão ligado. Lido uma vez.
- Instrumentação continua opt-in (`SR_FRAME_TIMELINE` + `SR_FRAME_TIMELINE_DETAIL=1`); nada novo roda sem elas.
- Política de texturas decidida: watch + backoff continua; nada sobre hash de **texturas**. O hash de **buffers de vértice** (`RefreshTrackedBuffer`) só é **medido** nesta fase, não alterado.
- `sr_renderer=native` nunca cai para Xenos. Avise o progresso entre passos longos (build, bench).
- Commits só como parte desta execução; trailer exato `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`; nunca commitar `port/superman_returns_manifest.toml`, `logs/` nem `artifacts/`.
- Ao editar docs com caminhos do Windows use a ferramenta Edit (não heredoc de shell) e confira `grep -cP '\x08'` = 0.

---

### Task 1: `Resolve` preguiçoso com desligador por ambiente

**Files:**
- Modify: `port/src/native_renderer/native_renderer.h` (declaração de `PlanBuffer`)
- Modify: `port/src/native_renderer/native_renderer.cpp` (`PlanBuffer`, `PlanStreams`)

**Interfaces:**
- Produces: `BufferPlan PlanBuffer(uint8_t* base, uint32_t address, uint32_t size, uint32_t decl, uint32_t stride, uint32_t format, uint32_t phase, uint32_t need_begin, uint32_t need_end, bool& ok, uint32_t reset_index = UINT32_MAX, VertexRange* lazy_range = nullptr, uint32_t lazy_offset = 0)`. Quando `lazy_range != nullptr`, `need_begin/need_end` são ignorados e calculados sob demanda no ramo sujo, com a mesma fórmula que `PlanStreams` usava.

- [ ] **Step 1: Declaração**

Em `native_renderer.h`, mova a linha `struct VertexRange;` (hoje logo depois da declaração de `PlanBuffer`) para **antes** da declaração de `PlanBuffer` e troque a declaração por:

```cpp
  struct VertexRange;
  BufferPlan PlanBuffer(uint8_t* base, uint32_t address, uint32_t size, uint32_t decl,
                        uint32_t stride, uint32_t format, uint32_t phase, uint32_t need_begin,
                        uint32_t need_end, bool& ok, uint32_t reset_index = UINT32_MAX,
                        VertexRange* lazy_range = nullptr, uint32_t lazy_offset = 0);
```

Atualize também o comentário de `VertexRange` (≈ linha 408) para: "Vertex range of a draw, computed only when a dirty buffer needs it (PlanBuffer resolves it lazily; indexed draws scan their indices)."

- [ ] **Step 2: `PlanBuffer`**

Na definição em `native_renderer.cpp`, acrescente os dois parâmetros no fim da lista (`uint32_t reset_index, VertexRange* lazy_range, uint32_t lazy_offset`). No ramo "encontrado", troque

```cpp
    RefreshTrackedBuffer(t);
    if (!t.dirty) return plan;
    uint32_t b = need_begin, e = need_end;
    align_range(b, e);
```

por

```cpp
    RefreshTrackedBuffer(t);
    if (!t.dirty) return plan;
    uint32_t b = need_begin, e = need_end;
    if (lazy_range) {
      // The dirty branch is the only reader of the needed range (the clean and new-buffer
      // branches ignore it), so the index scan is paid only here.
      {
        DetailScope resolve_scope(capture_timings.fs_resolve_ns);
        lazy_range->Resolve();
      }
      if (lazy_range->end != ~0u) {
        const uint64_t lo = uint64_t(lazy_offset) + uint64_t(lazy_range->first) * stride;
        const uint64_t hi = uint64_t(lazy_offset) + uint64_t(lazy_range->end) * stride;
        b = uint32_t(std::min<uint64_t>(lo, size));
        e = uint32_t(std::min<uint64_t>(hi, size));
      }
    }
    align_range(b, e);
```

(`need_begin`/`need_end` chegam como `0` e `~0u` quando `lazy_range` é usado, que é o valor que `PlanStreams` já usa quando não há intervalo.)

- [ ] **Step 3: `PlanStreams`**

No ramo lento de `Renderer::PlanStreams`, o bloco atual é (confira o texto exato antes de editar):

```cpp
      uint32_t need_begin = 0, need_end = ~0u;
      if (range) {
        {DetailScope resolve_scope(capture_timings.fs_resolve_ns);range->Resolve();}
        if (range->end != ~0u) {
          uint64_t b = uint64_t(offset) + uint64_t(range->first) * stride;
          uint64_t e = uint64_t(offset) + uint64_t(range->end) * stride;
          need_begin = uint32_t(std::min<uint64_t>(b, buffer_size));
          need_end = uint32_t(std::min<uint64_t>(e, buffer_size));
        }
      }
      bool ok = false;
      {
        DetailScope buffer_scope(capture_timings.fs_buffer_ns);
        sp.buffer = PlanBuffer(base, buffer_base, buffer_size, decl, stride, s << 8, phase,
                               need_begin, need_end, ok);
      }
```

Troque por:

```cpp
      uint32_t need_begin = 0, need_end = ~0u;
      // SR_NATIVE_LAZY_RESOLVE=0 restores the eager range scan (same-binary A/B).
      static const bool lazy_resolve = [] {
        const char* value = std::getenv("SR_NATIVE_LAZY_RESOLVE");
        return !(value && *value == '0');
      }();
      if (range && !lazy_resolve) {
        {DetailScope resolve_scope(capture_timings.fs_resolve_ns);range->Resolve();}
        if (range->end != ~0u) {
          uint64_t b = uint64_t(offset) + uint64_t(range->first) * stride;
          uint64_t e = uint64_t(offset) + uint64_t(range->end) * stride;
          need_begin = uint32_t(std::min<uint64_t>(b, buffer_size));
          need_end = uint32_t(std::min<uint64_t>(e, buffer_size));
        }
      }
      bool ok = false;
      {
        DetailScope buffer_scope(capture_timings.fs_buffer_ns);
        sp.buffer = PlanBuffer(base, buffer_base, buffer_size, decl, stride, s << 8, phase,
                               need_begin, need_end, ok, UINT32_MAX,
                               lazy_resolve ? range : nullptr, offset);
      }
```

Nota de contabilidade: com o modo preguiçoso, o `Resolve` passa a rodar dentro de `PlanBuffer`, portanto `fs_resolve` fica **aninhado em `fs_buffer`** (e não mais ao lado dele). Atualize o comentário da tabela de estágios do plano/doc na Task 3.

- [ ] **Step 4: Compilar**

```powershell
$env:SR_BUILD_LAUNCHER = 'OFF'
cmd /c ".\build.cmd" 2>&1 | Select-Object -Last 15
Remove-Item env:SR_BUILD_LAUNCHER
```

Rode em background; avise o usuário que leva minutos. Expected: sem `error:`. Confirme que `git status` só mostra os dois arquivos (e o `port/superman_returns_manifest.toml` que o build regenera, que **não** se commita).

- [ ] **Step 5: Verificação rápida de comportamento (Vulkan, um bench cada modo)**

```powershell
$exe = "$PWD\port\out\build\win-amd64-release\superman_returns.exe"
$env:SR_NATIVE_LAZY_RESOLVE = '0'
powershell -NoProfile -File tools\bench\bench_api.ps1 -Api vulkan -Name lz_off_smoke_vulkan -Exe $exe -Timeline -TimelineDetail -Profile
Remove-Item env:SR_NATIVE_LAZY_RESOLVE
powershell -NoProfile -File tools\bench\bench_api.ps1 -Api vulkan -Name lz_on_smoke_vulkan -Exe $exe -Timeline -TimelineDetail -Profile
```

Expected: ambos chegam ao gameplay; com `OFF` o `fs_resolve` ≈ 0,9 ms; com `ON` o `fs_resolve` cai bem abaixo (só os casos sujos/precisando do intervalo) e `fs_plan` cai em valor parecido. O gate de imagem entra na Task 3. Se o modo ligado mostrar `fs_resolve` igual ao desligado, **pare** e reporte (o ramo sujo seria dominante, contradizendo a medição de `dirty ≈ 1/quadro`).

- [ ] **Step 6: Commit**

```powershell
git add port/src/native_renderer/native_renderer.h port/src/native_renderer/native_renderer.cpp
git commit -m "perf(native): resolve the vertex range only when the planned buffer is dirty" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Estágios de refresh/hash dos buffers de vértice (só medição)

**Files:**
- Modify: `port/src/graphics/frame_timeline.h`, `tests/native/test_frame_timeline.cpp`
- Modify: `tools/analysis/frame_timeline_report.py`, `tests/tools/test_frame_timeline_report.py`
- Modify: `port/src/native_renderer/native_renderer.cpp`

**Interfaces:**
- Produces: `TimelineStage::{kFbRefresh,kFbHash}` (CSV `fb_refresh`, `fb_hash`), ambos listados em `FRONT_END_SUB` (aninham-se em `fs_buffer`/`fs_other`, nunca somados); campos de contagem na linha de log `native front-end stream plan` (`refresh_calls`, `refresh_hashes`, `refresh_hash_kb`).

- [ ] **Step 1: Testes que falham**

`tests/native/test_frame_timeline.cpp`:

```cpp
SR_TEST(timeline_buffer_refresh_stages_use_their_csv_names) {
  FrameTimeline timeline("unused.csv");
  timeline.RecordBusy(TimelineStage::kFbRefresh, 8, 55);
  timeline.RecordBusy(TimelineStage::kFbHash, 8, 66);
  const std::string rows = Rows(timeline, 8, 8);
  SR_CHECK(rows.find("8,fb_refresh,0,0,55,0\n") != std::string::npos);
  SR_CHECK(rows.find("8,fb_hash,0,0,66,0\n") != std::string::npos);
}
```

`tests/tools/test_frame_timeline_report.py` (reutilize `detail_rows`, `write_csv`, `MS`):

```python
def test_buffer_refresh_stages_are_listed_and_not_summed(tmp_path):
    rows = []
    for frame in range(1, 4):
        rows += detail_rows(frame)
        rows += [(frame, "fb_refresh", 0, 0, 2 * MS, 0), (frame, "fb_hash", 0, 0, 1 * MS, 0)]
    path = tmp_path / "fb.csv"
    write_csv(path, rows)
    result = ftr.analyze(ftr.load(path))
    assert result["stages"]["fb_refresh"]["mean"] == pytest.approx(2.0)
    assert result["stages"]["fb_hash"]["mean"] == pytest.approx(1.0)
    assert result["stages"]["frontend_other"]["mean"] == pytest.approx(1.0)  # unchanged by the new stages
    text = ftr.render(result)
    assert "fb_refresh" in text and "fb_hash" in text
```

- [ ] **Step 2: Rodar e ver falhar**

```powershell
. .\tools\dev_env.ps1
cmake --build build/tests-native
python -m pytest tests/tools/test_frame_timeline_report.py -q
```

Expected: FAIL (`kFbRefresh` não existe; `KeyError: 'fb_refresh'`).

- [ ] **Step 3: Implementar cabeçalho e relatório**

`frame_timeline.h`: no enum acrescente `kFbRefresh, kFbHash,` depois de `kFsBuffer,` e em `TimelineStageName`:

```cpp
    case TimelineStage::kFbRefresh: return "fb_refresh";
    case TimelineStage::kFbHash: return "fb_hash";
```

`frame_timeline_report.py`: acrescente `"fb_refresh", "fb_hash"` ao fim de `FRONT_END_SUB`. Corrija também o comentário de `FRONT_END_SUB` para: "fe_pm4/fe_textures ⊂ fe_end; fs_resolve/fs_buffer ⊂ fs_plan (with the lazy range, fs_resolve ⊂ fs_buffer); fb_refresh ⊂ fs_buffer or fs_other; fb_hash ⊂ fb_refresh — listed, never summed".

- [ ] **Step 4: Rodar e ver passar**

```powershell
cmake --build build/tests-native
ctest --test-dir build/tests-native --output-on-failure
python -m pytest tests/tools -q
```

- [ ] **Step 5: Escopos e contadores no renderer**

Em `CaptureTimings` acrescente `uint64_t fb_refresh_ns=0,fb_hash_ns=0;`. Em `StreamPlanCounters` acrescente `uint64_t refresh_calls=0,refresh_hashes=0,refresh_hash_bytes=0;`.

Em `Renderer::RefreshTrackedBuffer`:
- primeira linha do corpo: `DetailScope refresh_scope(capture_timings.fb_refresh_ns);` e `if (graphics::FrameTimeline::Detail()) ++stream_plan_counters.refresh_calls;`
- dentro do lambda do `t.content.Refresh(front_frame_, page_written, [&] {...})`, como primeiras linhas do lambda: `DetailScope hash_scope(capture_timings.fb_hash_ns);` e `if (graphics::FrameTimeline::Detail()) {++stream_plan_counters.refresh_hashes;stream_plan_counters.refresh_hash_bytes+=t.size;}`.

(`stream_plan_counters` e `capture_timings` são `thread_local` da thread do jogo; chamadas de outras threads não são gravadas.)

No `OnSwap`, depois dos `RecordBusy` já existentes do bloco `Detail()`, acrescente:

```cpp
      timeline.RecordBusy(graphics::TimelineStage::kFbRefresh, swap_number, t.fb_refresh_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFbHash, swap_number, t.fb_hash_ns);
```

e estenda a linha de log do `% 120` acrescentando ao fim da mensagem `" | buffer refresh: calls={} hashes={} hash_kb={}"` com os argumentos `c.refresh_calls/120, c.refresh_hashes/120, c.refresh_hash_bytes/120/1024` (antes de `c={}`).

- [ ] **Step 6: Compilar e verificar**

Build em background e um bench Vulkan com `-Timeline -TimelineDetail -Profile` (nome `fb_smoke_vulkan`). Expected: a tabela tem `fb_refresh` e `fb_hash` com ≤ 5% descartados; sanidade: `fb_hash ≤ fb_refresh`; `fb_refresh` ≤ `fs_buffer + fs_other` aproximadamente (a primeira chamada de `RefreshTrackedBuffer` fica fora de `fs_plan`); a linha de log mostra `buffer refresh: calls=… hashes=… hash_kb=…`. Se uma regra falhar, **pare** e reporte.

- [ ] **Step 7: Commit**

```powershell
git add port/src/graphics/frame_timeline.h tests/native/test_frame_timeline.cpp tools/analysis/frame_timeline_report.py tests/tools/test_frame_timeline_report.py port/src/native_renderer/native_renderer.cpp
git commit -m "feat(native): time the vertex-buffer refresh and its content hash in the front end" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: A/B, gate de imagem e documento

**Files:**
- Modify: `docs/native-renderer-timeline.md`

- [ ] **Step 1: A/B no mesmo binário (Vulkan)**

Ordem OFF/ON/ON/OFF, cada uma com `-Timeline -TimelineDetail -Profile` (o perfil dá `draws=`). Um bench por vez, sem builds em paralelo; avise o usuário (levam minutos).

```powershell
$exe = "$PWD\port\out\build\win-amd64-release\superman_returns.exe"
foreach ($step in @(@('off1','0'), @('on1',''), @('on2',''), @('off2','0'))) {
  if ($step[1]) { $env:SR_NATIVE_LAZY_RESOLVE = $step[1] } else { Remove-Item env:SR_NATIVE_LAZY_RESOLVE -ErrorAction SilentlyContinue }
  powershell -NoProfile -File tools\bench\bench_api.ps1 -Api vulkan -Name "lz_$($step[0])_vulkan" -Exe $exe -Timeline -TimelineDetail -Profile
}
Remove-Item env:SR_NATIVE_LAZY_RESOLVE -ErrorAction SilentlyContinue
```

Repita uma vez em falha de infraestrutura; se falhar de novo, pare e reporte.

- [ ] **Step 2: Gate de imagem e fixtures**

Grave a referência numa execução do modo antigo e compare a do modo novo (o gate de imagem compara o screenshot do instante de detecção do HUD):

```powershell
$env:SR_NATIVE_LAZY_RESOLVE = '0'
powershell -NoProfile -File tools\bench\bench_api.ps1 -Api vulkan -Name lz_gate_off_vulkan -Exe $exe -Gate record
Remove-Item env:SR_NATIVE_LAZY_RESOLVE
powershell -NoProfile -File tools\bench\bench_api.ps1 -Api vulkan -Name lz_gate_on_vulkan -Exe $exe -Gate check
```

Se existir uma referência dourada em `artifacts/golden` que a sessão não deve sobrescrever, **não** use `-Gate record`: use `-Gate check` nas duas execuções e reporte os valores (PSNR/histograma) de ambas. Rode também `.\build\tests-vulkan\sr_vulkan_tests.exe` e `ctest --test-dir build/tests-native` (não tocam o front-end, mas confirmam que nada quebrou).

- [ ] **Step 3: Documento**

Acrescente a `docs/native-renderer-timeline.md` a seção "Resolve preguiçoso (Fase 1.1c)" com: as tabelas reais das quatro execuções do A/B; a comparação **por draw** dos estágios `fs_resolve`, `fs_buffer`, `fs_plan`, `fe_streams` e `frontend` entre OFF e ON (média por modo; µs/draw usando `draws=` das linhas `Vulkan profile` das execuções do mesmo modo); o resultado do gate de imagem; o efeito em FPS e a ressalva de variação de cena (as duas execuções de cada modo); e a regra de aceitação: o estágio caiu por draw? (se não, a mudança deve ser revertida: diga isso). Registre a mudança de aninhamento (`fs_resolve ⊂ fs_buffer` no modo preguiçoso) e atualize o aviso do relatório. Acrescente a decomposição de `fs_buffer` com os novos estágios: `fb_refresh` e `fb_hash` por quadro, número de chamadas/hashes/KB hasheados por quadro da linha `buffer refresh`, e o que isso sugere para a próxima otimização (sem afirmar além dos números; o hash de buffers de vértice não é alterado nesta fase). Sem marcadores `<...>`; releia contra as seções anteriores.

- [ ] **Step 4: Suítes e commit**

```powershell
python -m pytest tests/tools -q
git add docs/native-renderer-timeline.md
git commit -m "docs: measure the lazy vertex range and the buffer refresh cost" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Self-review

**Cobertura:** a otimização do teto medido (`fs_resolve` ≈ 0,95 ms) está na Task 1 com desligador e A/B; a decomposição do maior bloco restante (`fs_buffer` ≈ 2,3 ms) está na Task 2; verificação (estágio por draw, gate de imagem, suítes) na Task 3.

**Equivalência:** `need_begin/need_end` só são lidos em `PlanBuffer` depois do `if (!t.dirty) return plan;`; o ramo "buffer novo" ignora o intervalo; `Resolve()` só altera o `VertexRange` local do draw (idempotente). O modo preguiçoso calcula `b/e` com a mesma fórmula que `PlanStreams` usava (`lo = offset + first*stride`, `hi = offset + end*stride`, ambos limitados a `size = buffer_size`).

**Riscos:** (1) o front-end não tem teste unitário (precisa do SDK); a rede de segurança é a equivalência estrutural, o desligador no mesmo binário, o gate de imagem (grosseiro) e as suítes; (2) `lazy_range` aponta para um `VertexRange` que vive durante o draw (variável local do chamador), então o ponteiro não vaza; (3) o debug `sr_native_debug_vs` relê `range->first/end` e chama `Resolve()` de novo (idempotente); (4) a contabilidade de `fs_resolve` muda (aninha em `fs_buffer`), e os documentos anteriores falam em "ao lado"; a Task 3 atualiza o aviso.
