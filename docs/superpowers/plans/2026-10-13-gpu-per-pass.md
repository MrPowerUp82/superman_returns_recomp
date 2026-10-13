# Fase 2.1: tempo de GPU por passe nas duas APIs — Plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Descobrir **onde** a GPU do Vulkan gasta os ~10 ms a mais que a do D3D12 (`gpu_real` ≈ 31 ms contra ≈ 22,4–23,1 ms, Fase 2.0), medindo o tempo de GPU por *passe* e comparando as duas APIs com a mesma chave. Os ids de passe do perfil do jogo valem `-1`, então a chave é a assinatura do conjunto de alvos de render: **largura × altura × nº de alvos de cor × tem profundidade**. É a mesma ideia do relatório de pesquisa recebido (achar passes/resolves/cópias redundantes antes de otimizar), aplicada ao nosso renderer.

**Architecture:** (1) Um agregador puro (`GpuPassStats`, sem dependência de API) recebe, por quadro, a lista de segmentos `{chave, ns}` mais o tempo "outro" (lacunas entre passes: transferências, barreiras, resolves) e imprime, a cada 120 quadros, os maiores grupos por tempo total por quadro. (2) Vulkan: timestamps adicionais no início e no fim de cada render pass, em um query pool próprio por slot. (3) D3D12: timestamps nas mudanças do conjunto de alvos (`OMSetRenderTargets`), reaproveitando o heap de timestamps existente. Tudo atrás de `SR_GPU_PASS_TIMING=1`, que só deve ser usado em medição (desligado por padrão).

**Tech Stack:** C++20 (Vulkan e D3D12), PowerShell, Python 3.

## Global Constraints

- Só diagnóstico: nenhuma mudança de renderização, ordem de comandos, barreiras ou sincronização. Os timestamps são comandos extras de consulta (`vkCmdWriteTimestamp` / `EndQuery`) escritos apenas quando `SR_GPU_PASS_TIMING=1`; sem a variável, os command buffers ficam idênticos aos atuais.
- A imagem final não muda (confirmar com o gate de imagem uma vez com a variável ligada e uma desligada).
- A chave de passe é a mesma nas duas APIs: `{width, height, colors, depth}`; formatos específicos da API não entram na chave (só no log de detalhe, se houver).
- `sr_renderer=native` nunca cai para Xenos. Avise o progresso entre passos longos.
- Commits só como parte desta execução; trailer exato `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`; nunca commitar `port/superman_returns_manifest.toml`, `logs/` nem `artifacts/`.
- Em docs com caminhos do Windows use a ferramenta Edit e confira `grep -cP '\x08'` = 0.

## Contexto medido e lido

- Vulkan: `gpu_real` 30,7–32,8 ms (83–85% do intervalo), D3D12: 22,4–23,1 ms; razão 1,33–1,46× (dependente da cena); por draw ≈ 11,4–12,0 µs contra ≈ 8,3 µs.
- Vulkan abre um render pass quando `open_pass_ != pass` (`GameRenderer`, `vkCmdBeginRenderPass`) e o fecha em `ClosePass()` (`vkCmdEndRenderPass`); `TargetPass` guarda `extent`, `formats[4]`, `depth_format`, `color_count`. Resolves, cópias, clears e aliases de profundidade ocorrem **fora** de render passes (entre `ClosePass` e a próxima abertura).
- D3D12: o conjunto de alvos muda em `Renderer::` (≈ `native_renderer.cpp` ~2630–2665): `bool changed = rt_count != bound_rt_count_ || ds != bound_ds_ || ...` seguido de `OMSetRenderTargets`. O D3D12 já tem um heap de timestamps (`ts_heap_`, `kTsPerFrame = 64` por slot, `kFramesInFlight = 3`) lido em `BeginFrameTimestamp`, hoje com o último timestamp reservado para o fim do quadro.

---

### Task 1: Agregador puro `GpuPassStats`

**Files:**
- Create: `port/src/graphics/gpu_pass_stats.h`
- Create: `tests/native/test_gpu_pass_stats.cpp`
- Modify: `tests/native/CMakeLists.txt` (acrescente `test_gpu_pass_stats.cpp` às fontes de `sr_native_tests`, depois de `test_frame_timeline.cpp`)

