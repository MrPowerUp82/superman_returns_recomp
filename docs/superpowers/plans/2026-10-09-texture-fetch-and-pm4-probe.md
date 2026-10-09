# Fase 1.2: leitura preguiçosa dos fetch constants e sondas da captura de PM4 — Plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Atacar os dois maiores blocos que restam no front-end do Vulkan: `fe_textures` (≈ 5,7 ms) e `fe_pm4` (≈ 4,1 ms). (1) Otimização segura: `CaptureTextures` lê os 6 registradores de fetch constant de cada um dos 32 slots antes de saber se o slot tem textura; passa a ler só o primeiro registrador e os outros cinco apenas para slots ligados. (2) Medição: separar `fe_pm4` em leitura da memória do guest, cópias e varredura do parser, antes de decidir qualquer mudança nele.

**Architecture:** (1) A leitura dos 5 registradores restantes vai depois do teste `IsTextureBound(fetch[0])`; um desligador por ambiente (`SR_NATIVE_LAZY_FETCH=0`) restaura a leitura completa no mesmo binário. (2) `CapturePm4Dependencies` ganha um parâmetro opcional `Pm4CaptureProbe*` (sem dependência do SDK) que acumula, em ns, o tempo da cópia do ring primário, das leituras do guest e das cópias para o arena; a chamada de produção só o passa no modo detalhado.

**Tech Stack:** C++20, Python 3 + pytest, PowerShell.

## Global Constraints

- Imagem bit a bit idêntica; a otimização só deixa de ler registradores cujo valor é descartado (slots desligados), sem efeito colateral nas leituras (`Pm4Mirror::written/reg` e `Load32` são puros).
- Aceita só se o custo do **seu estágio** (`fe_textures`) cair por draw; senão é revertida.
- Política de texturas decidida (watch + backoff continua); nada altera hash ou revalidação de texturas.
- Instrumentação opt-in (`SR_FRAME_TIMELINE` + `SR_FRAME_TIMELINE_DETAIL=1`); nada novo roda sem elas; `capture_timings` e novos contadores em ns.
- `sr_renderer=native` nunca cai para Xenos. Avise o progresso entre passos longos.
- Commits só como parte desta execução; trailer exato `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`; nunca commitar `port/superman_returns_manifest.toml`, `logs/` nem `artifacts/`.
- Em docs com caminhos do Windows use a ferramenta Edit e confira `grep -cP '\x08'` = 0.

## Contexto medido e lido

Vulkan, ≈ 2700 draws/quadro: `fe_textures` ≈ 5,7 ms (≈ 2,1 µs/draw), `fe_pm4` ≈ 4,1 ms (≈ 1,5 µs/draw). Em `CaptureTextures` (a cada draw, 32 slots), o laço monta o array de 6 palavras de cada slot (`capture_mirror_.written(reg) ? reg(reg) : Load32(...)`) e só depois testa `IsTextureBound(fetch[0])`: 192 leituras de registrador por draw, quase todas descartadas. Em `CapturePm4Dependencies` (a cada comando com ring), cada dependência lida faz: `ReadProcessMemory` + alocação (`CheckedGuestReads::Read`), cópia para `sources` (outra alocação) e cópia para o arena (`batch.bytes.insert`), além da cópia do ring primário. Os logs de captura mostram ≈ 490 leituras e ≈ 5,6 MB por quadro (máx. 42 MB).

## Estágios novos no CSV

| Estágio | O que mede (ns, thread do jogo) |
| --- | --- |
| `fp_primary` | cópia do ring primário (`std::vector primary(...)`) em `CapturePm4Dependencies` |
| `fp_read` | chamadas `read(address, length)` do leitor do guest (inclui `ReadProcessMemory` e a alocação do `CheckedGuestReads`) |
| `fp_copy` | cópia de cada dependência para `sources` e para o arena (`batch.bytes.insert` + `ranges.push_back`) |

Aninham-se em `fe_pm4`. O relatório deriva `fp_scan = fe_pm4 − fp_primary − fp_read − fp_copy` (≥ 0): o tempo do parser e do resto.

---

### Task 1: Leitura preguiçosa dos fetch constants em `CaptureTextures`

**Files:**
- Modify: `port/src/native_renderer/native_renderer.cpp` (`CaptureTextures`)

**Interfaces:**
- Produces: mesmo `cur_.textures`, `cur_.texture_errors` e estado de `captured_textures_` de antes. `SR_NATIVE_LAZY_FETCH=0` restaura a leitura completa (A/B no mesmo binário).

- [ ] **Step 1: Verificar a premissa no código**

