# Vulkan nativo: paridade de FPS com o D3D12 (ciclo 3)

## Objetivo aprovado

Levar o renderer nativo Vulkan a **média ≥ 29 FPS parado e andando** no `tools\bench\bench_api.ps1`, na Intel UHD deste notebook, em **duas execuções seguidas**, com o D3D12 sem regredir. O ciclo 1 dobrou o FPS (de ~8 para ~15,5) e o ciclo 2 (feito pelo Gemini) foi revertido. O que sobra é CPU: a gravação do quadro (44 a 63 ms) e a captura de texturas (~22 ms).

Aprovado em 2026-10-06 (abordagem A: dieta por draw e hash paralelo, em passos medidos).

## Resumo do entendimento

- **O quê:** reduzir o custo de CPU por quadro do Vulkan até ele alcançar o D3D12.
- **Por quê:** o Vulkan é o padrão do launcher, mas faz ~15,5 FPS contra ~27 a 30 do D3D12.
- **Para quem:** o autor e os jogadores que abrirem o launcher com as opções padrão.
- **Restrições:** imagem idêntica; texturas **exatas a cada quadro**, sem backoff, sem atraso e sem vigilância de páginas que mude essa regra; só otimização interna de CPU.
- **Fora do escopo:** paridade em outras GPUs; as opções que o Vulkan recusa (FXAA, SSAO, MSAA, escala interna, sombras em qualidade maior); a validação M3 de cenas; replay offline de quadros; `texture_watch_` com backoff; gravação em várias threads (só como contingência, com nova decisão).

## Suposições

1. A meta é a média do `bench_api.ps1` em duas execuções seguidas, parado e andando, em gameplay real (o `bench.ps1` só mede com o HUD visível).
2. O hash paralelo usa no máximo 3 a 4 threads auxiliares e roda na memória do jogo com a proteção SEH que já existe. Hoje a CPU usa ~6,4 dos 12 threads lógicos. Em máquinas com poucos núcleos, a thread do jogo não pode ficar mais lenta que hoje.
3. O gate de imagem compara o screenshot do **instante em que o HUD é detectado** (câmera e pose iniciais) com uma referência da build boa, por PSNR e distância de histograma de cores. Detecta corrupção grosseira, não diferenças de poucos pixels, e o limite é calibrado com a variação natural entre execuções.
4. Cada passo é um commit próprio, revisado e medido antes do seguinte; se não reduzir o custo do **seu estágio**, é revertido.
5. Sem requisitos de segurança nem de escala. Confiabilidade: nenhum crash nem travamento novo; os fixtures de GPU continuam como regressão.
6. As validation layers do Vulkan não estão instaladas nesta máquina: `validation_errors=0` nos fixtures não prova validação. Os fixtures (com checagem de pixels) e o gate de imagem são a rede de segurança.

## Linha de base medida (2026-10-05, gameplay real, Intel UHD)

| Renderer | Parado | Andando | GPU |
| --- | --- | --- | --- |
| Vulkan | 15,5 (mín. 12,3) | 15,6 (mín. 13,1) | 50 a 55% |
| D3D12 | 24,5 a 29,5 | 26,4 a 29,8 | 70 a 85% (a cena varia entre execuções) |

Perfil do Vulkan (`SR_VULKAN_PROFILE=1`, médias por 120 quadros, ~2.800 draws):

| Estágio | Custo por quadro | Thread |
| --- | --- | --- |
| Captura de texturas | 19 a 24 ms (hash 14 a 18, leitura 0; PM4 5 a 7) | do jogo |
| Replay | 12 a 20 ms (+ 3 a 5 de sink) | worker (com folga) |
| Gravação | **44 a 63 ms** | de gravação |
| dentro da gravação | descritores 13 a 19; bindings 6,5 a 9; uploads 3 a 6,5; texturas 4,5 a 6; targets 2,5 a 4,5; pipeline 3 a 4; comandos 3 a 3,8; resolves ~0,8 | |
| `fence` e `queue` | 2 a 3 ms e 0 | resolvidos no ciclo 1 |

## Causas encontradas no código

