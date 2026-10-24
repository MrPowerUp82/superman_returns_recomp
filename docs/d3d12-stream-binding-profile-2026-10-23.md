# Perfil de BindVertexStreams — 23/10/2026

Continuação da etapa CPU aprovada. Após a vetorização de constantes (`aee0c29`), streams permaneceu em 11,51 ms/quadro; esse total mistura aplicação de buffers, binding IA, preparação de SRVs e leitura/montagem de atributos. O incremento atual acrescenta somente diagnóstico opcional, sem novos caches ou decisões de renderização.

## Instrumentação

Exige `SR_NATIVE_STREAMS_PROFILE=1` **e** um habilitador do perfil CPU, normalmente `SR_NATIVE_CPU_PROFILE=1`. Por padrão não lê os novos relógios nem incrementa os contadores. Segue a publicação do worker existente a cada 120 swaps; não estende esse perfil para execução síncrona nem mede o caminho Vulkan, que não executa BindVertexStreams.

`native CPU stream detail` publica setup, ApplyBuffer, IA, SRV, leitura de declaração e montagem de metadata. São regiões distintas dentro de streams. Tempos de destruição/controle não cobertos continuam no pai. `native CPU stream totals` publica contagens brutas do bloco: chamadas, streams, bindings IA, buffers ausentes, chamadas com falha, streams dinâmicos, SRVs criados/reusados/falhos, leituras de declaração, atributos, metadatas escritas e actions 0/1/2/other do plano. Actions são pedidos do plano, não contagens de uploads GPU efetivos.

Partições verificáveis: streams=missing+ia_binds; dynamic_streams=srv_created+srv_reused+srv_failed; declaration_reads=srv_created+srv_reused; soma das actions=streams. Não igualar chamadas de BindVertexStreams a draws das constantes: há outros caminhos de draw.

## Build, revisão e controles

A revisão independente encontrou uma cópia indevida no diagnóstico: `const auto declaration` copiava o vetor retornado por referência por ReadVertexDeclaration. Foi corrigido para `const auto&` antes de qualquer coleta válida. A primeira build foi descartada da medição. Os demais limites de timers/continues, partições, gating e reset não tiveram apontamentos. A build corrigida passou, com warnings existentes, e o CTest passou 2/2 (243 testes nativos e capture budget).

EXE corrigido SHA256 `326F68BC9284ED04DF76245CC40425A8C3EE2F56C290259A9584984E042361C3`; runtime `BDF4B0E8D6F7421C74AFBA67460866952BA8E8E7ECEC8FC560EBF898FDC36DC1`. Build normal preservada: `C1FC299694D25F4BE7115A7C5BBD9CC7674C2CA3C7FE67A11F3220B9B1BF75A8`.

| Controle no mesmo EXE | Resultado |
| --- | --- |
| CPU On, streams Off | Um bloco worker, zero linhas novas de streams |
| CPU Off, streams On | Mais de 120 swaps, zero linhas worker/streams |

Manifestos e validações: `logs/stream_detail_20261023_flags2_{off,alone}_{launch,validation}.json`. São controles de habilitação no título, não gameplay. A primeira tentativa de leitura do log ativo falhou por compartilhamento de arquivo; o helper de coleta passou a abrir com FileShare ReadWrite/Delete. Essa tentativa foi excluída; não mudou o renderer.

## Gameplay

Coleta `stream_detail_20261023_game1`: D3D12 1280×720, escala 1, MSAA 1, limite 60, VSync On, tracking On, shared cache Off, stacked resolves Off, perfil CPU/constantes/streams On, timeline sem detalhe. New Game confirmado visualmente, aquecimento adicional de pelo menos 25 s. Janela UTC 23:06:34,003–23:08:28,307 (20:06–20:08 local), **114,30 s**, 11 blocos completos e 1.407 quadros de timeline com margem de 250 ms. Sem builds/testes ou trace ativo na janela; câmera de idle e tráfego variaram.

| Região distinta | Média ms/quadro |
| --- | ---: |
| Setup: reset metadata, device, fetch dinâmico | 2,381 |
| ApplyBuffer | 4,617 |
| Binding IA | 1,305 |
| Preparação SRV | 3,551 |
| Leitura/decodificação de declaração | 0,703 |
| Montagem de metadata | 0,167 |
| Streams total, pai | 14,260 |

Contagens médias por quadro: **2.504,47 chamadas**, 3.545,39 streams e bindings IA, 2.069,84 streams dinâmicos, **1.359,25 SRVs criados** e 710,59 reusos na mesma submissão. Houve 2.069,84 leituras de declaração e 7.313,33 metadatas escritas. Planos: action0 3.539,50; action1 3,00; action2 2,90. Zero missing, falha de chamada, falha de SRV ou action desconhecida. Todas as partições e subtempos publicados passaram na validação.

As contagens de plano não localizam o custo de ApplyBuffer por action: poucos uploads podem ser caros e action0 pode precisar criar um recurso ausente no executor. Não atribuir seus 4,62 ms somente ao lookup ou somente aos uploads. Streams total inclui overhead dos novos relógios; a diferença em relação aos 11,51 ms anteriores não comprova regressão de renderização.

Timeline: intervalo médio 80,8 ms (12,4 FPS), worker 69,16 ms e GPU real 7,03 ms. Continua diagnóstico com profiling, sem aceitação de 60 FPS. Logs/manifestos/relatórios: `logs/stream_detail_20261023_game1*`, `logs/timeline_stream_detail_20261023_game1_window.{json,txt}`. Capturas `game1_window.jpg` e `game1_end.jpg`. Processo próprio encerrado após a coleta.

## Próximo experimento justificado

A leitura de declaração não é o maior custo deste caminho. O SRV de buffer é imutável enquanto seu recurso/forma permanecerem iguais, mas o código atual recria sua view quando `raw_srv_fence != next_fence_value_`. O trace de contadores confirma recriação contínua; o timing inclui lookup, alocador, GetDesc, CreateShaderResourceView e retirada, sem separar a responsabilidade de cada chamada.

Um experimento possível é manter um **cache limitado** de views dos buffers residentes, com invalidação quando o recurso muda e retirada por fence, Off por padrão. Não reter um descritor para cada BufferEntry sem limite: o log terminou com cerca de 28 mil buffers vivos e o heap tem 32.768 descritores, também usados por texturas. O experimento precisa de limite/evicção/invalidação testados, comparação Off/On e métricas de criação/hits/pressão do heap. Não altera uploads, dados de vértices nem a validade temporal de um recurso. ApplyBuffer continua alvo separado para subdividir se necessário.