**Interfaces:**
- Produces (namespace `superman_returns::graphics`): `struct GpuPassKey {uint32_t width, height, colors; bool depth;}` com `operator==` e um `GpuPassKeyHash`; `struct GpuPassSegment {GpuPassKey key; uint64_t ns;}`; `class GpuPassStats` com `void AddFrame(const std::vector<GpuPassSegment>& segments, uint64_t other_ns)`, `uint64_t frames() const`, `std::string Format(size_t limit) const` e `void Reset()`.

- [ ] **Step 1: Teste que falha**

Crie `tests/native/test_gpu_pass_stats.cpp`:

```cpp
#include "../graphics/gpu_pass_stats.h"
#include "test_main.h"
#include <string>
using namespace superman_returns::graphics;

namespace {
GpuPassKey Key(uint32_t w, uint32_t h, uint32_t colors, bool depth) { return {w, h, colors, depth}; }
}

SR_TEST(gpu_pass_stats_sums_segments_per_key_across_frames) {
  GpuPassStats stats;
  stats.AddFrame({{Key(1280, 720, 1, true), 4000000}, {Key(256, 144, 0, true), 1000000},
                  {Key(1280, 720, 1, true), 2000000}}, 500000);
  stats.AddFrame({{Key(1280, 720, 1, true), 6000000}}, 1500000);
  SR_CHECK_EQ(stats.frames(), 2u);
  const std::string text = stats.Format(8);
  // 12 ms over 2 frames = 6.00 ms per frame in 3/2 = 1.5 passes per frame
  SR_CHECK(text.find("1280x720 c1 d1: 6.00 ms") != std::string::npos);
  SR_CHECK(text.find("1.5 passes") != std::string::npos);
  SR_CHECK(text.find("256x144 c0 d1: 0.50 ms") != std::string::npos);
  SR_CHECK(text.find("other 1.00 ms") != std::string::npos);  // (0.5 + 1.5) ms over 2 frames
}

SR_TEST(gpu_pass_stats_orders_by_total_time_and_applies_the_limit) {
  GpuPassStats stats;
  stats.AddFrame({{Key(100, 100, 1, false), 1000000}, {Key(200, 200, 1, false), 3000000},
                  {Key(300, 300, 1, false), 2000000}}, 0);
  const std::string two = stats.Format(2);
  SR_CHECK(two.find("200x200") < two.find("300x300"));
  SR_CHECK(two.find("100x100") == std::string::npos);
  SR_CHECK(two.find("more") != std::string::npos);  // says one group was left out
}

SR_TEST(gpu_pass_stats_reset_and_empty_format) {
  GpuPassStats stats;
  SR_CHECK_EQ(stats.frames(), 0u);
  SR_CHECK(stats.Format(4).find("no frames") != std::string::npos);
  stats.AddFrame({{Key(1, 1, 1, false), 1000}}, 0);
  stats.Reset();
  SR_CHECK_EQ(stats.frames(), 0u);
  SR_CHECK(stats.Format(4).find("no frames") != std::string::npos);
}
```

- [ ] **Step 2: Rodar e ver falhar**

```powershell
. .\tools\dev_env.ps1
cmake --build build/tests-native
```

Expected: FAIL de compilação (`gpu_pass_stats.h` não existe).

- [ ] **Step 3: Implementar**

Crie `port/src/graphics/gpu_pass_stats.h`:

