# Replay com constantes compactas — 19/10/2026

Meta: 60 FPS reais, Vulkan nativo, escala 1 e geração de quadros desligada.
Esta etapa continua a [preparação em blocos](native-vulkan-binding-batches-results-2026-10-19.md).
Não declarar a meta atingida a partir dos números isolados abaixo.

## Mudança

`DrawPacket::constants` ocupa 560 bytes em vez dos 12288 bytes da representação
de upload. Guarda dois bancos compartilhados e 512 bytes específicos do draw.
O packet completo agora ocupa 10144 bytes neste build Windows x64.

O mirror PM4 publica um banco normalizado e imutável por versão VS/PS. Draws
da mesma versão compartilham a memória; escrever novos registradores ou
destruir/resetar o mirror não altera packets anteriores. Bancos editados por
fixtures/draws manuais usam armazenamento privado. Copiar um banco editável
faz uma cópia profunda, mesmo se o chamador conservar um ponteiro `data()`.

Os 3584 bytes de metadados de vértices duplicados no antigo bloco de constantes
foram removidos do packet. O backend portátil reconstrói esses bytes a partir
de `vertex_fetch` durante o upload; o global usa a declaração compartilhada.
`ConstantSnapshot` continua com 12288 bytes para a GPU, nos offsets anteriores.
Os uploads agora copiam cada banco explicitamente, sem interpretar a classe
compacta como um bloco de bytes GPU.

## Testes e medição isolada

O teste de tamanho falhou antes da mudança. Um teste adicional expôs aliasing
de um ponteiro de edição depois de copiar o banco; a versão final usa ownership
privada para os bancos editáveis e passou o caso.

Replay Release, 3008 draws, lotes de 32, pool desligado, dois conjuntos de
packets vivos, dez rodadas com duas de aquecimento; medianas das oito restantes:

| Medida | Antes, bancos repetidos | Compacto, bancos repetidos |
| --- | ---: | ---: |
| Construção do arena | 24,8477 ms | 10,7617 ms |
| Decode | 11,6512 ms | 0,8984 ms |
| Preparação + decode | 36,4440 ms | 11,6649 ms |
| Ciclo incluindo descarte anterior | 46,1838 ms | 17,4779 ms |

Esse fixture não inclui todo o estado de dispositivo do jogo. A queda não
prevê o ganho de gameplay. Uma nova variante muda cerca de 70% dos bancos VS
e 40% dos PS, proporções próximas às observadas no jogo: ciclo de 31,2832 ms,
incluindo 12,2666 ms de decode. Não há baseline antigo equivalente para essa
variante; não compará-la como se fosse a mesma carga do fixture anterior.
Todos os checksums passaram.

Evidências: `logs/native60_compact_replay_{before,after,varied}_perf.log`.

Verificação concluída antes do teste no jogo:

- 190 testes nativos, zero falhas; CTest nativo 2/2.
- 108 testes Vulkan, zero falhas; CTest Vulkan 1/1.
- Prova GPU com bancos compartilhados nos caminhos global e portátil,
  índices/offsets/IA, texturas e versões pendentes: validação ativa, zero erros.
- Contrato portátil, passes mesclados, recorder assíncrono, composição e
  interface imediata também passaram seus testes GPU.
- Revisão independente não encontrou achados acionáveis em versões,
  ownership, normalização, edição e uploads.

O CTest inicialmente acusou a criação dos dois bancos frios no teste de
alocações. O estado PM4 e seus snapshots passaram a ser aquecidos antes da
janela contada. Os limites permaneceram 32 headers para 32 comandos no caso
copiado e zero alocações na repetição do caso com batch congelado.

Logs: `logs/native60_compact_replay_*`, incluindo RED de tamanho e de aliasing.

## Verificação no jogo

Build concluído (`logs/native60_compact_replay_game_build.log`). SHA-256:
`62E34402C27F3CB122E14C21E22B8D64306E89D210CCD2D2E7215099E5FA7448`.
O binário anterior `03AA7CFF...` está preservado como `binding_batches_after.exe`;
este novo está como `compact_replay_after.exe`, ambos em
`artifacts/native-renderer-performance/`.

Vulkan nativo, contrato global=1, escala 1, MSAA 1, LSFG desligado, limite 60,
janela lógica 1280×720, swapchain físico 1600×900:

| Rodada deste executável | FPS parado | FPS em movimento | Mínimo em movimento |
| --- | ---: | ---: | ---: |
| Com perfil e timeline detalhada | 9,71 | 10,22 | 8,3 |
| Sem perfil nem timeline | 10,06 | 10,64 | 9,5 |

Timers nominais de 20 s por cenário; 10–11 amostras de FPS do guest por janela,
sem builds ou testes concorrentes. O helper pode incluir amostras de leitura
dos logs posteriores ao timer. Na rodada limpa não houve timeline ou mensagens
do perfil por draw. Apresentações repetidas não contam como quadros novos.

Personagem, cidade e HUD foram observados no gameplay. Câmera de inatividade e
percursos continuam não determinísticos; não houve equivalência de pixels
entre rodadas reais. Nem a diferença entre modos nem as medições anteriores
provam ganho ou regressão causal. A referência antiga de aproximadamente
13,5 FPS permanece válida no registro antigo, sem servir como cena equivalente.

No perfil próximo ao percurso, decode caiu para cerca de 24 ms; arena_setup
continuou perto de 13 ms. A redução de representação não eliminou o custo de
inicializar o restante do estado. A timeline recortada em movimento registrou:

| Estágio | Média |
| --- | ---: |
| Game (inclui trabalho/esperas sem atribuição exclusiva à simulação) | 100,65 ms |
| Frontend | 45,65 ms |
| Worker | 49,37 ms |
| Record | 72,89 ms |
| GPU real | 15,19 ms |

Os estágios se sobrepõem; não somar. Estes tempos são da rodada instrumentada,
não da limpa. A GPU também tem pouca folga para receber mais trabalho. O ganho
de 2,6× no fixture repetido não se traduziu em aumento comprovado de FPS no jogo.

Evidências: `logs/native60_compact_{replay,clean}_20261019_*` e
`logs/native60_compact_replay_{idle,forward}_timeline.json`.

**A meta de 60 FPS continua pendente.** A próxima etapa deve distribuir a
preparação/gravação de estados imutáveis entre núcleos, respeitando a ordem de
uploads, transições, resolves e aliases. Paralelizar somente os cerca de 3–4 ms
de emissão de comandos seria insuficiente; frontend e replay também precisam
caber no orçamento de 16,67 ms.

Nenhum commit, pacote público ou publicação foi realizado nesta etapa.

Continuação: [replay paralelo e comparação no jogo](native-vulkan-parallel-replay-results-2026-10-19.md).
