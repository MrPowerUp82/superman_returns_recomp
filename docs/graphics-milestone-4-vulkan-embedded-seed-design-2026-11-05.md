# M4 — seed portátil de pipelines Vulkan embutido

Design escrito antes da implementação. A inicialização Vulkan já carrega o
journal local `<driver_cache>.descriptions` e pré-compila seus pipelines usando
somente artefatos prontos da biblioteca SRVK. Uma instalação nova não possui
esse journal; os recursos RCDATA 2 e 3 atendem o backend D3D12. A busca limitada
no código em `port/` não encontrou uso de RCDATA 4.

## Evidência e limite de cobertura

Em 05/11/2026, a leitura de
`build/vulkan-m3-runtime/driver.cache.descriptions` encontrou SRVPSO versão 1,
ABI 1, 268 descriptions únicas, 68.688 bytes e checksum FNV64 íntegro
`e6cdf415a933ca79`. SHA256:
`a92aebbb3054795e5e601a5c9af6f866db7637182724e00c53d78fb6401354b2`.
O journal aumentou em relação à observação anterior de 243 pipelines. A cópia
versionada congela essa leitura. São palavras numéricas de estado normalizado e
digests de shaders; não inclui containers, SPIR-V, handles ou cache de driver.
Essa observação cobre os pipelines presentes nesse arquivo, não todas as fases
possíveis do jogo. Não há evidência suficiente para produzir um seed ABI 2.

## Mudança

Versionar o asset em `port/assets/vulkan_pipeline_seed.bin`. Em builds Windows
com `SR_VULKAN_GAME`, embutir como RCDATA 4 quando
`SR_NATIVE_EMBED_PIPELINE_SEED` estiver habilitado. Expor override explícito
`SR_VULKAN_PIPELINE_SEED`, seguindo a convenção do seed D3D12. Atualizar o objeto
RC quando mudar o asset.

No provider Vulkan, carregar o recurso somente quando
`sr_native_pipeline_cache=true` e o backend usar ABI portátil. Uma ponte de uma
linha em GameRenderer encaminha o seed ao GamePipelineStore, antes de
PrewarmObserved. Não importar o seed portátil no perfil global/ABI 2.

O store valida versão, ABI, limites e checksum pelo parser existente, então
mescla por igualdade completa de GamePipelineKey. Preservar todos os registros
únicos locais, ordem local e depois entradas novas do seed. Fazer a mescla em
temporário; erro, ABI divergente ou capacidade excedida preserva o estado local
integralmente. Deduplicar entradas locais e embutidas. O limite continua 8.192
descriptions. Cache desabilitado não importa seed nem grava journal.

O checkpoint existente persiste a união e novas observações. Se um journal local
estiver inválido, manter sua proteção contra sobrescrita e permitir que o seed
embutido forneça pré-compilação sem substituir o arquivo inválido. Falhas de
pré-compilação continuam visíveis e permitem o fallback atual em gameplay.
Nenhum fatal, hook guest ou mudança de captura/tradução de shaders.

## Validação

Testes CPU com dispatch Vulkan simulado: instalação sem journal importa seed e
reutiliza PSOs pré-compilados sem tentativas de criação em draw; journal local
com pipeline adicional sobrevive ao merge/checkpoint; duplicatas não crescem;
checksum e ABI errados não alteram o journal; cache desabilitado ignora import;
capacidade excedida não descarta registros. Validar também o asset versionado
com o parser e o decoder reais. Build CPU limitado a `-j2`.

O build Release, execução Vulkan, benchmark de três rodadas e screenshots ficam
com o agente principal. Testes simulados não comprovam zero stalls de driver nem
cobertura integral do jogo.

## Verificação obtida

Build Release incluiu RCDATA4. Na execução com driver cache e descriptions
anteriores preservados em logs/graphics_native_keyboard_final_*.before, o log
graphics_native_keyboard_final.log registrou `local=0 merged=268 abi=1` e
`observed=268 ready=268 failed=0`. O prewarm frio levou aproximadamente1,17s.
É custo de inicialização; não prova ausência de compilação posterior em outras
fases. A suíte Vulkan terminou com120tests/0failed, incluindo merge atômico,
capacidade, checksum/ABI, decoder de todos268estados versionados e importação
em store novo seguido de prewarm/lookup sem tentativas de criação em draw.
Cache desabilitado recusou importação e fez zero prewarm nesse teste.

## Ampliação observada em06/11

A matriz graphics_final_owner_20261106 encontrou34/0/3 description misses nas
três rodadas Vulkan. Compilações de shader, falhas e shaders desconhecidos
continuaram zerados; esses misses são PSOs novos, não shaders ausentes da SRVK.
Portanto268 descrições não certificam o conjunto agora observado. O journal
persistido já contém302 estados ABI1, com checksum válido.

Antes de trocar o recurso embutido, executar uma coleta maior (65s) para dar ao
checkpoint periódico de5s tempo de registrar os estados descobertos no fim da
janela de30s. Validar magic, versão, ABI, count e FNV64; copiar somente o journal
numérico válido. Atualizar o teste que decodifica TODOS os estados do recurso,
recompilar e repetir três benchmarks com os caches locais preservados fora do
caminho normal. Não promover uma rodada com description misses a zero-runtime.
O resultado e o número final de registros serão documentados após a coleta.

A coleta auxiliar terminou válida sem novos PSOs. O recurso final contém302
registros/76.880bytes, SHA256
fa0f052b2b4190d022922fb562b4e07aae6cb51e00ac30da1d982e7894d66fa9.
O teste decodificou todos302 estados; suíte120/120 passou. No build244ea9da...
as três rodadas graphics_final_body_vk302_20261106_vulkan_r{1,2,3} registraram
zero criações, compilações e misses. A primeira importou302/302 com cache local
ausente. Os arquivos do dono foram preservados e restaurados por hash; os
novos arquivos e logs ficaram separados como evidência local.

## Fechamento posterior de 06/11

A terceira rodada do build3d3ff312 revelou mais uma descrição. O recurso final contém303 registros/77040bytes, SHA256204307a4786cb7d660bfed61b99b7570ece662866cb03bce797edcb908530909. O decoderCPU validou todos303;120testes passaram. No executável final082c3f4b..., três rodadas de60s importaram local=0/merged=303 e terminaram com zero PSOs/compilações/misses. Ambos os caches originais foram restaurados por hash. Detalhes e limitações no relatório graphics-m134-final-validation-2026-11-06.md.