```cpp
#pragma once
// Per-pass GPU time aggregation shared by the D3D12 and Vulkan native renderers (SR_GPU_PASS_TIMING=1).
// A "pass" is a run of draws on the same render-target set; the key is API independent so the two
// backends can be compared: width x height x colour target count x has-depth.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace superman_returns::graphics {

struct GpuPassKey {
  uint32_t width = 0, height = 0, colors = 0;
  bool depth = false;
  bool operator==(const GpuPassKey&) const = default;
};
struct GpuPassKeyHash {
  size_t operator()(const GpuPassKey& key) const {
    size_t h = key.width;
    h = h * 1000003u ^ key.height;
    h = h * 1000003u ^ (key.colors * 2u + (key.depth ? 1u : 0u));
    return h;
  }
};
struct GpuPassSegment {
  GpuPassKey key;
  uint64_t ns = 0;
};

class GpuPassStats {
 public:
  // One frame: the pass segments, plus the frame's remaining GPU time (gaps between passes).
  void AddFrame(const std::vector<GpuPassSegment>& segments, uint64_t other_ns) {
    ++frames_;
    other_ns_ += other_ns;
    for (const auto& segment : segments) {
      Entry& entry = entries_[segment.key];
      entry.ns += segment.ns;
      ++entry.passes;
    }
  }
  uint64_t frames() const { return frames_; }
  void Reset() {
    entries_.clear();
    frames_ = 0;
    other_ns_ = 0;
  }
  // "gpu passes (per frame over N frames): 1280x720 c1 d1: 6.00 ms (1.5 passes) | ... | other 1.00 ms"
  std::string Format(size_t limit) const {
    if (!frames_) return "gpu passes: no frames";
    std::vector<std::pair<GpuPassKey, Entry>> sorted(entries_.begin(), entries_.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second.ns > b.second.ns; });
    const double frames = double(frames_);
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "gpu passes (per frame over %llu frames):", (unsigned long long)frames_);
    std::string text = buffer;
    for (size_t i = 0; i < sorted.size() && i < limit; ++i) {
      const auto& [key, entry] = sorted[i];
      std::snprintf(buffer, sizeof(buffer), " %ux%u c%u d%d: %.2f ms (%.1f passes) |", key.width, key.height,
                    key.colors, key.depth ? 1 : 0, double(entry.ns) / 1e6 / frames, double(entry.passes) / frames);
      text += buffer;
    }
    if (sorted.size() > limit) {
      double rest = 0;
      for (size_t i = limit; i < sorted.size(); ++i) rest += double(sorted[i].second.ns);
      std::snprintf(buffer, sizeof(buffer), " %zu more groups: %.2f ms |", sorted.size() - limit, rest / 1e6 / frames);
      text += buffer;
    }
    std::snprintf(buffer, sizeof(buffer), " other %.2f ms", double(other_ns_) / 1e6 / frames);
    return text + buffer;
  }

 private:
  struct Entry {
    uint64_t ns = 0, passes = 0;
  };
  std::unordered_map<GpuPassKey, Entry, GpuPassKeyHash> entries_;
  uint64_t frames_ = 0, other_ns_ = 0;
};

}  // namespace superman_returns::graphics
```

- [ ] **Step 4: Rodar e ver passar**

```powershell
cmake --build build/tests-native
ctest --test-dir build/tests-native --output-on-failure
.\build\tests-native\sr_native_tests.exe | Select-String "gpu_pass"
```

Expected: ctest 100% e três `PASS gpu_pass_stats_...`.

- [ ] **Step 5: Commit**

