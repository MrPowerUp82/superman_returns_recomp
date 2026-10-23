# D3D12: custo do worker e rastreamento de texturas — 23/10/2026

## Resultado

O worker consome CPU efetivamente: a coleta do SO mediu **0,955 núcleo** na thread identificada pelo log como worker. Uma parcela grande do custo é a validação de conteúdo de texturas por hash. Na nova comparação diagnóstica do rastreador já existente, o hash residual caiu de **35,58 para 0,36 ms/quadro**, enquanto o worker da timeline caiu de **95,23 para 60,74 ms**. A GPU permaneceu em aproximadamente 7 ms.

**60 FPS não foram atingidos nem validados. O modo ativo continua experimental; Off permanece padrão.** O gate de imagem reprovou capturas com câmera, pose e opacidade do HUD diferentes. Não há promoção de modo ou novo patch de implementação nesta entrega.

O áudio ficou fora deste trabalho, conforme orientação do usuário, que está acessando a máquina por AnyDesk. O acesso remoto estava ativo no contexto desta investigação; não houve comparação controlada de seu custo.

## Estado e preparação

- HEAD `bc286c5`, contendo `c2d7517`; o histórico e as docs foram conferidos antes de agir.
- Única alteração inicial: o relatório não commitado da validação A/B/C desta mesma sessão.
- i7-10750H + RTX 2060; D3D12 nativo, 1280×720, escala 1, MSAA 1, VSync, limite 60, FXAA e LSFG desligados, entrada sintética `SR_BENCH_INPUT`, mouse desabilitado.
- EXE stock preservado, SHA256 `C1FC299694D25F4BE7115A7C5BBD9CC7674C2CA3C7FE67A11F3220B9B1BF75A8`.
- Foram usados timers existentes `SR_NATIVE_CPU_PROFILE=1`. A primeira rodada também usou `SR_FRAME_TIMELINE_DETAIL=1`; a comparação Off/On usou detalhe desligado nas duas rodadas.
- Perfil ligado acrescenta leituras de relógio e logging. Estas rodadas são diagnósticos, não aceitação de FPS. Não foram feitas builds ou testes concorrentes com gameplay.

## 1. Worker stock: ocupação e regiões caras

Rodada `worker_priority_20261023_profile1`. Gameplay observado visualmente, seguido de aquecimento. A análise descartou o primeiro bloco de 120 swaps que cruzava a borda do aquecimento. Os 11 blocos completos restantes deram:

| Região instrumentada | Média ms/quadro | Mínimo–máximo |
| --- | ---: | ---: |
| Execute do worker | 94,30 | 80,68–109,01 |
| Hash de texturas | 36,14 | 33,03–39,22 |

O hash está aninhado no lookup, nas constantes e na preparação: não somar esses tempos. O custo de hash ficou em aproximadamente 38% da média do Execute nesses blocos. As verificações leram cerca de 82 MiB por quadro, com aproximadamente 700 checks/quadro. Os blocos registraram zero rearmes e zero recarregamentos de textura; isso não prova ausência de qualquer escrita guest, nem autoriza dispensar hashes sem cobertura completa.

Nos últimos 240 quadros, a timeline registrou intervalo 88,49 ms, worker 86,96 ms, GPU real 6,89 ms e frontend 22,09 ms. `game_guest` de 54,18 ms é o resíduo fora dos hooks medidos e pode conter esperas; não é uma medição exclusiva de lógica do jogo.

### CPU por thread

O script existente `tools/analysis/sample_thread_cpu.ps1` fez uma coleta válida de 30 s, com 1.512 intervalos de thread. Para a thread **10060**, identificada pelas linhas `native CPU worker`:

- CPU acumulada: 27.984,375 ms em 29,2949932 s de intervalos válidos.
- Ocupação média: 0,955261 núcleo.
- Tempo de kernel incluído: 3.625 ms.
- Amostras locais entre 14:23:01,041 e 14:23:29,172.

Uma tentativa anterior falhou com referência nula e gerou CSV parcial; ela foi excluída. Uma tentativa curta de 2 s confirmou recuperação, mas também não entrou na conclusão. A conclusão usa somente `worker_priority_20261023_profile1_threads2.csv`. O script e as threads do jogo não foram alterados ou suspensos.

