# D3D12: rastreamento de escritas para reduzir hashes de recursos

Data: 20/10/2026. Status: direção aprovada em conversa; esta especificação
ainda requer revisão do usuário. Não há autorização de implementação nesta etapa.

## Objetivo e limite deste projeto

O usuário quer 60 FPS reais no renderer nativo D3D12 no i7-10750H + RTX 2060.
A direção aprovada começa por eliminar verificações de conteúdo de recursos
inalterados, preservando imagem e comportamento do jogo. A referência é
1280×720, escala 1, MSAA 1, VSync ligado e geração de quadros desligada.

Este primeiro projeto trata das **texturas lidas da memória guest pelo D3D12**.
Buffers de vértices/índices permanecem na política atual; o contrato deve permitir
adicioná-los em outro projeto. Resolves e superfícies produzidos pela GPU continuam
seguindo suas transições e identidades próprias. Não substituir rotinas gráficas
Xbox, alterar shaders, paralelizar gravação, reduzir qualidade nem modificar Vulkan.

Reduzir esse estágio é um passo para os 16,67 ms por quadro, não uma promessa de
60 FPS. O projeto só pode declarar essa meta atendida se o caminho completo,
incluindo simulação e apresentação, for medido e validado separadamente.

## Evidência atual

- `GetTextureSrvIndex` verifica por hash, uma vez por quadro de uso, toda textura
  cujo tamanho base é até 4 MB, independentemente do aviso de escrita.
- Nas últimas janelas instrumentadas de `logs/d3d12_60_profile.log`, o hash custou
  aproximadamente 21–23 ms/quadro, lendo cerca de 76–86 MiB. São tempos de parede
  sob instrumentação; não equivalem a uma economia garantida no FPS final.
- A implementação usa `DescribeTextureRanges`, cobrindo mips, caudas e slices.
  Essa cobertura e a partição/ordem do hash devem ser mantidas na referência.
- `OnPhysicalWrite` publica páginas modificadas e depois incrementa um contador
  de notificações. Isso não é um contador de escritas concluídas.
- O contrato de `rex::memory::Memory` em `rex/system/xmemory.h` documenta callbacks
  de uso único nas vistas virtuais da memória física. Escritas pelo ponteiro físico
  do host não os disparam; callbacks também podem ser espúrios e ocorrer em releases.
- A investigação anterior de produtores de vídeo não provou cobertura geral de
  texturas e foi encerrada como caminho de otimização de gameplay.

Resultados e limitações atuais: [investigação D3D12](../../native-d3d12-60fps-2026-10-20.md).
O gate visual daquele par falhou com câmeras/HUD diferentes. Ele não constitui
referência aprovada para promover este novo caminho.

## Decisão de arquitetura

Introduzir um rastreador de identidade, cobertura e invalidação de memória.
Uma textura só pode dispensar o hash quando a ausência de escrita é garantida
pelo mecanismo de cobertura, em sua alocação atual. Ausência de mudanças em
amostras anteriores não autoriza essa dispensa.

| Alternativa | Decisão |
| --- | --- |
| Aumentar o intervalo entre hashes | Não adotar: pode atrasar atualizações |
| Reutilizar apenas o callback/contador físico existente | Insuficiente: há caminhos de escrita fora dele |
| Confiar apenas em Unlock ou no produtor de vídeo identificado | Insuficiente: cobertura geral não demonstrada |
| Cobertura explícita por alocação, com fallback de hash | Adotar, primeiro em modo de comparação |

O hash continua como caminho normal para cobertura desconhecida ou perdida.
O caminho novo fica opcional até passar pelos critérios de correção e desempenho.

## Componentes e contratos

### 1. Registro de alocações e intervalos

Identidade de armazenamento: endereço físico canônico, intervalo de alocação e
geração de alocação de 64 bits. Reutilizar o endereço ou objeto guest não reutiliza
a geração. As vistas de textura preservam seus descritores completos e recursos
D3D12 independentes; compartilhar memória não torna duas vistas equivalentes.

O registro recebe alocação, mapeamento/alias, mudança de proteção, decommit, release
e reset. Qualquer transição invalida as provas relacionadas. Páginas compartilhadas
invalidam conservadoramente todos os recursos sobrepostos. Todas as faixas de mips
e slices devem pertencer a alocações identificadas e cobertas; caso contrário,
a textura inteira permanece no caminho de hash atual.

O domínio de memória física é de 512 MiB. Metadados por página são limitados a
esse domínio; registros de vistas são removidos com suas entradas do cache.
Não criar um registro permanente para cada uso de uma textura.

### 2. Cobertura de escrita no runtime