```powershell
git add port/src/graphics/gpu_pass_stats.h tests/native/test_gpu_pass_stats.cpp tests/native/CMakeLists.txt
git commit -m "feat(native): add the API-independent GPU pass aggregator" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Timestamps por passe no Vulkan

**Files:**
- Modify: `port/src/graphics/vulkan/game_renderer.h`, `game_renderer.cpp` (abrir/fechar passes)
- Modify: `port/src/graphics/vulkan/game_frame.h`, `game_frame.cpp` (query pool, colheita, agregação, log)

**Interfaces:**
- Consumes: `GpuPassStats`, `GpuPassKey`, `GpuPassSegment` (Task 1), os `vkCmdWriteTimestamp`/`vkCmdResetQueryPool`/`vkGetQueryPoolResults`/`vkCreateQueryPool` já carregados.
- Produces: uma linha de log por 120 quadros, `native Vulkan gpu passes (per frame over 120 frames): ...`, só com `SR_GPU_PASS_TIMING=1`.

Leia primeiro `GameRenderer::Record`/`Draw` (abertura em `if(open_pass_!=pass) { ClosePass(); ... vkCmdBeginRenderPass ... }`) e `ClosePass()`, e `GameFrame::RecordFrame`/`HarvestTimestamps`/`Initialize`. Aplique o desenho abaixo ao texto **atual** (estilo denso dos arquivos).

- [ ] **Step 1: Desenho**

1. `GameRenderer` ganha uma interface mínima de gancho, definida em `game_renderer.h`:

```cpp
// SR_GPU_PASS_TIMING=1: the frame owner supplies a timestamp writer; null = off (no commands recorded).
struct PassTimer {
  virtual ~PassTimer() = default;
  // Called right before vkCmdBeginRenderPass / right before vkCmdEndRenderPass of every game render pass.
  virtual void PassBegin(VkCommandBuffer command, uint32_t width, uint32_t height, uint32_t colors, bool depth) = 0;
  virtual void PassEnd(VkCommandBuffer command) = 0;
};
```

e um setter `void SetPassTimer(PassTimer* timer)`. Em `GameRenderer`, onde o render pass é aberto (logo antes de `vkCmdBeginRenderPass`) chame `if(pass_timer_) pass_timer_->PassBegin(command,pass->extent.width,pass->extent.height,pass->color_count,pass->depth_format!=VK_FORMAT_UNDEFINED);`, e em `ClosePass()` chame `if(pass_timer_) pass_timer_->PassEnd(command_);` imediatamente antes de `vkCmdEndRenderPass`. Nenhuma outra alteração em `GameRenderer`.
2. `GameFrame` implementa `PassTimer` (classe interna): um query pool próprio `pass_stamps_` com `kSlots * kMaxPassStamps` consultas (`kMaxPassStamps = 128`, 2 por passe, 64 passes por quadro; excedente é ignorado e contado). `PassBegin`/`PassEnd` escrevem `vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pass_stamps_, slot*kMaxPassStamps + n)` e guardam, por slot, `{chave, índice_begin, índice_end}`. O reset das consultas do slot (`vkCmdResetQueryPool`) é gravado no começo do `upload_` do quadro (mesmo lugar do reset dos timestamps do quadro), antes de qualquer escrita. O quadro dono do slot é identificado por `slot_swap_` (já existe).
3. Colheita: no mesmo ponto em que `HarvestTimestamps(slot)` roda (slot concluído), leia os `n` timestamps do slot com `vkGetQueryPoolResults(..., VK_QUERY_RESULT_64_BIT)`; para cada passe `i` o segmento é `(stamp_end − stamp_begin) & mask × timestampPeriod` e a chave vem do registro; o tempo "outro" do quadro é `gpu_total_do_quadro − soma(segmentos)` (use o mesmo `duration` já calculado por `SplitGpuFrame`, em ns; limite inferior 0). Alimente um `GpuPassStats`; a cada 120 quadros colhidos, `c_.Log("native Vulkan "+stats.Format(12))` e `stats.Reset()`.
4. Tudo condicionado a `SR_GPU_PASS_TIMING=1` (lido uma vez, estático): sem a variável, `SetPassTimer` não é chamado, o pool não é criado e nenhum comando é gravado. Se a criação do pool falhar, registre uma linha e continue (não aborte).

- [ ] **Step 2: Implementar e compilar**

```powershell
. .\tools\dev_env.ps1
cmake --build build/tests-vulkan
.\build\tests-vulkan\sr_vulkan_tests.exe | Select-Object -Last 1
$env:SR_BUILD_LAUNCHER = 'OFF'
cmd /c ".\build.cmd" 2>&1 | Select-Object -Last 15
Remove-Item env:SR_BUILD_LAUNCHER
```

Expected: `88 tests, 0 failed checks`; build sem `error:` (build em background; avise o usuário).

- [ ] **Step 3: Fixture de GPU com a variável ligada**

```powershell
$env:SR_GPU_PASS_TIMING = '1'
.\build\tests-vulkan\sr_vulkan_resources_test.exe --game-frame
.\build\tests-vulkan\sr_vulkan_resources_test.exe --game-frame-async
Remove-Item env:SR_GPU_PASS_TIMING
```

Expected: ambos saem com exit 0 e `validation_errors` não aumenta (se as camadas de validação não estiverem instaladas, registre isso). Os comandos novos só existem com a variável.

- [ ] **Step 4: Verificação em jogo**

Um bench Vulkan com `SR_GPU_PASS_TIMING=1` e `-Timeline -Profile` (nome `gp_smoke_vulkan`; o log copiado fica em `logs/bench_gp_smoke_vulkan.log`):

```powershell
$env:SR_GPU_PASS_TIMING = '1'
powershell -NoProfile -File tools\bench\bench_api.ps1 -Api vulkan -Name gp_smoke_vulkan -Exe "$PWD\port\out\build\win-amd64-release\superman_returns.exe" -Timeline -Profile
Remove-Item env:SR_GPU_PASS_TIMING
Select-String -Path logs\bench_gp_smoke_vulkan.log -Pattern "gpu passes" | Select-Object -Last 3
```

Expected: linhas `native Vulkan gpu passes (per frame over 120 frames): ...` com vários grupos; a soma dos grupos mais `other` ≈ `gpu_real` do relatório (tolerância de alguns por cento: o relatório mede do primeiro ao último timestamp do quadro e os grupos cobrem só os passes); ≤ 5% de quadros descartados. Se a soma divergir muito (> 15%), **pare** e reporte.

- [ ] **Step 5: Commit**

```powershell
git add port/src/graphics/vulkan/game_renderer.h port/src/graphics/vulkan/game_renderer.cpp port/src/graphics/vulkan/game_frame.h port/src/graphics/vulkan/game_frame.cpp
git commit -m "feat(vulkan): time each render pass on the GPU, grouped by target signature" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Timestamps por passe no D3D12