Evidências locais: `logs/worker_priority_20261023_profile1_{launch,aggregate,threads_summary}.json`, `logs/worker_priority_20261023_profile1_threads2.csv`, log completo e `logs/timeline_worker_priority_20261023_profile1_tail240.{json,txt}`.

## 2. Preparação da variante existente de rastreamento

A implementação já existia e seus limites foram lidos em [d3d12-texture-tracking-results.md](d3d12-texture-tracking-results.md). Não foi criada uma segunda solução de cache.

`setup_memory_tracking_sdk.ps1 -VerifyOnly` detectou divergência no manifesto. A comparação dos cinco arquivos compartilhados mostrou:

- Cada cópia correspondia ao hash do manifesto anterior, portanto não havia edição local desconhecida nesses arquivos.
- As diferenças para os fontes canônicos do repositório eram exclusivamente CRLF/LF.
- O diff dos arquivos versionados do SDK correspondia ao patch previsto pelo setup.

O setup existente sincronizou as cópias geradas e atualizou seu manifesto. A verificação completa, incluindo dependências, passou depois. O SDK permanece no SHA fixado `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`.

O executável e todas as DLLs anteriores dessa saída foram preservados em `artifacts/native-renderer-performance/worker_priority_20261023_tracking_previous/`, com `hashes.json`.

Build separado, concluído com código 0:

```text
SR_MEMORY_TRACKING_SDK=ON
SR_BUILD_DIR=port/out/build/win-amd64-write-tracking
SR_BUILD_LAUNCHER=OFF
build.cmd
```

O CTest de memória/rastreamento passou **4/4**, em 4,69 s. Logs: `logs/worker_priority_20261023_tracking_build.log` e `logs/worker_priority_20261023_tracking_ctest.log`.

Binário usado nas duas rodadas seguintes:

- EXE SHA256: `AE8289CB208A03129EDB101B1AE4A5BC2C951451CA0B09FE9DCD4B1DE01ACC02`.
- `rexruntime.dll` SHA256: `9F9987F18275D3B4F3083A892F7FE1406212B2B32BBC8CB9FB0813B6835AB053`.

O executável stock e suas configurações não foram substituídos. Não houve mudança semântica em código do SDK, jogo, shader ou benchmark.

## 3. Comparação diagnóstica Off/On no mesmo EXE/DLL

Rodadas `worker_priority_20261023_tracking_off1` e `worker_priority_20261023_tracking_on1`. A variável que seleciona o modo foi `SR_D3D12_TEXTURE_WRITE_TRACKING=off|on`; os argumentos, EXE, runtime, profiling e timeline foram iguais. O primeiro Start de On ocorreu ainda no carregamento; a seleção foi repetida após observar o título. Só gameplay aquecido entrou na análise.

Ambas permaneceram na esquina inicial de New Game. Um comando curto de D-pad acordou o HUD sem mover o personagem. A câmera automática e a animação variaram. Não houve percurso de movimento nesta comparação; a validade em streaming/movimento não foi reavaliada aqui.

As bordas QPC foram salvas diretamente nos arquivos `*_window.json` e `*_end.json`. A análise excluiu 250 ms de cada borda e quadros que cruzavam esses limites. Os timers agregados usam somente blocos completos de 120 swaps cujas duas bordas de publicação estavam dentro da janela.

| Medida | Off | On |
| --- | ---: | ---: |
| Blocos completos de 120 swaps | 6 | 7 |
| Quadros completos da timeline | 786 | 926 |
| Intervalo da timeline, ms | 96,92 | 72,77 |
| Worker da timeline, ms | 95,23 | 60,74 |
| Game da timeline, ms | 76,75 | 72,72 |
| Front wait, ms | 20,13 | 0,00 |
| GPU real, ms | 7,48 | 6,88 |
| Execute do worker, ms | 92,97 | 59,56 |
| PrepareDraw, ms | 63,17 | 29,62 |
| UploadConstants, ms | 54,04 | 20,89 |
| BindVertexStreams, ms | 10,23 | 9,65 |
| Lookup de texturas, ms | 38,34 | 5,64 |
| Hash residual, ms | 35,58 | 0,36 |
| Hash checks/quadro | 705,00 | 7,14 |
| KiB hasheados/quadro | 84.320,67 | 711,71 |
| Draws/quadro nos blocos de timers | 2.574,33 | 2.534,29 |

