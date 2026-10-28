# Benchmark da tela de título — plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Um botão "Benchmark" no launcher abre o jogo direto no sobrevoo da tela de título, sem interface, mede 60 s, mostra o resultado na tela e salva em arquivo.

**Architecture:** Regras puras (corte da interface, relógio de fases, estatísticas) num header testável. Um controlador no jogo liga as regras: conta quadros no hook de apresentação `sub_82112050`, neutraliza a entrada, grava arquivos e expõe o estado para um diálogo ImGui. Os dois renderers (D3D12 `Renderer::PrepareDraw`, Vulkan `GameRenderer::Draw`) descartam os draws depois do filtro final da cena e informam se ele foi desenhado no quadro.

**Tech Stack:** C++20 (clang), ReXGlue SDK v0.10.0 (cvars, hooks, input, ImGui), D3D12/Vulkan nativos, launcher WPF .NET 8, testes `tests/native` (SR_TEST) e `tests/launcher`.

**Spec:** `docs/superpowers/specs/2026-10-28-title-benchmark-design.md`

## Global Constraints

- Entrada: botão "Benchmark" no launcher + opção `--sr_benchmark=true`; `--sr_benchmark_exit=true` fecha sozinho após salvar (código 0 válido, 1 inválido).
- `sr_benchmark` implica pular a introdução; nunca enviar duas vezes a mesma flag (o parser do SDK rejeita e o jogo cai em "--game_data_root was not provided").
- Corte: depois do draw com PS `B397B05F4CE5D5BB` (filtro final), descartar todos os draws seguintes do quadro. Só no modo benchmark.
- Cena começou = primeiro quadro em que o filtro final foi desenhado. Aquecimento 5 s, medição 60 s.
- Quadro sem filtro final durante aquecimento ou medição = rodada inválida, motivo "a cena 3D foi interrompida (vídeo de demonstração ou carregamento)". Cena não aparece em 120 s após o boot = inválida, motivo "a cena do título não apareceu".
- Entrada do jogador neutra durante todo o modo.
- Métricas a partir dos intervalos entre chamadas do hook de apresentação `sub_82112050`: FPS médio, mínimo, 1% low, 0,1% low, tempo de quadro p50/p95/p99 e máximo, quadros, duração, válido/motivo; mais API, GPU, resolução, escala, sombra, MSAA, VSync, limite de FPS, versão.
- Arquivos: `benchmarks/benchmark-AAAAMMDD-HHMMSS.json` e `.csv` (um tempo de quadro em ms por linha) ao lado do executável.
- Não alterar simulação, limitador, qualidade ou áudio por estar em benchmark. Sem HID/SendInput.
- Textos visíveis ao usuário em pt-BR.

## Review Focus

- Jogador aperta tecla/controle durante o benchmark: o menu não deve avançar (Task 2 testa a política de entrada; Task 6 verifica no jogo).
- Launcher com renderizador Xenos selecionado: o botão deve recusar com mensagem em vez de abrir sem corte (Task 5).
- Usuário com "Pular introdução" desligado no launcher: o benchmark ainda deve pular, sem flag duplicada (Task 5).
- GPU rápida em que o vídeo de demonstração chega antes de 65 s: a rodada deve sair inválida, nunca medir o vídeo (Task 1 testa a regra; Task 6 verifica estendendo a janela).
- Pasta do executável sem permissão de escrita: mostrar o resultado na tela mesmo assim e dizer que não salvou (Task 2).

---

### Task 1: Regras puras do benchmark

**Files:**
- Create: `port/src/title_benchmark_policy.h`
- Create: `tests/native/test_title_benchmark_policy.cpp`
- Modify: `tests/native/CMakeLists.txt` (adicionar o teste em `sr_native_tests`)