1. **Cópias de 12 KB por draw.** `BuildBindings` copia o `ConstantSnapshot` (12 KB), `RemapTextureBindings` copia e "move" o `DrawBindings` inteiro (mais duas), e `Prepare` copia para a arena: ~48 KB por draw, ~130 MB por quadro.
2. **Alocações de heap por draw:** `requests` (vector), `streams` (`std::map`), `vertices` (vector), a chave do cache de descritores, o `make_shared<DescriptorDraw>`, as cópias atômicas de `shared_ptr` dos recursos e o `pending_.Keep`.
3. **Comandos reemitidos a cada draw:** 4 descriptor sets, vertex buffers, index buffer, viewport, scissor, blend e stencil, mesmo sem mudança.
4. **Hash de ~170 MB de texturas por quadro** em SSE2 (~10 GB/s), todo na thread do jogo.

## Lições do ciclo 2 (revertido)

- Ligar o `texture_watch_` em `SetupPresentation`, antes de a memória do jogo existir, fechava o Vulkan com `0xC0000005`. Além disso, o watch liga um backoff de 2 a 16 quadros que atrasa texturas dinâmicas.
- Tirar a posse dos recursos junto com as alocações (`DescriptorDraw::resources` e o `pending_.Keep`) corrompeu a imagem: uma textura substituída podia ser destruída enquanto a GPU ainda a usava.
- Os 92 + 69 testes, o contrato e os 15 fixtures passaram mesmo assim. Faltam um gate de imagem e um teste de posse.

## Decision Log

| Decisão | Alternativas | Motivo |
| --- | --- | --- |
| Texturas exatas por quadro; hash paralelo | híbrida (exata nas pequenas, vigilância nas grandes); vigilância com backoff | a restrição de imagem idêntica do ciclo 1; o backoff atrasa HUD, minimapa e vídeo |
| Gravação: menos trabalho por draw primeiro; paralelizar só se precisar | ir direto para gravação paralela; só reduzir o custo por draw | menor risco; a decisão sobre a fase 2 só depois de medir |
| Verificação: gate automático de imagem antes de otimizar | só revisão visual; replay offline de um quadro | o ciclo 2 passou em todos os testes e corrompeu a imagem |
| Abordagem A (dieta por draw + hash paralelo) | B (preparo no worker); C (gravação paralela agora) | B coloca `ResourceStore` e `DescriptorStore` em duas threads; C é o maior risco |
| Constantes escritas uma vez, no lugar final (arena) | manter cópias; só trocar o ponteiro | corta ~36 KB por draw |
| Posse por submissão, com `held_serial` e sem repetição | remover a posse (ciclo 2); manter vetores por draw | mantém a garantia e custa menos |
| `DescriptorDraw` por valor, sem pool | pool de draws por submissão | os ponteiros do pool invalidaram no ciclo 2 |
| Estado sombra com invalidação conservadora e desligador por ambiente | filtro sem desligador | diagnóstico A/B se aparecer problema |
| Hash: despacho AVX2 antes do pool; pool com spin curto e cvar | só pool; só AVX2 | AVX2 é exato e sem threads; o pool só entra se faltar |
| Referência do gate em `artifacts\golden`, capturada na detecção do HUD | referência no repositório; comparar o screenshot do `idle` | a cena do `idle` varia entre execuções; a do instante do HUD é estável |

## Design

### 0. Gate de imagem (pré-requisito)

