# Superman Returns Recomp

Projeto experimental de recompilação estática da versão Xbox 360 de **Superman Returns** para PC com [ReXGlue v0.10.0](https://github.com/rexglue/rexglue-sdk). O código-fonte C++ é gerado localmente a partir do `default.xex` fornecido pelo usuário. O projeto ainda não é uma versão jogável.

## Versão analisada

- Xbox 360 Title ID: `454107ED`; Media ID: `64A4002A`; versão do XEX: `0.0.0.1`.
- SHA-256 de `default.xex`: `C8F243ACD99DE9A91F5AE4F409721C0E954E3D5EB96861419D3DA07B8106DB2B`.
- A ISO contém `default.xex` e 12 arquivos `.AST` em `DATA/`. Os arquivos do jogo ficam em `game/`, ignorados pelo Git.

## Estado atual

O jogo compila, carrega os arquivos `.AST`, renderiza via D3D12 (plugin `xenos`) e chega à tela de título 3D (**PRESS START**) com áudio. Menus e gameplay ainda não foram testados.

Em 2026-10-02, o renderer `native` também chegou ao início da gameplay: Superman, rua, prédios, HUD e minimapa aparecem, e o movimento com teclado foi verificado. Foram corrigidos o layout do viewport e o hook de clear com cor float4. Ainda há geometria deformada, iluminação escura e pipelines ausentes; a imagem não está validada contra o Xenos. Detalhes em [validação da gameplay nativa](docs/native-gameplay-validation.md).

### Correções específicas do jogo

- **Travamento do áudio / crash após 1–2 min em cenas 3D** ([`port/src/xma_fixes.cpp`](port/src/xma_fixes.cpp)): o mixer de áudio fica em espera ativa pelo decodificador XMA sem timeout. Sob a emulação de XMA do runtime alguns contextos nunca entregam amostras (contexto já liberado, ou buffer de saída marcado cheio), a thread de áudio travava, a fila de comandos de 1 MB (`sub_8264C540`) parava de ser esvaziada e transbordava sobre o heap. Um hook midasm em `0x826595B8` reativa o contexto e, se ele estiver liberado, se o kick não mudar nada (sem entrada ou buffer de saída cheio) ou se não entregar em 1 ms, faz o mixer seguir para o próximo stream.
- **Som engasgando quando o Jor-El começa a falar** (mesmo hook): o `XMAEnableContext` do runtime decodifica na hora, então um contexto sem dados de entrada (a voz do Jor-El vem em stream do disco) não vai entregar nada enquanto o mixer espera. A versão anterior do hook esperava até 4 ms por stream em todo passe do mixer; com a voz dividida em vários contextos isso passava de um quadro de áudio (5,3 ms), o mixer ficava atrás do tempo real e só o som engasgava. Agora o hook desiste assim que um kick não faz progresso.

As dicas de limites de função estão em [`port/superman_returns_manifest.toml`](port/superman_returns_manifest.toml). A maior parte foi encontrada por [`tools/find_missing_funcs.py`](tools/find_missing_funcs.py), que procura thunks de vtable e funções pequenas que a análise estática do ReXGlue não descobre. O código gerado não tem nenhum `REX_FATAL`.

## Instalar sem compilar

Quem não quer compilar pode usar a página do instalador (GitHub Pages deste repositório): ela lê o seu ISO ou a sua pasta do jogo **no navegador**, confere o `default.xex`, baixa o build pré-compilado e grava tudo em uma pasta ou em um `.zip`. O pacote não contém dados do jogo nem shaders traduzidos: os shaders são traduzidos no seu PC na primeira execução e ficam em cache. Detalhes, limites e como gerar e publicar um build em [`docs/installer.md`](docs/installer.md).

## Como jogar

O launcher com GUI permite escolher GPU, resolução, controles e opções gráficas antes
de iniciar. Compile-o com `powershell -File tools/build_launcher.ps1` e abra
`artifacts/launcher/SupermanReturnsLauncher.exe`. A versão portátil inclui .NET;
detalhes em [launcher/README.md](launcher/README.md). O pacote de distribuição também
inclui o launcher. A seleção de GPU exige o executável do jogo da mesma build.

Depois de compilar com `build.cmd`:

| Script | Uso |
| --- | --- |
| `run.cmd` | Controle de Xbox (ou compatível com XInput/SDL). Conecte-o antes de abrir o jogo. |
| `run_keyboard.cmd` | Teclado e mouse. O runtime emula um controle de Xbox 360 e o mouse move a câmera. |

Os dois aceitam opções extras, por exemplo `run.cmd --fullscreen`. O log fica em `logs/game.log`.

### Desempenho

O jogo roda a 30 FPS no Xbox 360. Aqui o gargalo é a emulação da GPU do Xbox 360 (Xenos): com GPU integrada, ela fica a ~98% nas cenas 3D. Medições num i5-13420H com Intel UHD:

| Modo de render (`--render_target_path_d3d12`) | Vídeos de abertura | Cena 3D (tela de título) |
| --- | --- | --- |
| `rov` (padrão do runtime) | 30 FPS | ~2 FPS |
| `rtv` (padrão do `run.cmd`) | ~16 FPS | ~10 FPS |

O jogo já aplica por padrão, em `port/src/superman_returns_app.h`, as opções de GPU medidas neste hardware: `render_target_path_d3d12=rtv`, `depth_float24_convert_in_pixel_shader=true` (sem ela o céu e o fundo ficam pretos no `rtv`) e `native_stencil_value_output_d3d12_intel=true` (dica do Xenia para GPUs Intel, [xenia-canary#542](https://github.com/xenia-canary/xenia-canary/issues/542): 7,6 → 10,2 FPS na rua). Qualquer opção passada na linha de comando prevalece.

Para medir, `F3` abre um painel com o FPS do jogo, e `set SR_LOG_FPS=1` antes de rodar grava o FPS em `logs/game.log` a cada 2 s. `F4` abre as configurações do runtime.

#### Presets (`sr_preset`)

`--sr_preset=quality` (padrão) mantém o visual atual; `--sr_preset=performance` hoje não muda nada (o `sr_post_effects=false` foi retirado do preset após a validação abaixo); `--sr_preset=custom` não altera nenhuma opção. O preset só preenche opções que continuam no padrão: uma opção passada na linha de comando, no ambiente ou no `.toml` prevalece. `sr_render_scale` ainda é experimental e nenhum preset o altera. Detalhes e limitações em [`docs/performance-design.md`](docs/performance-design.md).

#### Pós-processamento (`sr_post_effects`, experimental, não validado)

`--sr_post_effects=false` (padrão `true`, exige reiniciar) desliga a cadeia de bloom/raios de luz. Exige o backend D3D12 em processo (`tools/setup_gpu_source.ps1` antes do `build.cmd`); sem ele a opção é ignorada com aviso no log. Com a opção desligada, o processador de comandos do projeto pula os quads (primitiva 13 = *quad list*) das superfícies menores que a cena (640, 320, 160 e 80 pixels de largura) e o resolve que segue cada um. Os passes na superfície de 1280 (entre eles a composição final) e o HUD continuam. A lista fica em [`port/src/native_renderer/post_effects.h`](port/src/native_renderer/post_effects.h) e foi derivada do trace [`docs/data/gpu_groups_gameplay_intel_uhd.csv`](docs/data/gpu_groups_gameplay_intel_uhd.csv). **Validado em 2026-09-29 (i5-13420H + Intel UHD, `post_effects_check.ps1`, 2 pares): nenhum ganho de FPS (−1% parado, −6% andando, dentro do ruído) e imagem corrompida com a opção desligada (ruído na tela: os resolves pulados deixam lixo nas texturas que a composição final lê). Não use `false` até a opção ser refeita.** O teste anterior que pulou todos os draws de primitiva 13 (inclusive os de 1280 e o HUD) deu 8,4 FPS contra 7,1, com imagem não verificada; não é uma estimativa desta opção.

Para validar em casa: `powershell -File tools\post_effects_check.ps1 -Pairs 2 -Trace`. O script roda o `tools\bench.ps1` com a opção ligada e desligada, compara os screenshots (`logs\post_effects_<nome>_*_diff.png`), confere no log quantos passes foram pulados e, com `-Trace`, lista a ordem dos passes de um quadro com `tools\post_effects_trace.py`. Se a imagem quebrar (tela preta, brilho congelado ou lixo), o relatório do trace indica quais passes mantidos leem as texturas que deixaram de ser atualizadas.

### Desenvolvimento do renderizador próprio

O modo experimental `--sr_renderer=trace` já usa um processador de comandos do projeto. Ele registra draws, cópias, swaps, hashes de shaders e estados gráficos, mas **ainda delega a renderização ao backend Xenos D3D12**. É uma ferramenta de migração, sem ganho de FPS esperado. O modo padrão é `native` (veja abaixo).

Para compilar o modo de captura, execute `powershell -File tools\setup_gpu_source.ps1` antes de `build.cmd`. O script baixa somente o código gráfico do ReXGlue v0.10.0 para `.tools/`. Para capturar 120 quadros de gameplay após o início de um jogo novo, execute `powershell -File tools\capture_gpu_trace.ps1 -Name gameplay -Gameplay -Frames 120`. O script fecha o processo que iniciou e grava o CSV bruto, o resumo e uma imagem de referência em `logs/`. Veja a [análise e os critérios de migração](docs/native-renderer.md).

#### Ferramentas de otimização para rodar em casa

- `powershell -File tools\bisect_codegen_flags.ps1`: testa cada opção de registrador do codegen (`cr_as_local`, `ctr_as_local`, `xer_as_local`, `reserved_as_local`, `non_argument_as_local`) sozinha — codegen, `build.cmd`, boot com `--sr_skip_intro=true` e `tools\bench.ps1` se bootar — e grava `logs\codegen_bisect.csv`. Restaura o manifesto e recompila no final. Ligadas juntas, elas deixaram o jogo sem nenhum quadro.
- `python tools\find_spin_loops.py`: lista em `port\generated\default\` laços curtos de espera ativa (com `db16cyc` ou que só releem memória e comparam) em `logs\spin_loops.csv`, candidatos a hooks de espera real. É análise estática: confirmar com um perfil antes de criar hooks.

### Teclado e mouse (padrão)

Os atalhos vêm do ReXGlue SDK v0.10.0. Cada botão aceita várias teclas separadas por vírgula.

| Controle Xbox 360 | Teclado / mouse |
| --- | --- |
| Analógico esquerdo | `W` `A` `S` `D` |
| Analógico direito (câmera) | Mouse ou setas `↑` `↓` `←` `→` |
| Clique no analógico esquerdo (L3) | `F` |
| Clique no analógico direito (R3) | `K` |
| A | `Espaço` ou `;` |
| B | `Backspace` ou `'` |
| X | `L` |
| Y | `P` |
| LT (gatilho esquerdo) | `Q` ou `I` |
| RT (gatilho direito) | `E` ou `O` |
| LB (ombro esquerdo) | `1` |
| RB (ombro direito) | `3` |
| D-pad | `Shift` + setas |
| Start | `Enter` ou `X` |
| Back | `Tab` ou `Z` |

As ações de cada botão são as do jogo original no Xbox 360. Para mudar um atalho, passe a opção correspondente, por exemplo:

```bat
run_keyboard.cmd --keybind_a=Space,LMB --keybind_x=RMB --mnk_sensitivity=1.5
```

Opções disponíveis: `--keybind_a`, `_b`, `_x`, `_y`, `_left_trigger`, `_right_trigger`, `_left_shoulder`, `_right_shoulder`, `_lstick_up/down/left/right/press`, `_rstick_up/down/left/right/press`, `_dpad_up/down/left/right`, `_back`, `_start`, `_guide`. As teclas aceitam letras e números, `Space`, `Return`, `Tab`, `Backspace`, `Escape`, setas (`Up`, `Down`, `Left`, `Right`), combinações com `Shift+`/`Control+`/`Alt+` e os botões do mouse `LMB`, `RMB`, `MMB`. Sem `--mnk_mouse`, a câmera fica só nas setas. `--mnk_sensitivity` ajusta a velocidade do mouse (padrão `1.0`).

## Estrutura

- `port/`: projeto CMake e manifesto ReXGlue.
- `game/`: arquivos extraídos da ISO, mantidos apenas localmente.
- `.tools/`: ferramentas e SDK baixados localmente.
- `build.cmd`: configura e compila a versão Windows x64.
- `run.cmd` / `run_keyboard.cmd`: iniciam o jogo com controle ou com teclado e mouse.
- `tools/`: scripts de análise do executável.

## Preparação no Windows

1. Instale Visual Studio Build Tools com a carga de trabalho C++ e o Windows SDK.
2. Extraia o [ReXGlue SDK v0.10.0 para Windows](https://github.com/rexglue/rexglue-sdk/releases/tag/v0.10.0) em `.tools/rexglue-sdk/`, deixando `rexglue.exe` em `.tools/rexglue-sdk/win-amd64/bin/`.
3. Extraia o pacote portátil [Clang/LLVM 23.1.2 x86_64 Windows](https://github.com/llvm/llvm-project/releases/tag/llvmorg-23.1.2) em `.tools/`, deixando `clang++.exe` em `.tools/clang+llvm-23.1.2-x86_64-pc-windows-msvc/bin/`.
4. Extraia sua própria ISO com [xdvdfs](https://github.com/antangelo/xdvdfs): copie `default.xex` para `game/default.xex` e `DATA/` para `game/DATA/`.
5. Execute `build.cmd` na raiz do projeto. O executável, quando compilado, fica em `port/out/build/win-amd64-release/`.

O `build.cmd` usa o manifesto para regenerar o C++ automaticamente. O diretório `port/generated/default/` é descartável e não deve ser editado diretamente. Ajustes para o jogo devem ficar no manifesto, em `port/src/` ou em uma etapa de patch reproduzível.

### Renderizador nativo (padrão, experimental)

`port/src/native_renderer/` traz o renderizador D3D12 do [rexglue-native-kit](https://github.com/crazyriddler/rexglue-native-kit), que intercepta as funções Direct3D do XDK e desenha sem emular a GPU Xenos. **Ele é o padrão do projeto**: o `build.cmd` compila com `SR_NATIVE=RENDERER` (e baixa o código de GPU com `tools/setup_gpu_source.ps1` na primeira vez) e `sr_renderer` vale `native`. Se o renderer não puder ser usado (build sem o código de GPU, sem a biblioteca de shaders, ou falha ao iniciar), o jogo volta sozinho para `xenos` com um aviso `sr_renderer=native: ...; using the xenos backend` no log. Para usar o backend antigo: `run.cmd --sr_renderer=xenos`; para compilar sem o renderer: `set SR_NATIVE=OFF` antes do `build.cmd`. O renderer precisa da biblioteca de pré-shaders (abaixo). O plano, a lista de endereços e a checklist para validar em casa (`tools/native_validate.ps1`) estão em [`docs/native-port-plan.md`](docs/native-port-plan.md). Os testes que não precisam do jogo ficam em `tests/`.

#### Pré-shaders (`sr_native_preshaders`)

Como no [nfsmw-nx](https://github.com/StevensND/nfsmw-nx), os shaders do jogo são traduzidos **antes** de jogar e ficam em um arquivo local, `superman_returns_shaders.srsl`, ao lado do `superman_returns.exe`. Ele guarda cada container original (do seu jogo) junto com o DXIL já compilado. Durante o jogo, o renderer reconhece cada shader comparando o container que o Direct3D do XDK recebe com esses originais, em vez de calcular um hash da memória do jogo na hora do desenho (o Direct3D altera essas cópias, e por isso os hashes nunca batiam).

- Gerar: `powershell -File tools\shaders\build_corpus.ps1` (passo 6 roda `tools/shaders/make_preshaders.py` e copia a biblioteca para cada `superman_returns.exe` em `port/out/build`). Um build `SR_NATIVE=RENDERER` também a gera ao lado do executável.
- `--sr_native_preshaders=true` (padrão): usa a biblioteca se ela existir; sem ela, volta ao pack embutido e aos hashes antigos.
- `--sr_native_preshaders_path=<arquivo>`: outro caminho para a biblioteca.
- `--sr_native_pipeline_cache=true` (padrão): pré-compila no início os pipelines já vistos (`superman_returns_pipelines.bin`), para evitar travadas na primeira vez.
- Diagnóstico: com `--sr_native_dump_shader_dir=logs\native_shaders`, os containers não reconhecidos vão para `logs\native_shaders\unmatched\`; `python tools\shaders\make_preshaders.py --diagnose logs\native_shaders\unmatched` mostra o original mais próximo e as palavras que diferem.

A biblioteca contém dados derivados do jogo: não a envie para o Git.

## Próximos marcos técnicos

1. Capturar e classificar os comandos, shaders e passes gráficos do título e do mundo aberto.
2. Desenvolver um renderizador específico para o jogo e comparar imagem e FPS com o backend Xenos. A arquitetura e os critérios de aceite estão em [`docs/native-renderer.md`](docs/native-renderer.md).
3. Testar menus, entrada e gameplay real; só considerar o jogo jogável após essa validação.

Os arquivos originais do jogo não são incluídos no projeto.