Confirme e reporte, com referências de linha: (a) em `CaptureTextures`, `fetch[1..5]` só são usados depois de `if(!IsTextureBound(fetch[0])) continue;` (o `continue` vem antes de qualquer outro uso do array); (b) `Pm4Mirror::written(reg)` e `reg(reg)` e `Load32` não têm efeito colateral. Se encontrar qualquer uso de `fetch[1..5]` antes do teste, ou efeito colateral, **pare** e reporte BLOCKED.

- [ ] **Step 2: Implementar**

Em `Renderer::CaptureTextures`, troque

```cpp
  for (uint32_t slot=0;slot<32;++slot) {
    std::array<uint32_t,6> fetch{};
    for(uint32_t i=0;i<6;++i) {
      uint32_t reg=Pm4Mirror::kFetchConstantBase+slot*6+i;
      fetch[i]=capture_mirror_.written(reg) ? capture_mirror_.reg(reg)
          : Load32(base,cur_.device+kDev.fetch_constants+slot*24+i*4);
    }
    if(!IsTextureBound(fetch[0])) continue;
```

por

```cpp
  // SR_NATIVE_LAZY_FETCH=0 restores the full read of the six fetch registers of every slot
  // (same-binary A/B). By default only register 0 is read before the bound test.
  static const bool lazy_fetch = [] {
    const char* value = std::getenv("SR_NATIVE_LAZY_FETCH");
    return !(value && *value == '0');
  }();
  const uint32_t first_pass = lazy_fetch ? 1 : 6;
  for (uint32_t slot=0;slot<32;++slot) {
    std::array<uint32_t,6> fetch{};
    auto read_fetch = [&](uint32_t i) {
      uint32_t reg=Pm4Mirror::kFetchConstantBase+slot*6+i;
      return capture_mirror_.written(reg) ? capture_mirror_.reg(reg)
          : Load32(base,cur_.device+kDev.fetch_constants+slot*24+i*4);
    };
    for(uint32_t i=0;i<first_pass;++i) fetch[i]=read_fetch(i);
    if(!IsTextureBound(fetch[0])) continue;
    for(uint32_t i=first_pass;i<6;++i) fetch[i]=read_fetch(i);
```

(O resto do laço fica como está. `first_pass = 1` lê só o registrador 0 antes do teste e os outros cinco depois; `first_pass = 6` reproduz o comportamento antigo.)

- [ ] **Step 3: Compilar e A/B rápido (Vulkan, um bench por modo)**

```powershell
$env:SR_BUILD_LAUNCHER = 'OFF'
cmd /c ".\build.cmd" 2>&1 | Select-Object -Last 15
Remove-Item env:SR_BUILD_LAUNCHER
$exe = "$PWD\port\out\build\win-amd64-release\superman_returns.exe"
$env:SR_NATIVE_LAZY_FETCH = '0'
powershell -NoProfile -File tools\bench\bench_api.ps1 -Api vulkan -Name lf_off_smoke_vulkan -Exe $exe -Timeline -TimelineDetail -Profile
Remove-Item env:SR_NATIVE_LAZY_FETCH
powershell -NoProfile -File tools\bench\bench_api.ps1 -Api vulkan -Name lf_on_smoke_vulkan -Exe $exe -Timeline -TimelineDetail -Profile
```

Build e benches em background, um de cada vez; avise o usuário que levam minutos. Expected: sem `error:`; ambos chegam ao gameplay com ≤ 5% de quadros descartados; `fe_textures` menor no modo ligado. Se não cair, **pare** e reporte os números (o custo não estaria nos registradores).

- [ ] **Step 4: Commit**

