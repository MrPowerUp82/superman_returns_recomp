# D3D12: prioridades do worker e leituras PM4 — 24/10/2026

## Escopo e evidência externa

O usuário priorizou desempenho do renderer a partir do novo relato de gangg111/DriftKing: worker de aproximadamente 20 ms antes de otimizações, jogo/GPU perto de 10 ms, custos de leitura de upload, hash de texturas, VirtualQuery nas constantes e recriação de render target; depois, 60 FPS limitados e aproximadamente 108 sem limite. São afirmações sobre um fork privado, não medições deste checkout. Prints preservados em `artifacts/renderer-focus-reference-20261024/`.

Áudio ficou fora desta etapa. A última escuta mostrou música cortando no prompt de pouso, enquanto efeitos e falas continuam; não está corrigido.

## Preparação e referência atual

HEAD `fd0e3ec`, branch `codex/native-parity-d3d12`, árvore inicialmente limpa. Histórico, desenho aprovado e relatórios anteriores foram conferidos antes da instrumentação. `build.cmd` passou com SDK de tracking On, saída `port/out/build/win-amd64-write-tracking` e launcher Off: `logs/renderer_focus_20261024_build.log`.

Referência `renderer_focus_20261024_base1`: D3D12/RTX 2060, 1280×720, escala 1, MSAA 1, VSync, cap 60, sem LSFG, tracking de texturas On, cache de buffer SRV 4096, shared cache Off, stacked resolves Off. Perfis CPU/constantes/streams e timeline detalhada On; portanto, existe overhead de diagnóstico. Sem build/testes concorrentes com o jogo. Manifesto, hashes e argumentos completos em `logs/renderer_focus_20261024_base1_launch.json`.

| Janela nominal | Duração | Quadros completos | Blocos completos de 120 swaps | Intervalo médio | Worker | GPU real | Hash de texturas nos blocos |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Idle | 21,89 s | 433 | 3 | 49,19 ms | 39,82 ms | 5,55 ms | 0,12 ms |
| Forward | 20,01 s | 386 | 3 | 50,39 ms | 40,68 ms | 5,83 ms | 0,35 ms |

Os timers de bloco e a timeline não selecionam exatamente os mesmos quadros. A relação QPC/UTC foi amostrada durante a análise, com margem de 250 ms nas bordas; não é timestamp de corte exato. Todos os 13 blocos publicados foram conferidos quanto à contabilidade de constantes.

**Limite de cena:** o benchmark terminou com `scene is not gameplay`. A captura inicial mostra personagem na rua com HUD; a captura final mostra uma transição/cinemática acima da cidade sem HUD. As janelas não são um percurso controlado de gameplay. Os logs servem para localizar custos, não certificar ganho de FPS, equivalência de imagem ou 60 FPS. Médias de amostras de `guest fps` foram 20,1/19,8; não comparar diretamente com rodadas anteriores para atribuir ganho a código.

## Auditoria das quatro pistas

| Pista | Dado deste projeto | Consequência |
| --- | --- | --- |
| Hash de texturas por quadro | Já confirmado no relatório de 23/10: 35,58→0,36 ms com tracking. Na referência atual On: 0,12/0,35 ms | Custo grande no caminho padrão Off; promoção de On continua dependendo de correção visual/streaming e custo completo do runtime |
| Leitura CPU do upload GPU | Os caminhos normais de `SwapBufferRange`, `ApplyBuffer` e vértices inline preparam dados em scratch CPU e depois copiam para upload | Não há evidência nesta etapa de leitura de upload que explique o custo restante; isto não é auditoria de todo caminho de debug/efeitos |
| VirtualQuery nas constantes | `Pm4Mirror::ScanFrom`, LOAD_ALU_CONSTANT sem MemoryReader, chama HostReadable/VirtualQuery | Pista presente; acrescentar medição opcional antes de escolher substituição |
| Render target recriado duas vezes/quadro | GetSurface mantém cache por EDRAM/formato/tamanho/MSAA; resolve usa chave com endereço/formato/tamanho. Zero criações de superfícies nas duas janelas; contador cumulativo de resolve criou 16 texturas até quadro 600 e 17 até quadro 1200 | Não reproduzido o churn relatado nessa cena. Um novo resolve em 600 quadros não equivale a duas recriações por quadro; escala/opções/streaming continuam casos separados |

## Ensaio isolado de APIs

`logs/renderer_focus_20261024_query_probe.ps1` reservou/commitou regiões privadas de 4, 64 e 512 MiB e tocou apenas os primeiros 64 ou 4096 bytes. Foram quatro rodadas de 250 chamadas por método, após aquecimento, sem o jogo. VirtualQuery retornou o tamanho esperado, e ReadProcessMemory copiou todos os bytes, com zero falhas.

- VirtualQuery: 2,24–2,57 µs/chamada.
- ReadProcessMemory de 4 KiB: 1,50–1,56 µs/chamada.
- ReadProcessMemory de 64 bytes: 0,59–0,69 µs/chamada.

