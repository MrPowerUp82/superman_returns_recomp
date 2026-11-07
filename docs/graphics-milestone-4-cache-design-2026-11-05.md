# Milestone 4 — cache auditável de shaders e PSOs (05/11/2026)

Estado: design para implementação autorizada. Validação por execução coordenada
pelo agente principal; este trabalho não executa build, testes, jogo ou GPU.

## Objetivo e limites

Evitar recompilar descrições D3D12 já observadas e tornar verificáveis a cobertura
do corpus local e os misses de cada rota. Os arquivos de shaders, containers,
SPIR-V e DXIL são derivados da cópia local do jogo e permanecem em artifacts/
ou no diretório do usuário; somente ferramentas, metadados e fontes entram no Git.
Não há promessa de cobertura mundial de gameplay por um corpus finito.

A implementação usa Python existente nas ferramentas, lê CNPS v1/v2 e grava v2; não
modifica as fontes originais do SDK nem o SDK canônico isolado. A instrumentação
C++ entra em pipeline_cache.{h,cpp}; o renderer compartilhado recebe instruções
exatas de integração em documento separado. Não se alteram os defaults gráficos.

## Evidência anterior e falhas identificadas

- artifacts/shaders/catalog.json informa 241 shaders (99 VS, 142 PS). Os reports
  Vulkan portable/global dizem 241 ready; esses totais precisam ser conferidos
  contra os bytes e o corpus, não tomados como uma prova.
- Baseline r1 registrou 813 PSOs precompilados, 376 embedded/6199 local e 108
  criações síncronas; r2/r3 registraram 113/117. As mensagens antigas de shaders
  especializados diziam translated também para hits de disco, portanto não
  demonstram novo compile DXC. Criação de PSO, shader pronto e compilação DXIL
  são eventos distintos e receberão contadores separados.
- Take() consome ready_[key] ao substituir o ComPtr por nullptr. Outras chaves
  da mesma descrição então perdem o objeto, embora by_desc_ ainda o anuncie.
- Take() espera apenas chaves já em pending_: uma descrição conhecida ainda na
  fila pode cair no compilador síncrono do renderer. A configuração do cache
  não garante que precompile termine antes do primeiro draw.
- O carregamento usa known_ como deduplicador de chave, embora o comentário do
  contrato permita descrições distintas com uma chave (mudanças de settings).
  A associação direta precisa se limitar à chave cuja descrição é preservada;
  variantes continuam sendo recuperadas por descrição.

## Abordagens

1. Aumentar apenas seed: acrescenta rotas, mas não corrige consumo de objetos,
   corrida da fila nem ausência de dados de rota. Rejeitado como solução única.
2. Reutilizar objetos canônicos, esperar trabalho conhecido, unir seeds locais
   e auditar bytes + logs + rotas: escolhido como alteração conservadora.
3. Proibir todo compile no draw: pode remover draws necessários à imagem;
   rejeitado. Misses reais conservam fallback e logs, sem mascarar cobertura.

## Comportamento

O cache mantém o PSO pronto até o fim da vida do cache, com aliases de chave para
uma descrição canônica. TakeByDesc reutiliza o mesmo objeto; Take de um item
conhecido e ainda na fila espera o worker e o prioriza. Falhas de lookup e driver
são terminais e acordam waiters, inclusive quando compiled_ é zero. Somente o
resultado bem-sucedido conta como precompiled; tentativas/falhas são separadas.
Misses distinguem unknown_key, unknown_desc, unavailable e stopping; os totais
finais são cumulativos, e misses de lookup não são chamados compiles runtime.

O packer aceita múltiplos CNPS compatíveis, preserva descrições diferentes mesmo
com chave igual e registra proveniência/hash de cada entrada. Não gera shaders.
A deduplicação de bytes é conservadora: padding diferente pode manter registros
redundantes; o cache nativo deduplica campo a campo com DescHash.

