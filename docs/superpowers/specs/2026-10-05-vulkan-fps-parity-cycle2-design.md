# Vulkan nativo: paridade de FPS com o D3D12 (Ciclo 2)

## Objetivo aprovado

Alcançar a paridade de desempenho com o D3D12 (≥ 29 FPS em gameplay) no renderer nativo Vulkan na Intel UHD, eliminando os dois gargalos de CPU remanescentes do Ciclo 1: captura de texturas e overhead de gravação de comandos.

Aprovado em 2026-10-05 (abordagem 2: habilitar `texture_watch_` e aplicar zero-allocation na gravação).

## Resumo do entendimento

- **O quê:** Reduzir o tempo de verificação de texturas (`textures_ms`) para < 2 ms e o tempo de gravação (`record`) para < 10 ms por quadro.
- **Por quê:** O Ciclo 1 dobrou o FPS de ~8 para ~15, mas o alvo é 30 FPS. O perfil aponta que `textures_ms` leva ~22 ms e `record` ~60 ms. Sem reduzir esses tempos, a CPU continua sendo o limite.
- **Como:**
  1. Habilitando a vigilância de páginas modificadas (`texture_watch_`) no Vulkan, igual ao D3D12, o que fará o backend hashear apenas texturas que sofreram escrita.
  2. Implementando uma arquitetura de gravação "zero-allocation" para eliminar as ~11.000 alocações na heap por quadro no hot-path de `Draw`, além de filtrar mudanças de estado redundantes.
- **Restrições:** Manter a equivalência visual estrita (imagem idêntica ao D3D12); aprovação nos testes nativos e de GPU; não quebrar ou alterar a inicialização do D3D12.

## Linha de base medida (Fim do Ciclo 1)

Bench no modo janela, argumentos do launcher:

| Renderer | Parado | Andando | GPU |
| --- | --- | --- | --- |
| D3D12 | 28,7 | 28,3 | 85% |
| Vulkan | 14,3 | 15,5 | 45 a 50% |

Perfil do Vulkan (`SR_VULKAN_PROFILE=1`):

| Estágio | Custo por quadro | Onde |
| --- | --- | --- |
| Captura de texturas | 21 a 24 ms | thread do jogo |
| Gravação | 57 a 63 ms | thread de gravação |
| (Bindings e states) | (~9 ms de chamadas repetidas) | thread de gravação |

## Design

### 1. `texture_watch_` no Vulkan

Hoje, `Renderer::EnsureInitialized` no D3D12 chama `EnsureSystemWatchers`, mas o Vulkan cria o provider de forma independente.

- **Modificação:** Interceptar ou modificar a inicialização do Vulkan para chamar a mesma lógica de `EnsureSystemWatchers` que o D3D12 usa, ativando `texture_watch_ = true` e registrando o callback `OnPhysicalWrite` no `REX_KERNEL_MEMORY()`.
- **Impacto esperado:** Em vez de recalcular o XXH3 dos ~170 MB de textura vinculadas todo quadro (1488 texturas), o Vulkan usará o sistema de páginas "sujas". As texturas não modificadas serão ignoradas. `textures_ms` deve cair de ~22 ms para < 2 ms (idealmente zero se nada mudar).
- **Risco:** Desajustes no rastreamento de páginas se o backend Vulkan acessar a memória de forma diferente; o que é improvável dado que ambos rodam sobre o mesmo emulador de guest.

### 2. Gravação de comandos Zero-Allocation e Caching

O pipeline atual do Vulkan (`GameRenderer::Draw` e `BuildBindings`) aloca e descarta objetos na heap a cada um dos ~2.800 draws.

- **Modificação:**
  - Substituir containers com alocação dinâmica (`std::vector`, `std::map`) por stack arrays / `std::array` com limites máximos conhecidos nas chamadas de draw (ex: `vertices` limitados, bindings de recursos fixos por pipeline).
  - Atualizar `UploadTransient` em `DescriptorStore::Prepare` para alocar e atualizar buffers dinâmicos (constant buffers) de forma contígua em um grande buffer pré-alocado (Ring Buffer ou Linear Allocator por quadro), evitando `std::make_shared<BufferResource>` por draw.
  - Implementar um pool genérico de reciclagem para objetos ou convertê-los para value-types, abolindo o `std::make_shared<DescriptorDraw>`.
  - Introduzir filtragem de estado no hot-path: fazer shadow-caching do estado atual do pipeline (`vkCmdSetViewport`, `vkCmdSetScissor`, `vkCmdSetBlendConstants`, `vkCmdSetStencilReference`) e omitir chamadas redundantes para o driver caso não tenham mudado. O cache do Set 1..3 já atende bem, mas a filtragem direta dos chamados do driver é necessária.

- **Impacto esperado:** Eliminação completa da pressão no GC/HeapAllocator. A thread de gravação (`record`) não fará page faults ou procuras em free-lists. O tempo de gravação deve despencar de ~60 ms para < 10 ms.
- **Risco:** Vazamento de vida útil (lifetimes) em buffers temporários caso o reciclador não sincronize corretamente com o frame in-flight. Mitigado usando pools restritos atrelados ao índice do swapchain ou ao comando de fence, permitindo liberar e sobrescrever a memória após o driver confirmar que terminou.

## Ordem e critério de parada

A implementação deve ser faseada e testada após cada mudança:

1. **Passo 1 (Texturas):** Ligar `texture_watch_`, verificar funcionamento visual, rodar `bench.ps1` e confirmar a queda de `textures_ms` no perfil do Vulkan.
2. **Passo 2 (Zero-alloc):** Substituir containers e alocadores no hot-path. Testar regressões de memória. Rodar bench.
3. **Passo 3 (State caching):** Filtrar chamadas redudantes no driver. Testar consistência visual. Rodar bench.

A meta principal (≥ 29 FPS na Intel UHD) deve ser alcançada após as três mudanças combinadas.

## Verificação

- Todos os testes nativos (`native_tests`), em especial de GPU e shaders, devem continuar passando.
- A equivalência visual será verificada via cena estática (screenshots de gameplay com/sem as mudanças).
- Confirmação do `texture_watch_` reduzindo o overhead por logging do `SR_VULKAN_PROFILE=1`.
- Memória: nenhuma subida de RAM persistente entre os quadros (o zero-alloc allocator deve reciclar sem vazamentos).
- Desempenho monitorado via `tools\bench.ps1` contra o baseline do D3D12 e do Ciclo 1 Vulkan.
