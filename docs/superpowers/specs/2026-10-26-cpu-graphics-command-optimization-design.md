# Especificação de Design: Otimização do Processamento de Comandos Gráficos na CPU

**Data:** 2026-10-26  
**Escopo:** `Pm4Mirror` / Captura PM4 (Estágio 1), Captura no Front-End (Estágio 2) e Preparação/Replay na Worker Thread D3D12 & Vulkan (Estágio 3).

---

## 1. Contexto e Objetivo

As medições recentes de perfilamento de CPU no renderizador nativo (`logs/bench_fps60_prepare_profiling_261026.log` e `logs/timeline_fps60_streamsets1024_261026.txt`) mostram que o custo de processamento de comandos gráficos na CPU está distribuído em três estágios principais:

1. **Parsing e espelhamento PM4 (`Pm4Mirror` e `CapturePm4Dependencies`):**
   - `Pm4Mirror::FloatConstants(bool pixel)` reconstrói e sanitiza todas as 1024 palavras (4 KB) do banco de constantes float sempre que `vs_version` ou `ps_version` avança, mesmo quando um comando altera apenas poucas constantes (`vs_capture` + `ps_capture` ~2.0–2.3 ms/frame).
   - `Pm4Mirror::ScanFrom` instancia um segundo `thread_local CheckedGuestReads` dentro do `case 0x2F` (`LOAD_ALU_CONSTANT`) e chama `Reset()` (que varre um vetor de até 32 buffers) a cada pacote (64k–83k vezes a cada 120 swaps).
   - `CapturePm4Dependencies` aloca um `std::vector<Dependency>` temporário e um `std::vector<uint8_t>` por leitura indireta/ALU em cada comando antes de copiar para `batch.bytes`.

2. **Captura no Front-End (`Renderer::CaptureDevice`, `CaptureBytes`, `DrawIndexedVertices`, `PlanStreams`):**
   - `CaptureDevice` realiza 10 a 18 chamadas separadas a `CaptureBytes` para intervalos adjacentes de `D3DDevice`, inflando `cmd.range_count` em `batch_->ranges` e degradando todas as buscas lineares subsequentes (`Load32` na worker D3D12 e `CapturedMemory::Read` / `ReadUnambiguous` no Vulkan).
   - `DrawIndexedVertices` gasta ~3.09 ms/frame (`fe_index`) chamando `PlanBuffer` diretamente sem passar pelo cache de sets associativos (`front_stream_cache_`), executando hash `XXH3_64bits` e busca em `tracked_` (`std::unordered_map`) em 100% dos draws indexados.
   - `PlanStreams` gasta ~1.11 ms/frame em `fs_prep` reavaliando `DynamicVertexFetch` e varrendo atributos de `ReadVertexDeclaration` em todo draw, além de iterar pelos 16 slots de stream em vez de usar apenas os bits ativos de `streams_used`.

3. **Worker Thread D3D12 (`UploadConstants`, `BindVertexStreams`) e Replay Vulkan (`DecodeCapturedPacket`, `DeviceState`):**
   - `UploadConstants` decodifica `ReadVertexDeclaration(base, decl)` duas vezes por draw (para `vertex_fetch_[...][3]` e para os bits de normal compactada em `spec`).
   - `BindVertexStreams` executa `IASetVertexBuffers` em todos os streams de todos os draws (~435k chamadas / 120 swaps), mesmo quando a `D3D12_VERTEX_BUFFER_VIEW` é idêntica à já vinculada no slot, e faz um segundo lookup `buffers_.at(sp.buffer.key)` para `dynamic_streams` apesar de `ApplyBuffer` já ter localizado a `BufferEntry`.
   - `DecodeCapturedPacket` (Vulkan) grava as constantes booleanas e de loop duas vezes quando `cmd.device != 0`, chama `Declaration(memory, decl)` repetidamente para cada stream do mesmo draw, e `DeviceState` faz leituras escalares individuais `U32(d.memory, ...)` em vez de usar `ReadUnambiguous` nos blocos já capturados de `D3DDevice`.

**Objetivo:** Eliminar esses custos redundantes diretamente no caminho padrão de execução (tanto para D3D12 quanto para Vulkan), mantendo intactos todos os invariantes de segurança de memória e os 483 testes nativos existentes.

---

## 2. Arquitetura e Componentes

### 2.1 Estágio 1: `Pm4Mirror`, `CheckedGuestReads` e `CapturePm4Dependencies`

