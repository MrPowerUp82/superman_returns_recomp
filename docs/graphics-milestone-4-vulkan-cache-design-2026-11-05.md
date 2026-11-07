# M4 Vulkan — descrições observadas, prewarm e auditoria (05/11/2026)

Implementação autorizada na coordenação; o agente principal controla execução de
build/testes/GPU. Este desenho antecede o código. O objetivo é retirar do draw a
criação de pipelines Vulkan previamente observados e demonstrar por rota a
quantidade real de misses, jobs, hits e compilações. SRVK 241/241 só cobre o corpus.

## Estado existente e escolha

GamePipelineStore mantém GamePipelineKey/objetos apenas em memória e persiste o
blob opaco do driver. A chave já contém estado normalizado, formatos, bindings,
attributes e digests SPIR-V. Reusar somente driver.cache não remove
vkCreateGraphicsPipelines do primeiro draw. O serviço SRVK retorna artefatos
prontos, mas um miss pode lançar Python e este pode dar hit de disco.

Escolha: persistir as chaves normalizadas em formato SRVPSO próprio, recuperar
artefatos prontos por digest/stage/ABI, reconstruir o plano e um render pass
compatível sem framebuffer, criar pipelines antes de publicar o backend. A
alternativa de serializar DrawPacket guardaria dados e ponteiros desnecessários;
reutilizar CNPS seria incorreto por API/layout. O cache opaco existente é mantido.

## Contrato

- SRVPSO v1 contém ABI, checksum, registros de u64 limitados e contagens máximas.
  Não contém handles, ponteiros, containers, DXIL ou SPIR-V. Descrições só são
  adicionadas após criação bem-sucedida de uma combinação realmente observada.
- Cada descrição deve reconstruir a mesma chave normalizada; entradas inválidas,
  shaders ausentes/ambíguos ou ABI diferente ficam visíveis e não viram hits.
- A publicação reaproveita CheckpointCache e substituição atômica. Sem flag
  sr_native_pipeline_cache, não carregar/salvar driver cache ou descrições, nem
  fazer prewarm adicional; cache em memória e fallback correto continuam.
- Prewarm usa somente shaders prontos do SRVK/serviço; não lança traduções a
  partir de uma descrição. Bibliotecas diferentes invalidam digests naturalmente.
- Contadores próprios Vulkan distinguem tentativas/falhas de pipeline, startup
  versus runtime, miss de descrição, hit de memória, miss SRVK, job de processo,
  cache hit de tradução e invocações reais emitter/DXC. Registros suplementares
  do processo mantêm o protocolo SVR3 existente sem alterações incompatíveis.
- Provider do benchmark lê somente atomics de vida de processo. Frames/draws
  são publicados no renderer/recording thread; nenhuma leitura de mapas de outro
  thread. Missing instrumentation nunca é interpretado como zero.

## Utilitários e limites

GamePipelineStore compartilhado também contará compositor/UI. Preparar seus
estados para formatos de apresentação suportados, incluindo linhas/triângulos
UI; as dimensões/scissor são dinâmicas. Inicializar os dois kernels de depth/
alias durante startup quando a opção estiver ligada. Toda criação posterior
continua contada. Código opaco de frame generation/driver não é certificado por
contadores das chamadas pertencentes ao port.

Não há afirmação all-game. A primeira execução pode registrar combinações novas;
a seguinte, com mesmo percurso/API/profile/settings, deve demonstrar ausência
real de misses/compiles e manter a imagem correta. Novos formatos/estados conservam
fallback. Falhas de arquivo/seed/prewarm são reportadas, sem apagar caches antigos.

## Validação coordenada

Testes CPU: roundtrip/checksum/truncamento/ABI/limites dos seeds, reconstrução de
estado sem handles, lookup de shader por digest, separação hits/misses, cache
Vulkan stub com criação inicial versus reutilização e prewarm. Regressão do
serviço e store existentes deve continuar passando. Testes com dispositivo real,
rotas e captura visual ficam exclusivamente com o agente principal. Nenhum
resultado zero será inferido de sucesso de parsing do seed ou de SRVK 241/241.

## Implementação final e evidência CPU

O cache de descrições foi implementado no GamePipelineStore e usa
`<driver.cache>[.global].descriptions`. SRVPSO v1 tem header de 32 bytes:
magic SRVPSO, versão, ABI, count, reservado e checksum FNV64 do payload. Cada
registro possui count de palavras/reservado e a sequência GamePipelineKey em
u64 little-endian, limitada a 8192 registros/2048 palavras/64 MiB. Formatos,
bindings, atributos, estado fixo e digests SPIR-V são reconstruídos; nenhum
VkRenderPass, VkPipeline, framebuffer ou ponteiro é persistido. O prewarm cria
um render pass compatível sem framebuffer, usa shaders prontos por digest/stage/
ABI e conserva os PSOs no mesmo mapa usado pelo draw. Lookup ausente/ambíguo,
seed corrompido ou falha de dispositivo são reportados e mantêm o fallback.
Seed inválido fica preservado e sua escrita é desabilitada no processo.

