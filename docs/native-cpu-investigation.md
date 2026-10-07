# Investigação de CPU do renderer nativo

Estudo do código e dos perfis locais de 6 de outubro de 2026. Objetivo: aumentar
FPS em D3D12 e Vulkan sem reduzir qualidade gráfica nem depender de uma GPU
específica. Este documento registra candidatos e um desenho de trabalho; não
representa ganho já implementado ou medido.

## Evidências e limites

Os últimos perfis D3D12 mostram execução do worker entre 62,56 e 70,91 ms por
quadro, preparação entre 30,57 e 34,91 ms, a função UploadConstants entre
23,82 e 27,11 ms, e streams entre 13,07 e 18,21 ms. São medidas aninhadas,
não somáveis. No mesmo gameplay, os timestamps GPU registram 7,53 e 7,11 ms
por quadro. Isso reforça que existe bastante trabalho de CPU a atacar, mas
não explica sozinho a diferença entre notebooks: não há perfil comparável do
outro equipamento, e os timestamps GPU não abrangem toda a latência do sistema.

No Vulkan, os últimos três intervalos de captura mostram 176,8–178,8 MB lidos
por quadro, 28–29 ms de hash e 41–42 ms em CaptureTextures. Os contadores de
texturas efetivamente alteradas são zero nesses intervalos. A gravação Vulkan
leva 57,3–76,1 ms, incluindo bindings de 13,2–18,3 ms e descritores de
14,3–19,1 ms. Captura, replay e gravação rodam em threads distintas; seus
custos não podem ser somados para prever FPS.

Fontes locais: logs/bench_native_c2_clean_after_d3d12.log e
logs/bench_native_c2_clean_after_vulkan.log. Esses arquivos são ignorados pelo
Git. A comparação anterior não demonstrou melhora consistente de FPS; os
caches existentes continuam sendo experimento, não uma otimização validada.

## 1. Revalidação de texturas: prioridade para ambas as APIs

### Vulkan: problema concreto de inicialização

Renderer::EnsureInitialized inicializa page_write_seq_, registra
OnPhysicalWrite e ativa texture_watch_. Esse método pertence ao caminho
D3D12, acessado por BeginFrame. InstallPacketSink apenas instala callbacks;
o caminho Vulkan de Execute retorna antes de BeginFrame e não inicializa
a vigilância. CaptureTextures, com texture_watch_ falso, força a verificação
por hash na primeira utilização de cada textura em cada quadro. Isso também
impede o backoff já escrito nessa função de ser usado. Os logs com
watch_dirty=0, revalidated=0 e dezenas de ms de hash são compatíveis com esse
fluxo.

Desenho recomendado: extrair somente a inicialização do monitor de memória
para uma rotina comum, invocada antes da primeira captura em ambas as APIs.
Não chamar EnsureInitialized pelo Vulkan: ele também inicializa recursos e
faz casts específicos do presenter D3D12. Publicar o monitor apenas depois
de as tabelas estarem prontas, inicializar uma única vez e conservar seu
lifetime enquanto callbacks puderem ocorrer. Verificar o contrato de
registro/desregistro do SDK antes de alterar shutdown.

### D3D12: hash recorrente mesmo com vigilância ativa

GetTextureSrvIndex revalida por hash, uma vez por quadro, qualquer textura
com até 4 MB. A política existe porque escritas por aliases virtuais, como
as do decodificador de vídeo, podem escapar da vigilância física. Não é
seguro simplesmente remover essa verificação ou chamar textura pequena de
estática. UploadConstants chama essa rotina: o rótulo 'constantes' inclui
validação de textura, resolução de views e samplers.

Desenho recomendado: medir separadamente bytes/tempo de hash no D3D12 e
compartilhar uma política de revalidação por recurso com o frontend Vulkan.
Otimizar primeiro texturas cuja cobertura de escrita puder ser comprovada;
recursos não cobertos mantêm fallback conservador. Backoff de validação para
recursos não cobertos deve ser uma decisão explícita, pois pode atrasar uma
atualização visual. A meta é evitar releituras desnecessárias sem mascarar
escritas reais, não apenas trocar qualidade por FPS.

Validação necessária: escrever por aliases físicos e virtuais, alternar
conteúdo durante o mesmo frame, testar vídeo/UI, streaming, resolves e
reutilização de endereços; conferir bytes capturados e imagens antes/depois.

## 2. PM4: eliminar estado temporário desnecessário

CapturePm4Dependencies constrói `Pm4Mirror local` mesmo quando recebe um
mirror persistente. O binário release confirma que o compilador não eliminou
o custo: reserva 0x16ce8 bytes de stack e executa memset de 0x16a14 bytes
(92.692 bytes) antes de testar o ponteiro do mirror. A desmontagem está em
build/native-cpu-pm4-disassembly.txt, gerada com llvm-objdump sobre o objeto
release existente, sem recompilar o jogo.