#### A. Atualização Incremental por Faixa Suja (`dirty_min_` / `dirty_max_`) em `Pm4Mirror`
- **Arquivos:** `port/src/native_renderer/pm4_mirror.h`, `port/src/native_renderer/pm4_mirror.cpp`
- **Mecanismo:**
  - Adicionar em `Pm4Mirror`:
    ```cpp
    mutable std::array<uint32_t, 2> float_dirty_min_{0, 0};
    mutable std::array<uint32_t, 2> float_dirty_max_{1024, 1024};
    ```
  - Em `Pm4Mirror::Write(uint32_t index, uint32_t value)`: quando `index - kAluConstantBase < 0x800u && regs_[index] != value`, calcular `bank = (index >= kAluConstantBase + 0x400) ? 1 : 0` e `offset = (index - kAluConstantBase) & 0x3FFu`, atualizando:
    ```cpp
    float_dirty_min_[bank] = std::min(float_dirty_min_[bank], offset);
    float_dirty_max_[bank] = std::max(float_dirty_max_[bank], offset + 1);
    ```
  - Em `Pm4Mirror::WriteRange(uint32_t first, const uint8_t* words, uint32_t count)`: no loop por banco (VS/PS), rastrear o menor índice `first_changed` e o maior índice `last_changed + 1` onde `destination[i] != value`. Se `changes > 0`, atualizar `float_dirty_min_[bank]` e `float_dirty_max_[bank]` com os limites efetivamente alterados.
  - Em `Pm4Mirror::FloatConstants(bool pixel)`:
    - Quando `float_cache_version_[bank] != version`:
      - Se `float_dirty_min_[bank] < float_dirty_max_[bank]`, sanitizar NaN -> `0` apenas na subfaixa `[float_dirty_min_[bank], float_dirty_max_[bank])` copiando de `regs_ + kAluConstantBase + bank * 1024` para `float_cache_[bank]`.
      - Resetar `float_dirty_min_[bank] = 1024` e `float_dirty_max_[bank] = 0`, e atualizar `float_cache_version_[bank] = version`.
    - O estado inicial (`float_dirty_min_ = {0, 0}`, `float_dirty_max_ = {1024, 1024}`) garante que a primeira materialização inicialize todas as 1024 dwords do cache.

#### B. Buffer Scratch Dedicado para Leituras Efêmeras em `CheckedGuestReads`
- **Arquivos:** `port/src/native_renderer/checked_guest_memory.h`, `port/src/native_renderer/pm4_mirror.cpp`
- **Mecanismo:**
  - Adicionar `std::span<const uint8_t> ReadScratch(const void* source, uint32_t length)` em `CheckedGuestReads` que usa um único `std::vector<uint8_t> scratch_` dedicado (encolhido apenas se `scratch_.capacity() > 64 * 1024`), sem tocar no vetor `copies_` de buffers indiretos aninhados.
  - Em `Pm4Mirror::ScanFrom`, reutilizar a mesma instância `thread_local CheckedGuestReads indirect_reads` chamando `indirect_reads.ReadScratch(live_source, size * 4)` no `case 0x2F` (`LOAD_ALU_CONSTANT`), eliminando a segunda variável `thread_local` e o loop de `Reset()` por pacote.

#### C. Reuso de Arena `thread_local` em `CapturePm4Dependencies`
- **Arquivo:** `port/src/graphics/guest/pm4_capture.cpp`
- **Mecanismo:**
  - Substituir `std::vector<Dependency> sources` local por um pool `thread_local` de vetores reutilizáveis (`CheckedGuestReads` ou `DependencyScratch` com slots reutilizados), de modo que `LOAD_ALU_CONSTANT` e `INDIRECT_BUFFER` durante `CapturePm4Dependencies` reutilizem buffers já alocados (limitando retenção máxima a 32 slots de até 64 KiB, idêntico à política de `CheckedGuestReads::Reset()`).

---

### 2.2 Estágio 2: Captura no Front-End (`native_renderer.cpp`)

#### A. Coalescência de Intervalos Contíguos em `Renderer::CaptureBytes` e `CaptureDevice`
- **Arquivo:** `port/src/native_renderer/native_renderer.cpp`
- **Mecanismo:**
  - Em `Renderer::CaptureBytes(uint8_t* base, uint32_t address, uint32_t length)`:
    - Antes de dar `push_back` em `batch_->ranges`, verificar se `batch_->ranges.size() > cur_.range_first`.
    - Se sim, inspecionar `CaptureRange& last = batch_->ranges.back()`. Se `uint64_t(last.address) + last.length == address` e `size_t(last.offset) + last.length == offset` (onde `offset == batch_->bytes.size()`), apenas anexar `[base + address, base + address + length)` em `batch_->bytes` e somar `last.length += length`.
  - Em `Renderer::CaptureDevice(uint8_t* base, uint32_t dev)`:
    - Quando `cur_.packet_check` for verdadeiro, não capturar `dev + kDev.register_shadow` duas vezes: `CaptureBytes(base, dev + kDev.vs_bools, kDev.register_shadow - kDev.vs_bools)` seguido de `CaptureBytes(base, dev + kDev.register_shadow, 0x1D0)` serão coalescidos ou emitidos em ordem contígua para formar um único `CaptureRange`.