```powershell
git add port/src/native_renderer/native_renderer.cpp
git commit -m "perf(native): read the fetch constants of a texture slot only when the slot is bound" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Sondas da captura de PM4 (leitura, cópias, cópia do ring)

**Files:**
- Modify: `port/src/graphics/guest/pm4_capture.h`, `port/src/graphics/guest/pm4_capture.cpp`
- Modify: `tests/native/test_capture_budget.cpp` ou o arquivo de teste existente de `CapturePm4Dependencies` (veja `tests/native/test_pm4_mirror.cpp` e `tests/native/test_render_packets.cpp`; escolha o que já chama a função)
- Modify: `port/src/graphics/frame_timeline.h`, `tests/native/test_frame_timeline.cpp`
- Modify: `tools/analysis/frame_timeline_report.py`, `tests/tools/test_frame_timeline_report.py`
- Modify: `port/src/native_renderer/native_renderer.cpp`

**Interfaces:**
- Produces: `struct Pm4CaptureProbe { uint64_t primary_ns = 0, read_ns = 0, copy_ns = 0, reads = 0, bytes = 0; };` (em `pm4_capture.h`, sem dependência do SDK) e `bool CapturePm4Dependencies(WorkBatch&, const WorkCmd&, const GuestMemoryReader&, std::string& error, native::Pm4Mirror* mirror = nullptr, Pm4CaptureProbe* probe = nullptr)`. Estágios `kFpPrimary`, `kFpRead`, `kFpCopy` (CSV `fp_primary`, `fp_read`, `fp_copy`) e o derivado `fp_scan` no relatório.

- [ ] **Step 1: Testes que falham**

C++ (`test_frame_timeline.cpp`):

```cpp
SR_TEST(timeline_pm4_probe_stages_use_their_csv_names) {
  FrameTimeline timeline("unused.csv");
  timeline.RecordBusy(TimelineStage::kFpPrimary, 3, 11);
  timeline.RecordBusy(TimelineStage::kFpRead, 3, 22);
  timeline.RecordBusy(TimelineStage::kFpCopy, 3, 33);
  const std::string rows = Rows(timeline, 3, 3);
  SR_CHECK(rows.find("3,fp_primary,0,0,11,0\n") != std::string::npos);
  SR_CHECK(rows.find("3,fp_read,0,0,22,0\n") != std::string::npos);
  SR_CHECK(rows.find("3,fp_copy,0,0,33,0\n") != std::string::npos);
}
```

Python (`test_frame_timeline_report.py`, reutilizando `detail_rows`, `write_csv`, `MS`, `ftr`):

```python
def test_fp_scan_is_derived_from_the_pm4_probe_parts(tmp_path):
    rows = []
    for frame in range(1, 4):
        rows += detail_rows(frame)
        rows += [(frame, "fe_pm4", 0, 0, 4 * MS, 0), (frame, "fp_primary", 0, 0, 0, 0),
                 (frame, "fp_read", 0, 0, 1 * MS, 0), (frame, "fp_copy", 0, 0, 1 * MS, 0)]
    path = tmp_path / "pm4.csv"
    write_csv(path, rows)
    stages = ftr.analyze(ftr.load(path))["stages"]
    assert stages["fp_read"]["mean"] == pytest.approx(1.0)
    assert stages["fp_scan"]["mean"] == pytest.approx(2.0)   # 4 - 0 - 1 - 1
    assert stages["frontend_other"]["mean"] == pytest.approx(1.0)  # unchanged


def test_fp_scan_is_clamped_and_absent_without_the_probe(tmp_path):
    rows = []
    for frame in range(1, 4):
        rows += detail_rows(frame)
        rows += [(frame, "fe_pm4", 0, 0, 1 * MS, 0), (frame, "fp_read", 0, 0, 2 * MS, 0)]
    path = tmp_path / "clamp3.csv"
    write_csv(path, rows)
    assert ftr.analyze(ftr.load(path))["stages"]["fp_scan"]["mean"] == 0.0
    plain = tmp_path / "plain3.csv"
    write_csv(plain, [r for f in range(1, 4) for r in detail_rows(f)])
    assert "fp_scan" not in ftr.analyze(ftr.load(plain))["stages"]
