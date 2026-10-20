# Contrato global Vulkan — 17/10/2026

## Resultado e alcance

O compilador tem um perfil experimental explícito `global`, com ABI
`sr-vulkan-global-v1`, backend/cache `vulkan-global` e relatório separado.
O perfil portátil continua sendo o padrão. O executável do jogo continua com
SHA256 `A1E78317C3DD455BAB2A4C80BB98C7FF5E6CBC7D8963F8CA1D5D6B7625F8628A`.

O trabalho elimina uma incerteza para a mudança estrutural do recorder:
os shaders podem consultar tabelas globais mantendo suas entradas e saídas,
sem exigir draws indiretos nem alterar o emissor de funções de entrada.
Ainda não há integração desse ABI no recorder nem nova medição de FPS do jogo.
A última rodada real continua em aproximadamente 13–15 FPS. A meta de
60 quadros reais por segundo no Vulkan nativo permanece pendente.

## Contrato implementado no compilador

`tools/shaders/vulkan_global_contract.py` adapta uma cópia do common portátil
em memória. Recusa declarações desconhecidas, preserva as funções existentes
de fetch, bool, loop, gamma e escala e mantém o arquivo de entrada intacto.

- Set 0: imagens 2D no binding 0, buffers no 1, samplers no 2, imagens 3D no 3
  e cube no 4; arrays de descritores sem tamanho fixo no shader.
- Push constants de 8 bytes: índice do draw e slot do buffer da tabela da
  submissão, disponíveis para VS e PS.
- Cabeçalho de 16 bytes: slots dos buffers de constantes, layouts e streams.
- Registro por draw de 16 bytes: offsets de layout, streams e constantes.
- Constantes por draw: VS 4096 + PS 4096 + shared 512 = **8704 bytes**.
  O registro de 16 bytes não substitui esses bancos.
- Layout: até 224 atributos de 16 bytes, compartilhados entre draws.
  Cada stream por draw ocupa 16 bytes e fornece buffer, offset e stride;
  o fetch existente recebe os metadados reconstruídos na GPU.
- Flags compactadas de textura continuam presentes para gamma, escala dos
  texels guest e filtragem. O acesso ao heap mascara somente o índice.

A identidade do cache inclui o backend, contrato, argumentos, ferramentas e
hash do common efetivamente adaptado. Uma entrada portátil não responde a uma
requisição global. O runtime existente não foi habilitado para esse perfil.
O validador portátil continua recusando os descritores globais.

`required_features` inclui a indexação dinâmica de storage buffers, além da
indexação não uniforme, arrays em runtime, descritores parcialmente preenchidos
e atualização após bind. As capacidades e os limites reais dos heaps ainda
devem ser negociados pelo futuro renderer. O relatório global não apresenta
arrays sem tamanho como se exigissem zero descritores alocados.

## Evidência com shaders reais

Os 241 containers locais passaram pelo emissor existente, pelo perfil global
e pelo `spirv-val` local. As entradas e saídas dos 241 artefatos foram comparadas
com o perfil portátil e permaneceram iguais. Isso verifica compilação,
metadados e interfaces; não equivale a executar os shaders do jogo em uma cena.
O corpus inclui usos dos cinco tipos de binding. Não redistribuir esses
containers ou os shaders derivados como parte do código-fonte.

Reprodução no checkout preparado:

```powershell
python tools/shaders/validate_vulkan_corpus.py --profile global --jobs 4
python -m pytest tests -q
```

O relatório fica em `artifacts/shaders/vulkan-global/report.json`, e o cache em
`artifacts/shaders/vulkan-global/shader_cache/vulkan-global/`.
São 102 testes Python aprovados. Evidências:
`logs/native60_global_python_suite.log`,
`logs/native60_global_profile_corpus_final.log` e
`logs/native60_global_profile_interfaces.log`.

Uma revisão independente apontou a ausência da feature de indexação dinâmica
de storage buffers na declaração do contrato. Um teste reproduziu a ausência;
a declaração foi corrigida e a suíte passou novamente.

## Readbacks e recursos pendentes

A fixture descartável compara o `GameRenderer` atual com três alternativas:
draws diretos com DrawIndex, multi-draw indireto e draws diretos com push
constants. Esta última usa o common produzido pelo novo módulo do projeto.