#### B. Fast-Path de Cache para Index Buffer em `Renderer::DrawIndexedVertices`
- **Arquivos:** `port/src/native_renderer/native_renderer.h`, `port/src/native_renderer/native_renderer.cpp`
- **Mecanismo:**
  - Utilizar o banco `front_stream_cache_[16]` e `front_stream_cache_next_[16]` (já alocados em `Renderer`) para o Index Buffer em `DrawIndexedVertices`:
    - O conjunto (`set`) é indexado por `address_hash = address ^ (address >> 12) ^ (address >> 23)`.
    - Os campos comparados no slot são `tracked != nullptr`, `address`, `size`, `decl == reset_index` (armazenando `reset_index` no campo `decl` do slot 16, já que index buffers têm `decl = 0`), `stride == 0` e `format == ((index32 ? 2u : 1u) | (endian << 2))`.
    - Quando houver hit nos campos e `!RefreshTrackedBuffer(*sc.tracked)` (e `!REXCVAR_GET(sr_native_debug_buffers_always_dirty)`), construir `cur_.index` diretamente em memória (`action = 0`, `key = sc.key`, `address`, `size`, `format`, `reset_index`) sem chamar `PlanBuffer`, sem recalcular `XXH3_64bits` e sem procurar em `tracked_`.
    - Quando houver miss ou o buffer estiver `dirty`, chamar `PlanBuffer` passando `cached_fields_match ? sc.tracked : nullptr` e `sc.key`, atualizando o slot de `front_stream_cache_[16]`.

#### C. Otimização de `PlanStreams` e Pré-cálculo de `streams_used`
- **Arquivo:** `port/src/native_renderer/native_renderer.cpp`
- **Mecanismo:**
  - Na estrutura cacheada por `ReadVertexDeclaration` (ou em `DecodedVertexDeclaration`), armazenar `uint32_t streams_used` e `uint32_t normal_spec_bits` calculados uma única vez quando a declaração de vértices é decodificada.
  - Em `PlanStreams`:
    - Obter `streams_used` diretamente da declaração cacheada em O(1).
    - Substituir o loop `for (uint32_t s = 0; s < 16; ++s)` por iteração sobre os bits ativos de `streams_used` (`for (uint32_t mask = streams_used; mask; mask &= mask - 1)` com `std::countr_zero(mask)`).
    - Içar leituras de CVars de debug (`sr_native_debug_buffers_always_dirty`, `sr_native_debug_vb_addr`, `sr_native_debug_vs`) para fora do loop de streams.

---

### 2.3 Estágio 3: Worker Thread D3D12 e Replay Vulkan

#### A. Passagem Única de `VertexDeclaration` e Limpeza Enxuta em `UploadConstants` / `BindVertexStreams` (D3D12)
- **Arquivo:** `port/src/native_renderer/native_renderer.cpp`
- **Mecanismo:**
  - Em `UploadConstants`: ler `ReadVertexDeclaration(base, input_decl)` uma única vez no início da montagem das constantes compartilhadas (`setup`), preenchendo `vertex_fetch_[...][3]` e acumulando simultaneamente os bits `spec_normal_bits` (`0x2A2187` -> `1u << 6`, `0x2A2190`/`0x2A2390` -> `1u`), reutilizando `spec_normal_bits` mais abaixo ao compor `s[112] = spec` sem reler nem re-iterar a declaração.
  - Em `BindVertexStreams`:
    - Mudar a assinatura de `ApplyBuffer` para retornar `BufferEntry*` (em vez de `const BufferEntry*`), usando diretamente `*vb` no caminho `dynamic_fetch` e eliminando o segundo hash lookup `buffers_.at(sp.buffer.key)`.
    - Rastrear uma bitmask `uint16_t bound_vertex_views_mask_ = 0` no `Renderer` (zerada sempre que `frame_state_bound_ = false` ou `command_list_->Reset(...)` ocorre). Em `BindVertexStreams`, só chamar `command_list_->IASetVertexBuffers(sp.stream, 1, &view)` quando `!(bound_vertex_views_mask_ & (1u << sp.stream))` ou `guest_vertex_views_[sp.stream]` diferir de `view`.

