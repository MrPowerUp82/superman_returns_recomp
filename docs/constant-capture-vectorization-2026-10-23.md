# Vetorização da captura de constantes — 23/10/2026

Etapa CPU do desenho aprovado em `superpowers/specs/2026-10-23-native-renderer-parity-design.md`. Áudio adiado. Não implementa RT, DLSS ou menu, nem comprova 60 FPS.

## Evidência anterior à mudança

O perfil da fundação mediu 3,541 ms VS + 2,075 ms PS por quadro na obtenção/sanitização das constantes. O dump do objeto Release real mostrou loop escalar, mesmo com Clang 23, `-O3` e baseline `x86-64-v2`. Artefato: `logs/constant_snapshot_20261023_baseline_asm.txt`.

Um protótipo isolado, sem alterar a função do jogo, guardou `source.data()` antes do loop. O mesmo compilador passou a emitir SIMD. Os resultados foram apresentados antes da mudança de produção; o protótipo permaneceu em logs ignorados.

## Mudança e contrato

`constant_snapshot.cpp` agora guarda `const uint32_t* input=source.data()` e lê `input[i]`. O loop, checagem de tamanho, retorno, limpeza de erro e sanitização inteira permanecem com o mesmo contrato. Não acrescenta restrict, alinhamento obrigatório, fast-math, AVX2 ou alteração de versões/bancos/uploads. O compilador mantém tratamento de alias para o caminho in-place.

Três novos casos de teste cobrem ambos os sinais e todos os expoentes IEEE com mantissas de borda, dados pseudoaleatórios, NaN→zero preservando ±infinito/zero/denormais, ambas as ordens de bytes, in-place, fonte sem alinhamento SIMD e maior que o banco, truncamento sem escrita e limpeza de erro após sucesso. A caracterização passou antes da mudança e a suíte final passou com **243 testes, zero falhas**, incluindo uma reexecução após retomar a sessão.

## Gate manual RED → GREEN

`tools/bench/bench_constant_capture.cpp` liga o objeto real da build otimizada em um executável separado, sem duplicar a função. Usa 512 bancos (2 MiB), nove rodadas por endian, 131.072 capturas por rodada, descarta a primeira rodada e calcula a mediana das oito restantes. Consome os valores em checksum. Não rodou junto ao jogo ou à build.

| Objeto real ligado | Host ns/banco | Guest big endian ns/banco | Gate local 700 ns host |
| --- | ---: | ---: | --- |
| Antes | 1.597,955 | 1.627,694 | RED, exit 1 |
| Depois | 523,212 | 627,447 | GREEN, exit 0 |

Checksum idêntico `5040463666936212`. Redução de custo do kernel host de aproximadamente 67%; isso não é ganho equivalente de FPS. O teto de 700 ns é manual e específico deste i7-10750H, sem teste de tempo no CI. Não houve fixação de clock/afinidade; os valores absolutos variam. Assembly posterior contém operações SIMD inteiras (`pcmpeqd`, `pandn`) e `pshufb` no caminho endian. Logs: `constant_capture_20261023_gate_{red,green}.log` e `constant_capture_20261023_green_asm.txt`.

## Integração

Build incremental do jogo passou, seguida de 243 testes nativos. A revisão independente não encontrou problemas acionáveis em aliasing, limites, testes ou benchmark; não executou o jogo. O primeiro pedido de revisão falhou por limite de uso e foi retomado com sucesso depois.

EXE experimental SHA256 `1A40033AD16E48314A88F5F561917B476355CFAD2CF806A12E078F9E9074D496`, runtime `BDF4B0E8D6F7421C74AFBA67460866952BA8E8E7ECEC8FC560EBF898FDC36DC1`. Build normal preservada: `C1FC299694D25F4BE7115A7C5BBD9CC7674C2CA3C7FE67A11F3220B9B1BF75A8`.

`game1` terminou durante a interrupção da sessão sem gameplay confirmado; seus dados não entram na comparação de gameplay. A coleta válida `constant_capture_20261023_game2` usa D3D12, 1280×720, escala 1, MSAA 1, limite 60, VSync On, tracking On, shared cache Off e stacked resolves Off. Os perfis CPU/constantes e a timeline estão ligados. New Game foi confirmado visualmente, seguido de aquecimento. Janela UTC 22:50:43,858–22:52:39,988 (19:50–19:52 local), **116,13 segundos**, 11 blocos completos com ambas as bordas dentro da janela e 1.370 quadros completos de timeline, com margem de 250 ms. Sem builds/testes concorrentes e sem trace ativo.

| Medida diagnóstica | Fundação, 9 blocos | Atual, 11 blocos |
| --- | ---: | ---: |
| Captura VS, ms/quadro | 3,541 | 1,336 |
| Captura PS, ms/quadro | 2,075 | 0,750 |
| Escrita VS+PS, ms/quadro | 1,087 | 1,058 |
| Captura VS por upload, ns | 1.946,8 | 769,6 |
| Captura PS por upload, ns | 1.915,2 | 719,8 |
| UploadConstants, ms/quadro | 25,002 | 21,258 |
| Slots, aninhado, ms/quadro | 11,294 | 11,248 |
| Lookup dentro de slots, ms/quadro | 6,490 | 6,561 |
| Streams, ms/quadro | 11,270 | 11,509 |

A captura VS+PS passou de 5,616 para **2,086 ms/quadro** nessas janelas; o custo por upload caiu aproximadamente 60% VS e 62% PS. Os cenários/câmeras de idle variaram e o runtime da fundação tem hash diferente. Portanto, a evidência causal isolada é o gate do kernel; a rodada integrada confirma custo menor nessa região e ausência de falhas de contabilidade, sem atribuir a ela todo o delta de FPS entre rodadas.

Todos os blocos publicados foram validados quanto a uploads+reusos=draws, bytes=uploads×4096, subtempos dentro dos pais e falhas de alocação zero. Relatório e análise preservados em `logs/constant_capture_20261023_game2_{blocks.json,analysis.txt}`; manifestos e limites da janela no mesmo prefixo. O filtro inicial de timeline deu zero quadros porque linhas de duração GPU têm endpoints QPC zerados por contrato; o filtro foi corrigido para usar os endpoints CPU e conservar as durações GPU. Não foi necessário alterar a instrumentação do jogo.

Timeline: intervalo médio **84,4 ms (11,9 FPS)**, worker **67,09 ms**, GPU real **6,89 ms**. São dados com profiling ligado, sem aceitação de desempenho. Não houve platô ocioso de 33 ms; **60 FPS continuam não atingidos**. Gameplay e decolagem foram observados, com capturas `game2_window.jpg`, `game2_end.jpg` e `game2_flight.jpg`. O processo próprio foi encerrado após a coleta.

Próximo alvo de CPU: subdividir o custo de streams (11,5 ms) e/ou slots (11,2 ms), onde alocação/aplicação de buffers, SRVs, declarações e lookups ainda se misturam. Os dados não justificam cache baseado somente em endereço, remover efeitos ou mudar constantes de simulação.
