# Pesquisa adicional: reduzir o trabalho de bindings Vulkan

Pesquisa em fontes primárias e consulta local, 19/10/2026. Meta: 60 FPS reais,
Vulkan nativo, escala 1, sem geração de quadros.

## O que mudou na recomendação

O replay paralelo anterior não demonstrou ganho: avanço serial 11,33 FPS,
paralelo 11,25 FPS, no mesmo executável. O perfil instrumentado mostrou
Frontend 44,95 ms, Worker 42,36 ms, Record 75,02 ms e GPU real 14,57 ms.
Os estágios se sobrepõem. Dentro da gravação, bindings consumiram ~35 ms;
esse rótulo inclui trabalho da aplicação e chamadas do driver.
[Evidência local](native-vulkan-parallel-replay-results-2026-10-19.md).

A prioridade passa a ser eliminar preparação redundante e identificar o custo
interno desses bindings. Mais threads de replay ou uma extensão nova, isoladas,
não demonstram solução para os ~16,67 ms necessários a cada estágio serial.

## 1. Reutilizar constantes já residentes na GPU

O backend Vulkan do Xenia mantém máscaras de constantes atualizadas, invalida
somente o estado afetado e envia os registradores de float usados pelo shader.
Mantém bool/loop/fetch em grupos próprios. Isso está implementado em
`UpdateBindings`, não apenas sugerido em um tutorial.
[Xenia, versão consultada](https://github.com/xenia-project/xenia/blob/95a5c3ee250f80c3b9d139658649d9ffb6db3eec/src/xenia/gpu/vulkan/vulkan_command_processor.cc#L3861).

**Aplicação proposta, ainda não medida:** bancos VS, PS e estado shared com
referências GPU independentes, reutilizados por versão e por shader. Hoje
`global_draw_bindings.cpp:143` copia 4096 + 4096 bytes a cada draw, mesmo que
os snapshots CPU sejam compartilhados. Em 3000 draws, são ~23,4 MiB só de
VS/PS. O cache portátil já evita algumas cópias; o global não tem reutilização
equivalente desses offsets. Separar os bancos pode exigir outra ABI de shader.
Compressão por registradores usados exige metadados ausentes em ShaderCapture
e tratamento conservador de indexação dinâmica. Não remover constantes que
possam ser lidas indiretamente.

## 2. Preparar novamente apenas o estado alterado

O DXVK consulta uma máscara de recursos alterados antes de preparar bindings;
se o layout não exige atualização, retorna sem reconstruir descritores.
[DXVK: estado de recursos](https://github.com/doitsujin/dxvk/blob/2cb40186c43d617b5bd349a3756721aa4bd24b6d/src/dxvk/dxvk_context.cpp#L7339).

**Aplicação proposta:** um estado imutável de bindings por conjunto/versão,
incluindo layout de vértices, streams, fetch de texturas, samplers e owners dos
recursos. Reutilizar esse estado até uma mudança relevante. O código atual já
tem caches de slots, layouts e samplers, mas ainda calcula chaves, consulta
recursos e monta arrays em cada Prepare. Evitar esse trabalho completo é uma
hipótese diferente de reduzir apenas as aquisições de slots.

Invalidar corretamente quando uploads substituem owners, resolves criam
novas views, aliases mudam e um endereço guest é reutilizado. Um fetch igual
não prova que o recurso físico ainda é o mesmo.

## 3. Worker específico para descritores

O DXVK possui `DxvkDescriptorCopyWorker`: acumula metadados em ring buffer,
processa uploads em outra thread e fornece sincronização para a submissão.
O caminho de heaps agrupa escritas de descritores.
[Interface e sincronização](https://github.com/doitsujin/dxvk/blob/2cb40186c43d617b5bd349a3756721aa4bd24b6d/src/dxvk/dxvk_descriptor_worker.h#L58),
[implementação](https://github.com/doitsujin/dxvk/blob/2cb40186c43d617b5bd349a3756721aa4bd24b6d/src/dxvk/dxvk_descriptor_worker.cpp#L76).

**Aplicação condicional:** separar planejamento ordenado de versões/slots da
escrita dos dados e descritores, com páginas exclusivas e conclusão antes de
submeter à GPU. O experimento anterior paralelizou arenas/decode de packets;
ele não implementou esse trabalho no backend. A adaptação só merece prioridade
se amostragem mostrar custo relevante nas escritas/API: não presumir que os
35 ms de bindings sejam todos vkUpdateDescriptorSets.

## 4. Nova opção confirmada no computador: VK_EXT_descriptor_heap

A Khronos anunciou a extensão em 23/01/2026. Ela transfere o gerenciamento de
descritores para memória controlada pela aplicação e possui compatibilidade
com bindings existentes.
[Anúncio oficial](https://www.khronos.org/blog/vulkan-introduces-roadmap-2026-and-new-descriptor-heap-extension).

O guia mostra mapeamento de set/binding de SPIR-V existente para offsets do
heap. Há também formas de passar endereços de buffers. Isso permite estudar
uma transição sem presumir reescrita de todos os shaders. O exemplo demonstra
heaps de recursos e samplers, escrita de descritores e bind desses heaps.
[Guia](https://docs.vulkan.org/guide/latest/descriptor_heap.html),
[exemplo oficial](https://docs.vulkan.org/samples/latest/samples/extensions/descriptor_heap/README.html).

### Consulta local efetuada

Uma sonda criou somente uma instância e consultou extensões, features e tipos
de memória. Não criou VkDevice, não abriu o jogo e não alterou o driver.

| RTX 2060, driver Vulkan 2588114944 | Resultado |
| --- | --- |
| VK_EXT_descriptor_heap anunciado | 1 |
| descriptorHeap, feature consultada | 1 |
| descriptorHeapCaptureReplay | 1 |
| VK_EXT_descriptor_buffer anunciado / descriptorBuffer | 1 / 1 |
| bufferDeviceAddress | 1 |
| shaderInt64 | 1 |
| DGC NV e EXT anunciados | 1 / 1 |

O driver Windows informado é 32.0.16.1714. Não há motivo demonstrado para
substituí-lo por um beta antigo apenas porque o tutorial usa outra versão.

Evidências: `logs/native60_research_features_20261019.log` e
`logs/native60_research_driver_20261019.json`. O header principal do projeto
continua em versão 290. A sonda declarou localmente apenas a estrutura de
features nova, com layout e sType conferidos no header oficial de referência.
[Estrutura oficial](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceDescriptorHeapFeaturesEXT.html).

**Distinção importante:** a classe local GlobalDescriptorHeap usa descriptor
sets com indexing; seu nome não significa que já usa VK_EXT_descriptor_heap.
O novo caminho é viável quanto à feature, mas ainda não foi implementado nem
medido. Ele não elimina automaticamente hashes, buscas e cópias da aplicação.

## 5. Buffer Device Address como alternativa para buffers

O exemplo da Khronos mostra acesso a buffers por endereço, inclusive ponteiros
passados em push constants. Texturas e samplers continuam precisando de um
mecanismo de descritores. Endereços brutos exigem que o aplicativo assegure
limites e duração da memória.
[Buffer Device Address](https://docs.vulkan.org/samples/latest/samples/extensions/buffer_device_address/README.html).

**Hipótese:** descrições de draw com referências GPU diretas podem reduzir
traduções de IDs/slots para streams e constantes. Essa mudança também exige
estudo de ABI, validação de limites e retenção de buffers até o fence. O suporte
da RTX foi consultado; desempenho e compatibilidade do renderer não foram.

## 6. Memória mapeada e diagnóstico do custo real

A especificação alerta para leituras e operações read-modify-write em memória
write-combined e recomenda escritas em linhas de cache completas. Memória
HOST_CACHED atende outro padrão de acesso.
[Especificação de memória](https://docs.vulkan.org/spec/latest/chapters/memory.html).

A GPU expõe memória host-visible coerente sem cache (tipo 3), host-visible
coerente com cache (tipo 4), e memória local à GPU host-visible (tipo 5).
O seletor atual prefere coerência e não prefere cache. Isso não prova que a
memória escolhida esteja errada: as escritas atuais já usam memcpy e o cache
de constantes compara uma cópia CPU, evitando ler o upload mapeado. Testar
scratch CPU cacheado + cópia contígua por página é apenas uma hipótese a medir,
incluindo a cópia adicional e seu efeito na GPU.

O Nsight Systems permite correlacionar amostras CPU, chamadas Vulkan e filas
GPU no Windows. É uma forma de distinguir memcpy/hash/alocador, driver e
esperas antes de escolher uma reestruturação.
[Documentação NVIDIA](https://docs.nvidia.com/nsight-systems/UserGuide/index.html),
[plataformas](https://developer.nvidia.com/nsight-systems/get-started).
Não foi instalado nem executado nesta pesquisa.

## 7. DGC continua condicional

O exemplo da NVIDIA mostra que pré-processamento usa compute, pode precisar
de memória significativa e é sensível às barreiras. Seus resultados rápidos
incluem cargas ordenadas/agrupadas ou execução unordered em cenários próprios.
[Exemplo e condições da medição](https://github.com/nvpro-samples/vk_device_generated_cmds/blob/master/README.md).

Neste projeto a emissão direta leva aproximadamente 3–4 ms e a GPU já está em
~14,6 ms. Migrar somente essa emissão, acrescentando preprocessamento GPU,
não resolve os ~35 ms de preparação nem garante caber em 16,67 ms. Preservar
ordem de transparências, resolves e aliases. DGC permanece um protótipo
posterior, condicionado ao custo total e à imagem equivalente.

## Próxima decisão recomendada

1. Amostrar e dividir os ~35 ms: chaves/layouts, resolução/slots, constantes,
   samplers, chamadas de descritores e flushes.
2. Priorizar estado imutável reutilizável e constantes GPU por versão, se esses
   forem os maiores custos. Medir captura, replay, backend e FPS no total.
3. Comparar worker de descritores/descriptor heaps apenas se o custo de driver
   justificar. As features estão disponíveis; benefício não está provado.
4. Usar uma sequência de câmera/entrada controlada e conferir pixels/versões;
   o avanço anterior encontrou uma parede e não serve de benchmark determinístico.

Esta pesquisa adicionou fontes verificáveis e uma consulta de capacidades.
Não houve nova medição de FPS nem alteração no renderer. A meta continua
pendente; nenhuma fonte consultada garante a aceleração necessária neste jogo.

Fontes de DXVK e Xenia foram fixadas por commit e SHA-256 em
`build/native60_research_followup/sources.json`, evitando depender de master
durante uma implementação futura.
