# XMA: bloco final retido pelo decoder — 22/10/2026

## Causa

O decoder XMA do SDK (`src/audio/xma_context.cpp`, legível com
`git -C .tools/rexglue-sdk-source show HEAD:src/audio/xma_context.cpp`) entrega a
saída com um bloco de atraso: a cauda de cada bloco decodificado fica em
`carry_frame_` até o próximo bloco fornecer a cabeça do quadro de saída
(`kDecoderStartPadding` = 192). `Work()` não esvazia o carry quando a entrada
acaba, então o último quadro de 512 amostras de um stream nunca sai e o mixer
(`sub_826592F0`) espera por ele. O hook antigo só desistia da espera; o áudio
não travava mais, mas o final de cada stream perdia esse quadro.

## Correção (`port/src/xma_fixes.cpp`, `port/src/xma_flush_policy.h`)

Quando o chute no contexto não progride, o carry é emitido seguido de silêncio
e o contexto é chutado uma vez mais, somente se todos os sinais indicam fim de
stream: contexto alocado, sem buffer de entrada válido, saída válida, bloco
carregado e nenhum subquadro pendente, pedido do mixer que consome tudo o que o
trecho atual tem (`[som+36]`), e sem outro trecho na fila (`[som+48]->next`).
Vozes em streaming que só esperam o disco ficam de fora (`input pending`).

O som do mixer não é visível do hook no meio do assembly; um hook de entrada em
`sub_826592F0` guarda `r3` (som) e `r5` (pedido) em estado local da thread.
Os membros privados do decoder são acessados por instanciação explícita de
template, então mudança no layout do SDK vira erro de compilação.

`SR_XMA_FLUSH=0` desliga o flush.

## Log

- `XMA fix: flushed final block of context ... [flush #N, M declined]`
  (os 50 primeiros e depois a cada 100).
- `XMA fix: no final-block flush ... : <motivo>` na primeira recusa de cada motivo.
- `XMA fix: N flushes, M declined: motivo=contagem ...` a cada 100 recusas.

## Estado

Compila; 7 testes da política passam. Em jogo, na máquina de trabalho (Intel UHD),
só apareceram recusas `input pending` e `released`; nenhum flush foi disparado,
e o corte de música após a decolagem não foi reproduzido nem validado.
