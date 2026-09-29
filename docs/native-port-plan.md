# Plano de port do renderizador nativo (rexglue-native-kit → Superman Returns)

Status: **código portado às cegas, nada compilado nem executado.** Este documento foi escrito
sem os arquivos do jogo (sem `default.xex`, sem `DATA/`, sem `port/generated/`). Nenhum número
de FPS, imagem ou endereço abaixo foi medido ou confirmado neste trabalho; todo endereço de
Superman Returns vem de `docs/data/xdk_match.tsv` ou do código já existente em `port/src/` e
está marcado **não confirmado** até o dono rodar a checklist da seção 8 em casa.

Referência: [crazyriddler/rexglue-native-kit](https://github.com/crazyriddler/rexglue-native-kit)
no commit `136bc6c4` (lido em 2026-09-29). Arquivos citados como `kit:<caminho>` são desse commit.

## 1. Por que este caminho

As medições anteriores (ver [native-renderer.md](native-renderer.md) e
[performance-design.md](performance-design.md)) mostram que o gargalo é a emulação da GPU
Xenos em si (GPU a ~99%, ~10 FPS no i5-13420H + Intel UHD, ~15 FPS no i7 10ª ger. + RTX 2060).
Otimizar passes isolados rendeu no máximo 5–30%. O kit elimina a emulação: intercepta ~25
funções Direct3D do XDK linkadas estaticamente no jogo, lê o estado do `D3DDevice` guest e
desenha em D3D12 com shaders recompilados offline (XenosRecomp), sem tradutor de shaders,
cache de render targets EDRAM ou processador de comandos Xenos. Em Conan (2007) o kit chegou
a 47–58 dB de PSNR contra o Xenos e 4,0 ms/quadro. **Isso não prova nada para Superman
Returns**: o jogo é de 2006, usa uma revisão diferente do XDK (ver seção 4) e outro motor.

## 2. Inventário do kit

### 2.1 Genérico (copiado para `port/src/native_renderer/`)

| Arquivo do kit (`kit:reference/conan/port/src/native/`) | Destino | Papel | Mudanças no port |
|---|---|---|---|
| `pm4_mirror.{h,cpp}` | `pm4_mirror.{h,cpp}` | Espelho do register file a partir do segmento de comandos do XDK | só namespace; sem dependência do SDK (testado em `tests/native`) |
| `texture_decode.{h,cpp}` | idem + `xenos_tiling.h` | Decodificação CPU de texturas Xenos (tiling 2D/3D, swizzle, endian) | endereçamento tiled e swizzle extraídos para header puro testável |
| `shader_registry.{h,cpp}` | idem + `shader_container.h` | Hash do container no CreateShader do XDK | parser do cabeçalho extraído (testável); dump opcional de containers |
| `pipeline_cache.{h,cpp}` | idem | Registro/pré-compilação de PSOs | arquivo `superman_returns_pipelines.bin` |
| `native_graphics_system.{h,cpp}` | idem | `IGraphicsSystem` sem Xenos: MMIO, consumidor PM4 só de sincronização, vblank | adaptado ao SDK v0.10.0 **oficial** (ver 2.3) |
| `native_renderer.{h,cpp}` | idem | Captura nas threads guest, worker D3D12, superfícies EDRAM, resolves, texturas, PSOs, apresentação | fatos do jogo movidos para `game_profile.h`; passes do Conan viram papéis desativados |
| `hang_watchdog.{h,cpp}` | idem | Pilhas de todas as threads quando os swaps param | só namespace |
| `shaders/*.hlsl` + `*_ps.h`/`*_vs.h` | `shaders/` | Shaders auxiliares do próprio kit (blit, cópia de profundidade, alias EDRAM, FXAA, SSAO) já compilados em DXIL | copiados sem mudança (não são shaders do jogo) |
| `d3d_capture.cpp`, `native_hooks.cpp` | `native_hooks.cpp` | Hooks das funções do XDK | endereços vêm de `game_profile.h`; cada hook só compila com o endereço confirmado |

### 2.2 Específico de Conan (não copiado ou substituído)

| Item do kit | Decisão |
|---|---|
| `CONAN_PASS_HOOK` (27 funções da tabela de passes em `0x82A25A70`) e `kPassNames` | Removido. SR não tem tabela de passes conhecida; os papéis de passe em `game_profile.h` ficam `-1`, o que desliga as opções que dependem deles (SSAO, FXAA antes do HUD, partículas suaves, escala de sombra, compilação assíncrona de PSO) |
| Hook `sub_82533178` (Present do motor de Conan) | Substituído pelo hook já existente de `sub_82112050` (candidato a `D3DDevice_Swap`, ver 3.1) |
| `sub_824E7678` (cache de lookup por nome, EXP-047) | Não portado: otimização do motor de Conan |
| `projection_regs.inc` (`g_mProjectionToWorld` por shader) | Substituído por tabela vazia; gerar com `tools/shaders/gen_projection_regs.py` quando o nome da constante de SR for conhecido |
| Launcher, `settings.cpp`, `.rc`, ícone | Não portado: o projeto usa cvars `sr_*` e o F4 do runtime |
| Heurística de atlas de sombra no patch do XenosRecomp (`g_ShadowMapTextureAtlas`, literais múltiplos de 0,25/4096) | O patch é aplicado como está; a heurística só age em shaders que declaram esse sampler. Conferir no catálogo de SR (seção 6) |

### 2.3 Divergências inevitáveis: SDK oficial vs. fork do kit

O kit compila contra um **fork** do ReXGlue v0.10.0 (`kit:sdk/KIT_SDK_CHANGES.md`). Este projeto
usa o SDK oficial v0.10.0 (`f5337cd`) e as fontes de GPU em `.tools/rexglue-sdk-source`
(`tools/setup_gpu_source.ps1`). O renderer do kit usa estas APIs que só existem no fork:

| API do fork | Uso no kit | Solução aqui |
|---|---|---|
| `IGraphicsSystem::GetGammaRamp256`, `guest_frame_counter` | rampa DC_LUT no blit final; contador de swaps | métodos próprios de `NativeGraphicsSystem` (sem `override`); o renderer consulta o sistema nativo diretamente |
| `rex::perf::BenchElapsedMs`, `RegisterBenchExitCallback`, `RegisterSampledThread`, `CounterId::kApp0..3` | diagnósticos de benchmark | `sdk_compat.h`: relógio local desde o primeiro swap; callbacks e perfis viram no-op |
| `rex::perf::RegisterGpuSwapCallback` (swap do CP Xenos) e `bench_screenshot_swaps` | modo A/B quadro a quadro | `SrCommandProcessor::IssueSwap` (já existe no modo `trace`) chama o observador do renderer e grava a saída do Xenos (`CaptureGuestOutput`) nos mesmos swaps |
| `graphics_flags.h` (`unlocked_vblank_rate`) | vblank destravado | cvar própria `sr_native_unlocked_vblank_hz` |
| `gpu_null_draws` (Xenos só sincroniza) | modo intermediário "Xenos sincroniza, nativo desenha" | não existe no SDK oficial; o port só tem dois modos: nativo puro (`NativeGraphicsSystem`) e A/B (Xenos completo + nativo fora da tela) |
| Correções de runtime do fork (timer de alta resolução, fila de áudio, occlusion query atrasada, waitable swap chain) | desempenho e estabilidade em Conan | **não portadas**; podem afetar FPS/estabilidade do modo nativo. Avaliar depois das primeiras medições |

## 3. Fatos específicos do jogo (tabela da seção 2 do `kit:docs/GAME_ADAPTATION_GUIDE.md`)

Todos ficam em `port/src/native_renderer/game_profile.h`.

| Fato | Valor em Conan | O que sabemos de SR | Situação |
|---|---|---|---|
| Ponteiro global do device (`kDevicePtrAddr`) | `0x82C81A64` | desconhecido | **não é necessário**: com `0` o renderer usa o `r3` (device) das próprias chamadas D3D interceptadas. O modo `capture` ainda lista os globais que guardam o device (`device_pointer_globals`), caso se queira fixar um |
| Offsets do `D3DDevice` (fetch 0x480, consts 0x780/0x1780, bools/loops 0x2780.., decl 0x2E24, RTs 0x3090, DS 0x30A0, texturas 0x30F8, viewport 0x3160, shaders 0x318C/0x3190, anel +0x30/+0x34) | ver coluna | 93 funções `exact` em `xdk_match.tsv`, mas o `xdk_sigs.py` **mascara** os imediatos de loads/stores, então os offsets não estão provados | valores de Conan, **não confirmados**; conferir no disassembly dos setters (3.2) |
| Front buffer (global `0x82C81A68`) | argumento de Resolve/Swap | `r4` do candidato a Swap | lido do argumento, sem global |
| Resolução de saída | 1280×720 | 1280×720: `render_scale.cpp` (`sub_82611A20` modo 4 e `sub_822EB148` gravam 1280/720) | conhecido pelo código existente; confirmar no Swap |
| Resolução da cena | 1024×576 esticada | o motor grava 1280×720 como tamanho de render (`device+60`), então fator 1 | `full_scene_resolution` desligado |
| Tabela de passes (29) | `0x82A25A70` | desconhecida | papéis `-1` (seção 2.2) |
| Detecção do passe de sombras | alvos quadrados no passe 4 | desconhecida | depende da tabela de passes |
| Nome do sampler do atlas de sombras | `g_ShadowMapTextureAtlas` | desconhecido | conferir na reflexão do catálogo |
| Constante de câmera inversa | `g_mProjectionToWorld` | desconhecida | SSAO/partículas suaves desligados |
| Função Present do motor | `sub_82533178` | `sub_82112050` é o único chamador de `VdSwap` (hook em `frame_stats.cpp`) | provável `D3DDevice_Swap` do XDK; **não confirmado** |
| Loops de espera ativa (`db16cyc`) | 8 hooks | não perfilado | fora do escopo deste port |
| Cadência de vblank | 2 vblanks por quadro | o jogo roda a 30 FPS no console; cadência não medida | suposição de Conan (`vblanks_per_frame = 2`), **não confirmada** |
| Title ID / local dos shaders | `545107DA`, `shaders/shaders.stx` | `454107ED`; containers possivelmente dentro dos `.AST` (formato desconhecido; podem estar comprimidos) | `extract_shaders.py` varre todos os arquivos; se achar 0, usar o dump em tempo de execução (`sr_native_dump_shader_dir`) |
| Nomes de arquivos | `conan.exe`, `conan.cfg`, `conan_pipelines.bin` | `superman_returns.exe`, config `.toml` do runtime, `superman_returns_pipelines.bin` | renomeados |

### 3.1 Endereços dos hooks

Colunas: papel; endereço em Conan (`ref_addr`); melhor candidato em SR (`best_addr` do TSV);
`status`/`score` do `xdk_sigs.py`; como confirmar. Nenhum está confirmado. Os comandos
`q.py`, `pm4scan.py` etc. são as ferramentas do kit em `tools/re/` (seção 8 explica como
prepará-las). "Bloco D3D" = faixa `0x820F0000–0x82114000`, onde estão os 93 matches `exact`
(setters de render state, `D3D_RingAlloc` 820FC910, `D3DDevice_CreateTexture` 820FFF08,
`D3D_WriteDirtyRegisterRange` 82107520, `D3D_LoadShaderLiterals` 82108470,
`D3D_ReserveInlineConstants` 82113010). Candidatos fora dessa faixa são quase certamente
falsos.

| # | Papel (macro em `game_profile.h`) | Conan | Candidato SR | Status (score) | Como confirmar |
|---|---|---|---|---|---|
| 1 | `DrawVertices` | 82580918 | 820FBBF8 | missing (0,060) | `pm4scan.py`: a função deve montar `DRAW_INDX` auto-index `0xC0012201`; `q.py dis`: args `(dev r3, prim r4, start r5, count r6)`; muitos chamadores do jogo |
| 2 | `DrawIndexedVertices` | 82580D00 | 820FC000 | missing (0,062) | `pm4scan.py`: `DRAW_INDX` com DMA de índices `0xC0032201`; args `(dev, prim, base, startIndex, count)` |
| 3 | `DrawVerticesUP` | 825808B8 | 820FBBB0 | missing (0,583) | `q.py callees`: chama BeginVertices, memcpy e EndVertices |
| 4 | `BeginVertices` | 825803F8 | 820FB6E8 | missing (0,121) | chamada por #3; devolve em `r3` um ponteiro dentro do segmento de comandos |
| 5 | `EndVertices` | 82580898 | TSV: 82551FD8 (fora do bloco); hipótese por layout: ~820FBB90 (em Conan fica 0x20 antes de DrawVerticesUP) | missing (0,333) | `q.py callees 820FBBB0`: a chamada depois do memcpy |
| 6 | `Resolve` | 822F5028 | 8210C5F8 | missing (0,090) | escreve `RB_COPY_*` (0x2318–0x231B); chamada pela função de apresentação antes do Swap; 11 argumentos |
| 7 | `BeginTiling` | 822F3EF8 | 828801A8 (fora do bloco) | missing (0,043) | `pm4scan.py`: `SET_BIN_MASK/SELECT` (0x50/0x51/0x60–0x63). Se o jogo não usa predicated tiling, 0 chamadores: marcar como ausente (`0`) |
| 8 | `EndTiling` | 822F4480 | 8210DA98 | missing (0,047) | idem; chama Resolve por tile |
| 9 | `Clear` | 822F9EE0 | 82101998 | fuzzy (0,979) | args `(dev, Count, pRects, Flags, Color, Z(f1), Stencil)`; chama a draw interna (candidato 82102448) |
| 10 | `RingMakeSpace` (troca de segmento) | 822DF848 | 827631D8 (fora do bloco) | missing (0,172) | `q.py callees 820FC910`: a função que `D3D_RingAlloc` (exact) chama quando falta espaço |
| 11 | `RingAllocLargeSegment` | 822DF548 | 820FCF90 | missing (0,372) | idem, caminho de alocação grande |
| 12 | `ReserveInlineConstants` | 82580358 | 82113010 | **exact** (1,000) | conferir args `(dev, r4, r5, count)` e o `SET_CONSTANT` 0x2D |
| 13 | `LoadShaderLiterals` | 822F7408 | 82108470 | **exact** (1,000) | conferir o `LOAD_ALU_CONSTANT` 0x2F |
| 14 | `GpuBeginShaderConstantF4` | 822E7A48 | 82861150 (fora do bloco) | missing (0,079) | pode não ser usada por SR; procurar função do bloco D3D que devolve ponteiros em `r6`/`r7` para o anel. 0 chamadores → ausente |
| 15 | `VertexBufferUnlock` | 822EA0D8 | 820F4000 | **exact** (1,000) | conferir que só lê o objeto em `r3` |
| 16 | `IndexBufferUnlock` | 822EA1E0 | 820F4150 (ambíguo com 82769CB0) | ambiguous (1,000) | escolher o do bloco D3D; confirmar que CreateIndexBuffer (820F4040, exact) é o vizinho |
| 17 | `CreateShaderA` | 822E84C0 | 826A8050 (fora do bloco) | missing (0,132) | `q.py grep "lis r[0-9]+,4138"` (0x102A, magia do container) e `pm4scan`; recebe o container em `r3`, devolve o objeto em `r3` |
| 18 | `CreateShaderB` | 822E85D0 | 827A6130 (fora do bloco) | missing (0,167) | idem (VS/PS); em Conan são dois criadores |
| 19 | `Swap` | 822E8EB8 | TSV: 82645788 (lixo); projeto: **`sub_82112050`** | missing (0,035) | `sub_82112050` é o único chamador de `VdSwap` e chama `VdGetSystemCommandBuffer` (código recompilado já inspecionado); confirmar `r3` = device, `r4` = textura do front buffer |
| 20 | `BlockOnFence` | 822DF1D8 | 822115D8 | missing (0,240) | `q.py callers 820F33E8`: quem chama o poll em loop; args `(dev, fence)` |
| 21 | `PollGpuProgress` | 822DE050 | 820F33E8 | **exact** (1,000) | conferir retorno 1 enquanto espera |

Os setters usados apenas para confirmar offsets (seção 3.2): `SetIndices` 820F2C08 (exact),
`SetStreamSource` 820F2A50 (fuzzy), `SetTexture` 82100310 (missing 0,468),
`SetRenderTarget` 820F2CA0 (missing 0,468), `SetDepthStencilSurface` 820F2FD0 (missing 0,456),
`SetVertexShader` 820F4E88 (missing 0,312), `SetPixelShader` 820F5218 (missing 0,526),
`SetViewportF` 82102608 (missing 0,169), `CreateTexture` 820FFF08 (exact).

### 3.2 Offsets do device

Para cada offset, `q.py dis <setter>` e procurar o `stw`/`lwz` com base no device (`r3` na entrada):

| Offset (Conan) | Campo | Setter para conferir |
|---|---|---|
| 0x308C | índice | SetIndices 820F2C08 |
| 0x30A4 / 0x30E8 | streams / strides | SetStreamSource 820F2A50 |
| 0x30F8 (+4·i) e 0x480 (+24·i) | texturas / fetch constants | SetTexture (candidato 82100310) |
| 0x3090 (+4·i) / 0x30A0 | RTs / DS | SetRenderTarget / SetDepthStencilSurface |
| 0x318C / 0x3190 | shaders | SetVertexShader / SetPixelShader |
| 0x2E24 | declaração de vértices | SetVertexDeclaration (TSV sem candidato bom) |
| 0x3160 | viewport em float | SetViewportF |
| 0x780 / 0x1780 | constantes float VS/PS | qualquer `SetVertexShaderConstantF` (achar pelo `constwriters.py`) |
| +0x30 / +0x34 | ponteiro de escrita / limite do segmento | D3D_RingAlloc 820FC910 (exact) |
| 10908 / 10896 | fence corrente / ponteiro da fence concluída | BlockOnFence (#20) |

Se todos baterem, a revisão do D3D é a mesma de Conan nesses pontos e os valores do perfil
podem ser marcados confirmados. Se algum diferir, corrigir o valor em `game_profile.h`.

## 4. Revisão do XDK

Conan usa o XDK 2.0.5632. A revisão de SR é desconhecida. `tools/xex_libraries.py
game/default.xex` lista as bibliotecas estáticas declaradas no cabeçalho do XEX (nome e
versão, por exemplo `D3D9 2.0.xxxx.x`); o cabeçalho é lido sem descriptografar o executável.
Se a versão do `D3D9`/`D3DX9` for igual à de Conan, os offsets da seção 3.2 tendem a bater;
se for anterior, espere diferenças na ordem das funções e possivelmente nos offsets.

## 5. Como o código portado está protegido

- `port/src/native_renderer/game_profile.h` concentra os endereços e fatos. Cada endereço
  tem `SR_CONFIRMED_<PAPEL>` (0/1), `SR_ABSENT_<PAPEL>` (papel que o jogo não usa) e o
  comentário `// UNCONFIRMED`. O layout do device tem `kDeviceLayoutConfirmed`.
- CMake `SR_NATIVE` = `OFF` (padrão) | `CAPTURE` | `RENDERER`:
  - `OFF`: nada do renderer nativo é compilado; o executável é o mesmo de antes.
    `--sr_renderer=native` registra no log uma vez e usa `xenos`.
  - `CAPTURE`: compila o renderer; **só os hooks com endereço confirmado** são gerados. O
    renderer não pode ser ativado (`sr_renderer=native` cai para `xenos`), mas os hooks de
    observação funcionam com `--sr_native_capture=true`: chamadas por papel, amostras de
    argumentos, devices vistos, globais que apontam para o device
    (`logs/native_capture.json`, regravado a cada 300 swaps) e, com
    `--sr_native_dump_shader_dir`, os containers de shader criados pelo jogo.
  - `RENDERER`: `static_assert(kProfileConfirmed)` impede a compilação enquanto houver
    endereço não confirmado. Só então `--sr_renderer=native` ativa o renderer.
- Fallback para `xenos`, com log uma vez por causa, quando: o build não tem o renderer; o
  perfil não está confirmado; não há shaders DXIL (pacote embutido ou `sr_native_shader_dir`);
  a apresentação D3D12 ou a configuração da GPU guest do sistema nativo falha (troca
  transparente, antes de o jogo usar a GPU). Durante o jogo, cada caso não suportado (formato de
  textura, primitiva, shader ausente no pacote, pacote PM4 desconhecido) é registrado **uma
  vez** por caso. Não é possível voltar para Xenos no meio da sessão: o contrato de GPU guest
  (anel, MMIO, interrupções) pertence a um único `IGraphicsSystem` do início ao fim. Para
  diagnosticar com segurança use o modo A/B (`--sr_native_ab_mode=true`): Xenos continua
  desenhando e apresentando, o renderer nativo desenha fora da tela e, nos swaps de
  `--sr_native_ab_swaps`, as duas saídas do **mesmo quadro** vão para
  `--sr_native_dump_dir` (`s<swap>_output_1280x720.raw` e `s<swap>_xenos_output.raw`).
- Opções do kit que dependem de passes do jogo (`sr_native_fxaa`,
  `sr_native_ambient_occlusion`, `sr_native_soft_particles`, `sr_native_shadow_quality`) são
  desligadas com aviso enquanto os papéis de passe forem `-1`.
- Todas as cvars do kit foram renomeadas `native_*` → `sr_native_*` (e as opções gráficas,
  por exemplo `render_scale` → `sr_native_render_scale`), na categoria
  "Superman Returns Native" do F4.

## 6. Pipeline de shaders

`tools/shaders/` (copiado do kit, adaptado) roda **localmente contra o jogo do dono**; nenhuma
saída é comitada (`artifacts/` e `.tools/` são ignorados):

1. `tools/shaders/fetch_xenosrecomp.py`: clona reblue-XenosRecomp no pin `339af41` em
   `.tools/xenosrecomp/src`, aplica `tools/shaders/xenosrecomp/patches/0001-conan-recomp.patch`
   (o patch do kit, sem mudanças) e baixa fmt 12.1.0 e xxHash 0.8.3. Verificado aqui: o patch
   aplica no pin e o `XenosRecompCorpus` compila com clang 18 no Linux.
2. `extract_shaders.py`: varredura byte a byte dos arquivos em `game/` e da imagem decodificada
   (`logs/default_image.bin`) procurando containers (magia `0x102A11xx`), ou de um diretório
   de dumps em tempo de execução (`--dump-dir`).
3. `build_catalog.py`: tradução HLSL, compilação DXIL com DXC, reflexão →
   `artifacts/shaders/catalog.json` e `artifacts/shaders/dxil/*.dxil`.
4. `pack_shaders.py`: empacota o DXIL (formato `CNSH` do kit) para o CMake embutir no
   executável (`SR_NATIVE=RENDERER` com `artifacts/shaders/dxil` presente), ou use
   `--sr_native_shader_dir=<repo>\artifacts\shaders\dxil`.

`tools/shaders/build_corpus.ps1` faz tudo em sequência (DXC 1.9.2607 em `.tools/dxc`,
tradutor, extração, junção com o cache `.xsh` do Xenos, catálogo). O catálogo fica em
`artifacts/shaders/SHADER_CATALOG.md` (no kit ia para `docs/`; aqui nada gerado entra no Git).

Riscos: os containers podem estar comprimidos dentro dos `.AST`; a cobertura do XenosRecomp
em SR é desconhecida (em Conan o patch levou de 234 para 591 de 591). Os 123 dumps de
microcódigo já feitos com `--dump_shaders` (Xenos) **não servem** ao XenosRecomp: faltam o
cabeçalho do container e a tabela de constantes.

## 7. Compilação

**Não foi possível compilar nem linkar o executável no ambiente da nuvem**: o SDK é
Windows/clang com D3D12 e não há `port/generated/`. O que foi verificado aqui:

- `clang++ -fsyntax-only --target=x86_64-w64-mingw32` de todos os `.cpp` do renderer contra os
  headers do ReXGlue v0.10.0 (`f5337cd`), inclusive com todos os papéis forçados como
  confirmados numa cópia temporária e com `SR_NATIVE_RENDERER_BUILD=1`. Restam apenas erros de
  declarações que o `d3d12.h` do mingw não tem (`ID3D12InfoQueue1`, `ID3D12Device9`,
  `D3D12_ENCODE_BASIC_FILTER`, `SetThreadDescription`), em trechos idênticos ao kit.
- O `static_assert` do build `RENDERER` dispara com o perfil atual (não confirmado).
- Os testes de `tests/native/` e `tests/tools/` (código puro, sem SDK nem jogo).
- `tools/native_validate.ps1 -Step kit` executado com PowerShell 7 no Linux; os demais `.ps1`
  só passaram pelo parser do PowerShell.

Todo o resto precisa ser compilado e executado pelo dono. O código foi mantido o mais próximo
possível do kit (diferenças listadas na seção 2) para reduzir o risco.

## 8. Checklist para rodar em casa

`tools/native_validate.ps1 -Step <passo>` automatiza os passos marcados com ▶ e grava tudo em
`logs/native_validate/`. Anote aqui cada resultado com data, máquina e comando. Até lá, todo
número relacionado ao renderer nativo é **não medido**. O caminho do repositório não deve ter
espaços (o `bench.ps1` separa `-ExtraArgs` por espaço).

1. **Pré-requisitos**: `build.cmd` funcionando, `tools/setup_gpu_source.ps1` já rodado (o
   renderer nativo exige as fontes de GPU em processo), Python 3 e Git.
2. ▶ `-Step kit`: clona o kit no commit `136bc6c4` em `.tools/rexglue-native-kit` (só
   `tools/re`, `tools/binutils`, `tools/kitcfg.py`), escreve o `kit.env` com `PORT_DIR` = `port/`
   e instala `numpy`/`xxhash`.
3. ▶ `-Step image`: roda o jogo com `SR_DUMP_IMAGE` (já existe em `superman_returns_app.h`),
   grava `port/logs/default_image.bin` e o disassembly `port/logs/default_full.dis` com o
   `objdump` PowerPC do kit.
4. ▶ `-Step sigs`: `xdk_sigs.py match` → `logs/native_validate/xdk_match.tsv`; informa quantas
   linhas diferem de `docs/data/xdk_match.tsv`.
5. ▶ `-Step re`: para cada papel de `game_profile.h`, `q.py dis/callers/callees` do candidato
   (`re/<PAPEL>.txt`), `pm4scan.py`, busca da magia `0x102A` dos containers e o disassembly dos
   setters da tabela 3.2 (`re/setter_*.txt`). **A decisão é manual**: para cada papel, aplicar o
   critério da tabela 3.1, corrigir o endereço se preciso, pôr `SR_CONFIRMED_<PAPEL> 1` (ou
   `SR_ABSENT_<PAPEL> 1` se o jogo não usa a função) e trocar `// UNCONFIRMED` pela evidência.
   Conferir os offsets da tabela 3.2 e então `kDeviceLayoutConfirmed = true`.
6. ▶ `-Step xex`: `tools/xex_libraries.py game/default.xex` (revisão do XDK, seção 4).
7. ▶ `-Step build -Native CAPTURE`: `build.cmd` com `SR_NATIVE=CAPTURE`. Um endereço que não é
   início de função no codegen falha no link (`__imp__sub_XXXXXXXX` indefinido): também é
   evidência de endereço errado.
8. ▶ `-Step capture`: `bench.ps1` com `--sr_native_capture=true` e
   `--sr_native_dump_shader_dir=logs/native_shaders` → `logs/native_capture.json` e os
   containers. Critérios: cada papel confirmado com chamadas (draws por swap na ordem de
   grandeza de `docs/data/gpu_passes_gameplay_intel_uhd.csv`), um único device em `devices`,
   amostras de argumentos coerentes (primitiva 1–13 no `r4` dos draws, containers válidos no
   CreateShader, sem `anomalies`).
9. ▶ `-Step shaders`: `tools/shaders/build_corpus.ps1` sobre `game/` e `logs/native_shaders`;
   ver quantos containers foram traduzidos/compilados em `artifacts/shaders/SHADER_CATALOG.md`.
10. ▶ `-Step build -Native RENDERER`: exige todos os papéis e o layout confirmados
    (`static_assert`). Com `artifacts/shaders/dxil` presente, o DXIL é embutido.
11. ▶ `-Step ab`: uma execução com `--sr_renderer=native --sr_native_ab_mode=true` (Xenos
    desenha e apresenta, o nativo desenha fora da tela) grava as duas saídas nos swaps de
    `-AbSwaps` e roda `tools/native_ab_compare.py` (PSNR por swap, imagens PPM). Critério do
    kit: ≥ 40 dB. Abaixo disso, investigar com `--sr_native_trace_frame_at_s`,
    `--sr_native_dump_frame_at_s` e `--sr_native_dump_dir`.
12. ▶ `-Step bench`: `bench.ps1` em `xenos` e em `--sr_renderer=native`; avisa se a execução
    nativa caiu para `xenos` (nesse caso os números são do Xenos). Meta: média ≥ 30 e mínimo
    ≥ 27 FPS nos dois cenários no i5-13420H + Intel UHD.
13. **Estabilidade** (manual): 10 minutos no mundo aberto em `native`, sem crash nem
    travamento de áudio (regressão do XMA); `--sr_native_hang_watchdog_s` registra as pilhas se
    os swaps pararem.

## 9. O que ficou como placeholder

- Todos os endereços de SR (seção 3.1). O ponteiro global do device fica `0` (o renderer usa
  o `r3` dos hooks).
- Offsets do device: valores de Conan.
- Papéis de passe: `-1`; SSAO, FXAA antes do HUD, partículas suaves e escala de sombra
  ficam desligados.
- `projection_regs.inc`: vazio.
- Pacote de pipelines base (RCDATA 3): não existe; só o arquivo local
  `superman_returns_pipelines.bin` é gravado.
- Cadência de vblank: 2 por quadro (valor de Conan).
