# Vulkan nativo: paridade de FPS com o D3D12

## Objetivo aprovado

Fazer o renderer nativo Vulkan atingir o mesmo FPS do D3D12 **nesta Intel UHD**:
média **≥ 29 FPS parado e andando** no `tools\bench.ps1` (New Game, janela 1280x720,
limite de 30 FPS), onde o D3D12 faz 30. A imagem não pode mudar: só entram
otimizações internas de CPU, sem atraso em texturas dinâmicas e sem mudança
arquitetural grande.

Aprovado em 2026-10-05 (abordagem 1: atacar os três custos medidos, em ordem).

## Resumo do entendimento

- **O quê:** reduzir o custo de CPU por quadro do Vulkan até ele alcançar o D3D12.
- **Por quê:** o Vulkan é o padrão do launcher, mas faz ~8 FPS contra 30 do D3D12, com
  a GPU a ~35%. Quem usa o padrão tem uma experiência muito pior.
- **Para quem:** o autor e os jogadores que abrirem o launcher com as opções padrão.
- **Restrições:** imagem idêntica; só otimização interna; sem relaxar a verificação
  de texturas; sem mudar o D3D12.
- **Fora do escopo:** paridade em outras GPUs; as opções que o Vulkan ainda recusa
  (FXAA, SSAO, MSAA, escala interna etc.); fechar a validação M3 de cenas; reestruturar
  o pipeline.

## Suposições

1. O gargalo é CPU em três estágios: captura de texturas, esperas de sincronização e
   gravação do quadro (confirmado pelo perfil abaixo).
2. "Imagem idêntica" se garante estruturalmente em cada passo (mesmos hashes, mesmos
   comandos, equivalência testada) e se confere numa cena determinística.
3. O limite de 30 FPS é o teto; passar dele é irrelevante.
4. Sem requisitos de segurança nem de escala. Confiabilidade: nenhum crash, travamento
   ou corrupção de imagem novo.
5. O autor valida no próprio notebook pelo bench.

## Linha de base medida (2026-10-05, Intel UHD, gameplay)

Bench no modo janela, argumentos do launcher:

| Renderer | Parado | Andando | GPU |
| --- | --- | --- | --- |
| D3D12 | 29,5 a 30 | 29,8 a 30 | 85% |
| Vulkan | 7,7 a 8,3 | 8,0 a 10,1 | 31 a 36% |

Perfil do Vulkan (`SR_VULKAN_PROFILE=1`, médias por 120 quadros, ~1.700 a 2.300 draws):

| Estágio | Custo por quadro | Onde |
| --- | --- | --- |
| Captura de texturas | 37 a 46 ms (leitura 28 a 35, hash 4 a 5) | thread do jogo |
| Replay | 12 a 20 ms (+ 3 a 5 de sink) | worker; bloqueado 55 a 110 ms em `swap_enqueue` |
| `fence` | 27 a 34 ms | thread de gravação (inclui fila para o lock) |
| `queue` | 22 a 36 ms | thread de gravação |
| Gravação | 38 ms (33 a 58) | thread de gravação |
| dentro da gravação | descritores 13,5 a 18,6; bindings 4,7 a 5,9; uploads 2,7 a 6; texturas 3,2 a 4,2; targets ~2; pipeline 2 a 10 | |

## Causas encontradas

1. **Vigilância de escrita desligada no Vulkan.** O inicializador do D3D12
   (`Renderer::EnsureInitialized`) é onde `texture_watch_` é ligado, e ele nunca roda
   no Vulkan. Toda textura vinculada é relida e re-hasheada todo quadro (105 a 140 MB),
   mesmo com `changed=0`.
2. **Contenção no `gpu_mutex`.** `PaintAndPresentImpl` segura o lock durante todo o
   `FrameLoop::DrawGame`, que espera um fence, faz `vkAcquireNextImageKHR` (bloqueia
   até o vblank com FIFO) e espera `images_in_flight`. O jogo usa o mesmo mutex para
   submeter e para `Retire`, então espera atrás do vblank da UI.
3. **Descritores caros por draw.** Cada draw monta uma chave de ~190 `uint64`, calcula
   hash, compara e trava `weak_ptr`; além disso aloca e atualiza um descriptor set e
   copia 3 x 4 KB de constantes.

## Decision Log

| Decisão | Alternativas | Motivo |
| --- | --- | --- |
| Meta: ≥ 29 FPS nesta Intel UHD | paridade relativa em qualquer GPU; só ≥ 20 FPS | direto, mensurável e é onde o D3D12 hoje faz 30 |
| Imagem idêntica, otimização interna | atraso de 1 a 2 quadros em texturas dinâmicas; mudança arquitetural | não arriscar a imagem; o ganho previsto vem de custos desperdiçados |
| Abordagem 1: três custos em ordem, medindo a cada passo | só ligar a vigilância de escrita; reestruturar o pipeline | a vigilância traz backoff de até 16 quadros (atrasa texturas); reestruturar é o maior risco |
| Hash no lugar em vez de copiar | ligar `texture_watch_` no Vulkan | mantém a verificação exata por quadro, sem backoff |
| Esperas fora do lock | segunda fila para apresentação; mudar o modo de apresentação | mínima mudança; mesma sequência de comandos |
| Offsets dinâmicos e memo de draw | cache maior; reduzir draws | remove as chamadas de driver por draw sem alterar a saída |