Foram 48 readbacks: 3 variantes de bool/loop, 2 ou 3000 draws, com e sem índices,
nos quatro caminhos. Todos os 256 pixels e quatro canais de cada imagem são
verificados sem tolerância, também byte a byte contra a referência atual.
São exercitados bancos VS/PS, o semantic 223, float2/half2, firstVertex,
firstIndex e baseVertex positivos e negativos, troca de textura e ordem.

A prova adicional mantém duas submissões realmente pendentes, confirmadas
por `vkGetFenceStatus`, enquanto prepara e atualiza os descritores da segunda.
Um semáforo timeline libera a GPU somente depois disso. As duas versões usam
slots e recursos distintos e seus readbacks passam. Reutilizar os slots da
primeira submissão faz a comparação de pixels falhar, como esperado.
Os outros controles negativos forçam draw 0, bool zero e loop zero e também
falham nas asserções de pixels esperadas.

As ferramentas Khronos estavam instaladas em `.tools/vulkan-validation/Bin`,
fora do PATH anterior. A execução final com a layer ativa passou com
`validation_active=1 validation_errors=0`. É validação padrão; não afirmar
GPU-assisted validation ou sincronização instrumentada habilitadas.

Uma versão anterior da sonda usava um evento host para manter a GPU pendente.
A layer identificou uso inválido desse evento. Essa versão foi substituída
pelo semáforo timeline; seus resultados não servem como prova válida da
sincronização. O log de falha foi preservado em
`logs/native60_contract_validation.log`, e a correção em
`logs/native60_contract_product_header_validation.log`.

Reprodução da fixture no checkout preparado:

```powershell
build/run_native60_contract_batch.ps1 -Name native60_contract_check -Validation
build/run_native60_contract_batch.ps1 -Name native60_contract_reuse -SkipBuild -Fault reuse_slots
```

## Tempo de CPU da fixture

Na execução final sem validation layers, seis casos com 3000 draws mediram:

| Caminho | Mediana de preparar + gravar | Faixa |
|---|---:|---:|
| GameRenderer atual | 24,21 ms | 22,62–30,10 ms |
| Global, draw direto com push | 9,33 ms | 7,52–12,86 ms |
| Global, draw direto com DrawIndex | 9,08 ms | 7,49–10,67 ms |
| Global, multi-draw indireto | 8,88 ms | 6,83–11,79 ms |

A janela começa antes da preparação dos recursos e termina depois da gravação,
antes do submit/wait. Inclui alocações e criação inicial do pipeline da fixture;
não mede só chamadas `vkCmdDraw`. Log:
`logs/native60_contract_product_timing.log`, ordem invertida de referência.
Os dois casos adicionais de vida útil não entram nessa mediana.

Esse resultado orienta a primeira integração para draws diretos com push:
a mudança de descritores e metadados concentra o ganho observado; draw indireto
exige mudanças extras para um ganho pequeno nessa carga. A fixture tem um
pipeline, duas texturas e um pass simples. Não permite prever a redução dos
51 ms do recorder real nem o FPS final.

## Integração ainda necessária

Conectar o contrato ao `Context`, ao serviço/cache de shaders do runtime,
ao `GamePipelineStore` e ao recorder. Implementar um heap que associe slots a
versões de recursos e os recicle somente depois da fence; incluir samplers,
todos os tipos de imagem e mudanças de views de resolves/aliases. A preparação
deve compartilhar layouts e escrever constantes/streams diretamente na arena.
Preservar a sequência, passes, estados dinâmicos e dependências atuais.

Antes de usar o perfil no jogo, ainda faltam pixels com samplers, gamma,
3D/cube, múltiplos estados/passes, resolves e aliases. A captura/produtor e o
worker também continuam acima do orçamento de 16,67 ms e precisam da mudança
de representação: acelerar apenas o recorder não estabelece 60 FPS.

Fontes, binário e logs da fixture ficam no snapshot local
`artifacts/native-renderer-performance/global_contract_2026-10-17/`.
São artefatos de investigação que dependem do checkout, não um executável do
jogo nem um pacote de distribuição. Nenhum commit ou publicação nesta etapa.
