# Continuação do renderer nativo — 02/10/2026 (parte 2)

Retomada de `checkpoint2.md`. Tudo abaixo está commitado em `main` (de `e4e8e2e` a `334c874`). Medições feitas no laptop de trabalho (Intel UHD), com a sequência do `tools/bench_defaults_tmp.ps1`; não são benchmarks controlados.

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

## Como reproduzir as medições

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\bench_defaults_tmp.ps1 -Name <nome> -WorldTimeout 150 -ExtraArgs "--sr_native_profile_frame_at_s=80"
```

O script espera ~12 s entre Enter (Start) e Espaço (Start New Game); com a máquina lenta ou com dumps pesados esse tempo pode ser curto (o jogo fica no menu "Load a game?"). Capturas e logs ficam em `logs/` (ignorado pelo Git). Para ver a imagem em intervalos, `scratchpad/seq_capture.ps1` (fora do repositório) tira uma captura a cada N segundos depois do Start New Game.