## Design

### Passo a: captura de texturas

A revalidação de cada textura vinculada passa a calcular o XXH3 **direto na memória do
jogo**, sem copiar para um vetor. A cópia só acontece quando o hash muda
(`CaptureTexture`, como hoje).

- Novo helper `HashGuestRange(base, endereço, tamanho, seed)` em
  `port/src/native_renderer/`, com o hash dentro de `__try/__except`. Página ilegível
  devolve falha e o mesmo resultado de hoje (`Texture memory is not readable`, nova
  tentativa a cada 16 quadros).
- O valor do hash é idêntico ao atual (mesmo XXH3, seed encadeado entre faixas), então
  o `content_hash` guardado continua válido.
- A verificação continua exata, uma vez por quadro e por textura.
- Não muda: `CaptureTexture`, o snapshot, a leitura de PM4 e o D3D12.

Estimativa (hipótese): `textures_ms` de ~40 para ~10 a 15 ms, limitado pela largura de
banda de memória.

### Passo b: esperas de fence e queue

`FrameLoop::DrawGame` recebe o mutex (ou um par lock/unlock) e faz as esperas fora dele:
`vkWaitForFences` do slot, `vkAcquireNextImageKHR` e a espera de `images_in_flight`. O
lock passa a cobrir só o que usa a fila e os recursos compartilhados: `prepare`/gravação
da composição, `vkQueueSubmit`, `vkQueuePresentKHR` e o `retire`.

- O swapchain só é usado pela thread de UI, e a reconexão do swapchain também roda nela,
  então o `acquire` sem lock não compete com nada.
- O `retire` do slot continua sob lock.
- Mesma sequência de comandos e mesma ordem: só muda quando cada thread espera.

Estimativa (hipótese): `queue` + `fence` caem de ~55 a 65 ms para poucos ms.

### Passo c: gravação do quadro (descritores)

1. **Offsets dinâmicos.** O set 0 passa a usar `STORAGE_BUFFER_DYNAMIC`. Um set por
   submissão aponta para a arena de constantes, e cada draw só passa 3 offsets no
   `vkCmdBindDescriptorSets`. As constantes de um draw vão num bloco contíguo de 12 KB,
   com uma cópia. Os shaders não mudam. Elimina `vkAllocateDescriptorSets` e
   `vkUpdateDescriptorSets` por draw.
2. **Memo do draw anterior.** Se texturas, vertex buffers e os bits de sampler do `fetch`
   são idênticos aos do draw anterior da mesma submissão, reusa a entrada sem refazer a
   chave. Chave igual dá a mesma entrada, e os recursos seguem vivos porque o draw
   anterior os referencia.

Estimativa (hipótese): gravação de ~38 para ~25 ms.

## Ordem e critério de parada

Cada passo (a, b, c) é um commit próprio. Depois de cada um: bench no Vulkan (parado e
andando) e o `Vulkan profile`, com os números registrados. Para quando as duas medições
passarem de 29 FPS. Se após os três ainda ficar abaixo, **não se relaxa nenhuma
restrição sem perguntar**; o próximo candidato é hashear texturas em paralelo, já que a
thread do jogo ainda pode passar de 33 ms.

## Verificação

- Testes unitários em `tests/`: hash no lugar igual a `memcpy` + XXH3 para vários
  tamanhos e seeds, e página `PAGE_NOACCESS` devolve falha sem crash; memo de descritores
  equivalente à chave completa; offsets dinâmicos corretos.
- `tools\verify_vulkan_m3.ps1` continua passando, inclusive os fixtures de GPU.
- Imagem: cena determinística (tela de título, após N quadros), comparada antes e depois
  por dump de quadro (`SR_VULKAN_DUMP_FRAME`) e por screenshot, com tolerância.
- `tools\bench.ps1` no Vulkan e no D3D12 (este não pode regredir).
- Resultados em `docs/vulkan-m3.md` e no README.

## Riscos

- O passo b pode abrir uma corrida entre pintura e gravação; mitigado por manter
  `submit`, `present`, `prepare` e `retire` sob lock.
- O hash no lugar depende de SEH funcionar no clang/MSVC alvo; confirmar com o teste de
  página inacessível.
- Os ganhos são hipóteses. A thread do jogo (jogo + captura) pode continuar acima de
  33 ms mesmo após o passo a.
- Offsets dinâmicos mudam o layout do set 0: precisam de verificação em GPU
  (`verify_vulkan_m3.ps1`) e compatibilidade com o limite
  `minStorageBufferOffsetAlignment`.

## Perguntas em aberto

- Quanto da thread do jogo é lógica do jogo e quanto é captura? Só se sabe depois do
  passo a.
- O GPU real por quadro, com as esperas removidas, ainda cabe em 33 ms na Intel UHD?
  Só se sabe depois do passo b.
