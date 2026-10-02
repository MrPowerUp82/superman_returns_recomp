# Checkpoint: Superman Returns Recomp — Renderizador Nativo Direct3D 12

## Atualização 02/10/2026 — pré-shaders e identificação de shaders

**Onde a sessão anterior parou** (`checkpoint1.md`): o build `RENDERER` já rodava, mas todos os draws eram pulados (`p0:0/Nshader`). Os 120 hashes calculados na hora do desenho, a partir das cópias dos containers dentro dos objetos de shader (+40/+872), não batiam com nenhum dos 167 do catálogo. Motivo: o Direct3D do XDK altera essas cópias, e dentro dos criadores `sub_820F5840`/`sub_820F6690` os campos de tamanho do container no MemStream (`r4+20`) estão zerados (o `sub_820F9C78` só os restaura depois).

**O que mudou** (como no [nfsmw-nx](https://github.com/StevensND/nfsmw-nx)):

1. **Biblioteca de pré-shaders** `superman_returns_shaders.srsl` (`port/src/native_renderer/shader_library.h`, gerada por `tools/shaders/make_preshaders.py`). Ela guarda cada container original + o DXIL e fica ao lado do exe.
2. **Identificação na criação**, antes de o Direct3D mexer no container: os hooks dos criadores comparam o container do MemStream com os originais. Funciona com os campos de tamanho zerados (match `body`), com o header completo (match `exact`) e, no fallback da hora do desenho, pelo microcódigo (match `microcode`, só quando há um único candidato). Toda comparação é confirmada byte a byte.
3. **Fallback na hora do desenho** sem recalcular a cada draw: um objeto não reconhecido é examinado só uma vez e registrado no log (`shader object XXXXXXXX (vs|ps) not recognised`).
4. **Opções**: `sr_native_preshaders` (padrão `true`) e `sr_native_preshaders_path`. Os DXIL são procurados nesta ordem: `sr_native_shader_dir` → pré-shaders → pack embutido → `artifacts/shaders/dxil`.
5. Removidos o papel `CreateShaderOuter`, que nunca teve hook (quebrava `tests/native`), e o dump temporário `logs\inline_*.bin`.

**Próximos passos no Windows:**

```powershell
# 1. Corpus + biblioteca (passo 6 novo; instala ao lado dos exes em port/out/build)
powershell -File tools\shaders\build_corpus.ps1
python tools\shaders\make_preshaders.py --verify artifacts\shaders\superman_returns_shaders.srsl

# 2. Build RENDERER (também gera a biblioteca ao lado do exe)
powershell -File tools\native_validate.ps1 -Step build -Native RENDERER

# 3. Smoke test com dump dos não reconhecidos
.\port\out\build\win-amd64-release\superman_returns.exe --game_data_root=game --sr_renderer=native --sr_skip_intro=true --sr_native_dump_shader_dir=logs\native_shaders --log_file=logs\game.log
Select-String logs\game.log -Pattern "pre-shaders|exact|body match|microcode|not recognised"
```

**O que verificar no log:**
- `pre-shaders: N shaders from ...`: a biblioteca carregou.
- `shader XXXXXXXX = HASH.vs (CreateShaderA, body match; ...)`: os contadores `exact`/`body` devem subir na criação.
- Se aparecer `not recognised`: rode `python tools\shaders\make_preshaders.py --diagnose logs\native_shaders\unmatched`. O resultado mostra o original mais próximo e as palavras que diferem; se o microcódigo for encontrado num deslocamento diferente, ajuste a leitura em `ResolveContainer`/`ResolveInline` (`shader_registry.cpp`).
- Os draws devem passar de `p0:0/Nshader` para valores diferentes de zero.

Testes sem o jogo: `cmake -S tests/native -B build/tests-native && cmake --build build/tests-native && build\tests-native\sr_native_tests` (44 OK) e `python -m pytest tests/tools` (38 OK).

---

## Histórico (01/10/2026)

**Data:** 01/10/2026  
**Status Atual:** Fases 1 e 2 **100% Concluídas**. Pronto para iniciar a **Fase 3 (Build CAPTURE & Coleta de Shaders)** no novo computador.

---

## 1. Resumo do Progresso & Descobertas Técnicas

### 1.1 Versão do XDK e Disassembly (Fase 1)
- **Versão do XDK Identificada:** `2.0.3529.0` (LTCG build, diferente de Conan que usava `2.0.5632.0`).
- **Disassembly Base:** `port/logs/default_image.bin` e `port/logs/default_full.dis` gerados via `powerpc-none-elf-objdump.exe`.
- **Bloco Direct3D:** Localizado no intervalo `0x820F0000`–`0x82114000`. Candidatos do TSV fora desta faixa eram funções do motor do jogo (falsos positivos).

### 1.2 Hooks de Funções D3D Confirmados (Fase 2)
Todos os 21 papéis necessários para o renderizador nativo foram inspecionados, validados e confirmados em `port/src/native_renderer/game_profile.h`:

| Papel (Role) | Endereço Confirmado | Status / Evidência |
|---|---|---|
| `DRAW_VERTICES` | `0x820FBBF8` | `SR_CONFIRMED 1` — sub_820FBBF8, 3 args (dev, prim_type, count) |
| `DRAW_INDEXED_VERTICES` | `0x820FC000` | `SR_CONFIRMED 1` — sub_820FC000, 5 args |
| `DRAW_VERTICES_UP` | `0x820FBBB0` | `SR_CONFIRMED 1` — sub_820FBBB0, 5 args, desenha sem buffer pré-alocado |
| `BEGIN_VERTICES` | `0x820FB6E8` | `SR_CONFIRMED 1` — sub_820FB6E8, alocação de inline vertices |
| `END_VERTICES` | `0x820FBBA0` | `SR_CONFIRMED 1` — sub_820FBBA0, fecha bloco de inline vertices |
| `RESOLVE` | `0x8210C5F8` | `SR_CONFIRMED 1` — sub_8210C5F8, 11 args, grava registradores RB_COPY (0x2318) |
| `BEGIN_TILING` | `0x8210D588` | `SR_CONFIRMED 1` — sub_8210D588, configura binning e limpa tiles |
| `END_TILING` | `0x8210DA98` | `SR_CONFIRMED 1` — sub_8210DA98, resolve por tile EDRAM |
| `CLEAR` | `0x82101998` | `SR_CONFIRMED 1` — sub_82101998, unpack de cores e clear draw |
| `RING_MAKE_SPACE` | `0x820FD8C0` | `SR_CONFIRMED 1` — sub_820FD8C0, chamado por RingAlloc (820FC910) em wrap |
| `RING_ALLOC_LARGE` | `0x820FCF90` | `SR_CONFIRMED 1` — sub_820FCF90, alocação de segmentos grandes |
| `RESERVE_INLINE_CONSTANTS` | `0x82113010` | `SR_CONFIRMED 1` — sub_82113010, emite opcode SET_CONSTANT (0x2D) |
| `LOAD_SHADER_LITERALS` | `0x82108470` | `SR_CONFIRMED 1` — sub_82108470, emite opcode LOAD_ALU_CONSTANT (0x2F) |
| `GPU_BEGIN_SHADER_CONSTANT_F4`| `0x82861150` | `SR_CONFIRMED 1`, `SR_ABSENT 1` — Ausente no XDK 2.0.3529 LTCG |
| `VERTEX_BUFFER_UNLOCK` | `0x820F4000` | `SR_CONFIRMED 1` — sub_820F4000, unlock de vertex buffer |
| `INDEX_BUFFER_UNLOCK` | `0x820F4150` | `SR_CONFIRMED 1` — sub_820F4150, unlock de index buffer |
| `CREATE_SHADER_A` | `0x820F5840` | `SR_CONFIRMED 1` — sub_820F5840, CreateVertexShader |
| `CREATE_SHADER_B` | `0x820F6690` | `SR_CONFIRMED 1` — sub_820F6690, CreatePixelShader |
| `SWAP` | `0x82112050` | `SR_CONFIRMED 1` — sub_82112050, único chamador de VdSwap |
| `BLOCK_ON_FENCE` | `0x820FCB30` | `SR_CONFIRMED 1` — sub_820FCB30, laço de espera em fence de GPU |
| `POLL_GPU_PROGRESS` | `0x820F33E8` | `SR_CONFIRMED 1` — sub_820F33E8, leitura de status da GPU |

### 1.3 Layout do `D3DDevice` no XDK 2.0.3529 Confirmado
Mapeado através da análise estática das funções setters do XDK (`sub_820F2A50`, `sub_820F2C08`, `sub_820F2CA0`, `sub_820F2FD0`, `sub_82100310`, `sub_82102608`):

- `fetch_constants`: `0x400` (1024 bytes, 32 estágios × 24 bytes)
- `ring_write`: `0x28` (+40 em decimal)
- `ring_limit`: `0x2C` (+44 em decimal)
- `fence_completed_ptr`: `10768` (`0x2A10`)
- `fence_current`: `10780` (`0x2A1C`)
- `index_buffer`: `0x2F84` (12164)
- `render_targets`: `0x2F88` (12168, base do array de 4 RTs)
- `depth_stencil`: `0x2F98` (12184)
- `stream_buffers`: `0x2F9C` (12188, 16 buffers de vértice)
- `stream_strides`: `0x2FE0` (12256)
- `textures`: `0x2FF0` (12272, 26 texturas)
- `shader_a` (VS): `0x3080` (12416)
- `shader_b` (PS): `0x3084` (12420)
- `viewport`: `0x3090` (12432)
- `kDeviceLayoutConfirmed = true;`

### 1.4 Suporte a Criadores de Shaders
- Atualizado `port/src/native_renderer/shader_registry.cpp`: os criadores internos do XDK 2.0.3529 recebem `(device, container)` nos registradores `(r3, r4)`. O registro agora inspeciona ambos para garantir que todos os containers de shaders sejam capturados.

---

## 2. Como Transferir para o Novo Computador

### Opção A: Via Git (Recomendada)

1. **No PC Atual, envie as alterações de código para o repositório:**
   ```powershell
   git add port/src/native_renderer/game_profile.h port/src/native_renderer/shader_registry.cpp CHECKPOINT_NATIVE_RENDERER.md
   git commit -m "feat(native_renderer): confirm XDK 2.0.3529 hooks and device layout"
   git push origin main
   ```

2. **Copie manualmente os arquivos não rastreados pelo Git (que estão no `.gitignore`):**
   - **`game/`** (ESSENCIAL): Contém o `default.xex` e toda a pasta de dados do jogo. Sem ela o jogo não roda.
   - **`.tools/`** (OPCIONAL mas poupa downloads): Contém o compilador LLVM/Clang (`clang+llvm-23.1.2-x86_64-pc-windows-msvc`), o `rexglue-sdk`, e os binários auxiliares. Se não copiar, veja a Seção 3 para reinstalá-los.

3. **No Novo PC:**
   ```powershell
   git clone https://github.com/MrPowerUp82/superman_returns_recomp.git
   cd superman_returns_recomp
   # Cole a pasta game/ e a pasta .tools/ na raiz do projeto
   ```

### Opção B: Cópia Direta de Pasta (Pendrive / SSD Externo / Rede / Nuvem)
Copie a pasta inteira `superman_returns_recomp` para o novo PC. Dessa forma, todos os binários, assets e arquivos de log acompanham a cópia diretamente.

---

## 3. Pré-requisitos de Ambiente no Novo PC

Certifique-se de que o novo PC possua:
1. **Sistema Operacional:** Windows 10 ou 11 (64-bit).
2. **GPU:** Placa de vídeo com suporte nativo a DirectX 12 (Direct3D 12 Feature Level 11_0 ou superior).
3. **Visual Studio 2022:**
   - Carga de trabalho: *"Desenvolvimento para desktop com C++"*.
   - Componentes individuais: MSVC v143, Windows 10/11 SDK, Ferramentas CMake para Windows, Ninja.
4. **Python 3.10+** instalado e adicionado ao `PATH`:
   - Instale as bibliotecas necessárias abrindo o terminal:
     ```powershell
     pip install numpy xxhash pytest
     ```
5. **Git para Windows** instalado.

---

## 4. Configuração Inicial no Novo PC

Abra o **PowerShell** na raiz do projeto:

```powershell
# 1. Se clonou pelo git e não copiou .tools/rexglue-sdk-source:
powershell -File tools\setup_gpu_source.ps1

# 2. Configurar o rexglue-native-kit:
powershell -File tools\native_validate.ps1 -Step kit

# 3. Validar a consistência do perfil D3D e dos testes unitários:
python -m pytest tests/tools/test_game_profile.py
```
*(Todos os 3 testes devem passar com `3 passed`).*

---

## 5. Roteiro de Execução: Fases 3 a 6

### Passo 1: Compilar a Versão CAPTURE (Fase 3)
Compila o executável configurado para interceptar todas as chamadas Direct3D e registrar os containers de shaders carregados pelo jogo:
```powershell
powershell -File tools\native_validate.ps1 -Step build -Native CAPTURE
```

### Passo 2: Executar Captura de Shaders em Jogo (Fase 3)
Execute o jogo por alguns minutos, navegue pelos menus e jogue um trecho de gameplay para que todos os shaders sejam ativados:
```powershell
powershell -File tools\native_validate.ps1 -Step capture
```
*Ou execute diretamente com as flags:*
```powershell
.\port\out\build\win-amd64-release\superman_returns.exe --game_data_root="game" --sr_native_capture=true --sr_native_dump_shader_dir="logs/native_shaders"
```
**O que verificar:**
- O arquivo `logs/native_capture.json` deve listar o device e chamadas para cada hook.
- A pasta `logs/native_shaders/` deve conter os arquivos `.bin` de containers de shaders.

### Passo 3: Compilar o Corpus de Shaders Offline (Fase 4)
Este passo traduz os shaders capturados (bytecode Xenos) para HLSL e compila em DXIL usando `XenosRecomp` + `DXC`:
```powershell
powershell -File tools\shaders\build_corpus.ps1 -DumpDir logs\native_shaders
```
**Resultado esperado:**
- Shaders compilados em `artifacts/shaders/dxil/*.dxil`.
- Relatório em `artifacts/shaders/SHADER_CATALOG.md`.

### Passo 4: Compilar a Versão RENDERER Nativa Final (Fase 5)
Compila com `-DSR_NATIVE=RENDERER`. Como o layout e hooks já estão confirmados e os shaders foram gerados, o executável nativo D3D12 será gerado:
```powershell
powershell -File tools\native_validate.ps1 -Step build -Native RENDERER
```

### Passo 5: Validação A/B e Comparação Visual (Fase 5)
Executa o modo A/B (Xenos renderiza na tela enquanto o renderizador nativo renderiza offscreen e compara os quadros com PSNR):
```powershell
powershell -File tools\native_validate.ps1 -Step ab
```
**Critério de Sucesso:**
- PSNR médio ≥ 40 dB em relação ao Xenos.
- Se algum quadro divergir, inspecione as saídas geradas em `logs/native_ab/`.

### Passo 6: Executar Nativo & Benchmark de Performance (Fase 6)
Rode o jogo no modo 100% nativo:
```powershell
.\run.cmd --sr_renderer=native
```
Para rodar a bateria comparativa de FPS entre Xenos e Nativo:
```powershell
powershell -File tools\native_validate.ps1 -Step bench
```
**Meta:** 30 FPS estáveis com frametime constante e sem 99% de uso de GPU em emulação.

---

## 6. Arquivos Modificados nesta Sessão

1. `port/src/native_renderer/game_profile.h`:
   - 21 hooks D3D confirmados.
   - `DeviceLayout` com offsets do XDK 2.0.3529 atualizados.
   - `kDeviceLayoutConfirmed = true`.
2. `port/src/native_renderer/shader_registry.cpp`:
   - Detecção de container em `r3` ou `r4` no hook de `OnCreateShader`.
3. `CHECKPOINT_NATIVE_RENDERER.md`:
   - Este guia consolidado para retomada imediata em outra máquina.
