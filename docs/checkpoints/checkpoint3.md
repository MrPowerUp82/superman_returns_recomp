# Continuação do renderer nativo — 02/10/2026 (parte 2)

Retomada de `checkpoint2.md`. Tudo abaixo está commitado e publicado em `main` (de `e4e8e2e` a `af45215`, sem alterações pendentes). Medições feitas no laptop de trabalho (Intel UHD), com a sequência do `tools/bench_defaults_tmp.ps1`; não são benchmarks controlados.

## Estado atual

- Gameplay renderiza sem draws pulados (`skipped=0` em ~10M draws), sem falhas de criação de PSO.
- **30 FPS travados** (limite do próprio jogo): parado 30, andando 30, GPU entre 29% e 87%.
- Capa aparece e se mexe corretamente (confirmado pelo usuário). Sarjeta azul corrigida.
- O corpus de shaders gerado e os dumps de runtime continuam locais (`logs/`, `artifacts/`, ignorados pelo Git). Para reconstruir: `build_corpus.ps1 -DumpDir <dir> -NoSpirv` e depois `.\build.cmd`. O corpus atual tem 240 shaders (98 VS, 142 PS).

## Correções desta rodada

1. **PSOs / shaders ausentes** (`e4e8e2e`). Os skips por `pso` vinham de três VS: `A5232A3184AA617C` (lê `NORMAL1`, o tradutor abortava por falta de `vk::location`) e `C8A4458E8F870A92` / `9C34733AC0F58C2D` (sem tabela de constantes). Patches `0008-unlocated-vertex-input.patch` e `0009-empty-constant-table.patch` do tradutor, mais `extract_shaders.py` e `build_catalog.py` aceitando `ctab == 0`. Os 9 patches aplicam limpos ao pin.
2. **Capa não aparecia** (`37ef166`). É um quad list **indexado** (prim 13); `ExecDrawIndexedVertices` pulava todo `quads` sem contar (os skips sem categoria do resumo). Agora cada grupo de 4 índices vira dois triângulos, expandidos na thread do guest e capturados com o comando.
3. **Sarjeta azul** (`6fcbcbf`). O HDR começava como `(0,0,30,1)`: o 8888 limpo (`0xFF000000`) reinterpretado como 7e3 dá B=30,0 e A=1. O terreno distante (VS `4F17209D…` / PS `559F422B…`, blend alfa, fade por distância) se misturava a esse azul, e o céu desenhado depois (reverse-Z, `GREATER_EQUAL`) não cobre pixels com profundidade do terreno. `sr_native_hdr_from_ldr_black` (padrão ligado) limpa o HDR para `(0,0,0,1)` nesse caso.
4. **Capa com movimento errado** (`9d9d603`). Os vértices da capa ficam em três conjuntos de streams de 2.576 bytes que rodam a cada frame e que o jogo reescreve **sem `Unlock`**; o rastreio de sujeira só vinha dos hooks de `Unlock`, então o host usava a pose de 3 frames antes. `RefreshTrackedBuffer`: vigilância física de escrita (não pegou essas escritas) e hash por frame do guest para buffers de até 32 KB (contador `front_frame_` incrementado em `OnSwap`).
5. **Ping-pong `f3`/`f12`** (`6c4541b`, ganho de FPS). Os formatos guest 3 (`2_10_10_10_FLOAT`) e 12 (`..._AS_16_16_16_16`) são o mesmo 7e3 e ambos viram RGBA16F, mas eram duas superfícies host, e a troca entre draws opacos (`f3`) e com blend (`f12`) custava uma cópia de tela cheia, ~35 por frame. `sr_native_merge_7e3_surfaces` (padrão ligado) mapeia o formato 3 para o 12: 2 reinterpretações por frame, idle 23,3 → 28,5 FPS, andando 21,2 → 28,2. O round-trip antigo também quantizava (precisão 7e3, limite 31,875, alfa de 2 bits); a superfície fundida mantém fp16.
6. **Shadow map** (`334c874`). O perfil do frame mostrou o shadow map (`f4@640x640`, ~800 draws, VS `C7C0E381…` / PS `DD925AEB…`) com ~30% da GPU, a ~8 µs por draw; é a única superfície multisample do jogo (4×). `sr_native_msaa_samples` agora vale 1 (desligado) por padrão: ~2,4 µs por draw e o jogo chega a 30/30. Com 2 amostras deu 29,8 com a GPU em 96–98%.

## Distribuição: tradução de shaders em runtime, pacote e instalador