**Interfaces:**
- Produces (namespace `superman_returns::benchmark`):
  - `constexpr uint64_t kSceneFilterPs = 0xB397B05F4CE5D5BBull;`
  - `struct UiCut { void BeginFrame(); bool ShouldSkip(uint64_t ps_hash); bool scene_drawn() const; };` — `ShouldSkip` devolve false para o draw do filtro e para tudo antes dele; true para todo draw depois dele no mesmo quadro. `BeginFrame` zera.
  - `enum class Phase { WaitingScene, Warmup, Measuring, Done };`
  - `struct Clock { explicit Clock(double warmup_s = 5, double measure_s = 60, double scene_timeout_s = 120); void OnFrame(double t_s, bool scene_drawn); Phase phase() const; bool valid() const; const std::string& reason() const; std::span<const double> frame_ms() const; };` — `t_s` é o tempo desde o boot; `frame_ms` só guarda intervalos dentro da medição.
  - `struct Stats { double avg_fps, min_fps, low1_fps, low01_fps, p50_ms, p95_ms, p99_ms, max_ms, seconds; uint32_t frames; };`
  - `Stats ComputeStats(std::span<const double> frame_ms);`

- [ ] **Step 1: Write the failing tests**

```cpp
SR_TEST(benchmark_ui_cut_skips_only_draws_after_the_scene_filter) {
  UiCut cut; cut.BeginFrame();
  SR_CHECK(!cut.ShouldSkip(0x8A6DD1D52752FAD6ull)); SR_CHECK(!cut.scene_drawn());
  SR_CHECK(!cut.ShouldSkip(kSceneFilterPs)); SR_CHECK(cut.scene_drawn());
  SR_CHECK(cut.ShouldSkip(0x40746066C58DA8D5ull)); SR_CHECK(cut.ShouldSkip(0));
  cut.BeginFrame(); SR_CHECK(!cut.scene_drawn()); SR_CHECK(!cut.ShouldSkip(0x40746066C58DA8D5ull));
}
SR_TEST(benchmark_clock_warms_up_then_measures_sixty_seconds) {
  Clock c; double t = 10;
  c.OnFrame(t, false); SR_CHECK(c.phase() == Phase::WaitingScene);
  c.OnFrame(t += 0.02, true); SR_CHECK(c.phase() == Phase::Warmup);
  while (t < 10.02 + 5) c.OnFrame(t += 0.02, true);
  SR_CHECK(c.phase() == Phase::Measuring);
  while (c.phase() == Phase::Measuring) c.OnFrame(t += 0.02, true);
  SR_CHECK(c.valid()); SR_CHECK(c.frame_ms().size() >= 2999 && c.frame_ms().size() <= 3001);
}
SR_TEST(benchmark_clock_invalidates_when_the_scene_stops) {
  Clock c; c.OnFrame(1, true); c.OnFrame(7, true); c.OnFrame(7.02, false);
  SR_CHECK(c.phase() == Phase::Done); SR_CHECK(!c.valid());
  SR_CHECK(c.reason() == "a cena 3D foi interrompida (vídeo de demonstração ou carregamento)");
}
SR_TEST(benchmark_clock_gives_up_when_the_title_never_appears) {
  Clock c; c.OnFrame(119, false); SR_CHECK(c.phase() == Phase::WaitingScene);
  c.OnFrame(121, false); SR_CHECK(c.phase() == Phase::Done); SR_CHECK(!c.valid());
  SR_CHECK(c.reason() == "a cena do título não apareceu");
}
SR_TEST(benchmark_stats_use_slowest_frames_for_lows) {
  std::vector<double> ms(1000, 10.0); ms[0] = 100.0; for (int i = 1; i < 10; ++i) ms[i] = 50.0;
  const auto s = ComputeStats(ms);
  SR_CHECK_EQ(s.frames, 1000u);
  SR_CHECK(std::abs(s.seconds - (990 * 0.010 + 9 * 0.050 + 0.100)) < 1e-9);
  SR_CHECK(std::abs(s.avg_fps - 1000 / s.seconds) < 1e-9);
  SR_CHECK(std::abs(s.min_fps - 10.0) < 1e-9);          // 1000 / max frame
  SR_CHECK(std::abs(s.low1_fps - 1000.0 / 55.0) < 1e-9); // mean of slowest 10 = (100+9*50)/10
  SR_CHECK(std::abs(s.low01_fps - 10.0) < 1e-9);         // slowest 1
  SR_CHECK(s.p50_ms == 10.0 && s.max_ms == 100.0);
  SR_CHECK(ComputeStats({}).frames == 0);                 // empty input: zeros, no division by zero
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build %TEMP%\sr-tests-native --target sr_native_tests` (após `tools\dev_env.ps1`)
Expected: erro de compilação, `title_benchmark_policy.h` não existe.

