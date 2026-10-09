# Fase 2.0: tempo real de GPU (sem sobreposição) no Vulkan e no D3D12 — Plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Responder, com números comparáveis entre as duas APIs, se a GPU é de fato um limitante do Vulkan. Hoje o estágio `gpu` do Vulkan (~32 ms, ~90% de uso) é um limite superior: ele vai do `TOP_OF_PIPE` do primeiro command buffer ao `BOTTOM_OF_PIPE` do último, com 2 quadros em voo, então pode incluir a espera atrás do quadro anterior. O `gpu` do D3D12 (~23 ms) vai do primeiro ao último timestamp da própria command list, e tem a mesma ambiguidade. Esta fase mede, nas duas APIs, o tempo de GPU **descontada a sobreposição** com o quadro anterior e o **intervalo ocioso** entre quadros.

**Architecture:** Cada quadro já escreve um timestamp de início e um de fim. Passamos a guardar o instante de fim do quadro anterior e calcular, por quadro: `overlap = max(0, fim_anterior − início)`, `idle = max(0, início − fim_anterior)`, `real = duração − min(overlap, duração)`. Dois estágios novos no CSV: `gpu_real` e `gpu_idle`. O estágio `gpu` continua como está (limite superior), para não mudar a leitura dos documentos anteriores.

**Tech Stack:** C++20 (Vulkan e D3D12), Python 3 + pytest, PowerShell.

## Global Constraints

- Só instrumento e diagnóstico: nenhuma mudança de renderização, ordem de comandos ou sincronização; a imagem não muda. Os timestamps já existem; só se lê e calcula.
- Ligado apenas com `SR_FRAME_TIMELINE`; sem ela nada novo roda.
- A aritmética de ticks respeita `timestampValidBits` (Vulkan; contador que dá a volta) e o contador de 64 bits do D3D12.
- `sr_renderer=native` nunca cai para Xenos. Avise o progresso entre passos longos.
- Commits só como parte desta execução; trailer exato `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`; nunca commitar `port/superman_returns_manifest.toml`, `logs/` nem `artifacts/`.
- Em docs com caminhos do Windows use a ferramenta Edit e confira `grep -cP '\x08'` = 0.

## Formato

| Estágio | O que mede (por quadro, só `busy_ns`) |
| --- | --- |
| `gpu_real` | duração do quadro na GPU menos a sobreposição com o quadro anterior |
| `gpu_idle` | intervalo (≥ 0) entre o fim do quadro anterior e o início deste; GPU parada esperando trabalho |

Relação: `gpu = gpu_real + overlap`, onde `overlap` não é gravado (o relatório o deriva como `gpu − gpu_real`).

---

### Task 1: Estágios `gpu_real` e `gpu_idle` no cabeçalho e no relatório

**Files:**
- Modify: `port/src/graphics/frame_timeline.h`, `tests/native/test_frame_timeline.cpp`
- Modify: `tools/analysis/frame_timeline_report.py`, `tests/tools/test_frame_timeline_report.py`

**Interfaces:**
- Produces: `TimelineStage::{kGpuReal,kGpuIdle}` (CSV `gpu_real`, `gpu_idle`); no relatório os dois estágios aparecem logo depois de `gpu`, e o derivado `gpu_overlap = max(0, gpu − gpu_real)` (só quando `gpu` e `gpu_real` existem no quadro). Eles **não** entram em `LIMITERS`.

- [ ] **Step 1: Testes que falham**

C++ (`test_frame_timeline.cpp`):

```cpp
SR_TEST(timeline_gpu_real_and_idle_use_their_csv_names) {
  FrameTimeline timeline("unused.csv");
  timeline.RecordBusy(TimelineStage::kGpuReal, 5, 111);
  timeline.RecordBusy(TimelineStage::kGpuIdle, 5, 222);
  const std::string rows = Rows(timeline, 5, 5);
  SR_CHECK(rows.find("5,gpu_real,0,0,111,0\n") != std::string::npos);
  SR_CHECK(rows.find("5,gpu_idle,0,0,222,0\n") != std::string::npos);
}
```

Python (`test_frame_timeline_report.py`; reutilize `frame_rows`, `write_csv`, `MS`, `ftr`, `pytest`):