As regiões de timers são aninhadas; não somar as linhas. Os agregados de 120 swaps e a timeline não têm exatamente a mesma seleção de quadros. As médias descrevem essas duas execuções, não um ganho causal de FPS aprovado.

Dentro do intervalo completo entre publicações do rastreador em On:

- Consultas: **2,4874 ms/quadro**, já incluídas no lookup.
- Bytes dispensados: **99,3122%** do total de bytes das validações consideradas por On nesse intervalo.
- Última publicação da janela: `mismatches=0`, `protection_failures=0`, `active_checks=0`.
- Houve perda de cobertura e checks desconhecidos/invalidados, com fallback para a política de hash: `coverage_losses=573`, `unknown_checks=6597`, `invalidated_checks=19706` acumulados até a última publicação considerada. A ausência de divergências detectadas não é prova universal de ausência de falso reuso.

On usa chave de descritor completa e pode ter volume de cache diferente de Off. O percentual de bytes dispensados é interno ao caminho On; não é redução de 99% da CPU total, nem comparação de um corpus idêntico de texturas entre os modos. O custo de runtime/proteção fora do worker não foi integralmente isolado nesta rodada.

### Imagens e gate

Capturas iniciais/finais: `logs/worker_priority_20261023_tracking_{off1,on1}_{start,end}.jpg`. O personagem e os prédios foram observados nas duas rodadas; câmera, pose e HUD não ficaram alinhados.

O gate existente foi executado sem alterar thresholds. A primeira execução no PowerShell 7 falhou ao resolver `System.Drawing`; a repetição no Windows PowerShell 5 completou e **reprovou o par**:

- PSNR global: 19,82 dB.
- Região logo: PSNR 26,03 dB contra mínimo 30; cromaticidade 0,0365 contra limite 0,03.
- Região personagem: diferença média 22,88 contra limite 20; cromaticidade 0,0866 contra limite 0,05.

Evidência: `logs/worker_priority_20261023_image_gate.json`. Isso não identifica regressão causada pelo rastreador, mas também não valida equivalência visual. Nenhum threshold ou referência foi relaxado.

Evidências numéricas: `logs/worker_priority_20261023_tracking_{off1,on1}_blocks.json` e `logs/timeline_worker_priority_20261023_tracking_{off1,on1}_window.{json,txt}`. Os manifestos `*_launch.json` registram hashes e argumentos. CSVs completos e logs foram preservados em `logs/`.

## Decisão e prioridade seguinte

1. Há evidência nova de ocupação real de CPU e confirmação de que o hash periódico de texturas é uma parcela grande do worker nesta configuração.
2. A alternativa já implementada de rastreamento reduziu esse custo nos diagnósticos atuais. Off continua padrão: faltam imagens alinhadas, repetições controladas e contabilização completa do custo do runtime para promoção.
3. Mesmo com hash residual baixo, o worker ficou em aproximadamente 61 ms e o intervalo em aproximadamente 73 ms. A meta de 16,67 ms não foi atingida. O problema não se resume aos hashes, nem aos supostos loops de 30 FPS.
4. O próximo alvo de perfil é o custo restante com reuso ativo: **UploadConstants ~20,89 ms, BindVertexStreams ~9,65 ms e ~20,29 ms de Execute fora de PrepareDraw e streams** nestes agregados. A última parcela é diferença de regiões medidas, não identificação de função ou autorização para remover trabalho. Reduzir chamadas redundantes exige demonstrar validade de estado e preservar os dados capturados por draw.

Nenhum commit, push, mudança de padrão, patch de limitador ou patch novo de renderer foi feito. Os processos de teste foram encerrados. A build experimental atual está em `port/out/build/win-amd64-write-tracking/`; os arquivos normais em `win-amd64-release/` continuam preservados.