**Files:**
- Modify: `port/src/native_renderer/native_renderer.h`, `native_renderer.cpp`

**Interfaces:**
- Consumes: `GpuPassStats`, `ts_heap_`, `PassTimestamp`, `BeginFrameTimestamp`, `EndFrameTimestamp` (existentes).
- Produces: `native D3D12 gpu passes (per frame over 120 frames): ...` por 120 quadros, só com `SR_GPU_PASS_TIMING=1`.

- [ ] **Step 1: Desenho**

Leia `Renderer::PassTimestamp`, `BeginFrameTimestamp`, `EndFrameTimestamp` e o ponto onde o conjunto de alvos muda (`bool changed = rt_count != bound_rt_count_ || ...` antes de `OMSetRenderTargets`, em `native_renderer.cpp`). O heap `ts_heap_` tem `kTsPerFrame = 64` consultas por slot, usadas hoje para o início do quadro, pelos tempos por `cmd.pass` (`sr_native_gpu_pass_timing`) e o último para o fim. Para não interferir, com `SR_GPU_PASS_TIMING=1` o modo por `cmd.pass` fica desligado (o cvar já é falso por padrão); os carimbos novos usam as mesmas consultas por slot:

1. A cada mudança do conjunto de alvos (bloco `if (changed)` antes de `OMSetRenderTargets`), escreva um timestamp (`EndQuery` do tipo timestamp, no índice seguinte do slot) e registre `{chave, índice}`, onde a chave é `{largura do alvo 0 (ou da profundidade), altura, rt_count, ds != nullptr}`. O passe anterior termina nesse mesmo carimbo. O último passe do quadro termina no carimbo de fim de quadro que já existe.
2. Colheita em `BeginFrameTimestamp` (onde o slot concluído já é lido): segmento `i` = `(t[i+1] − t[i])` convertido com `ts_frequency_`; o tempo "outro" do quadro é `gpu_total − soma(segmentos)` (≥ 0), onde `gpu_total` é o `t[n-1] − t[0]` já usado; alimente um `GpuPassStats`; a cada 120 quadros colhidos `REXLOG_INFO("native D3D12 {}", stats.Format(12))` e `Reset()`.
3. Se o limite de 63 carimbos por slot for atingido, descarte o excedente e conte (uma linha no log com o total descartado).
4. Condicionado a `SR_GPU_PASS_TIMING=1` (lido uma vez); sem a variável nenhum comando novo é gravado e o comportamento é idêntico ao atual.

- [ ] **Step 2: Implementar, compilar e verificar**

Build em background; avise o usuário. Depois um bench D3D12:

```powershell
$env:SR_GPU_PASS_TIMING = '1'
powershell -NoProfile -File tools\bench\bench_api.ps1 -Api d3d12 -Name gp_smoke_d3d12 -Exe "$PWD\port\out\build\win-amd64-release\superman_returns.exe" -Timeline -Profile
Remove-Item env:SR_GPU_PASS_TIMING
Select-String -Path logs\bench_gp_smoke_d3d12.log -Pattern "gpu passes" | Select-Object -Last 3
```

