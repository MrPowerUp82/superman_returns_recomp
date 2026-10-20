# Pesquisa para 60 FPS reais em Vulkan nativo — 19/10/2026

## Conclusão e ponto de partida

**Atualização após o replay paralelo:** a comparação no jogo não demonstrou
ganho. [Pesquisa adicional](native-vulkan-research-followup-2026-10-19.md)
prioriza reutilização de estado/constantes na GPU, usa DXVK e Xenia como
referências e confirma descriptor heaps no driver desta RTX 2060.

O caminho prioritário é transformar captura/replay e preparação por draw em
dados compactos preparados por lote, permitir gravação paralela desses dados
e avaliar execução indireta onde houver compatibilidade de estado. Trata-se
de uma hipótese de arquitetura apoiada pelos perfis locais e pelas fontes
abaixo, não de uma garantia de atingir 60 FPS.

Os aproximadamente 13,5 FPS antigos continuam sendo uma referência válida
registrada. As rodadas recentes ficaram em cerca de 10 FPS, mas cenas e
percursos não foram equivalentes. Não atribuir a queda apenas à câmera nem
declarar regressão causal sem comparação controlada. Mesma resolução física
de 1600×900 foi confirmada nas duas épocas.

Nas rodadas recentes em movimento, o tempo atribuído a Record Vulkan foi
79,58–83,74 ms, Worker 56,99–59,12 ms e GPU 13,93–14,86 ms. Esses tempos se
sobrepõem; não somá-los. Para sustentar 60 FPS, cada estágio serial precisa
caber em 16,67 ms: o recorder requer redução próxima de cinco vezes nessa
carga. A GPU já fica perto do orçamento, portanto transferir trabalho para
ela também exige medir a folga. As amostras incluem esperas; não equivalem
a tempo exclusivo de CPU. Evidência: [resultados locais](native-vulkan-runtime-results-2026-10-19.md).

## 1. Preparar recursos e constantes por lote