## 4. Continuação: cache compartilhado de constantes

Foi medido o experimento **já existente** `SR_NATIVE_SHARED_CONSTANTS_CACHE=1`, seguido de uma referência nova com `=0`. Nenhum código de implementação foi modificado. As duas rodadas usaram o mesmo EXE e DLL de tracking acima, rastreamento de texturas On, perfil de CPU ligado e timeline com detalhe 0; argumentos gráficos iguais. A ordem foi On → Off, uma execução de cada. Isso não é uma repetição controlada suficiente para promover o recurso.

Manifestos: `logs/worker_priority_20261023_shared_{on1,off1}_launch.json`. Gameplay de New Game foi observado antes do aquecimento de pelo menos 25 segundos. Houve um comando sintético D-pad Right de 500 ms na abertura de cada janela; não foi comandado deslocamento do personagem. Câmera, pose, HUD e tráfego variaram; a captura inicial On mostra outra perspectiva. Essas diferenças limitam a comparação.

- On: janela UTC 18:29:46,904, duração 97,14 s, 10 blocos completos de 120 swaps, 1.245 quadros completos de timeline.
- Off: janela UTC 18:34:26,978, duração 93,83 s, 8 blocos completos de 120 swaps, 1.156 quadros completos de timeline.
- Blocos foram aceitos somente com as duas bordas de publicação dentro da janela. A timeline usa o início/fim do estágio game com margem de 250 ms nas bordas; zero quadros descartados por estágio ausente.

| Medida | Cache Off novo | Cache On |
| --- | ---: | ---: |
| Intervalo da timeline, ms | 79,60 | 73,20 |
| Worker da timeline, ms | 67,45 | 61,94 |
| GPU real, ms | 7,07 | 6,70 |
| Execute, ms/quadro | 65,93 | 61,45 |
| PrepareDraw, ms/quadro | 32,24 | 30,68 |
| UploadConstants, ms/quadro | 22,59 | 21,84 |
| Região floats, ms/quadro | 6,93 | 6,44 |
| Região slots, ms/quadro | 11,06 | 9,79 |
| Lookup de texturas, ms/quadro | 6,32 | 5,45 |
| Hash de texturas, ms/quadro | 0,40 | 0,36 |
| Shared other, ms/quadro | 4,60 | 5,62 |
| Shared compare/upload, ms/quadro | 0,08 | 1,44 |
| BindVertexStreams, ms/quadro | 11,77 | 10,72 |
| Draws/quadro | 2.584,50 | 2.579,30 |
| Constantes, us/draw (razão das médias) | 8,74 | 8,47 |
| Streams, us/draw (razão das médias) | 4,55 | 4,16 |

Tempos aninhados não devem ser somados. Os agregados dos timers e a timeline cobrem conjuntos diferentes de quadros. A referência Off anterior, nesta mesma máquina, deu constantes 20,89 ms e Execute 59,56 ms, inferiores também à rodada On atual; existe variação entre execuções que impede atribuir a diferença global à opção.

O cache On registrou em média **1.124,9 hits e 1.454,1 uploads/quadro**: cerca de 43,6% de acertos e 4.500,8 KiB/quadro de upload evitado segundo o contador. Off registra zero uploads nesse contador específico porque ele só contabiliza misses do experimento; não significa ausência de uploads no caminho padrão. O trabalho de comparação/upload subiu 1,36 ms e o restante compartilhado subiu 1,02 ms. Menos upload não demonstrou menor custo dessa região.

O gate existente, sem alterar referências ou thresholds, reprovou as capturas iniciais: PSNR global 19,63 dB; cromaticidade do logo 0,06657 contra 0,03; diferença média na região personagem 48,30 contra 20, com cromaticidade 0,26687 contra 0,05. Evidência: `logs/worker_priority_20261023_shared_image_gate.json`. Isso não prova regressão do cache, nem equivalência visual.

Dados completos: `logs/worker_priority_20261023_shared_{on1,off1}_blocks.json`, logs com os mesmos prefixos, capturas `*_start.jpg`/`*_end.jpg`, e `logs/timeline_worker_priority_20261023_shared_{on1,off1}_window.{json,txt}`. CSVs e timestamps QPC foram preservados. Não houve build ou testes concorrentes com as janelas de medição; não foi necessário recompilar para alternar a variável existente.

