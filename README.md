# Superman Returns Recomp

Projeto experimental de recompilação estática da versão Xbox 360 de **Superman Returns** para PC com [ReXGlue v0.10.0](https://github.com/rexglue/rexglue-sdk). O código-fonte C++ é gerado localmente a partir do `default.xex` fornecido pelo usuário. O projeto ainda não é uma versão jogável.

## Versão analisada

- Xbox 360 Title ID: `454107ED`; Media ID: `64A4002A`; versão do XEX: `0.0.0.1`.
- SHA-256 de `default.xex`: `C8F243ACD99DE9A91F5AE4F409721C0E954E3D5EB96861419D3DA07B8106DB2B`.
- A ISO contém `default.xex` e 12 arquivos `.AST` em `DATA/`. Os arquivos do jogo ficam em `game/`, ignorados pelo Git.

## Estado atual

O jogo compila, carrega os arquivos `.AST`, renderiza via D3D12 (plugin `xenos`) e chega à tela de título 3D (**PRESS START**) com áudio. Menus e gameplay ainda não foram testados.

### Correções específicas do jogo

- **Travamento do áudio / crash após 1–2 min em cenas 3D** ([`port/src/xma_fixes.cpp`](port/src/xma_fixes.cpp)): o mixer de áudio fica em espera ativa pelo decodificador XMA sem timeout. Sob a emulação de XMA do runtime alguns contextos nunca entregam amostras (contexto já liberado, ou buffer de saída marcado cheio), a thread de áudio travava, a fila de comandos de 1 MB (`sub_8264C540`) parava de ser esvaziada e transbordava sobre o heap. Um hook midasm em `0x826595B8` reativa o contexto e, se ele estiver liberado ou não entregar em 4 ms, faz o mixer seguir para o próximo stream.

As dicas de limites de função estão em [`port/superman_returns_manifest.toml`](port/superman_returns_manifest.toml). A maior parte foi encontrada por [`tools/find_missing_funcs.py`](tools/find_missing_funcs.py), que procura thunks de vtable e funções pequenas que a análise estática do ReXGlue não descobre. O código gerado não tem nenhum `REX_FATAL`.

## Como jogar

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

Os scripts usam `rtv` junto com `--depth_float24_convert_in_pixel_shader=true`. Sem essa opção, no modo `rtv` o céu e o fundo distante ficam pretos: o depth float24 do Xbox 360 perde precisão na conversão e o céu, desenhado na profundidade máxima, falha no teste de profundidade. Em mundo aberto o jogo roda a ~8–10 FPS com Intel UHD. Com uma GPU dedicada (NVIDIA/AMD), vale testar `run.cmd --render_target_path_d3d12=rov`, que emula a EDRAM com mais precisão.

Para medir, `F3` abre um painel com o FPS do jogo, e `set SR_LOG_FPS=1` antes de rodar grava o FPS em `logs/game.log` a cada 2 s. `F4` abre as configurações do runtime.

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

## Próximos marcos técnicos

1. Testar menus, entrada (controle e teclado) e o início do gameplay.
2. Melhorar o desempenho de GPU em hardware integrado.
3. Só considerar o jogo jogável após passar por menus e gameplay real.

Os arquivos originais do jogo não são incluídos no projeto.