```python
def test_gpu_real_idle_and_overlap_are_listed_and_not_limiters(tmp_path):
    rows = []
    for frame in range(1, 6):
        rows += frame_rows(frame)   # gpu = 18 ms
        rows += [(frame, "gpu_real", 0, 0, 12 * MS, 0), (frame, "gpu_idle", 0, 0, 3 * MS, 0)]
    path = tmp_path / "gpu.csv"
    write_csv(path, rows)
    result = ftr.analyze(ftr.load(path))
    stages = result["stages"]
    assert stages["gpu_real"]["mean"] == pytest.approx(12.0)
    assert stages["gpu_idle"]["mean"] == pytest.approx(3.0)
    assert stages["gpu_overlap"]["mean"] == pytest.approx(6.0)     # 18 - 12
    assert "gpu_real" not in result["limiting_share"] and "gpu_idle" not in result["limiting_share"]
    names = [line.split()[0] for line in ftr.render(result).splitlines()[5:] if line.strip()]
    assert names.index("gpu_real") > names.index("gpu")


def test_gpu_overlap_is_clamped_and_absent_without_gpu_real(tmp_path):
    rows = []
    for frame in range(1, 4):
        rows += frame_rows(frame)
        rows += [(frame, "gpu_real", 0, 0, 20 * MS, 0)]   # more than gpu (18 ms)
    path = tmp_path / "clamp4.csv"
    write_csv(path, rows)
    assert ftr.analyze(ftr.load(path))["stages"]["gpu_overlap"]["mean"] == 0.0
    plain = tmp_path / "plain4.csv"
    write_csv(plain, [r for f in range(1, 4) for r in frame_rows(f)])
    assert "gpu_overlap" not in ftr.analyze(ftr.load(plain))["stages"]
```

- [ ] **Step 2: Rodar e ver falhar**

```powershell
. .\tools\dev_env.ps1
cmake --build build/tests-native
python -m pytest tests/tools/test_frame_timeline_report.py -q
```

Expected: FAIL (`kGpuReal` não existe; `KeyError: 'gpu_real'`).

- [ ] **Step 3: Implementar**

`frame_timeline.h`: no enum acrescente `kGpuReal, kGpuIdle,` logo depois de `kFpCopy,` (antes de `kCount`) e em `TimelineStageName`:

```cpp
    case TimelineStage::kGpuReal: return "gpu_real";
    case TimelineStage::kGpuIdle: return "gpu_idle";
```

`frame_timeline_report.py`: defina `GPU_SUB = ("gpu_real", "gpu_idle")`; acrescente `*GPU_SUB, "gpu_overlap"` à tupla `STAGES` logo depois de `"gpu"`; inclua `*GPU_SUB` no laço de estágios brutos de `analyze`; depois do bloco dos derivados do front-end, acrescente

```python
    both = [i for i in complete if "gpu" in frames[i] and "gpu_real" in frames[i]]
    if both:  # GPU time that overlapped the previous frame (upper-bound gpu minus the real time)
        overlap = [max(0, frames[i]["gpu"]["busy_ns"] - frames[i]["gpu_real"]["busy_ns"]) / 1e6 for i in both]
        stages["gpu_overlap"] = _summary(overlap, [0.0] * len(overlap), mean_interval, budget_ms)
```

(`LIMITERS` não muda.)

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
git commit -m "feat(tools): add gpu_real, gpu_idle and the derived gpu_overlap to the timeline" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Calcular o tempo real e o intervalo ocioso nas duas APIs

**Files:**
- Modify: `port/src/graphics/vulkan/game_frame.h`, `port/src/graphics/vulkan/game_frame.cpp`
- Modify: `port/src/native_renderer/native_renderer.h`, `port/src/native_renderer/native_renderer.cpp`

**Interfaces:**
- Consumes: os estágios da Task 1 e os timestamps existentes.
- Produces: linhas `gpu_real` e `gpu_idle` no CSV de cada API, ao lado de `gpu`.

- [ ] **Step 1: Vulkan**

Em `game_frame.h`, ao lado dos membros de timestamp (`timestamps_`, `slot_swap_`, `timestamp_mask_`, `timestamp_period_ns_`), acrescente `uint64_t prev_end_ticks_=0;bool have_prev_end_=false;`.