### Consequência para o próximo patch

O cache continua desligado por padrão; não há ganho causal ou equivalência visual aprovados. A inspeção do código mostra que a região chamada `floats` inclui `FlushRingConstants`, alocação e leituras de cvars/debug, além do upload VS/PS. `BindVertexStreams` reúne `ApplyBuffer`, bindings IA, preparação de SRVs para fetch dinâmico e leitura de declaração. Seus totais não identificam a operação culpada.

Próximo passo proposto: instrumentação opcional dessas sub-regiões, com contadores de versões VS/PS reutilizadas, bytes copiados, ações de `ApplyBuffer` e descritores criados. Primeiro medir; escolher depois uma mudança de comportamento respaldada pelos resultados. Nenhum reuso baseado apenas em endereço, dispensa de dados capturados, mudança de padrão ou patch de limitador foi aplicado nesta continuação. Áudio continua adiado.

## 5. Instrumentação das constantes, após autorização

O usuário autorizou seguir pelas constantes. Foi adicionada somente instrumentação em `port/src/native_renderer/native_renderer.cpp`, sem alterar decisões de reuso, bytes enviados, bindings ou padrões gráficos. O novo detalhe exige **`SR_NATIVE_CPU_PROFILE=1` e `SR_NATIVE_CONSTANTS_PROFILE=1`** (outros habilitadores antigos de perfil também satisfazem a primeira condição). A nova variável sozinha não habilita o perfil. Desligada por padrão, ela não coleta os novos relógios nem contadores. O helper `CpuTimer` passou a aceitar um habilitador explícito, preservando o comportamento dos timers antigos.

As novas linhas são publicadas e zeradas a cada 120 swaps:

- `native CPU constant detail`: tempos de flush do ring, alocação compartilhada inicial, alocação e obtenção/cópia VS e PS, montagem inicial compartilhada, estado derivado e root bindings.
- `native CPU constant detail totals`: contagens **brutas do bloco**, sem divisão/arredondamento por quadro, de draws, draws via mirror, uploads/reusos VS/PS, bytes VS/PS, falhas de alocação e pedidos pendentes no ring.

`vs_copy`/`ps_copy` incluem a chamada `fill`: obtenção do banco com `Pm4Mirror::FloatConstants`, sanitização quando sua versão muda e `memcpy` para o upload. Portanto, esses timers **não isolam o custo de memcpy**. As regiões detalhadas são distintas entre si, mas aninhadas nos totais antigos. `shared_alloc` mede apenas a alocação inicial do caminho com cache compartilhado desligado; a alocação de miss com cache ligado continua dentro do timer antigo `compare_upload`.

### Build e verificações

- Backup dos EXE/DLL anteriores em `artifacts/native-renderer-performance/constants_detail_20261023_previous/`, com hashes.
- `build.cmd`, SDK de tracking On e saída `win-amd64-write-tracking`, terminou com exit 0. Log: `logs/constants_detail_20261023_build.log`. O build contém avisos de depreciação de CRT; não é uma build sem warnings.
- Os quatro testes `ctest -R sr_memory_tracking` passaram em 3,98 s. Eles validam os contratos do runtime, não medem a precisão dos novos timers.
- Verificação no título com `SR_NATIVE_CONSTANTS_PROFILE=0`: timers antigos publicados, nenhuma linha de detalhe; processo encerrado. Manifesto/log/captura: `logs/constants_detail_20261023_disabled*`. Isso não é uma referência de gameplay para quantificar overhead.
- EXE instrumentado SHA256 `9DFCD0C05BAE6CBB35A835EC0A12113E1F0B14F3F4428CF69D0B1B62252067E1`; DLL de runtime reconstruída SHA256 `503D4885DC3271E2C5D78B8333505E15E14697060EA35EDEBDAEFBB1871AA1C8`. São artefatos novos, diferentes dos manifestos da etapa anterior.
- A build normal continua com SHA256 `C1FC299694D25F4BE7115A7C5BBD9CC7674C2CA3C7FE67A11F3220B9B1BF75A8`.