```

Para `CapturePm4Dependencies`, leia o teste existente que a chama e acrescente um caso que passa um `Pm4CaptureProbe` e verifica `probe.reads` e `probe.bytes` (contagem das leituras bem-sucedidas e total de bytes) para uma entrada com uma dependência conhecida, e que o resultado (`batch.bytes`, `batch.ranges`, retorno) é idêntico com e sem sonda. Se nenhum teste existente exercita dependências reais, crie o caso com um ring mínimo que referencia um buffer indireto (siga o padrão de `test_pm4_mirror.cpp`).

- [ ] **Step 2: Rodar e ver falhar**

```powershell
. .\tools\dev_env.ps1
cmake --build build/tests-native
python -m pytest tests/tools/test_frame_timeline_report.py -q
```

Expected: FAIL (`Pm4CaptureProbe` / `kFpPrimary` não existem; `KeyError: 'fp_read'`).

- [ ] **Step 3: Implementar**

`pm4_capture.h`: acrescente (sem novas dependências além de `<cstdint>`)

```cpp
// Optional nanosecond counters filled by CapturePm4Dependencies (diagnostics only).
struct Pm4CaptureProbe {
  uint64_t primary_ns = 0, read_ns = 0, copy_ns = 0, reads = 0, bytes = 0;
};
```

e o parâmetro final `Pm4CaptureProbe *probe = nullptr` na declaração.

`pm4_capture.cpp`: inclua `<chrono>`; com um helper local

```cpp
namespace {
inline uint64_t NowNs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch()).count());
}
}  // namespace
```

meça, **somente quando `probe != nullptr`**: (a) a construção de `primary` (`probe->primary_ns`); (b) cada chamada `read(address, length)` (`probe->read_ns`, e se bem-sucedida `++probe->reads; probe->bytes += length;`); (c) o trecho `sources.emplace_back(...)` até `batch.ranges.push_back(...)` (`probe->copy_ns`). Sem sonda, o código deve executar exatamente as mesmas instruções de antes (nenhum relógio lido). Não altere nenhum outro comportamento.

`frame_timeline.h`: acrescente `kFpPrimary, kFpRead, kFpCopy,` ao enum (depois de `kFbHash`) e os três nomes em `TimelineStageName`. `frame_timeline_report.py`: acrescente `"fp_primary", "fp_read", "fp_copy"` ao fim de `FRONT_END_SUB`, `"fp_scan"` à tupla `STAGES` logo depois de `"fe_end_rest"`, e depois do bloco que cria `fe_end_rest`:

```python
        probed = [i for i in with_frontend if "fe_pm4" in frames[i] and any(p in frames[i] for p in ("fp_primary", "fp_read", "fp_copy"))]
        if probed:  # the PM4 parser and the rest of CapturePm4Dependencies
            scan = [max(0, frames[i]["fe_pm4"]["busy_ns"]
                        - sum(frames[i][p]["busy_ns"] for p in ("fp_primary", "fp_read", "fp_copy") if p in frames[i])) / 1e6
                    for i in probed]
            stages["fp_scan"] = _summary(scan, [0.0] * len(scan), mean_interval, budget_ms)
```

`native_renderer.cpp`: em `CaptureTimings` acrescente `uint64_t fp_primary_ns=0,fp_read_ns=0,fp_copy_ns=0;` e em `StreamPlanCounters` `uint64_t pm4_reads=0,pm4_bytes=0;`. Em `EndCmd`, antes da chamada `graphics::guest::CapturePm4Dependencies(...)` declare `graphics::guest::Pm4CaptureProbe pm4_probe;`, passe `graphics::FrameTimeline::Detail() ? &pm4_probe : nullptr` como último argumento (depois de `&capture_mirror_`) e, logo depois do `pm4_scope.Stop();`, acrescente:

```cpp
    if (graphics::FrameTimeline::Detail()) {
      capture_timings.fp_primary_ns += pm4_probe.primary_ns;
      capture_timings.fp_read_ns += pm4_probe.read_ns;
      capture_timings.fp_copy_ns += pm4_probe.copy_ns;
      stream_plan_counters.pm4_reads += pm4_probe.reads;
      stream_plan_counters.pm4_bytes += pm4_probe.bytes;
    }
```

No `OnSwap`, no bloco `Detail()`, acrescente os três `RecordBusy` (`kFpPrimary`, `kFpRead`, `kFpCopy` com `t.fp_*_ns`) e estenda a linha de log do `% 120` com `" | pm4 deps: reads={} kb={}"` e os argumentos `c.pm4_reads/120, c.pm4_bytes/120/1024` (antes do reset dos contadores que já existe).

- [ ] **Step 4: Rodar e ver passar**

```powershell
cmake --build build/tests-native
ctest --test-dir build/tests-native --output-on-failure
python -m pytest tests/tools -q
```

Expected: tudo passando, incluindo o caso de equivalência com e sem sonda.

- [ ] **Step 5: Compilar o jogo e verificar (um bench Vulkan com `-Timeline -TimelineDetail -Profile`, nome `pp_smoke_vulkan`)**

Build e bench em background; avise o usuário. Expected: sem `error:`; a tabela tem `fp_primary`, `fp_read`, `fp_copy`, `fp_scan` com ≤ 5% descartados; sanidade: `fp_primary + fp_read + fp_copy ≤ fe_pm4`; a linha de log mostra `pm4 deps: reads=… kb=…` coerente com as ≈ 490 leituras e ≈ 5,6 MB dos logs antigos. Se uma regra falhar, **pare** e reporte.

- [ ] **Step 6: Commit**

```powershell
git add port/src/graphics/guest/pm4_capture.h port/src/graphics/guest/pm4_capture.cpp port/src/graphics/frame_timeline.h port/src/native_renderer/native_renderer.cpp tools/analysis/frame_timeline_report.py tests/native tests/tools/test_frame_timeline_report.py
git commit -m "feat(native): probe the PM4 dependency capture (primary copy, guest reads, arena copies)" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