São médias de um protótipo gerenciado/PInvoke com ordem fixa, não um benchmark do parser nem da memória física do jogo. Não provam que substituir a validação melhora FPS. JSON completo preservado no mesmo prefixo.

## Instrumentação implementada

Perfil por instância de Pm4Mirror: chamadas, bytes solicitados, consultas VirtualQuery, falhas e ns da validação de LOAD_ALU_CONSTANT com fonte viva. O temporizador exclui WriteRange. MemoryReader continua usando a captura sem validar memória viva. Leituras e proteção mantêm o caminho anterior.

Ativação somente com `SR_NATIVE_PM4_READ_PROFILE=1` e um modo de CPU profiling, no worker D3D12. Off é padrão; não há relógios nem incrementos novos de perfil quando desabilitado. A linha `native CPU PM4 live reads` publica os totais de 120 swaps e `validation_ms=validation_ns/120000000`. O primeiro intervalo pode excluir trabalho anterior à inicialização; só intervalos completos após aquecimento entram nas médias.

TDD: a interface inerte e os testes produziram **252 testes, sete checks falhando** exclusivamente nos contadores ausentes, com dados/versões preservados. Após implementar a instrumentação: **252 testes, zero falhas**, CTest **2/2**. Logs `pm4_reads_20261024_{red,green,ctest}.log`. Uma tentativa inicial nem iniciou a build por CMake fora do PATH; a execução válida seguinte usou VsDevCmd e o CMake já configurado.

Revisão independente somente leitura não encontrou achados acionáveis; verificou preservação do comportamento, gating, contagem de regiões/falhas e publicação na mesma thread. Não executou gameplay. O executável Release normal permanece com SHA256 `C1FC299694D25F4BE7115A7C5BBD9CC7674C2CA3C7FE67A11F3220B9B1BF75A8`.

## Resultado da coleta PM4

`renderer_focus_20261024_pm4_1` terminou com código 0 e o detector de gameplay aceitou a rodada. Usou as mesmas configurações da referência, com o novo perfil habilitado; o executável mudou por causa da instrumentação, mas a DLL de runtime manteve o hash `F64159813F9FCCCF8DF35CB9A32AFA72438129EBFC68A5663782F744A2DF0508`. Não atribuir uma diferença global de FPS entre essas duas builds ao perfil.

| Janela | Blocos completos | Consultas/quadro | Bytes/chamada | Validação/quadro | Execute/quadro |
| --- | ---: | ---: | ---: | ---: | ---: |
| Idle | 3 | 664,98 | 80,21 | 7,2507 ms | 41,06 ms |
| Forward | 2 | 579,81 | 78,47 | 7,2996 ms | 43,04 ms |

Treze blocos totais publicados passaram na contabilidade; só os blocos com ambas as bordas dentro da janela entram nas médias acima. Zero falhas de validação. O custo PM4 é aninhado em Execute, fora de PrepareDraw: não somar essas regiões como custos independentes. A sequência automática ainda pode atravessar instruções/transições do tutorial, e a aceitação do detector não estabelece que duas execuções tenham câmeras/poses idênticas.

## Experimento de cópia protegida

Os dados acima foram apresentados ao usuário antes desta mudança. `sr_native_pm4_bounded_reads=false` mantém o caminho anterior por padrão. No D3D12, com a opção On, somente fontes vivas de LOAD_ALU_CONSTANT usam o helper existente `CheckedGuestReads`/ReadProcessMemory; ele copia exatamente o pedido, rejeita leituras parciais/inacessíveis e não mantém cache de validade ou conteúdo entre loads.

O scratch é local à thread, reiniciado por load e consumido sincronamente por WriteRange. Os spans não sobrevivem para o próximo load/indireto. O caminho com MemoryReader continua separado, assim como o parse das referências indiretas e a ordem de bytes/sanitização/versões. A fonte continua viva, como antes; a cópia não transforma todo o frame em um snapshot atômico. A opção exige reinício e não muda Vulkan.

O perfil conta `bounded_calls`, preservando o total de chamadas/bytes/falhas e medindo a validação+cópia antes de WriteRange. Em modo Off conta VirtualQuery; em modo On o contador de consultas deve permanecer zero. Quando o perfil está desligado não há leituras novas de relógio nem incrementos de métricas.

TDD da opção: **254 testes, quatro checks falhando** com a interface ainda inerte; depois **254 testes, zero falhas**, CTest **2/2**. A comparação dos registradores/versões passou inclusive no RED, que falhou na ausência do novo mecanismo. Casos novos verificam refresh no mesmo endereço, zero comprimento, isolamento do leitor de captura e rejeição sem escrita parcial de registradores. A suíte existente do helper também cobre ponteiros inválidos, páginas sem acesso, leitura parcial, ownership após liberar a origem e reuso de scratch.

Build experimental passou; revisão independente do diff completo não encontrou achados acionáveis. Não há promoção de opções nem patch de limitador.

## A/B na mesma build