Cada alocação possui um estado de cobertura:

- **Desconhecida:** faltam caminhos de escrita ou informações de mapeamento.
- **Completa:** todo caminho capaz de escrever é observado e o ciclo de vida
  dos aliases/alocação também é conhecido.
- **Invalidada:** ocorreu uma transição, escrita não enquadrada ou falha do monitor.

Somente a cobertura completa permite uma prova de ausência de escrita. O número
de execuções sem divergência não promove cobertura desconhecida para completa.

A integração deve cobrir stores guest nas vistas de memória, writers do runtime
que usam o ponteiro físico direto e operações que entregam memória a I/O do host.
Proteções de página não devem depender de um kernel do host gerar exceção de CPU
ao escrever em buffers de I/O. Esses produtores precisam de notificação explícita
antes da escrita e de conclusão após seu término, inclusive nos caminhos de erro.

Uma notificação por proteção de página marca uma possível escrita **antes** de
liberar a página. Ela não encerra um produtor nem certifica seu conteúdo final.
Se não houver um protocolo que permita rearmar com segurança e observar todas as
escritas seguintes, a alocação perde cobertura e volta ao hash. Rearmar páginas
no fim do quadro, por si só, não resolve writers ainda ativos.

O monitor de escritas diretas usa apenas invalidação; não pode provocar espera
recursiva em data providers do SDK. Callbacks não chamam D3D12, não alocam memória
nem gravam logs. Publicam invalidações em metadados fixos; o dono do cache resolve
identidades, cria recursos e registra diagnósticos fora desses callbacks.

### 3. Prova de ausência de escrita e conteúdo validado

Separar explicitamente:

- geração da alocação e geração de cobertura;
- época de invalidação por página;
- produtores instrumentados ainda ativos;
- prova de conteúdo da versão que foi efetivamente enviada à GPU.

Um aviso de escrita avança a invalidação antes de tornar a memória gravável.
Produtores instrumentados ficam ativos durante toda a escrita e publicam a
conclusão com ordenação de memória definida. Um contador global de callbacks
terminados não substitui nenhuma dessas informações.

A prova de reutilização exige: mesma alocação e cobertura, todas as faixas
inalteradas desde a validação, nenhum produtor ativo e uma versão D3D12 válida.
O teste tem um ponto de observação definido; uma escrita posterior invalida a
próxima verificação. Não tornar a validação mais espaçada que a política atual.

Para criação/atualização, proteger/observar antes de ler e revalidar gerações,
épocas e produtores depois da leitura. A prova do upload deve corresponder aos
bytes efetivamente decodificados, preferencialmente por snapshot possuído.
Se houver corrida, leitura incompleta ou mudança de geração, não publicar prova
de ausência de escrita. Usar o caminho anterior, sem laço ilimitado de repetição.
Esta etapa não redefine a política existente de escrituras concorrentes guest.

Contadores nunca podem voltar silenciosamente a um valor antigo. Overflow,
perda de eventos ou inconsistência invalidam a cobertura antes de qualquer reuso.

### 4. Integração D3D12

No ponto de validação atual de `GetTextureSrvIndex`:

1. Resolver identidade da alocação e todas as faixas de conteúdo.
2. Consultar a prova de ausência de escrita.
3. Com prova válida, reutilizar o recurso e pular apenas seu hash de conteúdo.
4. Sem prova, executar a política anterior de hash/decodificação/upload.
5. Publicar uma nova prova apenas se os bytes enviados e o protocolo de cobertura
   permitirem certificar a versão.

Mudança de descritor, formato ou alocação continua invalidando as identidades
apropriadas. Retenção de recursos/descritores até o fence, ordem de draws, resolves
e cache por slot conservam seu comportamento. Não introduzir um segundo dono do
cache D3D12 nem fazer o callback de memória alterar suas estruturas.

## Dependência e limite de viabilidade

Hoje o projeto liga o core do runtime pela biblioteca pré-compilada do SDK.
O recorte local de fontes de GPU não fornece, sozinho, a implementação completa
de alocação e proteção de memória. A implementação deve começar pela inspeção
do core da mesma versão ReXGlue v0.10.0 e por uma integração reproduzível.

Extensões do runtime devem ser locais, versionadas e opcionais; não substituir
arquivos de `.tools` manualmente como resultado final nem alterar ABI sem atualizar
o build correspondente. O executável precisa continuar compilando com o SDK atual:
sem a capacidade nova, a cobertura é desconhecida e o hash permanece ativo.