A persistência aproveita CheckpointCache periódico já existente (primeiro
frame/depois a cada 5 s), publica driver cache e descrição por arquivos temporários
com substituição atômica separada. Não há I/O novo em Acquire/draw. Um encerramento
abrupto antes do checkpoint pode perder descrições novas; isso continua visível
como miss na próxima execução. Descrições não são copiadas para o Git.

O provider consulta sr_native_pipeline_cache. Off deixa ambos os paths de arquivo
vazios e não chama prewarm adicional; cache em memória/fallback continuam corretos.
On faz prewarm observado antes de publicar o backend, prepara compositor para
quatro formatos RGBA/BGRA UNORM/SRGB, UI linhas/triângulos nesses formatos e os
dois kernels compute de depth/alias. Novos formatos seguem fallback contado.
Um erro de prewarm de depth aborta inicialização para não deixar helper parcialmente
inicializado; falhas de descrições/UI/compositor são reportadas e mantêm fallback.

VulkanCacheAudit usa atomics próprios de vida de processo. O benchmark lê o
provider sem dereferenciar Host ou mapas de outra thread. Frames contam submissões
Vulkan bem-sucedidas e draws contam comandos originais emitidos. Tentativas e
falhas de vkCreateGraphicsPipelines/vkCreateComputePipelines do backend pertencente
ao port são separadas entre startup/runtime. Hits de pipeline, misses de descrição,
hits SRVK, hits memória, misses de shader, jobs, processos falhos, hits em disco,
invocações emitter/DXC e ausência de prova têm contadores distintos, publicados em
`native cache audit: vulkan_totals` no começo/fim da rota. O campo genérico
shader_misses do provider agrega conservadoramente misses de shader/descrição,
not-ready, jobs e provas desconhecidas; o log individual preserva o significado.

SVR3 e SRVK não mudaram. runtime_vulkan_shader.py publica sidecar ASCII
`result.bin.audit` (`schema cache_hit compiler_invocations`) antes do resultado
SVR3; compile_vulkan.py relata hit do cache validado e tentativas reais de chamar
emitter/DXC. Script antigo/sidecar ausente vira unknown_audits, nunca zero.
Jobs ainda ativos no snapshot da rota são logados e o auditor reprova sua janela.
O backend externo LSFG pode criar pipelines fora do dispatch pertencente ao port;
se habilitado no processo, a rota recebe vulkan_uninstrumented_scope e não é
certificada zero por cache_audit.py. Nenhum contador falso foi acrescentado ao
backend externo nem se confundiu a carga SRVK com cobertura de pipeline.

Validação CPU coordenada e executada: `cmake --build build/tests-vulkan --target
sr_vulkan_tests -j2`, depois `sr_vulkan_tests.exe`: 115 testes, zero checks falhos.
Inclui roundtrip/ABI/checksum/limites, reconstrução de raster/vertex sem handles,
prewarm de builtin com reutilização e falha de runtime contada, persistência de
uma descrição observada e prewarm na próxima instância via SRVK, lookup ausente
mantendo fallback, lookup de digest sem compilador e distinção entre disk hit,
invocação do compilador e sidecar ausente. Logs locais:
`artifacts/tests-m4/vulkan-cpu-build.log` e `vulkan-cpu.log`.

Python: test_vulkan_cache.py 5/5, test_runtime_vulkan_shader.py 8/8 e ferramentas
M4 16/16 no primeiro fechamento. Avisos getenv deprecated preexistentes apareceram
no build CPU em presentation_pacing.h/frame_loop/game_renderer/game_frame; nenhum
erro de compilação. Native provider depende do SDK e será validado pelo build
Release coordenado pelo agente principal, não pela suíte sr_vulkan_core isolada.

Não foi executado jogo, benchmark, teste GPU ou captura por este agente nesta
extensão Vulkan. Não há seed de percurso real novo nem prova de zero runtime
misses de gameplay. O primeiro percurso registra descrições; o seguinte precisa
mostrar prewarm e counters zero na mesma API/profile/settings, com captura real.

Revisão final: FindReadyByDigest usa índice construído uma vez por artefato pronto,
evita rehash do corpus inteiro por PSO e verifica colisões por bytes SPIR-V.
O parser também exige prefixo de estado canônico reconstruído, além dos limites.
A auditoria registra cada tentativa de subprocesso separadamente: um retry não
apaga as invocações emitter/DXC comprovadas do processo anterior; ausência de
sidecar em qualquer tentativa fica desconhecida. A regressão específica de retry
foi acrescentada. Build CPU final e suíte concluídos com **116 testes e zero checks
falhos**; ferramentas Python M4 finais **17/17**, incluindo recusa de escopo externo
LSFG e jobs em voo nos delimitadores. O build final incremental CPU saiu sem
warnings nas duas unidades recompiladas. Não houve execução GPU nesta revisão.