Com 3.000 chamadas isso corresponde a aproximadamente 278 MB de zeragem
adicional, se todas tiverem ring_bytes. A contagem real de chamadas precisa
ser medida; não se pode converter essa estimativa diretamente em FPS.

Desenho recomendado: construir o mirror local somente na ausência do mirror
do chamador e deixar o caminho persistente sem essa reserva de stack. Separar
o caminho local em helper se necessário para evitar __chkstk no caminho quente.
O benefício principal deve aparecer no Vulkan, que captura PM4 continuamente;
no D3D12 normal esse custo não existe, apenas no modo de checagem de pacotes.
Testar os dois modos, indiretos aninhados, falhas de leitura e rollback da arena.
Também medir as cópias temporárias de primary/sources antes de alterá-las:
elas hoje garantem spans estáveis durante crescimento da arena e recursão.

## 3. Consultas e leitura de estado por draw: compartilhado

PlanStreams/DynamicVertexFetch e, no worker D3D12, PrepareDraw e BindVertexStreams
consultam shaders repetidamente. LookupGuestShader e CaptureGuestShader adquirem
g_mutex em cada chamada; TryRegisterInlineShaders adquire o mesmo mutex até
quando os objetos já estão registrados. Vulkan também faz essas consultas na
captura. Não há medição isolada suficiente para atribuir os ms de streams a
esses locks; é um candidato para medir, não uma causa já estabelecida.

Desenho recomendado: resolver o par VS/PS uma vez na captura e carregar
metadados imutáveis necessários ao draw, como dynamic_vertex_fetch, junto ao
comando. A associação precisa de geração/identidade de conteúdo: um endereço
guest pode ser reutilizado. Manter o fallback de registro inline para objetos
que escapem dos hooks. Nunca guardar um ponteiro mutável do registry após
liberar o lock como nova estratégia de cache.

GuestPtr e CapturedMemory::Read fazem buscas reversas por faixa em cada leitura
escalar. DeviceState lê 121 registros do shadow, além de viewport, superfícies
e fetches. A declaração é consultada em várias fases apesar do cache de conteúdo.
Recomendo decodificar spans contíguos uma vez por comando e levar o resultado
às fases seguintes. Preservar a precedência de capturas sobrepostas, em especial
patches parciais posteriores. Medir número de buscas e faixas examinadas antes
de escolher um índice novo; um mapa adicional também pode custar mais que a
busca em conjuntos pequenos.

## 4. Pacotes e constantes Vulkan: reduzir tráfego e alocações

DrawPacket contém 12 KB de constantes, 3,5 KB de metadados de vértices e duas
tabelas de 4 KB de registros, além de referências e vetores. O decoder cria
esse estado por draw, copia VS/PS e os 1.024 registros mirrored, preenche
metadados e copia esses metadados para shared. BuildBindings posteriormente
reescreve a região de metadados shared para remapear streams. MapTransient
reserva novamente os 12 KB completos por draw. Portanto, sanitizar constantes
em cache não elimina essas cópias e inicializações.

Desenho recomendado para uma etapa posterior: snapshots imutáveis de bancos
VS/PS por versão e tabelas de registros compactas, com ownership até o consumo
da gravação. Usar arena por frame/batch; a reciclagem só ocorre quando todos os
consumidores terminarem. O upload deve respeitar os fences da GPU. A ABI de
shader existente permite bindings dinâmicos separados de VS/PS/shared, mas
DescriptorStore::Prepare hoje exige um bloco contíguo: separar buffers exige
planejamento e testes de lifetime, não apenas trocar um memcpy. Remover a cópia
redundante de metadados shared apenas depois de auditar todos os consumidores.

Não remover inicializações de campos indiscriminadamente: alguns caminhos
usam defaults zero. Não reduzir registros capturados sem listar quais são
consumidos por pipeline, resolve, tiling, depth bias e diagnóstico.

## 5. Descritores e atualizações Vulkan: depois dos anteriores

Já existe cache de descritores, pools e offsets dinâmicos. Portanto, 'adicionar
cache' não é uma proposta nova. Mesmo num hit, DescriptorStore::Shared resolve
identidades e monta chave para 32 buffers e 96 texturas. Procurar reutilização
do binding completo com geração dos recursos e invalidação de resolves pode
reduzir trabalho. Manter recursos vivos por serial/fence e conservar fallback
para transientes; não reutilizar descritores apontando para versões antigas.

GameRenderer::Upload aplica um patch parcial na cópia CPU e depois envia todo
o buffer. É outro custo concreto, mas os últimos perfis mostram apenas
51–211 KB de buffer uploads por quadro. Deve ficar atrás dos custos maiores.
Não substituir por escrita numa versão GPU que ainda possa estar em voo.