Se não for possível garantir observação e rearmamento corretos nos caminhos que
concentram os bytes hasheados, encerrar a promoção da otimização. Registrar quais
caminhos faltam e seu custo. Não substituir essa prova por uma whitelist de
endereços, por tamanho de textura, por heurística de conteúdo estático ou por
resultados de auditoria sem mudanças observadas.

## Modos e diagnóstico

- **Desligado, padrão inicial:** comportamento atual, sem instalar o monitor novo
  nem executar auditoria por quadro.
- **Comparação:** calcula a decisão candidata, mas mantém todos os hashes e
  uploads anteriores. Não usar seu FPS como medição da otimização.
- **Ativo, opcional:** dispensa o hash somente com prova válida; falhas voltam ao
  caminho anterior. Só disponibilizar após as verificações abaixo.

Registrar por janela: bytes/checks totais e elegíveis, custo do rastreador,
eventos por origem, recursos por estado de cobertura, falhas por motivo,
invalidações sem mudança e mudanças sem invalidação. Cada decisão candidata de
reutilização em modo de comparação deve guardar o contexto anterior e posterior
ao hash, para separar corridas, erros de identidade e perda de escrita.
Logs detalhados são limitados; a contagem agregada continua.

Uma mudança de hash com prova candidata válida reprova a cobertura daquele
domínio. A execução não pode continuar classificando-o como seguro. O mecanismo
precisa também detectar quebra do contrato, como escrita por produtor não coberto,
sem depender de o modo ativo ainda calcular o hash que removeu.

## Verificação e critérios de aceitação

### Correção determinística

Testar todas as classes de escrita e aliases, host físico direto e I/O; múltiplas
vistas, mips/slices e páginas compartilhadas; reuso do mesmo endereço e objeto;
decommit/release/proteção/reset; escrita antes/durante/depois da leitura e criação
do upload; produtores concorrentes, callbacks duplicados e de uso único; falha de
rearmamento, notificações perdidas e overflow. Testes de perda de cobertura devem
exigir fallback, nunca uma decisão de reuso.

Usar o runtime integrado real em testes de memória: testes de uma tabela simulada
não provam que stores pelas vistas e ponteiros reais sejam observados. Comparar
bytes capturados, hash de referência e conteúdo decodificado. Incluir uma violação
intencional de cobertura para provar que o modo de comparação a encontra.

### Jogo e imagem

Validar título, gameplay parado/em movimento, streaming, HUD/minimapa, capa,
vídeos e transições. Essas cenas são um mínimo de cobertura, não prova universal.
O modo de comparação deve apresentar zero decisões incorretas de reuso nos
cenários validados, além da demonstração do contrato de escrita.

Construir uma comparação de imagem com entradas e estado equivalentes e verificar
o alinhamento antes de aplicar os limites existentes do gate. Capturas de câmeras
diferentes tornam a rodada inconclusiva e devem ser refeitas; não relaxar limites.
Verificar também os casos de mudança de conteúdo por testes de pixels/recursos.
A camada de debug D3D12 não pôde ser habilitada na investigação atual; não declarar
essa validação feita se continuar indisponível.

### Desempenho e promoção

Após correção, repetir OFF/ON/ON/OFF com o mesmo executável, 60 FPS de limite,
configuração visual idêntica, sem profiling, sampler externo, build ou testes
concorrentes. Usar 20 s parado e 20 s andando, com gameplay confirmado no início
e fim. Registrar hash do executável, argumentos, cena e amostras de cada rodada.

Medir o custo dos estágios em execuções separadas. Para selecionar a otimização,
exigir redução repetida de pelo menos 30% no tempo total do estágio de validação
de texturas (rastreador + hash residual), considerando contagem e volume de
recursos comparáveis. Uma redução isolada em bytes ou ocupação de CPU não basta.
Se houver regressão repetida do FPS total ou do caminho crítico, não promover o
modo ativo ao padrão. Sem amostras comparáveis, o resultado permanece inconclusivo.

Mesmo com o estágio aprovado, registrar separadamente o FPS final. Só declarar
60 FPS se houver evidência do jogo completo dentro do orçamento, com imagem,
cadência e simulação corretas. Esta especificação não altera o limite padrão.

## Entrega esperada e próximo marco

Entrega: extensão de memória reproduzível quando necessária, rastreador testável,
integração D3D12 opcional, modo de comparação, testes reais de cobertura e relatório
de correção/custos/FPS. Falta de cobertura é um resultado explícito, não licença
para pular o hash.

O próximo marco é a revisão desta especificação pelo usuário. Após aprovação,
escrever o plano de implementação e escolher a forma de execução. As frentes de
estado gráfico incremental e substituição seletiva do XDK serão projetos seguintes,
guiados pelos custos que permanecerem após este marco.