Expected: linhas `native D3D12 gpu passes ...`; soma dos grupos + `other` ≈ `gpu_real` do relatório (≤ 15% de diferença); ≤ 5% de quadros descartados; as chaves vistas incluem as mesmas dimensões que o Vulkan (por exemplo `1280x720`), o que permite comparar. Se a soma divergir muito, **pare** e reporte.

- [ ] **Step 3: Commit**

```powershell
git add port/src/native_renderer/native_renderer.h port/src/native_renderer/native_renderer.cpp
git commit -m "feat(native): time each D3D12 render-target pass on the GPU, grouped by target signature" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Medir as duas APIs e documentar

**Files:**
- Modify: `docs/native-renderer-timeline.md`

- [ ] **Step 1: Medições**

Quatro benches (interleaved: vulkan, d3d12, d3d12, vulkan), todos com `SR_GPU_PASS_TIMING=1` e `-Timeline -Profile`, um por vez, sem builds em paralelo (avise o usuário; repita uma vez em falha de infraestrutura): nomes `gp1_vulkan`, `gp2_d3d12`, `gp3_d3d12`, `gp4_vulkan`. Rode também **um** bench Vulkan com a variável desligada e `-Gate check` e um com ela ligada e `-Gate check` (contra o `artifacts/golden` local; **não** regrave) para confirmar que a imagem não muda.

- [ ] **Step 2: Documento**

Acrescente a seção "Tempo de GPU por passe (Fase 2.1)": como ligar (`SR_GPU_PASS_TIMING=1`), a chave de passe e o que mede (do carimbo de início do passe ao de fim; "outro" = lacunas entre passes: transferências, resolves, barreiras, uploads), as linhas de log reais das últimas janelas de cada execução e uma tabela por assinatura (largura × altura, cores, profundidade) com ms por quadro e passes por quadro, **lado a lado** Vulkan e D3D12 (use as assinaturas comuns; diga quais existem só em uma API); a comparação do "outro"; o fechamento (soma dos grupos + outro contra o `gpu_real` do relatório de cada execução); o resultado do gate de imagem; ressalvas honestas (carimbos de fim de pipeline medem intervalo de execução, não ocupação; passes podem se sobrepor na GPU; os carimbos extras podem perturbar a GPU: compare `gpu_real` desta rodada com o das rodadas sem eles; uma máquina; variação de cena); e a conclusão: em quais assinaturas o Vulkan gasta mais que o D3D12 e quanto, **sem** afirmar a causa. Uma lista de candidatos de otimização de GPU *bit a bit* (eliminar passes/cópias/barreiras redundantes, reduzir bandwidth), cada um com a assinatura/estágio atacado, o teto (ms por quadro), o risco e a verificação, e o que ainda precisa ser medido (por exemplo, número de resolves/cópias por quadro). Nada além do que as tabelas mostram; sem marcadores `<...>`; releia contra as seções anteriores.

- [ ] **Step 3: Commit**

```powershell
python -m pytest tests/tools -q
git add docs/native-renderer-timeline.md
git commit -m "docs: measure the GPU time per render pass in Vulkan and D3D12" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Self-review

**Cobertura:** o agregador comum (Task 1) garante a mesma chave e o mesmo formato nas duas APIs; Tasks 2 e 3 instrumentam cada API nas fronteiras naturais de passe; Task 4 mede e documenta com o gate de imagem.

**Equivalência de imagem:** os carimbos são comandos de consulta extras e só existem com `SR_GPU_PASS_TIMING=1`; o gate de imagem com e sem a variável fecha a verificação.

**Riscos:** (1) carimbos extras podem perturbar o tempo de GPU (documentado e comparado com `gpu_real` sem eles); (2) o limite de carimbos por slot (Vulkan 64 passes; D3D12 63 carimbos) pode truncar quadros com muitos passes (contado e logado); (3) a chave por tamanho de alvo pode juntar passes diferentes de mesma assinatura (ex.: vários passes 1280×720 c1 d1): o resultado é agregado por assinatura, não por passe individual, e a conclusão só deve falar de assinaturas; (4) no Vulkan o "outro" inclui resolves e cópias fora de render pass, o que é exatamente o tipo de custo que o relatório de pesquisa manda caçar.
