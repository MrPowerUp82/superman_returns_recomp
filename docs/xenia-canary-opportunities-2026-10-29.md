# Melhorias aproveitáveis do Xenia Canary — 29/10/2026

## Escopo e método

Pesquisa solicitada sobre o próprio Canary, comparando seus mecanismos com o
renderer nativo e o runtime deste projeto. Fonte oficial: branch
`canary_experimental`, commit
[`48b318a2ad7c36f8f3901a94e290ba70ae0ffec5`](https://github.com/xenia-canary/xenia-canary/commit/48b318a2ad7c36f8f3901a94e290ba70ae0ffec5),
verificado no remoto e pelo conector GitHub. A cópia completa está em
`.tools/xenia-canary-research-20261029`, sem modificações. O runtime comparado
é o ReXGlue fixado em `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, com os
patches de tracking do projeto e as alterações locais atuais.

A skill `reverse-engineer-anything` foi aplicada pela rota de código-fonte:
ela orienta usar ferramentas normais quando a fonte completa está disponível.
Não houve decompilação, sessão nativa REA, instalação de provedores ou Evidence
IDs do REA. As referências abaixo são evidências de fonte e análise offline;
não são observações novas de execução do Canary.

Nenhum código de produção foi alterado nesta pesquisa. Não foi executado um
novo benchmark nem teste auditivo. Os ganhos descritos são hipóteses a medir.

## Prioridades

| Ordem | Candidato | Evidência de aplicabilidade | Benefício a testar |
| --- | --- | --- | --- |
| 1 | Preparar apenas texturas usadas pelo par VS/PS | No frame histórico: 13.810 de 21.120 avaliações de slots não correspondem a slots usados pelos shaders | Menos trabalho em slots, lookup, tracking e samplers |
| 2 | Separar armazenamento de textura e estado do sampler/view | 622 fetches distintos viram 580 grupos ao remover somente bits de sampler; 42 identidades extras em 39 grupos | Menos texturas, decodes, uploads e memória duplicados |
| 3 | Invalidar constantes apenas quando registradores usados mudarem | Canary possui mapas por shader; aqui as versões e uploads ainda são por banco | Menos uploads VS/PS e bindings redundantes |
| 4 | Drenar subframes XMA pendentes antes de encerrar por falta de entrada | Diferença direta no loop `Work()` do decoder | Melhor entrega de PCM e menos retries no mixer |
| 5 | Reutilizar alocações de texturas alteradas e atualizar só mips sujos | Canary separa dirty/watch de base/mips; aqui mudanças podem recriar a textura inteira | Menos criação de recursos e bytes decodificados/copied |

### 1. Máscara de texturas usadas

**Observação:** o Canary combina `GetUsedTextureMaskAfterTranslation()` do VS
e PS e passa essa máscara para `RequestTextures`:
[d3d12_command_processor.cc:2850](https://github.com/xenia-canary/xenia-canary/blob/48b318a2ad7c36f8f3901a94e290ba70ae0ffec5/src/xenia/gpu/d3d12/d3d12_command_processor.cc#L2850).
O nosso `Renderer::UploadConstants`, em
`port/src/native_renderer/native_renderer.cpp:4511`, percorre os 16 slots e
prepara todo fetch marcado como textura. O caminho global Vulkan também
percorre os slots ligados (`global_draw_bindings.cpp:167`).

**Evidência local:** o frame 2034 de
`logs/stacked_fix_20261023_on2_frame.json` contém 1.920 draws. Cruzando as linhas
detalhadas de todos os slots com `tfetch_filters` do catálogo: 21.120 bindings,
7.310 referenciados e 13.810 sem referência no par de shaders. Em 820 draws do
par `C7C0E381A3C9B79E` / `DD925AEBAD3D08B7`, havia 11 slots ligados e apenas um
usado. As chamadas TFETCH e seus comentários de metadata foram conferidos nos
241 HLSLs; não houve chamadas sem metadata nesse corpus.

**Inferência:** há trabalho evitável mesmo com cache de slots. Os 65,4% são
uma fração das avaliações de bindings desse frame, não do tempo de CPU e
muito menos do FPS. Binding avaliado também não equivale a cache miss.

**Aplicação:** publicar a máscara junto do shader traduzido, usar a união
VS/PS e preencher slots ignorados com descriptors nulos. Máscara desconhecida
deve conservar o caminho completo. Preservar bindings extras de variantes RT,
motion capture, probes e mudanças de shader. A máscara pode ser conservadora
quanto a branches: não é necessário adivinhar o ramo executado.

**Medição:** avaliações, misses, `constant_slots_ns`, `texture_lookup_ns`,
hashes/uploads e comparação de imagem nas mesmas cenas, com máscaras On/Off.

### 2. Armazenamento separado de sampler/view

**Observação:** o Canary mantém uma
[TextureKey de storage/layout](https://github.com/xenia-canary/xenia-canary/blob/48b318a2ad7c36f8f3901a94e290ba70ae0ffec5/src/xenia/gpu/texture_cache.h#L176)
e uma
[chave separada de SRV](https://github.com/xenia-canary/xenia-canary/blob/48b318a2ad7c36f8f3901a94e290ba70ae0ffec5/src/xenia/gpu/d3d12/d3d12_texture_cache.cc#L1946).
Aqui, D3D12 com tracking On usa os seis dwords como chave em
`native_renderer.cpp:3625`; Vulkan faz o mesmo em
`port/src/graphics/vulkan/descriptors.cpp:6`. O próprio SDK já tem separação
entre `TextureSRVKey` e `SamplerParameters` no backend Xenos.

**Evidência local:** no mesmo frame, 622 fetches de texturas de CPU formam 580
grupos removendo apenas clamp U/V/W, filtros mag/min/mip/aniso, filtros de
volume e LOD bias. Foram preservados formato, signs, swizzle, exp_adjust,
pitch, tiling, dimensões, níveis de mip e endereços. Há 39 grupos com variantes
e 42 identidades extras. Isso demonstra a presença dessas variantes; não mede
quantos bytes de alocações duplicadas estavam residentes.

**Aplicação:** começar compartilhando backing entre variantes somente de
sampler. Manter identidade exata de binding e provas de conteúdo/versionamento;
não reduzir a comparação do descritor atual. Signed/gamma e conversões que
alterem o decode precisam continuar separados ou usar views compatíveis.

**Medição:** backing allocations por storage, decodes, uploads, VRAM e frames
com mudança de sampler, conteúdo e identidade de alocação.

### 3. Constantes dependentes do shader

**Observação:** Canary usa
[ConstantRegisterMap](https://github.com/xenia-canary/xenia-canary/blob/48b318a2ad7c36f8f3901a94e290ba70ae0ffec5/src/xenia/gpu/shader.h#L751),
invalida CBVs apenas se um registrador utilizado mudou
([command processor:1841](https://github.com/xenia-canary/xenia-canary/blob/48b318a2ad7c36f8f3901a94e290ba70ae0ffec5/src/xenia/gpu/d3d12/d3d12_command_processor.cc#L1841))
e envia constantes conforme o mapa
([:4969](https://github.com/xenia-canary/xenia-canary/blob/48b318a2ad7c36f8f3901a94e290ba70ae0ffec5/src/xenia/gpu/d3d12/d3d12_command_processor.cc#L4969)).
Nosso `Pm4Mirror` já compara valores e sanitiza somente regiões sujas, mas
`UploadConstants` ainda aloca/copia 4.096 bytes por banco cuja versão mudou.
Também repete os três `SetGraphicsRootConstantBufferView` em
`native_renderer.cpp:4901`; o Canary usa bits de validade para esses bindings.

**Aplicação:** primeiro evitar invalidar o upload por mudanças em constantes
que o shader não lê, preservando o layout atual. Compactar a disposição dos
registradores exige alterar também o shader/ABI e não é um simples ajuste de
tamanho. Endereçamento dinâmico precisa de máscara completa, como no Canary.
Caching dos CBVs precisa ser invalidado em reset da command list, troca de
root signature e passes internos de blit/depth/motion.

**Medição:** uploads/reuses, bytes por banco, tempo de floats e root bindings.
O log anterior `texture_fast_ab_1_game.log` registra floats de 0,47–1,13 ms em
duas janelas finais; essa faixa é custo observado, não economia prometida.

### 4. Drenagem XMA antes de desistir

**Observação:**
[Canary xma_context_new.cc:183](https://github.com/xenia-canary/xenia-canary/blob/48b318a2ad7c36f8f3901a94e290ba70ae0ffec5/src/xenia/apu/xma_context_new.cc#L183)
continua o loop enquanto há subframes decodificados pendentes, mesmo quando a
entrada acabou ou o erro é 4. Ele também testa ausência de progresso para
evitar spin. Nosso runtime em
`.tools/rexglue-write-tracking-source/src/audio/xma_context.cpp:135` encerra o
loop nesses estados sem exigir que `current_frame_remaining_subframes_` seja
zero; `Consume()` entrega um número limitado de subframes por passagem.

**Inferência:** adaptar essa condição pode reduzir PCM retido e dependência
dos kicks do hook. É diferente das correções de packet bits, r3 e carry final
já feitas. Não comprova que esta seja a causa de um corte específico no jogo.

**Aplicação:** portar apenas a drenagem e o teste de progresso; preservar
carry, padding, truncamento de loop, espaço de saída e execução síncrona do
ReXGlue. Não substituir o decoder inteiro pelo do Canary.

**Medição:** subframes pendentes ao sair de `Work()`, blocos por kick,
retries/give-ups, contagem e conteúdo PCM mono/stereo, fim de stream e voz
aguardando novos pacotes; conferir música/voz após decolar no jogo.

### 5. Atualizações seletivas e alocações reutilizadas

**Observação:** Canary mantém
[watches independentes de base/mips](https://github.com/xenia-canary/xenia-canary/blob/48b318a2ad7c36f8f3901a94e290ba70ae0ffec5/src/xenia/gpu/texture_cache.cc#L543)
e [seleciona níveis a enviar](https://github.com/xenia-canary/xenia-canary/blob/48b318a2ad7c36f8f3901a94e290ba70ae0ffec5/src/xenia/gpu/d3d12/d3d12_texture_cache.cc#L1473).
Aqui o D3D12 aposenta recurso/SRV ao detectar outro hash
(`native_renderer.cpp:3757`); Vulkan aloca imagem/memória/view para outra versão
(`resources.cpp:281`). O backend Xenos do próprio SDK já tem carregamento
seletivo como referência adicional.

**Aplicação:** reaproveitar alocações compatíveis e depois limitar decode/copy
aos grupos de mips sujos. Preservar conteúdo antigo usado por draws gravados.
No Vulkan, uploads podem estar numa command buffer separada: sobrescrever o
mesmo recurso antes dos draws antigos exige versionamento/copy-on-write.
Packed mip tails e faixas sobrepostas precisam invalidar todos os níveis
atingidos; falta de cobertura exige fallback conservador.

**Medição:** criações de recursos/SRVs, bytes decodificados/enviados e tempo
por alteração; validar streaming de mips e várias versões na mesma submissão.

## Outras diferenças e o que não priorizar agora

| Candidato | Evidência e decisão |
| --- | --- |
| Agrupar barriers D3D12 | Canary `d3d12_command_processor.cc:111–151` acumula barriers; nosso `Transition():1841` chama a API por transição. Candidato médio: primeiro contar chamadas e medir; flush precisa ocorrer antes do consumidor/copy/clear correto. |
| Cadência do áudio | Canary `apu/audio_system.cc:94` usa deadlines de cerca de 5,333 ms e semáforos como back-pressure. Nosso `audio_system.cpp:99` usa WaitAny, e o SDL pode liberar vários tokens juntos. Candidato para jitter/underruns; adaptar a SDL3 e revisar ordem de locks, pause/resume e vida do cliente antes de portar. |
| Yield Windows | Canary `base/threading_win.cc:128` usa NtDelayExecution zero/Sleep(0); ReXGlue `core/threading_win.cpp:87` usa SwitchToThread e MemoryBarrier. Experimento somente em contenção comprovada, preservando ordenação de memória. Sem evidência atual de gargalo nesse caminho. |
| Pool de overflow de upload | Canary `ui/graphics_upload_buffer_pool.cc:20` recicla páginas após fence; aqui overflow é recriado/aposentado. Porém o log anterior mostrou `overflow_pages=0` e pico ~51 MiB abaixo do ring de 128 MiB: não é prioridade nessa amostra. |
| Mais threads/async para PSO | Já há precompile, cache e writer assíncrono aqui. No log anterior, 446 PSOs foram pré-compilados em 0,2 s; as 104 criações síncronas somaram 17,67 ms, máximo 0,68 ms. Não demonstram origem dos stalls de centenas de ms. Pular draws para imitar o async do Canary exige comprovar passes redesenhados; `kRedrawnPasses` ainda é `{-1}`. |
| Eventos/timers | Há diferenças de correção no Canary, mas os imports de timer/query-event citados não foram encontrados no mapa de registro gerado inspecionado. Impacto neste jogo não estabelecido; não classificar como otimização confirmada. |
| Otimizações de JIT | Não transferem diretamente à recompilação estática C++ deste projeto. |
| Vblank, staging CPU, tracking, cache de resolves | Já portados/implementados; não foram contados como novas oportunidades. Occlusion queries já foram investigadas anteriormente sem demonstrar benefício. |

## Evidência reproduzível e cobertura

Probe offline executado com sucesso:
`python .tools/canary_reference_probe_20261029.py`.
Resultado: `logs/xenia_canary_research_20261029_probe.json`, incluindo SHA256
dos dois inputs, cobertura TFETCH, contagens por par de shaders e as limitações.
O trace de 23/10 e o log de tempos de 29/10 são execuções diferentes e não
foram apresentados como uma medição única.

| Pergunta da investigação | Estado |
| --- | --- |
| Fonte oficial e versão identificadas? | Respondida: remoto, conector e checkout fixado. |
| Recursos/texturas comparados? | Respondida por fonte e contagens offline; ganhos de tempo ainda desconhecidos. |
| Shaders/constantes/pipelines comparados? | Respondida; máscara de uso é o candidato de CPU com melhor evidência local. |
| Threads/áudio comparados? | Respondida na fonte; XMA tem diferença concreta, efeito no Superman ainda exige teste. |
| Evitadas sugestões já implementadas/inaplicáveis? | Respondida e registradas acima. |
| Ganho de FPS, imagem e áudio comprovados? | Não: pesquisa não implementou candidatos nem executou novos ensaios de gameplay. |

## Implementações posteriores à pesquisa

1. [Máscara de texturas usadas](used-texture-mask-2026-10-29.md): implementada
   com fallback conservador e testes nas duas APIs.
2. [Armazenamento compartilhado entre samplers](texture-storage-reuse-2026-10-29.md):
   implementado com identidade exata, proteção de versões e readbacks GPU On/Off.
   Ativo por padrão no D3D12; Vulkan experimental/opt-in após queda repetida
   de FPS nos pares desta máquina.

3. [Constantes usadas pelo shader](used-constant-mask-2026-10-29.md): implementadas
   com fallback dinâmico, baseline de CPU e testes SDK/GPU. Experimental e Off
   por padrão após regressão nos dois pares D3D12; um par Vulkan foi favorável.

4. [Drenagem de PCM XMA pendente](xma-pending-drain-2026-10-30.md): aplicada ao
   runtime padrão, com regressão reproduzida e testes reais de Work/Consume.
   Mantém o flush de carry separado; continuidade auditiva após decolar ainda
   não foi confirmada nesta etapa.

5. [Atualizações seletivas de texturas](texture-updates-2026-10-30.md): implementadas
   nas duas APIs, com alocações compatíveis, baselines limitados e uploads de
   subresources alterados. No D3D12, versões pendentes podem copiar mips iguais
   pela GPU; no Vulkan, holders pendentes forçam substituição completa. O decode
   de CPU continua completo. Flag experimental `SR_NATIVE_TEXTURE_UPDATES=1`.

Os cinco candidatos receberam implementação. As opções experimentais conservam
seus padrões descritos nos relatórios; ganho geral de FPS, equivalência visual
de todas as cenas e continuidade auditiva após decolar não estão comprovados.