(Se `git add tests/native` incluir outros arquivos que você não alterou, liste apenas os que mudou.)

---

### Task 3: A/B, gate e documento

**Files:**
- Modify: `docs/native-renderer-timeline.md`

- [ ] **Step 1: A/B das texturas no mesmo binário (Vulkan)**

OFF/ON/ON/OFF com `SR_NATIVE_LAZY_FETCH=0` nos OFF, todos com `-Timeline -TimelineDetail -Profile` (um bench por vez, sem builds em paralelo; avise o usuário que levam minutos; repita uma vez em falha de infraestrutura):

```powershell
$exe = "$PWD\port\out\build\win-amd64-release\superman_returns.exe"
foreach ($step in @(@('off1','0'), @('on1',''), @('on2',''), @('off2','0'))) {
  if ($step[1]) { $env:SR_NATIVE_LAZY_FETCH = $step[1] } else { Remove-Item env:SR_NATIVE_LAZY_FETCH -ErrorAction SilentlyContinue }
  powershell -NoProfile -File tools\bench\bench_api.ps1 -Api vulkan -Name "lf_$($step[0])_vulkan" -Exe $exe -Timeline -TimelineDetail -Profile
}
Remove-Item env:SR_NATIVE_LAZY_FETCH -ErrorAction SilentlyContinue
```

- [ ] **Step 2: Gate de imagem e suítes**

Use `-Gate check` nos dois modos contra a referência local em `artifacts/golden` (criada na fase anterior; **não** regrave com `-Gate record`): uma execução ON e uma OFF, reportando PSNR, histograma e as regiões. Rode `ctest --test-dir build/tests-native`, `.\build\tests-vulkan\sr_vulkan_tests.exe` (recompile antes se o binário for anterior aos últimos commits) e `python -m pytest tests/tools -q`.

- [ ] **Step 3: Documento**

Acrescente a `docs/native-renderer-timeline.md` a seção "Fetch constants preguiçosos e sondas do PM4 (Fase 1.2)" com: as tabelas reais do A/B; a comparação **por draw** (µs/draw com `draws=` das linhas `Vulkan profile` do mesmo modo) de `fe_textures`, `fe_end`, `frontend` e `game`, e a regra de aceitação (o estágio caiu por draw? se não, reverter); as ressalvas de detalhe (cada `DetailScope` tem custo de relógio; aqui `fe_textures` é medido por um escopo por chamada de `EndCmd`, igual nos dois modos, então o custo é simétrico) e de variação de cena; o gate e as suítes. Da sonda do PM4 (execução `pp_smoke_vulkan` e, se quiser, as quatro do A/B, que também carregam as sondas): `fp_primary`, `fp_read`, `fp_copy`, `fp_scan` em ms e em µs/draw, leituras e KB por quadro, o que isso diz sobre onde está `fe_pm4` e quais mudanças seriam candidatas (cópia redundante de `sources`, alocação por leitura, chamada de sistema por dependência), cada uma com o estágio atacado, o teto, o risco (contrato de `GuestMemoryReader`, tempo de vida dos spans, equivalência bit a bit) e a verificação. Sem afirmar além dos números; sem marcadores `<...>`; releia contra as seções anteriores.

- [ ] **Step 4: Commit**

```powershell
python -m pytest tests/tools -q
git add docs/native-renderer-timeline.md
git commit -m "docs: measure the lazy fetch constants and the PM4 capture breakdown" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Self-review

**Cobertura:** a otimização do maior bloco (`fe_textures`, com desligador e A/B) está na Task 1; a decomposição do segundo maior (`fe_pm4`) na Task 2; verificação e documento na Task 3.

**Equivalência da Task 1:** `fetch[1..5]` só são lidos depois do teste de textura ligada nos dois modos; os valores lidos são os mesmos (leitura pura do espelho PM4 ou da memória do guest), então `cur_.textures` e `captured_textures_` ficam idênticos. O modo `SR_NATIVE_LAZY_FETCH=0` mantém a leitura completa antes do teste.

**Riscos:** (1) a sonda do PM4 mede com relógio dentro de `pm4_capture.cpp` só quando há `probe` (nenhuma instrução nova sem ela); (2) o teste de equivalência com e sem sonda fecha o contrato do arena; (3) `Pm4CaptureProbe` fica em `graphics/guest` (sem SDK) para continuar testável em `tests/native`; (4) os custos de relógio das sondas inflam `fe_pm4` no modo detalhado (documentado).