Extensão aprovada na coordenação: CNPS v2 acrescenta pixel_variant/pipeline_kind para
separar PS original, RT e coverage. O loader mantém leitura v1 e migra o arquivo
local por publicação atômica com backup .v1.bak. ShaderTranslator prepara as
especializações reconhecidas do corpus antes do percurso medido; o callback do
cache fornece esses bytes aos workers, sem recorrer ao PS original. Uma versão
nova da fingerprint usa conteúdo das ferramentas e contrato da compilação,
evitando reutilização indevida após troca de binários de mesmo tamanho.

O auditor offline confere containers e artefatos das bibliotecas, versões,
checksums, bounds, unicidade, stage, hashes, tamanho e correspondência exata com
raw/. Para Vulkan usa também a reflexão/verificador já existente por profile.
Totais em reports externos não substituem esta validação.

Um manifesto de rotas identifica cada log, API/profile, conclusão do percurso,
settings, executável e bibliotecas usados. O relatório retém hashes dos logs,
contagem de eventos e razões de insuficiência. Uma rota só é aprovada como zero
runtime misses com início/fim auditados e contadores explícitos, não pela
simples ausência de texto de erro. Logs legados continuam úteis como inventário,
mas nunca demonstram cobertura 100%. O seed deve ser medido novamente no mesmo
percurso depois de gerado; incorporá-lo não transforma o run anterior em hit.

## Integração e validação

O agente principal executará testes Python sintéticos de corrupção, unions e
relatórios fail-closed, build/testes nativos e percursos GPU sequenciais. Testes
de concorrência devem cobrir alias após primeiro Take, item ainda na fila,
shader ausente, driver failure e destruição com waiter. Instrumentação no
renderer precisa emitir counters de shader misses/compiles e PSOs criados,
inclusive variantes motion/RT e passes utilitários, em delimitadores de rota.
Sem esses hooks uma rota permanece insuficientemente instrumentada.

Critério de aceite: corpus local auditado por artefato/API, manifestos completos,
zero misses e zero compile runtime em todos os percursos exigidos depois do
prewarm, imagem correta e medições sem regressão. Qualquer outro estado é parcial
e deve relatar a lista real de faltas, sem arredondar para 100%.

### Percursos fora do benchmark de título

Adicionar canal diagnóstico opt-in `sr_native_cache_audit_commands`, lido no
present existente, para delimitar `begin <token>` / `end <token>` em gameplay.
Somente contabiliza atomics do renderer; não executa comandos guest nem altera
renderização. Poll máximo 10 Hz quando configurado, sem I/O quando desativado.
O harness exige HUD antes/depois e screenshots; fim precisa de provider idêntico,
contadores monotônicos e frames/draws positivos. Falhas não podem virar zero.

### Pipelines utilitários fora das descrições guest

A janela de 30 s expôs 99 criações em M1 e 100 em M2: 96 comparações de
TemporalMotion inicializadas somente no primeiro HDR elegível, mais utilitários
de reinterpretation e uma descrição guest tardia. A aceitação inclui estes
serviços, não apenas GetPipeline. Inicializar TemporalMotion e DXR compute no
startup, manter PSOs entre mudanças de resolução (os slots já redimensionam
recursos após fence), pré-aquecer reinterpretation nos formatos/tamanhos de MSAA
suportados. FXAA terá criação separada da gravação para pré-aquecer seu PSO.
Preservar contador cumulativo: não resetar os 99 para fingir zero. Acrescentar
seed de descrições tardias observadas e repetir a janela completa.

O journal SRTMPSO local revelou três estados enviesados D24S8: cull BACK, CCW,
bias5873, slopebits1024416809, clamp0, comparações EQUAL/ALWAYS/GEQUAL. Incorporar
esses registros numéricos ao prewarm independente de arquivo local. O seed
contém apenas metadados de rasterização; nenhum bytecode. Verificar instalação
sem CNPS/SRTMPSO locais, preservando os caches originais para restauração.