### Medição de gameplay

Rodada `constants_detail_20261023_game1`, New Game na interseção inicial, D3D12 com os mesmos argumentos gráficos anteriores, tracking On, cache compartilhado Off, perfil de CPU e detalhe das constantes On, detalhe de timeline Off. Gameplay confirmado por captura, aquecimento adicional de 26 segundos, depois janela UTC **18:47:10,841–18:48:52,920**, duração 102,08 s. Não houve comando de deslocamento ou D-pad nesta janela; a câmera de idle e o tráfego variaram.

Foram aceitos 10 blocos completos cujas duas bordas de publicação ficaram dentro da janela, e 1.327 quadros completos de timeline com margem de 250 ms nas bordas. Não houve build/teste concorrente. É uma rodada diagnóstica com overhead de relógios não isolado: não comparar seus FPS diretamente com rodadas anteriores para alegar ganho/regressão.

| Região | Média ms/quadro |
| --- | ---: |
| UploadConstants total | 22,818 |
| Floats total, aninhado | 7,324 |
| Flush do ring | 0,082 |
| Alocação shared inicial | 0,189 |
| Alocação VS | 0,075 |
| Obtenção/sanitização + cópia VS | 3,921 |
| Alocação PS | 0,058 |
| Obtenção/sanitização + cópia PS | 2,248 |
| Montagem inicial shared | 1,794 |
| Estado derivado shared | 1,476 |
| Root bindings | 0,766 |
| Slots total, aninhado | 10,729 |
| Lookup de texturas, dentro de slots | 5,878 |
| Hash, dentro do lookup | 0,373 |
| Samplers, dentro de slots | 1,847 |

As três alocações medidas somaram **0,322 ms/quadro**. Obtenção/sanitização+cópia VS/PS somaram **6,169 ms**, aproximadamente 27% das constantes. A região slots ainda consome aproximadamente 47% das constantes. As regiões não cobrem todo o tempo: há controle, checks e overhead de medição entre elas.

Reuso VS foi **29,00%** e PS **57,30%** dos draws. Houve em média **1.822,08 uploads VS e 1.095,97 uploads PS/quadro**, somando **11,40 MiB/quadro** de VS/PS, sem incluir shared. Todos os draws considerados usaram o mirror. Esses números não autorizam reutilizar versões diferentes.

Todos os **30 blocos publicados**, incluindo carregamento/título fora da janela de desempenho, passaram nas conferências: uploads + reusos = draws para VS e PS; bytes = uploads × 4096; contagem bruta dividida por 120 coincide com o contador antigo truncado; subtempos cabem em suas regiões-mãe com tolerância de arredondamento. Falhas de alocação e pedidos pendentes do ring foram zero. Essas verificações validam a contabilidade publicada, não a ausência de overhead ou equivalência visual em todo o jogo.

Na timeline da janela: intervalo médio 74,2 ms; worker 64,71 ms; GPU real 6,90 ms. Nos timers por 120 swaps, Execute 62,77 ms e streams 10,56 ms. Seleções diferentes explicam que os números de worker não sejam idênticos; não somar tempos de regiões aninhadas. 60 FPS continuam não atingidos.

Evidências: `logs/constants_detail_20261023_game1_{launch,window,end,blocks}.json`, log e capturas com o mesmo prefixo; `logs/timeline_constants_detail_20261023_game1_window.{json,txt}` e CSV original. O jogo foi encerrado ao terminar a coleta.

### Próximo passo respaldado pelos dados

Alocação e flush não explicam a maior parte do custo das constantes nesta rodada. A região de obtenção/sanitização+cópia VS/PS é um alvo confirmado de medição; a responsabilidade individual da sanitização e da escrita no upload permanece não identificada. O próximo experimento deve separá-las, preservando o tratamento de NaNs e infinitos, a ordem de bytes e os dados capturados por versão. Ainda não foi aplicado patch de otimização ou mudança de padrão. O restante de slots/lookup e os vértices continuam como alvos separados, e áudio segue adiado.