- [ ] **Step 3: Implement `port/src/title_benchmark_policy.h`**

Header-only, sem dependências do SDK. Lows: média dos `ceil(n * 0.01)` e `ceil(n * 0.001)` maiores tempos, convertida em FPS. Percentis por nearest-rank sobre a cópia ordenada. Fase `Warmup` começa no primeiro quadro com cena; `Measuring` quando `t - t_scene >= warmup_s`; `Done` quando `t - t_measure >= measure_s`. Quadro sem cena em `Warmup`/`Measuring` → `Done` inválido com o motivo exato acima.

- [ ] **Step 4: Run tests to verify they pass**

Run: `%TEMP%\sr-tests-native\sr_native_tests.exe`
Expected: `0 failed checks`, os 5 testes novos com PASS.

- [ ] **Step 5: Commit**

```bash
git add port/src/title_benchmark_policy.h tests/native/test_title_benchmark_policy.cpp tests/native/CMakeLists.txt
git commit -m "feat(benchmark): add title benchmark policy and stats"
```

### Task 2: Controlador do benchmark no jogo

**Files:**
- Create: `port/src/title_benchmark.h`, `port/src/title_benchmark.cpp`
- Modify: `port/src/sr_settings.cpp`, `port/src/sr_settings.h` (cvars), `port/src/skip_intro.cpp` (implicação), `port/src/frame_stats.cpp` (alimentar a cada apresentação), `port/src/superman_returns_app.h` (`OnPostSetup`), CMake de `port/` (novo `.cpp`)
- Test: `tests/native/test_title_benchmark_policy.cpp` (testes de `FormatResultJson` e da escrita com falha)

**Interfaces:**
- Consumes: `UiCut`, `Clock`, `ComputeStats`, `Stats` (Task 1).
- Produces (namespace `superman_returns::benchmark`):
  - cvars `sr_benchmark` (bool, false) e `sr_benchmark_exit` (bool, false); diagnósticos `sr_benchmark_warmup_s` (double, 5) e `sr_benchmark_measure_s` (double, 60).
  - `bool Active();` — `REXCVAR_GET(sr_benchmark)`.
  - `void NoteRendererFrame(bool scene_drawn);` — chamada pelos renderers no fim de cada quadro (atômico; o controlador usa o último valor).
  - `void OnGuestPresent();` — chamada em `sub_82112050`; avança o `Clock` com o tempo desde o boot e o último `scene_drawn`; ao chegar em `Done`, grava os arquivos uma vez.
  - `void SetGpuName(std::string name);` — renderers informam o adaptador.
  - `struct Result { Phase phase; bool valid; std::string reason; Stats stats; std::string settings_text, json_path; bool saved; };`
  - `Result Snapshot();` — para o diálogo (Task 4).
  - `std::string FormatResultJson(const Result&, std::string_view timestamp);` — pura, testável.
- Em `skip_intro.cpp`: pular quando `sr_skip_intro || sr_benchmark`.
- Em `OnPostSetup`: se `Active()`, `input_system()->SetActiveCallback([] { return false; })` depois de `InstallBenchmarkInput`, para neutralizar teclado e controle.
- Com `sr_benchmark_exit`, encerra o processo após gravar, com código 0 se válido e 1 se inválido, depois de `rex::FlushLogging()`.

- [ ] **Step 1: Write the failing tests**

`benchmark_result_json_contains_metrics_and_reason`: `FormatResultJson` de um `Result` inválido com `reason` e `Stats` conhecidos contém `"valid": false`, o motivo, `"avg_fps"` com o valor e o timestamp passado. `benchmark_result_reports_unsaved_file`: um `Result` com `saved=false` tem `json_path` vazio (o diálogo mostra "não foi possível salvar").

- [ ] **Step 2: Run tests to verify they fail** — mesmo comando da Task 1; esperado: símbolos inexistentes.

- [ ] **Step 3: Implement controlador, cvars e ligações**