#### B. Despacho Enxuto em `DecodeCapturedPacket` e `DeviceState` (Vulkan / Packet Sink)
- **Arquivo:** `port/src/graphics/guest/render_packet.cpp`
- **Mecanismo:**
  - Em `DecodeCapturedPacket`:
    - Se `cmd.device != 0`, preencher bool constants (`256..288`) e loop constants (`304..432`) uma única vez combinando `mirror.written(reg)` e um span obtido via `memory.ReadUnambiguous(Address(cmd.device, layout.vs_bools), ...)` (com fallback para `U32`), deixando o loop puramente do `mirror` apenas para o ramo `else` (`cmd.device == 0`).
    - No loop de streams (`for (uint64_t i = cmd.stream_first; ...)`), cachear `last_decl` e o ponteiro para o `std::vector<VertexAttribute>` retornado por `Declaration(memory, decl)` fora do loop para não re-invocar `Declaration` em cada stream quando `decl` é o mesmo.
  - Em `DeviceState`:
    - Usar `ReadUnambiguous` para o bloco de ponteiros e viewport de `D3DDevice` (`render_targets`, `depth_stencil`, `shader_a`, `shader_b`, `viewport`), extraindo as palavras com `CapturedWord` quando o bloco é contíguo e mantendo `U32(d.memory, ...)` como fallback seguro quando uma captura sintética de teste fragmentar esse intervalo.

---

## 3. Tratamento de Erros e Invariantes de Segurança

1. **Invariante de Memória Guest ao Vivo:** Nenhuma leitura ao vivo em `Pm4Mirror` deixará de passar por `CheckedGuestReads` (`Read` para `INDIRECT_BUFFER` aninhados e `ReadScratch` para `LOAD_ALU_CONSTANT`) quando `bounded_alu_reads` estiver ativo, ou por `HostReadable` no modo legado. Leituras parciais ou inválidas continuam incrementando `unreadable_alu_loads` / `unreadable_indirect_buffers` sem mutar registradores.
2. **Invariante de Precedência em `CapturedMemory`:** A coalescência em `CaptureBytes` ocorre **apenas** quando o novo intervalo é estritamente contíguo tanto no endereço virtual guest (`last.address + last.length == address`) quanto no offset da arena (`last.offset + last.length == batch_->bytes.size()`) e pertence ao mesmo comando (`batch_->ranges.size() > cur_.range_first`). Sobreposições ou escritas fora de ordem preservam intervalos separados e a semântica de "última escrita vence" em `CapturedMemory::Read` e `FindUnambiguousCapture`.
3. **Invariante de Invalidação de Buffers:** O cache de Index Buffer no slot 16 (`front_stream_cache_[16]`) aponta para a mesma `TrackedBuffer` em `tracked_` que é invalidada por `InvalidateGuestRange` e verificada por `RefreshTrackedBuffer`. Qualquer `Unlock` ou escrita física em página vigiada marca `t.dirty = true` e força a execução de `PlanBuffer`.
4. **Invariante de Estado D3D12:** `bound_vertex_views_mask_` é invalidada em todos os pontos onde o command list é resetado ou onde ` IASetVertexBuffers(0, 16, guest_vertex_views_)` precisa ser reestabelecido (`BeginFrame`, `PrepareDraw` debug dump, `ExecResolve` debug dump, `ExecOnPassEnd`, `WatchHdr`).

---

## 4. Estratégia de Testes e Verificação

1. **Testes Unitários (`tests/native/`):**
   - `test_pm4_mirror.cpp`: testar que atualizações parciais de constantes float (ex.: via `SET_CONSTANT` ou `LOAD_ALU_CONSTANT` em `c0` e `c255` de VS/PS) produzem exatamente o mesmo `FloatConstants(false/true)` e `ShareFloatConstants(false/true)`, incluindo sanitização de `NaN` para `0`, tanto na primeira leitura quanto após múltiplas mutações parciais seguidas.
   - Testes de captura (`captured_batch` / `render_packet`): verificar que capturas contíguas coalescidas funcionam com `CapturedMemory::Read`, `ReadUnambiguous` e `DecodeRenderPacket`.
   - Executar toda a suíte `ctest` e `sr_native_tests` (todos os 483+ testes devem passar).
2. **Compilação Integral:**
   - Compilar o executável completo `superman_returns.exe` via `build.cmd` no perfil `win-amd64-write-tracking`.
