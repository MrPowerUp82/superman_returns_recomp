# Vulkan M2: frontend compartilhado e shaders

O branch `codex/vulkan-backend` contém a interpretação guest compartilhada,
compilação SPIR-V com cache próprio e um teste do contrato em GPU. **O jogo
continua usando D3D12.** A integração dos draws do jogo pertence ao M3; este
marco não mede ganho de FPS em War World nem entrega um port Android.

## Compilar e verificar

```powershell
powershell -File tools/build_vulkan_m2.ps1
build/tests-vulkan/sr_vulkan_tests.exe
build/tests-vulkan/sr_vulkan_contract_test.exe --gpu-uuid=787589225ad18fb3ea2345350716d21b
python -m unittest discover -s tests/shaders -p test_*.py
python tools/shaders/validate_vulkan_corpus.py
```

O UUID do exemplo é o da RTX 2060 usada na verificação. Sem argumento, o teste
seleciona uma GPU compatível. O toolchain local requer VS/Clang, DXC e os headers
Vulkan preparados no M1. O script copia o emissor XenosRecomp já preparado,
aplica o patch Vulkan em `build/vulkan-m2/emitter-tree` e compila nessa árvore.
Preserva o checkout original em `.tools/xenosrecomp`; não executa fetch forçado.
O pin upstream é `339af41df2c23dbe3256c1c377716b81a0e0fe6b`.

A validação do corpus exige containers locais em `artifacts/shaders/raw`.
Corpus vazio é erro. `tools/build_vulkan_m2.ps1 -Corpus` também executa essa etapa
e falha explicitamente se algum shader falhar. Na máquina verificada existe um
shader conhecido inválido, portanto essa etapa retorna código 1 e grava o
relatório, mesmo com os outros 240 shaders prontos.

## Contrato e cache

A ABI `sr-vulkan-buffers-v1` usa Vulkan 1.1 e SPIR-V 1.3:

| Set | Binding | Recurso |
| --- | --- | --- |
| 0 | 0 / 1 | Constantes VS / PS, buffers de 4096 bytes |
| 0 | 2 | Shared constants, buffer de 4096 bytes |
| 1 | 0 / 1 / 2 | Arrays de 32 texturas 2D / 3D / Cube |
| 2 | 0 | Array de 32 samplers |
| 3 | 0 | Array de 32 buffers de vertex fetch |

O shared buffer preserva offsets de booleans, loops e 224 entradas de metadata
de 16 bytes a partir do offset 512. O consumidor Vulkan futuro deve remapear
índices globais de descritores D3D12 para as 32 posições locais por draw.
Constantes são carregadas como bytes; a variante não exige endereços físicos,
BDA, shaderInt64, arrays de descritores runtime ou descriptor indexing.
Y é invertido pelo DXC somente no VS; o viewport não repete a inversão.

`compile_vulkan.py` separa cache Vulkan de DXIL. A chave inclui conteúdo do
container, estágio, emissor, header comum, DXC e suas DLLs, argumentos e contrato.
Binário e metadata são publicados por arquivos temporários e rename; hits
exigem ambos os arquivos, header SPIR-V e hash do binário válidos.
Timeouts e falhas do emissor/DXC produzem diagnóstico explícito.

`spirv_metadata.py` inspeciona instruções, entrypoints, capacidades, descritores,
tipos e locations. Não substitui a validação semântica completa de SPIRV-Tools.
O relatório infere indexing dinâmico de access chains, incluindo samplers,
mesmo quando DXC não emite uma capability explícita. Limites de componentes
usam o intervalo conservador das locations, incluindo espaços entre elas.
`validate_pair` pode verificar interfaces VS/PS, mas não foram capturados os
pares efetivos de todos os draws do jogo neste marco.

## Evidência da execução em 2026-10-03

| Verificação | Resultado |
| --- | --- |
| Testes native | 57 passaram |
| Testes Vulkan | 31 passaram, incluindo features opcionais explícitas |
| Testes Python de shaders | 17 passaram |
| Checks do launcher | 13 passaram |
| Builds D3D12 padrão e opt-in Vulkan foundation | Compilaram |
| Smoke M1 | 120 frames; resize, minimizar e encerrar passaram |
| Corpus local | 240 de 241 prontos com compilação e inspeção de ABI |
| Fixture própria DXIL | VS e PS compilaram com o decoder real |
| Fixture própria Vulkan na RTX 2060 | Três variantes de pixels passaram |

O shader `978B0FF62C3693C2.vs.bin` continua falhando por parâmetro `iPosition0`
duplicado, também conhecido no caminho DXIL. Não foi substituído nem ocultado.
O relatório local fica em `artifacts/shaders/vulkan-m2/report.json`.

Máximos observados **por estágio**, sem implicar aprovação da combinação de
dois estágios ou de um pipeline layout completo:

| Requisito | Máximo |
| --- | ---: |
| Storage buffers | 34 |
| Sampled images | 96 |
| Samplers | 32 |
| Descriptor sets | 4 |
| Vertex attribute locations | 21 |
| Vertex output components | 80 |
| Fragment input components | 76 |

Indexing dinâmico de sampled images/samplers e storage buffers é necessário.
O consumidor M3 precisa verificar os limites por estágio e os limites agregados
do pipeline layout. Esta ABI fixa ainda não representa compatibilidade mobile
universal; uma redução dos arrays pode ser necessária em GPUs com limites menores.

O teste GPU usa alvo RGBA8_UNORM 64×64, textura branca, constantes e índices com
restart. Chama `srVertexFetch` do emissor com tipo XDK USHORT2 `0x2C2259`, slot 7.
Verifica dois triângulos verdes, variante vermelha com boolean/loop e clipping
Z=2 que deixa todo o alvo preto. Um controle negativo com normalização incorreta
de USHORT2 falhou na asserção de pixel; o decoder correto passou após restauração.
A fence precede readback e a memória não coerente é flush/invalidate quando preciso.

GPU: NVIDIA GeForce RTX 2060, UUID `787589225ad18fb3ea2345350716d21b`, API 1.4.
Validation layers/debug-utils e `spirv-val` estavam ausentes. Não se declara
validação externa realizada; o contador zero não prova ausência de erros de API.

Um programa local, não distribuído, executou os helpers C++ contra capturas de
War World: 142.556 índices, 22.247 separadores `0x7FFFFF`, 1024 constantes VS e
224 entradas de metadata da captura `warworld_gpu` preservadas no shared buffer.
Os metadados totalmente zerados da captura `data644` não foram usados como
evidência de vertex fetch. Isso verifica interpretação e cópia dos dados, não
a renderização completa da arena.

Revisão independente final encontrou e verificou correções dos limites de
componentes, do tratamento de OpSpecConstant truncado e da fixture USHORT2.
Shaders derivados do jogo, capturas, caches e programas locais de diagnóstico
permanecem ignorados e não entram na distribuição.

## Conferência visual ainda pendente

A execução D3D12 do M2 foi aberta e a abertura e a cena inicial da cidade
produziram imagem. A build isolada sem shaders embutidos precisa do corpus
DXIL, da biblioteca `.srsl` e das ferramentas runtime locais configurados.
Esses arquivos foram preparados ao lado do executável de QA em
`build/vulkan-main`; não são novos arquivos versionados deste marco.

Ainda falta conferir HUD e a arena War World com slot 1 carregado nesta
execução. Os inputs automáticos não avançaram o menu de forma confiável;
foi solicitada a operação manual ao usuário. Nenhum save foi sobrescrito.
O item de regressão visual do plano permanece aberto até essa conferência.