O `bench.ps1` salva `bench_<Nome>_start.png`, recortado na janela do jogo, **logo na detecção do HUD**. `tools\bench\image_gate.ps1` compara com uma referência: `-Record` grava a referência a partir da build atual, que é a boa; `-Check` compara. Métricas: PSNR e distância entre os histogramas de cor médios, via `System.Drawing`. A referência fica em `artifacts\golden\` (local, ignorada pelo Git e protegida do `clean.ps1`).

Calibração: gravo a referência, rodo a build boa 2 vezes para medir o ruído (esperado ~25 a 35 dB) e fixo o limite com margem. Prova de que funciona: rejeita uma imagem sintética com os canais RGB permutados (que reproduz a corrupção do ciclo 2) e aceita a boa. Teste automático com imagens sintéticas em `tests\tools\`. `bench_api.ps1 -Gate` roda tudo e falha o passo se o gate reprovar.

### 1. Constantes sem cópias e bindings sem alocação

O chamador pede à arena uma fatia de 12 KB antes de montar os bindings: `ResourceStore::MapTransient(12288)` devolve `{VkBuffer, offset, std::byte*}`. Os 8 KB de `vs` e `ps` são copiados direto de `draw.constants` para a fatia, e `BuildBindings` preenche os 4 KB de `shared` na própria fatia. `DrawBindings` deixa de carregar o `ConstantSnapshot` e guarda só IDs e índices; `Prepare` recebe a fatia, sem copiar. `MapTransient` passa pelo mesmo `Suballocate` de hoje: o chunk continua preso à submissão com `submissions_.Keep`, a fatia não vira `make_shared`, e o flush de memória não coerente acontece como hoje. `RemapTextureBindings`, `BuildBindings` e `Draw` trocam `vector` e `map` por arrays fixos no stack, e o remapeamento valida e depois aplica no lugar, sem copiar o `DrawBindings`. `composition.cpp` e `immediate.cpp` escrevem gamma e opções na mesma fatia. Medição esperada: `bindings` de 6,5 a 9 ms para ~2 a 3 ms.

### 2. Posse por submissão e descritores sem alocação

Cada `BufferResource`, `TextureResource` e entrada do cache ganha `held_serial`. `Hold(serial, ptr)` só acrescenta o recurso à lista da submissão se `held_serial != serial`; a lista é um `vector` por submissão, com capacidade reaproveitada, liberado no `Retire` do fence. Invariante: todo recurso que um draw grava passa por `Hold` antes de os comandos desse draw serem gravados. `DescriptorDraw` vira um valor (`sets`, os 3 offsets e um ponteiro cru para a entrada do cache, mantida viva pela lista), devolvido por valor, sem `make_shared`, sem pool. `Shared()` usa uma chave em array fixo com hash calculado junto; no acerto, os `weak_ptr` são promovidos direto para o `Hold`. Teste de posse: um fixture de GPU novo (`--hold-lifetime`) grava um draw com uma textura, a substitui por outra versão antes do fence e verifica que a antiga segue viva até o `Retire`, mais um teste unitário da lista. Medição esperada: `descriptors` de 13 a 19 ms para ~6 a 8 ms.

### 3. Filtro de comandos redundantes

`StateShadow` no `GameRenderer` guarda o último valor emitido de viewport, scissor, blend, stencil, sets 1 a 3, cada um dos 32 vertex buffers (handle e offset) e o index buffer (handle, offset, tipo). O set 0 (constantes) muda de offset a cada draw e é emitido sempre, mas sozinho (`firstSet=0`, 1 set, 3 offsets); os sets 1 a 3 só são reemitidos quando mudam, o que é válido porque todos os pipelines do jogo usam o mesmo `VkPipelineLayout`. Viewport, scissor, blend e stencil persistem no command buffer porque todos os pipelines do jogo declaram os mesmos quatro estados dinâmicos. O estado sombra é zerado em todo lugar em que hoje se zera `bound_pipeline_`, e também em `BeginSubmission`, `ClosePass()` e depois de qualquer outro gravador que use o command buffer (resolves, clears, aliases de depth, uploads com barreira). `SR_VULKAN_NO_STATE_FILTER=1`, lido uma vez, desliga o filtro. Teste unitário da classe, sem Vulkan. Medição esperada: `commands` de 3 a 3,8 ms para ~1 a 2 ms.

### 4. Hash de texturas

- **4a. XXH3 com despacho em AVX2** (`xxh_x86dispatch`), com seleção em tempo de execução: o mesmo valor de hash, ~2× mais rápido, sem threads. Meta: `hash_ms` de ~15 para ~8 ms.
- **4b. Pool de hash, só se a 4a não bastar.** Em `CaptureTextures`, cada draw coleta as texturas novas do quadro e, se o total passa de ~256 KB, as reparte entre 3 helpers e a própria thread, esperando o fim por um contador atômico. Cada textura é hasheada inteira por uma thread, em ordem, então o seed encadeado e o valor não mudam; a thread do jogo só segue depois do join (mesma semântica de hoje). Helpers esperam por spin curto e depois dormem. O número de threads é o cvar `sr_native_hash_threads`, com padrão por núcleos da máquina; 0 desliga.

## Ordem e critério de parada

Cada passo é um commit próprio: 0 gate de imagem → 1 constantes → 2 posse por submissão → 3 filtro de comandos → 4a hash AVX2 → 4b hash paralelo. Depois de cada passo: testes unitários, contrato de produção, os 15 fixtures de GPU, o gate de imagem e o bench do Vulkan (com perfil) e do D3D12 (sanidade). Se o passo não reduzir o custo do seu estágio, é revertido. A comparação é por custo de estágio normalizado por draw, porque o FPS varia com a cena. Meta final: média ≥ 29 FPS parado e andando, em duas execuções seguidas. Se depois do 4b ainda faltar, **paramos e perguntamos** antes da gravação paralela (fase 2).

## Riscos

- **Posse de recursos.** Mitigado pelo invariante de `Hold`, pelo fixture `--hold-lifetime` e pelo gate de imagem.
- **Estado sombra desatualizado.** Mitigado pela invalidação conservadora e pelo desligador por ambiente.
- **Hash paralelo disputando CPU** em máquinas com poucos núcleos. Mitigado pelo cvar e pelo padrão por núcleos.
- **O gate só pega corrupção grosseira**, e a cena varia entre execuções (por isso a comparação de custo é por estágio).
- **Sem validation layers** nesta máquina: os fixtures e o gate são a única rede.
- **A thread do jogo** (lógica do jogo, PM4 e captura) pode seguir acima de 33 ms mesmo com a captura reduzida. Só a medição responde.

## Perguntas em aberto

- A fase 1 da gravação leva os ~44 a 63 ms a ≤ 33 ms sozinha? Só depois dos passos 1 a 3.
- A thread do jogo cabe em ~33 ms depois do hash? Só depois do passo 4.

## Resultado (2026-10-06, ciclo 3)

Medições no i5-13420H + Intel UHD, janela 1280x720, vsync e limite de 30 FPS, New Game com HUD detectado, 20 s parado e 20 s andando. As duas execuções finais consecutivas foram feitas após recompilar a HEAD `0c87c6c`, às 14:42 e 14:44 (horário local). **A meta de média ≥ 29 FPS nas duas situações e nas duas execuções não foi atingida.** Os dois gates finais passaram sem alterar limites.

| Etapa | Parado (FPS) | Andando (FPS) | bindings / descriptors / commands (µs por draw) | textures_ms / hash_ms | record (ms) |
| --- | --- | --- | --- | --- | --- |
| Linha de base `c3_base` | 17,6 | 17,0 | 2,74–2,89 / 5,72–6,66 / 1,03–1,16 | 17–24 / 12–17 | 49,8–56,1 |
| 1 constantes `c3_t1` | 17,6 | 19,4 | 1,97–2,05 / 4,98–5,21 / 1,11–1,18 | 18–20 / 12–14 | 43,4–46,7 |
| 2 posse `c3_t2` | 17,8 | 19,5 | 2,08–2,13 / 3,91–4,30 / 1,06–1,15 | 20–22 / 13–15 | 40,4–44,2 |
| 3 filtro `c3_t3` | 17,9 | 18,4 | 2,05–2,16 / 3,86–5,00 / 0,99–1,12 | 19–22 / 13–16 | 44,9–47,9 |
| 4a AVX2 `c3_t4` | 17,2 | 18,2 | 2,12–2,17 / 3,98–4,15 / 1,08–1,14 | 15–20 / 10–13 | 46,6–47,9 |
| 4b pool `c3_t5` | não aplicado | não aplicado | — | — | — |
| 7 recursos/descriptors `c3_t7` | 17,3 | 19,6 | 2,68–2,71 / 2,22–2,82 / 1,52–1,59 | 18–21 / 12–14 | 30,2–35,5 |
| 8 targets/pipeline `c3_t8` (última tentativa) | 16,6 | 16,8 | 2,67–2,82 / 2,41–2,67 / 1,56–1,61 | 18–20 / 11–12 | 31,9–33,4 |
| 9 constantes/bind `c3_t9` (gate PASS) | 17,6 | 18,5 | 2,33–2,44 / 2,33–2,93 / 1,50–1,51 | 19–20 / 12–13 | 30,3–33,2 |
| Final `c3_final1` (14:42) | 17,4 | 18,4 | 2,50–2,59 / 2,24–2,41 / 1,54–1,62 | 17–19 / 11–13 | 28,5–30,9 |
| Final `c3_final2` (14:44) | 17,6 | 17,5 | 2,38–2,45 / 2,34–3,15 / 1,47–1,58 | 17–22 / 11–15 | 27,6–31,6 |
| D3D12 `c3_final_d3d12` (14:47) | 25,5 | 27,5 | n/a | n/a | n/a |

Custos: intervalo das três últimas médias de 120 quadros de cada log; µs/draw = ms × 1000 / draws da mesma linha. `textures_ms`/`hash_ms` são os três últimos snapshots de captura, não a média do benchmark. O ganho AVX2 medido isoladamente na Tarefa 4 foi hash 12,5 → 7,9 ms em média; os tails acima incluem variação de cena. Até a Tarefa 4, cada lap interno truncava frações de microssegundo, acumulando milissegundos por quadro; a Tarefa 7 passou a acumular nanossegundos antes de converter o perfil. Não comparar a soma das fases antigas com a nova sem essa ressalva.

Gate final: `c3_final1` PASS (PSNR 21,6 dB, histograma 0,057); `c3_final2` PASS (21,4 dB, 0,026). A verificação mantém PSNR/histograma globais e regiões de HUD/personagem; só detecta corrupção grosseira. Tentativas anteriores tiveram falsos negativos por câmera, posição da janela e pose; não foram usadas como prova de corrupção nem ocultadas. Logs, screenshots, CSV e referência local foram preservados; os nomes finais já existiam de uma sessão anterior, cujos artefatos disponíveis foram copiados para `logs/c3_previous_final_artifacts`, e os resultados atuais são identificados pelo horário no CSV.

O pool da Tarefa 5 não foi implementado: a gravação ainda excedia 33 ms após a Tarefa 4, e o usuário aprovou outra rodada por draw (Tarefas 7–9). Essa rodada trouxe a gravação para aproximadamente 28–32 ms, mas não resolveu o FPS: a investigação apontou replay worker (~40 ms), captura na thread do jogo (22–26 ms com PM4) e GPU (32–35 ms) como próximos limites. Não houve nova otimização nem mudança na exatidão de texturas durante o fechamento. Trabalho em worker/captura/GPU ou gravação paralela exige nova decisão do usuário. O ciclo 2 permanece revertido; seus atalhos de posse e vigilância de texturas não foram retomados.

Verificação final: build da HEAD exit 0; 93 testes nativos e 85 Vulkan sem falhas; contrato `--production-bindings`, recurso base, 15 fixtures de GPU com checagem de pixels e o fixture de posse `hold-lifetime` exit 0; cinco casos de HUD exit 0. A primeira execução de `test_image_gate.ps1` saiu com 1: os testes sintéticos passaram, mas o wildcard tratou quatro screenshots locais como positivos, incluindo D3D12 e tentativas historicamente rejeitadas (`bench_c3_final_d3d12_start.png`, `bench_c3_t4_d3d12_start.png`, `bench_c3_t8_checkaliases_start.png`, `bench_c3_t9_attempt1_start.png`). O commit separado `d5b866f` corrigiu a seleção com inventário explícito de positivos calibrados e negativos históricos, sem alterar limites nem histórico. A execução final passou: 43 checks, zero falhas e zero skips (13 sintéticos/API/CSV + 30 com imagens reais: 19 positivos e 11 negativos). O verify completo condicional não foi executado: `build/vulkan-main` está ausente, embora o corpus local exista (462 binários). As validation layers continuam ausentes; `validation_errors=0` não prova validação por camada. Cenas M3 e ciclo de vida da janela continuam fora deste fechamento.

D3D12 final: 25,5/27,5 FPS, dentro da faixa histórica de cenas (24,5–29,5 parado; 26,4–29,8 andando), mas inferior ao limite de 30 e ao resultado 29,8 da Tarefa 4. Uma execução com câmera variável não comprova ausência de regressão; a medição é inconclusiva nesse critério.
