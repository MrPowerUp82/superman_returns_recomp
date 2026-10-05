# tools/

Scripts de apoio ao projeto. Rode-os da raiz do repositório (por exemplo, `powershell -File tools\bench\bench_api.ps1 ...`).

## Raiz de `tools/`: compilar, preparar e publicar

Ficam aqui porque o `build.cmd`, os CMake e o código os chamam por caminho.

| Script | Para que serve |
| --- | --- |
| `dev_env.ps1` | Carrega o ambiente do Visual Studio e o clang/cmake/ninja do projeto na sessão PowerShell (`. .\tools\dev_env.ps1`), para compilar os testes. |
| `setup_gpu_source.ps1` | Baixa o código gráfico do ReXGlue v0.10.0 para `.tools/`; o renderer nativo precisa dele. |
| `setup_vulkan.ps1`, `setup_lsfg.ps1` | Baixam os cabeçalhos Vulkan v1.3.290 para `.tools/vulkan-headers`. São quase iguais; cada um é citado por um CMake (Vulkan e LSFG). |
| `build_launcher.ps1` | Compila o launcher WPF (.NET) na pasta do jogo e espelha em `artifacts/launcher`. |
| `build_vulkan.ps1`, `build_vulkan_m2.ps1` | Compilam a base Vulkan avulsa e preparam o emissor de shaders (XenosRecomp) do M2. |
| `verify_vulkan_m3.ps1` | Roda as suítes nativa, Vulkan e de texturas, os scripts de shader e os testes do launcher. |
| `native_validate.ps1` | Roteiro de validação do renderer nativo na sua máquina ([`docs/native-port-plan.md`](../docs/native-port-plan.md)). |
| `package_release.ps1`, `publish_release.ps1` | Montam o ZIP de distribuição e o publicam (branch `builds` e release do GitHub). Publicar é deliberado. |
| `clean.ps1` | Apaga o que é gerado e recriável (veja abaixo). |
| `shaders/` | Corpus de shaders: extração, tradução, empacotamento e validação (`build_corpus.ps1` é a porta de entrada). |
| `release/` | Arquivos que entram no pacote de distribuição (LEIAME, avisos, scripts de execução). |

## `bench/`: medir desempenho

| Script | Para que serve |
| --- | --- |
| `bench_api.ps1` | Roda o `bench.ps1` com os argumentos que o launcher passa para uma API (`-Api vulkan` ou `d3d12`); `-Profile` liga o perfil do Vulkan. É o ponto de partida. |
| `bench.ps1` | Inicia o jogo, começa um jogo novo e mede o FPS parado e andando. Só mede com o HUD de gameplay visível. Linhas em `logs/bench_results.csv`. |
| `bench_hud.ps1` | Detecção do HUD (duas barras finas, azul e vermelha) usada pelo `bench.ps1`; teste em `tests/tools/test_bench_hud.ps1`. |
| `vulkan_profile_summary.ps1` | Resume um log gravado com `SR_VULKAN_PROFILE=1`: custo por estágio da gravação e da captura de texturas. |
| `post_effects_check.ps1`, `post_effects_trace.py` | Teste A/B do `sr_post_effects` (bloom e raios de luz). |
| `render_scale_check.ps1` | Compara 100, 75 e 50% de resolução interna. |
| `bisect_codegen_flags.ps1` | Testa uma a uma as opções de registrador do codegen. |
| `bench_defaults_tmp.ps1` | Versão antiga do bench, das medições do [`checkpoint3`](../docs/checkpoints/checkpoint3.md). |

## `analysis/`: capturar e analisar

| Script | Para que serve |
| --- | --- |
| `capture_frame.ps1` | Captura um quadro com o RenderDoc. |
| `capture_gpu_trace.ps1`, `gpu_trace_report.py` | Capturam e resumem o trace de GPU do processador de comandos do projeto. |
| `gpu_pass_probe_report.py`, `gpu_shader_inventory.py` | Resumem o probe de passes e inventariam os dumps de shaders Xenos. |
| `rdc_dump_target.py`, `rdc_gpu_profile.py`, `rdc_inspect_pass.py`, `rdc_mesh_probe.py` | Scripts para rodar dentro do `qrenderdoc` (`--python`). |
| `find_missing_funcs.py`, `find_spin_loops.py` | Procuram funções que a análise estática não achou e laços de espera ativa no código gerado. |
| `xex_libraries.py`, `inspect_lsfg_dll.py` | Leem metadados do `default.xex` e da `Lossless.dll`, sem copiar dados para o repositório. |
| `native_ab_compare.py` | Compara dumps A/B do renderer nativo com o Xenos (PSNR). |

## Limpeza

`tools\clean.ps1` mostra, sem apagar, o que ocuparia espaço e pode ser recriado: logs e screenshots, caches e builds de teste avulsos. Para apagar, passe `-Apply`; os grupos são `-Logs`, `-Caches` e `-TestBuilds`.

Ele nunca toca em `.tools/`, `game/`, `artifacts/`, no executável do jogo (`port/out/build`), em `logs/bench_results.csv`, em `port/logs` nem nas pastas de dumps de shaders em `logs/` (`native_shaders`, `rt_corpus*`, `rt_shaders*`, `runtime_shaders`), nem em `build/vulkan-m2` e `build/vulkan-m3-runtime` (o tradutor de shaders e o cache do Vulkan em desenvolvimento). Tudo isso é entrada do `build_corpus.ps1` ou do jogo.