Em `GameFrame::HarvestTimestamps`, depois de `uint64_t ticks[2]{};` e do `vkGetQueryPoolResults` bem-sucedido, substitua o cálculo/gravação atual do `kGpu` por um bloco que, **mantendo** o `RecordBusy(kGpu, ..., duração)` igual ao de hoje, acrescenta:

```cpp
    const uint64_t mask=timestamp_mask_,begin=ticks[0]&mask,end=ticks[1]&mask;
    const uint64_t duration_ticks=(end-begin)&mask;
    uint64_t overlap_ticks=0,idle_ticks=0;
    if(have_prev_end_) {
      // Counters wrap at timestampValidBits: a small forward distance means the previous frame ended
      // after this one began (overlap); a backward one means the GPU sat idle in between.
      const uint64_t forward=(prev_end_ticks_-begin)&mask;   // prev_end - begin
      const uint64_t backward=(begin-prev_end_ticks_)&mask;  // begin - prev_end
      if(backward<=mask/2) idle_ticks=backward; else overlap_ticks=forward;
    }
    prev_end_ticks_=end;have_prev_end_=true;
    const uint64_t real_ticks=duration_ticks-std::min(overlap_ticks,duration_ticks);
    FrameTimeline::Global().RecordBusy(TimelineStage::kGpuReal,slot_swap_[slot],uint64_t(double(real_ticks)*timestamp_period_ns_));
    FrameTimeline::Global().RecordBusy(TimelineStage::kGpuIdle,slot_swap_[slot],uint64_t(double(idle_ticks)*timestamp_period_ns_));
```

Leia a função atual antes e integre ao código existente (variáveis e estilo denso do arquivo). Atenção: `HarvestTimestamps` é chamado na ordem dos quadros durante a gravação; no destrutor ele roda para os dois slots — se os dois tiverem quadro pendente, processe primeiro o de menor `slot_swap_` para manter a ordem cronológica.

- [ ] **Step 2: D3D12**

Em `native_renderer.h`, perto de `ts_frame_`, acrescente `uint64_t ts_prev_end_ = 0; bool ts_have_prev_ = false;`.

Em `Renderer::BeginFrameTimestamp`, no bloco que já faz `RecordBusy(kGpu, ts_frame_[frame_index_], ...)` (dentro de `if (n >= 2 && t[n - 1] > t[0])`), acrescente, usando o contador de 64 bits do D3D12 (`t[0]` início, `t[n-1]` fim):

```cpp
      {
        const uint64_t begin = t[0], end = t[n - 1];
        uint64_t overlap = 0, idle = 0;
        if (ts_have_prev_) {
          if (begin >= ts_prev_end_) idle = begin - ts_prev_end_;
          else overlap = ts_prev_end_ - begin;
        }
        ts_prev_end_ = end;
        ts_have_prev_ = true;
        const uint64_t duration = end - begin;
        const uint64_t real = duration - std::min(overlap, duration);
        graphics::FrameTimeline::Global().RecordBusy(graphics::TimelineStage::kGpuReal, ts_frame_[frame_index_],
            uint64_t(double(real) * 1e9 / double(ts_frequency_)));
        graphics::FrameTimeline::Global().RecordBusy(graphics::TimelineStage::kGpuIdle, ts_frame_[frame_index_],
            uint64_t(double(idle) * 1e9 / double(ts_frequency_)));
      }
```

(A harvest do D3D12 ocorre em ordem de slot/quadro, então `ts_prev_end_` é o fim do quadro anterior.)

- [ ] **Step 3: Compilar e verificar**

```powershell
. .\tools\dev_env.ps1
cmake --build build/tests-vulkan
.\build\tests-vulkan\sr_vulkan_tests.exe | Select-Object -Last 1
$env:SR_BUILD_LAUNCHER = 'OFF'
cmd /c ".\build.cmd" 2>&1 | Select-Object -Last 15
Remove-Item env:SR_BUILD_LAUNCHER
```

Expected: `88 tests, 0 failed checks`; build do jogo sem `error:`. Depois, um bench por API (em background, um de cada vez; avise o usuário):

```powershell
$exe = "$PWD\port\out\build\win-amd64-release\superman_returns.exe"
powershell -NoProfile -File tools\bench\bench_api.ps1 -Api vulkan -Name gr_smoke_vulkan -Exe $exe -Timeline
powershell -NoProfile -File tools\bench\bench_api.ps1 -Api d3d12 -Name gr_smoke_d3d12 -Exe $exe -Timeline
```