`renderer_focus_20261024_query2` e `renderer_focus_20261024_bounded1` terminaram com código 0, com detector de gameplay aceitando as duas. Um script conferiu os hashes do EXE/runtime e os argumentos: iguais, salvo `sr_native_pm4_bounded_reads=false|true`. Perfis e opções auxiliares também iguais. Sem builds/testes concorrentes. EXE `BB7BEFB6E17D63E397E679095F1F839706EA53EB0CCA20E936A21152CA8D00BB`, runtime `F64159813F9FCCCF8DF35CB9A32AFA72438129EBFC68A5663782F744A2DF0508`.

| Medida | Idle Query | Idle Bounded | Forward Query | Forward Bounded |
| --- | ---: | ---: | ---: | ---: |
| Janela, s | 21,47 | 21,50 | 20,01 | 20,02 |
| Blocos completos de 120 swaps | 2 | 3 | 2 | 2 |
| Quadros completos de timeline | 362 | 490 | 362 | 415 |
| Loads vivos/quadro nos blocos | 681,28 | 605,84 | 593,96 | 668,45 |
| Bytes/load | 78,94 | 81,92 | 79,21 | 79,43 |
| VirtualQuery/quadro nesses loads | 681,28 | 0 | 593,96 | 0 |
| Validação/cópia, ms/quadro | 7,9777 | 1,0010 | 7,6624 | 1,2662 |
| Validação/cópia, µs/load | 11,710 | 1,652 | 12,900 | 1,894 |
| Execute, ms/quadro nos blocos | 45,08 | 29,19 | 37,16 | 31,28 |
| Worker da timeline, ms | 48,92 | 31,84 | 41,33 | 32,55 |
| GPU real, ms | 6,88 | 5,26 | 6,18 | 5,86 |
| Intervalo da timeline, ms | 57,86 | 42,78 | 50,96 | 46,17 |

Redução do custo médio por load de **85,89% em idle** e **85,32% em forward**. A quantidade e o tamanho das leituras variam, mas forward On realizou mais loads e ainda teve custo total menor na região. As novas métricas mostram o mecanismo sendo usado: consultas zero e `bounded_calls=calls`. Isso se refere às cargas PM4 instrumentadas, não a todas as chamadas de VirtualQuery do processo.

Foram validados todos os **14 blocos Query e 14 Bounded**, inclusive os fora da janela: reconciliação de constantes, contadores de falha, relação ns/ms e tempo do probe dentro de Execute. Zero falhas de leitura nos resumos, zero criações de superfícies nas quatro janelas, sem linhas de falha de criação/removal encontradas. Em ambas as rodadas, as texturas de resolve cumulativas passaram de 16 no quadro 600 para 17 no quadro 1200: não foi reproduzida recriação duas vezes por quadro. O ramo de falha de leitura foi exercitado por testes, não observado no gameplay.

**Limite visual:** as capturas iniciais têm câmera/pose e opacidade do HUD diferentes; ambas mostram a cidade, personagem e overlay de FPS. O gate existente, sem alterar thresholds ou referências, reprovou o par: PSNR global **20,79 dB**, histograma **0,22368**, logo PSNR **16,71 dB** contra mínimo 30 e diferença média **51,31** contra limite 8. As imagens não são uma referência alinhada. Isso não comprova regressão causada pelo patch nem equivalência visual. Portanto, não promover a opção para padrão.

Outras regiões também variaram. O delta inteiro do worker/intervalo não pode ser atribuído à cópia protegida; as médias instrumentadas de FPS não são aceitação de 60 FPS. Janelas curtas e uma execução por configuração limitam a generalização. É necessário repetir com câmera/cena alinhadas, movimento/streaming mais amplo e medição sem profiling antes de promover o recurso.

Evidências: manifestos e logs `logs/{bench_,}renderer_focus_20261024_{query2,bounded1}*`, análises JSON e timelines TXT; `logs/pm4_bounded_20261024_comparison.json`, `logs/pm4_bounded_20261024_image_gate.json`. Os processos de teste foram encerrados. A build normal mantém o hash anterior; somente a experimental contém a mudança.

## Próximo custo a investigar

**60 FPS continuam não atingidos.** No modo Bounded, o worker ainda custa 31,84–32,55 ms. UploadConstants ficou em 11,89–13,37 ms/quadro, incluindo slots 6,07–6,90 ms; streams em 6,10–6,73 ms. Essas regiões são aninhadas e não devem ser somadas indiscriminadamente.

A thread do jogo também permanece acima do orçamento: 42,50–46,07 ms, com frontend 10,94–12,36 ms. O resíduo `game_guest` de 31,56–33,71 ms pode conter esperas e não é CPU exclusiva da simulação. Não há prova de platô ocioso que autorize patch nas constantes 1/30. O próximo perfil deve separar essas esperas/trabalho e o restante de slots/estado no worker. Frame pacing e pré-compilação de shaders continuam no desenho aprovado; esta mudança não os implementa.