`FormatResultJson` em `title_benchmark.cpp`, exportada no header e compilada também no `sr_native_tests` sem o SDK. Se isso exigir dependências do SDK, separar a formatação em `title_benchmark_format.h` header-only. Texto de configurações: valores das cvars `sr_native_api`, `window_width`×`window_height`, `sr_native_render_scale`, `sr_native_shadow_quality`, `sr_native_msaa_samples`, `vsync`, `sr_native_fps_limit`, mais o nome da GPU e a versão mostrada no título da janela. Arquivos em `<pasta do exe>/benchmarks/`; falha de escrita deixa `saved=false` sem abortar.

- [ ] **Step 4: Run tests and build the game**

Run: testes nativos (esperado `0 failed checks`) e `build.cmd` (esperado exit 0).

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(benchmark): drive the title benchmark from guest presents"
```

### Task 3: Corte da interface nos dois renderers

**Files:**
- Modify: `port/src/native_renderer/native_renderer.cpp` (`Renderer::PrepareDraw` logo após o cálculo de `ps_hash`, perto do `sr_native_skip_ps`; início de quadro onde `pass_draw_index_` é zerado; fim de quadro chama `NoteRendererFrame`), `native_renderer.h` (membro `benchmark::UiCut benchmark_cut_`)
- Modify: `port/src/graphics/vulkan/game_renderer.cpp` (`GameRenderer::Draw`: depois dos uploads e do retorno de `!draw.count`, se `benchmark::Active()` e `cut.ShouldSkip(draw.pixel_shader ? draw.pixel_shader->hash : 0)`, `return true` sem gravar o draw), `game_renderer.h` (membro `UiCut`; `BeginFrame` e `NoteRendererFrame` na fronteira de quadro do replay)
- Modify: informar `SetGpuName` onde cada API escolhe o adaptador (D3D12: descrição DXGI; Vulkan: `VkPhysicalDeviceProperties::deviceName`)

**Interfaces:**
- Consumes: `UiCut`, `kSceneFilterPs` (Task 1); `Active`, `NoteRendererFrame`, `SetGpuName` (Task 2).
- Produces: nenhum símbolo novo.

- [ ] **Step 1: D3D12** — em `PrepareDraw`, `if (benchmark::Active() && benchmark_cut_.ShouldSkip(ps_hash)) return false;` com `BeginFrame()` no início de cada quadro e `NoteRendererFrame(benchmark_cut_.scene_drawn())` antes de zerar. Os uploads do Vulkan precisam ser consumidos antes do corte (comentário em `GameRenderer::Draw`); no D3D12, `return false` em `PrepareDraw` já é o caminho de draw pulado.
- [ ] **Step 2: Vulkan** — como acima. Confirmar no log que `draw.pixel_shader->hash` do Vulkan é o mesmo hash de contêiner do D3D12 para `B397B05F4CE5D5BB` (o dump `packets.txt` do Vulkan imprime `ps=`). Se for diferente, comparar pelo mesmo campo que o D3D12 usa.
- [ ] **Step 3: Build** — `build.cmd`, esperado exit 0.
- [ ] **Step 4: Verificar no jogo, nas duas APIs** — `superman_returns.exe ... --sr_benchmark=true --sr_native_trace_swap=2500` (D3D12): no trace, nenhum `trace draw` depois do `ps B397B05F4CE5D5BB` desse quadro. Print da janela ou dump: sem logo, sem "PRESS START", sem linha superior. Vulkan: mesma checagem com `SR_VULKAN_DUMP_FRAME`.
- [ ] **Step 5: Commit**

```bash
git commit -m "feat(benchmark): hide title UI after the scene filter in D3D12 and Vulkan"
```

### Task 4: Tela de resultados

**Files:**
- Create: `port/src/title_benchmark_dialog.h`, `port/src/title_benchmark_dialog.cpp` (`rex::ui::ImGuiDialog`, persistente, mesmo padrão de `GraphicsOptionsDialog::Install`)
- Modify: `port/src/superman_returns_app.h` (`OnCreateDialogs`: instalar quando `benchmark::Active()`; apagar antes do drawer, como o diálogo gráfico)

**Interfaces:**
- Consumes: `Snapshot()`, `Result`, `Phase` (Task 2).

- [ ] **Step 1: Implement** — nada desenhado antes de `Phase::Done`. Em `Done`: janela centralizada com título "Benchmark — Superman Returns"; "Resultado válido", ou "Resultado inválido: <motivo>"; FPS médio, mínimo, 1% low, 0,1% low (uma casa decimal); tempos de quadro p50/p95/p99/máximo em ms; quadros e duração; texto de configurações; caminho do arquivo, ou "Não foi possível salvar o arquivo"; botão "Fechar". Esc/Enter também fecham. Fechar encerra o jogo pelo mesmo caminho de saída normal do app.
- [ ] **Step 2: Build e verificar** — `build.cmd`; rodada com `--sr_benchmark=true --sr_benchmark_measure_s=10`: depois de ~15 s a janela aparece sobre a cena e "Fechar" encerra o processo.
- [ ] **Step 3: Commit** — `git commit -m "feat(benchmark): show results screen"`

### Task 5: Botão no launcher

**Files:**
- Modify: `launcher/LauncherSettings.cs` (`BuildArgumentList(bool benchmark = false)` e `CreateLaunch(string baseDirectory, bool benchmark = false)`), `launcher/MainWindow.xaml` (botão "BENCHMARK" abaixo de "JOGAR"), `launcher/MainWindow.xaml.cs` (`Benchmark_Click`, mesmo fluxo de `Play_Click`)
- Test: `tests/launcher/Program.cs`

**Interfaces:**
- Produces: `IReadOnlyList<string> BuildArgumentList(bool benchmark = false)`; `ProcessStartInfo CreateLaunch(string baseDirectory, bool benchmark = false)`.

- [ ] **Step 1: Write the failing checks**

```csharp
var bench = new LauncherSettings { GraphicsApi = "d3d12", SkipIntro = false }.BuildArgumentList(benchmark: true);
Check(bench.Count(a => a.StartsWith("--sr_skip_intro=")) == 1 && bench.Contains("--sr_skip_intro=true"), "benchmark skips the intro once even when the preference is off");
Check(bench.Count(a => a.StartsWith("--sr_benchmark=")) == 1 && bench.Contains("--sr_benchmark=true"), "benchmark flag sent once");
Check(!new LauncherSettings().BuildArgumentList().Any(a => a.StartsWith("--sr_benchmark")), "normal play sends no benchmark flag");
var xenos = new LauncherSettings { GraphicsApi = "d3d12", Renderer = "xenos" };
// CreateLaunch(..., benchmark: true) com Xenos lança InvalidOperationException com
// "O benchmark precisa do renderizador Nativo (Direct3D 12 ou Vulkan)."
```

- [ ] **Step 2: Run to verify they fail** — `dotnet run --project tests/launcher/LauncherChecks.csproj`; esperado: erro de compilação ou FAIL.
- [ ] **Step 3: Implement** — o modo benchmark troca o valor de `--sr_skip_intro` (não acrescenta outro) e adiciona `--sr_benchmark=true`. Xenos é recusado com a mensagem exata acima, mostrada em `StatusText`.
- [ ] **Step 4: Run checks** — esperado: todos PASS. `tools\build_launcher.ps1` (via `build.cmd`) passa.
- [ ] **Step 5: Commit** — `git commit -m "feat(launcher): add benchmark button"`

### Task 6: Verificação de ponta a ponta e registro

**Files:**
- Modify: `docs/superpowers/specs/2026-10-28-title-benchmark-design.md` (seção de resultados da verificação)

- [ ] **Step 1: Três rodadas D3D12** — `superman_returns.exe <args do launcher> --sr_benchmark=true --sr_benchmark_exit=true`, três vezes seguidas. Esperado: código de saída 0 nas três, três JSON válidos e FPS médio dentro de ±3% entre elas. Se passar de 3%, registrar os números e o motivo provável (o voo não repete) antes de aceitar.
- [ ] **Step 2: Uma rodada Vulkan** — mesmo comando com `--sr_native_api=vulkan`; esperado: código 0 e sem interface.
- [ ] **Step 3: Detecção do vídeo** — `--sr_benchmark_measure_s=120`: esperado código 1 e motivo "a cena 3D foi interrompida (vídeo de demonstração ou carregamento)".
- [ ] **Step 4: Entrada neutra** — rodada interativa apertando Enter, Espaço e o controle durante a medição: o menu não avança e a rodada termina válida.
- [ ] **Step 5: Registrar e commitar** — números das rodadas na spec. `git commit -m "docs: record title benchmark verification"`