Sanidade a conferir nas duas tabelas e no CSV (todos os quadros): `gpu_real ≤ gpu`; `gpu_real ≥ 0`; `gpu_idle ≥ 0`; `gpu_real + gpu_idle` é da ordem do intervalo do quadro (não precisa fechar exatamente); descartados ≤ 5%. Se `gpu_real` ficar igual a `gpu` em ambas as APIs em todos os quadros ou `gpu_idle` for sempre 0, **pare** e reporte (o cálculo do fim anterior pode estar errado).

- [ ] **Step 4: Commit**

```powershell
git add port/src/graphics/vulkan/game_frame.h port/src/graphics/vulkan/game_frame.cpp port/src/native_renderer/native_renderer.h port/src/native_renderer/native_renderer.cpp
git commit -m "feat: record the overlap-corrected GPU time and the idle gap in Vulkan and D3D12" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Medir as duas APIs e documentar

**Files:**
- Modify: `docs/native-renderer-timeline.md`

- [ ] **Step 1: Medições**

Quatro benches, um por vez, sem builds nem carga em paralelo (avise o usuário; repita uma vez em falha de infraestrutura): duas por API, com `-Timeline` (sem `-TimelineDetail`, para o custo da instrumentação ser mínimo) e `-Profile` apenas na primeira de cada API se precisar de `draws=`.

```powershell
$exe = "$PWD\port\out\build\win-amd64-release\superman_returns.exe"
foreach ($api in 'vulkan','d3d12') { foreach ($n in 1,2) { powershell -NoProfile -File tools\bench\bench_api.ps1 -Api $api -Name "gr$n`_$api" -Exe $exe -Timeline } }
```

- [ ] **Step 2: Seção no documento**

Acrescente "Tempo real de GPU (Fase 2.0)" a `docs/native-renderer-timeline.md`: as quatro tabelas reais; uma tabela por API e execução com `gpu` (limite superior), `gpu_real`, `gpu_overlap`, `gpu_idle` e o intervalo do quadro (ms), mais a fração `gpu_real / intervalo` e `gpu_idle / intervalo`; a comparação Vulkan × D3D12 do `gpu_real` (a diferença de ~9 ms do `gpu` antigo sobrevive ou é sobreposição?); o que o `gpu_idle` diz (GPU ociosa ⇒ limitada pela CPU; sem ociosidade ⇒ a GPU é limitante); ressalvas honestas (timestamps de topo/fundo de pipeline medem o intervalo de execução, não ocupação; a GPU pode executar passes sobrepostos dentro do quadro; o `real` desconta só a sobreposição entre quadros; uma máquina; variação de cena; a deriva observada de ≈8% entre execuções); e a conclusão: a GPU do Vulkan é ou não um limitante, com os números, e quais seriam os próximos passos (por exemplo, tempo de GPU por passe com chave de agrupamento por assinatura do render target, se a GPU for limitante; ou voltar à CPU se `gpu_idle` for relevante). Nada além do que as tabelas mostram; sem marcadores `<...>`; releia contra as seções anteriores, em especial as que chamam o `gpu` do Vulkan de limite superior.

- [ ] **Step 3: Commit**

```powershell
python -m pytest tests/tools -q
git add docs/native-renderer-timeline.md
git commit -m "docs: measure the real GPU time and the idle gap in Vulkan and D3D12" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Self-review

**Cobertura:** Task 1 (formato e relatório), Task 2 (cálculo nas duas APIs com a mesma definição), Task 3 (medição e conclusão). `gpu` fica como limite superior para não quebrar a leitura histórica.

**Aritmética:** Vulkan usa máscara de `timestampValidBits` com distância "para trás" ≤ `mask/2` ⇒ ocioso, senão sobreposição; D3D12 usa 64 bits sem volta. `real = duração − min(sobreposição, duração)`.

**Riscos:** (1) a ordem de colheita dos timestamps (slot rotativo) precisa ser cronológica; o destrutor ordena pelo menor `slot_swap_`; (2) se o primeiro quadro colhido não tem anterior, `gpu_idle = 0` e `gpu_real = gpu`; (3) os timestamps medem execução, não ocupação; a conclusão não deve afirmar "GPU 100% ocupada"; (4) sem passe a passe, esta fase só diz *se* a GPU limita, não *onde*.