Pedido do usuário: distribuir o port como o [`nfsmw-nx`](https://github.com/StevensND/nfsmw-nx), com uma página de instalação que roda no navegador (modelo: https://stevensnd.github.io/nfsmw-nx-installer/). Decisões tomadas com o usuário: os shaders são traduzidos **na máquina dele, na primeira execução**; a página fica em `docs/` e o build vai para o branch `builds` mais uma Release. Detalhes e limites em [`docs/installer.md`](docs/installer.md).

Por que não gerar a biblioteca de shaders a partir do disco como o `nfsmw-nx`: o D3D12 exige DXIL **assinado** (não existe em WebAssembly) e os shaders que o jogo cria em runtime não casam com os containers do disco (por isso o corpus veio de dumps de runtime).

1. **Tradução em runtime** (`b64780e`, `port/src/native_renderer/shader_translator.{h,cpp}`). Os hooks de criação oferecem cada container ao tradutor (só quando não há biblioteca de pré-shaders); ele roda `sr_xenosrecomp.exe` (o `XenosRecompCorpus`) e o `dxc.exe` como **processos filhos** (o emissor dá `assert` em shaders que não suporta e isso não pode derrubar o jogo), em 2 threads de fundo, e guarda o DXIL em `shader_cache/<impressão>/<hash>.<vs|ps>.dxil` (a impressão resume as ferramentas). `LoadShader` espera até `sr_native_runtime_shader_wait_ms` (4000) por uma tradução em andamento e nunca guarda um "ausente" enquanto ela roda. Teste numa build de desenvolvimento: `--sr_native_preshaders=false --sr_native_ignore_pack=true`. Resultado: 188 shaders a ~190–270 ms cada, 1 falha (o VS `978B0FF62C3693C2`, o mesmo com `iPosition0` duplicado), 30 FPS; com o cache quente nada é traduzido.
2. **Build sem shaders embutidos**. `build.cmd` aceita `SR_BUILD_DIR` e `SR_EMBED_SHADERS=OFF` (opção CMake `SR_NATIVE_EMBED_SHADERS`): a build de distribuição (`port/out/build/win-amd64-dist`, ~3 min, 52 MB) não tem `.pak` nem `.srsl`.
3. **Pacote** (`tools/package_release.ps1 [-NoBuild]`, arquivos em `tools/release/`). `artifacts/release/superman_returns_win64.zip` (~32 MB): executável, `rexruntime.dll`, `rexgpu-xenos.dll`, os 4 DLLs do runtime do Visual C++ (implantação local permitida), `shader_tools/` (tradutor, `dxc.exe`, `dxcompiler.dll`, `dxil.dll`, `shader_common.h`), `run.cmd`, `run_keyboard.cmd`, `LEIAME.txt` (CRLF), licenças e `version.json`. Sem dados do jogo e sem shaders traduzidos. Verificado: um pacote montado só com o zip mais os arquivos do ISO, rodado de dentro da própria pasta (nada do repositório), traduziu os 188 shaders com as ferramentas do pacote e chegou ao gameplay.
4. **Página do instalador** (`1a43389`, `docs/index.html`, `docs/js/*`, pt/en). Lê o ISO (XDVDFS, offsets de partição 0, XGD1/2/3) ou a pasta no navegador, confere o SHA-256 do `default.xex` (`c8f243ac…06db2b`) e as 12 entradas de `DATA`, baixa o build e grava numa pasta (File System Access API) ou num `.zip` em fluxo (sem ZIP64: o pacote precisa ter menos de 4 GB, hoje ~2,3 GB). Há também "só atualizar o build" e "já tenho o zip do build". Testes: `node --test tests/web/installer.test.mjs` (6 passam, incluindo o disco real). No Chrome embutido: renderiza sem erros de console e `writePackage` + gravador de ZIP + `DecompressionStream` funcionam.
5. **Publicação** (`tools/publish_release.ps1 -Confirm`, exige o repositório sem alterações pendentes). O download direto de um asset de Release a partir do navegador **não funciona**: o último salto (`release-assets.githubusercontent.com`) não envia CORS (conferido com `curl`), enquanto `api.github.com` e `raw.githubusercontent.com` enviam `Access-Control-Allow-Origin: *`. Por isso o build vai para o branch órfão `builds` (um commit, force-push a cada versão) e a Release espelha o mesmo zip.

Publicado em 2026-10-02: página https://mrpowerup82.github.io/superman_returns_recomp/ (Pages: `main` / `docs`), Release https://github.com/MrPowerUp82/superman_returns_recomp/releases/tag/v2026.10.02-af45215 e branch `builds` (aviso "Compare & pull request" do GitHub deve ser ignorado: o branch é órfão e não deve ser mesclado). Na página publicada, o navegador baixou `version.json` e o zip de 33,5 MB e leu as 23 entradas.

## Ferramentas de diagnóstico novas (todas cvars `sr_native_*`)

- `profile_frame_at_s=<s>`: timestamp de GPU depois de cada comando de um frame; loga custo por operação, por alvo e por par (VS, PS) (linhas `native profile:`).
- `watch_blue` (+ `dump_frame_at_s`, `watch_xy`): lê o HDR de volta depois de cada draw de um frame e loga os draws que mudam o número de pixels "traço azul", o de pixels sentinela `(0,0,30)` e um pixel de sonda, com as operações não-draw entre as leituras. Repete até achar um frame com pelo menos 800 draws.
- `dump_vs=<hash>` (+ `dump_frame_at_s`, `dump_dir`): despeja todas as superfícies antes do primeiro draw de um VS e no draw seguinte (`before_*` / `after_*`). Cabeçalho do `.raw`: `u32 largura, altura, formato DXGI, pitch`. Os dumps legados de fim de passe e fim de frame não disputam mais o gatilho.
- `debug_vs=<hash>` / `debug_ps=<hash>`: streams, intervalo de vértices, primeiros/últimos vértices e constantes (`c0..c10` do VS, `c0..c40` do PS); `debug_nodraw_format=<n>`: grava todo o estado mas pula a chamada de draw dos alvos desse formato (separa custo de estado do custo de draw).
- O resumo a cada 600 frames lista os pares (VS, PS) com mais skips, e cada PSO criado loga layout host, declaração guest e estado de blend/depth (use `--sr_native_pipeline_cache=false`, senão PSOs pré-compilados não são logados).
- `shadow_scale_mul` (padrão 1.0): escala o shadow map detectado pela forma (quadrado ≥512, `f4` ou depth). O escalonamento por passe segue desativado para este jogo (o perfil não conhece o passe de sombra).

## Pendências e avisos

1. **Faixas marrons/verdes no tutorial do Jor-El** (cristais no canto inferior esquerdo, texto "GRAVITY", legenda "YOU CAN TAKE TO THE SKIES AT WILL, MY SON"). O Xenos de referência mostra o mesmo evento (desfoque radial, vinheta, facho no topo), mas só captei um frame fraco dele, então a intensidade não foi comparada no mesmo instante. Provavelmente intencional; aguarda o julgamento do usuário. Par que só existe nesse momento: VS `4536A1CD0C92C7C9` / PS `2C355C78E671ECCB` (objeto com mapa de normais, provavelmente o cristal).
2. **Qualidade da sombra sem MSAA não verificada em cena diurna** (a cena inicial é noturna). Para voltar ao MSAA do jogo: `--sr_native_msaa_samples=0`.
3. **Vigilância física de buffers**: dirtied ~260 mil buffers em ~100 s além do hash; provavelmente são escritas reais que antes ficavam desatualizadas, mas não identifiquei quais.
4. Uma reescrita de buffer pequeno **no meio do frame** ainda precisa de `Unlock`.
5. A suposição de que o hardware real também começa o HDR em preto é por resultado visual, não comprovada.
6. `sr_native_preshaders=false` foi validado (usa o pack embutido, 0 skips por shader).
7. O `.tools/xenosrecomp/src` é regerado por `fetch_xenosrecomp.py`; não rode o `build_corpus.ps1` com um shell dentro desse diretório (o re-clone falha por arquivo em uso).
8. **O instalador não foi exercitado com arquivos reais pelo navegador**: os seletores de ISO/pasta e a gravação em pasta dependem de gestos do usuário. O núcleo foi testado em Node com o disco real e no Chrome com um jogo falso em memória. Falta o usuário testar a página com o ISO ou a pasta dele.
9. `version.json` publicado tem um BOM UTF-8 (PowerShell `Set-Content -Encoding utf8`); o navegador o ignora, mas convém gravar sem BOM na próxima publicação.
10. Os quadros verdes no início do jogo são o vídeo de abertura (já existiam); não foram investigados.
11. O build publicado é derivado do código do jogo (recompilação estática): a decisão de publicá-lo foi do usuário, como no `nfsmw-nx`.

## Como reproduzir as medições

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\bench_defaults_tmp.ps1 -Name <nome> -WorldTimeout 150 -ExtraArgs "--sr_native_profile_frame_at_s=80"
```

O script espera ~12 s entre Enter (Start) e Espaço (Start New Game); com a máquina lenta ou com dumps pesados esse tempo pode ser curto (o jogo fica no menu "Load a game?"). Capturas e logs ficam em `logs/` (ignorado pelo Git). Para ver a imagem em intervalos, `scratchpad/seq_capture.ps1` (fora do repositório) tira uma captura a cada N segundos depois do Start New Game.