A Khronos recomenda reutilizar descritores, guardar identificadores em
materiais/meshes para evitar buscas repetidas e reunir dados em um ou poucos
buffers por quadro. No exemplo publicado, cache reduziu 44 para 27 ms, sem
mudança relevante na GPU; esse resultado não prevê o ganho neste jogo.
[Descriptor and buffer management](https://docs.vulkan.org/samples/latest/samples/performance/descriptor_management/README.html).

Aplicação proposta: identificadores estáveis por versão de recurso; preparar
uma vez o estado compartilhado e representar mudanças por draw. O caminho
global já existe, mas `global_draw_bindings.cpp` ainda copia 8192 bytes de
constantes por draw e faz aquisição de slots/recursos. Captura e replay também
precisam entrar na conta; otimizar somente a emissão de `vkCmdDraw` não remove
esse custo. Não eliminar alterações do guest que tenham efeitos necessários.

Primeiro experimento: medir separadamente captura, resolução de recursos,
constantes, preparação de pipelines e emissão; construir uma representação
imutável de draw preparado para um passe representativo, conservando a ordem
e as versões dos recursos. Aceitar somente ganho no custo total, incluindo a
preparação, com imagem equivalente.

## 2. Distribuir a gravação entre núcleos

A NVIDIA recomenda gravação paralela equilibrada e afirma que o driver não
distribui automaticamente as chamadas Vulkan entre workers. Também recomenda
evitar command buffers muito pequenos e respeitar dependências entre tarefas.
[Vulkan Dos and Don’ts](https://developer.nvidia.com/blog/vulkan-dos-donts/).

A Khronos demonstra secondary command buffers gravados em paralelo, com pools
separados por thread e quadro. Seu exemplo de cerca de 1800 draws obteve 15%
com oito threads; não se deve confundir esse resultado com aceleração de oito
vezes. [Command buffer usage](https://docs.vulkan.org/samples/latest/samples/performance/command_buffer_usage/README.html).

Aplicação proposta: comparar 1/2/4 workers sobre draws já preparados. Planejar
uploads, transições, resolves e aliasing antes de distribuir trechos contíguos
de um passe. Executar os trechos na ordem original. Cada worker possui seus
pools; evitar transformar ResourceStore/GlobalSlotTable compartilhados em um
mutex global. A preparação serial também precisa diminuir.

## 3. Execução indireta e comandos gerados pela GPU

Multi-draw indirect executa vários draws a partir de um buffer. A Khronos
demonstra geometria reunida em buffers comuns e seleção por offsets para
reduzir binds e emissão na CPU.
[GPU Rendering and Multi-Draw Indirect](https://docs.vulkan.org/samples/latest/samples/performance/multi_draw_indirect/README.html).

Aplicação proposta: começar por sequências adjacentes de estado compatível.
Não ordenar transparências arbitrariamente nem atravessar resolves. A maioria
dos vertex shaders deste projeto ainda usa entradas IA; multi-draw comum não
acomoda sozinho todas as trocas de buffers e pipelines.

Uma alternativa adicional é Device-Generated Commands (DGC), que permite
expressar trocas de buffers, push constants e grupos de shaders em sequências
armazenadas em buffers. Tem custo de pré-processamento e memória; a NVIDIA
alerta que não é uma solução universal para transferir gravação à GPU.
[Explicação da NVIDIA](https://developer.nvidia.com/blog/new-vulkan-device-generated-commands/).

O exemplo oficial atual compara gravação em threads com DGC NV/EXT, recomenda
EXT para implementações novas e mostra a importância de agrupar o
pré-processamento. Mudanças indiretas de shaders têm suporte opcional, que
deve ser consultado. Não copiar seus números obtidos em outra GPU/cena.
[Código e comparação da NVIDIA](https://github.com/nvpro-samples/vk_device_generated_cmds/blob/master/README.md).

### Consulta realizada neste computador

Uma sonda somente de leitura criou uma instância Vulkan e consultou o driver;
não criou device, não alterou configurações e não executou o jogo:

```text
device=NVIDIA GeForce RTX 2060
VK_NV_device_generated_commands=1
VK_EXT_device_generated_commands=1
deviceGeneratedCommands=1 [feature NV]
maxIndirectSequenceCount=4194303
maxIndirectCommandsTokenCount=32
```

Fonte local: `logs/native60_dgc_capabilities_2026-10-19.log`;
sonda: `build/native60_dgc_capabilities.cpp`. EXT foi confirmado na lista de
extensões; suas features/propriedades detalhadas ainda não foram consultadas.
Isso confirma uma opção para protótipo, não ganho de FPS ou compatibilidade
integral com o renderer. Os headers locais usados na sonda expõem as estruturas
NV, mas ainda não as EXT.

## O que não repetir como solução principal

O renderer já recicla command pools, usa ONE_TIME_SUBMIT, dois quadros em voo
e uma submissão com upload/draw. O3 e ThinLTO também já estão ativos. Bindless
isoladamente não demonstrou ganho perceptível no gameplay. Flags de registros
locais do recompilador já foram experimentadas, incluindo casos de quebra de
correção; não são uma opção inédita. Reduzir apenas chamadas XDK não explica
a aceleração necessária: no perfil antigo elas eram aproximadamente 8% do
tempo observado. Nenhuma dessas observações justifica abandonar a meta de
render nativo ou contar imagens repetidas como quadros novos.

## Ordem de execução e critério de decisão

1. Comparar o executável antigo preservado e o atual na mesma cena/percurso.
2. Preparar lotes compactos e medir custo total de captura/preparação/emissão.
3. Distribuir a gravação de lotes entre 2/4 workers, com ordem preservada.
4. Comparar emissão direta, MDI compatível e DGC num passe representativo.
5. Só integrar caminhos que mantenham pixels, versões de recursos e resolves
   corretos e reduzam o tempo total no jogo. Validar correção separadamente;
   medir desempenho em Release sem validação e sem builds concorrentes.

O marco final continua sendo 60 quadros reais sustentados, com Vulkan nativo,
escala 1 e geração de quadros desligada. Melhoras de um fixture não substituem
essa medição.