Referência técnica: a Khronos documenta que gerenciamento de descritores pode
custar mais CPU que os próprios draws, e recomenda reutilização de sets e
organização de buffers. Isso sustenta a direção, não prevê ganho neste jogo:
https://docs.vulkan.org/samples/latest/samples/performance/descriptor_management/README.html

A Microsoft recomenda evitar leituras CPU de heaps UPLOAD/write-combined:
https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-map
O ApplyBuffer D3D12 já usa scratch em RAM antes do memcpy para upload; essa
proteção deve ser preservada. Não propor sua implementação como se faltasse.

## Ordem e critério de aceitação

1. Corrigir a comparação antes de usar FPS como prova: detector de HUD com
   fixture negativa da abertura, cena/câmera verificadas e séries pareadas
   antes/depois alternadas. Registrar quantidade de draws, hashes, clocks,
   configuração e executável; rejeitar amostras durante builds.
2. Isolar tempos de revalidação de texturas no D3D12 e ativar o monitor comum
   no Vulkan, com testes de escritas e aliases antes de usar resultados visuais.
3. Eliminar a construção PM4 desnecessária e medir captura de forma isolada.
4. Reduzir consultas de shader e leituras escalares por comando, uma mudança
   por vez, preservando associação por geração e ordenação de memória capturada.
5. Somente então avaliar alteração dos pacotes/constantes e descritores Vulkan.

Para cada mudança: testes de semântica e lifetime, comparação visual em cidade,
vídeo/UI, cape/cloth e sombras/resolves; custo CPU, alocações e bytes separados
por estágio; várias execuções pareadas na mesma cena. Uma mudança só vira
'otimização validada' se reduzir seu custo e não regredir o FPS/resultado visual
nas amostras comparáveis. Nenhuma porcentagem de FPS está prometida aqui.

Não começar por paralelizar draws ou reordená-los: PM4, render targets, resolves
e uploads têm dependências de ordem. O projeto já possui workers e gravação
assíncrona Vulkan; mais threads não eliminam leituras e cópias repetidas.
'Jogo antigo' descreve o conteúdo, mas não o custo desta tradução de estado.


## Primeira implementa��o: captura e atualiza��es de buffers

Implementado ap�s autoriza��o para come�ar:

- Inicializa��o do monitor f�sico no primeiro BeginCmd, antes do worker e da
  captura, em vez de somente no inicializador D3D12. A captura Vulkan agora
  usa o mesmo monitor de texturas e buffers. O fallback de hash permanece.
- CapturePm4Dependencies aloca o Pm4Mirror de fallback somente quando n�o h�
  mirror do chamador. O disassembly anterior mostrava um memset incondicional
  de 92.692 bytes por chamada; o novo caminho com mirror persistente n�o o faz.
- Buffers de at� 32 KiB confirmam notifica��es por p�gina pelo conte�do.
  Escritas em aloca��es vizinhas deixam os intervalos j� capturados v�lidos;
  mudan�as reais dentro do mesmo frame continuam invalidando. A verifica��o
  por frame continua cobrindo aliases virtuais que escapem do monitor f�sico.
  Essa pol�tica � compartilhada por D3D12 e Vulkan.
- Vulkan reutiliza aloca��es device-local para atualiza��es parciais somente
  quando o pool � o �nico propriet�rio. IDs ativos, submissions e descritores
  em uso impedem a reutiliza��o. O limite de reten��o � de 64 MiB; excedentes
  seguem o caminho anterior. Buffers est�ticos n�o ocupam esse pool.

Ativar o monitor no Vulkan, isoladamente, exp�s um custo antes oculto:
137�180 uploads de buffers por frame, contra 8�20 nas amostras anteriores.
Cada vers�o criava VkBuffer e VkDeviceMemory. O teste intermedi�rio caiu para
6,0/5,2 FPS (parado/andando), com 34,6�42,6 ms em uploads e 49,2�66,6 ms
esperando fence. Esse resultado n�o foi aceito como otimiza��o.

Com o pool, o teste intermedi�rio seguinte mediu 9,3/9,3 FPS, uploads de
9,3�13,6 ms e fence de 3,0�3,7 ms. As cenas t�m contagens de draws diferentes;
esses intervalos sustentam a remo��o do custo de aloca��o, mas n�o estabelecem
porcentagem geral de ganho. A compara��o final est� em
[native-renderer-performance.md](native-renderer-performance.md).

Testes novos cobrem conte�do alterado duas vezes no mesmo frame, notifica��o
sem mudan�a de conte�do, fallback no frame seguinte, preserva��o do mirror
PM4 entre comandos e lifetime das vers�es Vulkan durante reutiliza��o. Revis�o
independente n�o encontrou problemas de lifetime ou de invalida��o. A valida��o
visual permanece limitada �s cenas registradas, sem garantia sobre todo o jogo.
