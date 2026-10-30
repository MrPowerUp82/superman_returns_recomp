# Crash durante a luta de Mongul — 30/10/2026

## Evidência

Esta falha é diferente da fragmentação Warworld → Metropolis. O log atual
mostra alocações bottom-up bem-sucedidas, inclusive o carregamento de
`objtiv.AST` às 19:06:00 em `A1000000`, com mais de 400 MB livres e maior trecho
livre acima de 370 MB. Não há a sequência anterior de falha seguida de leitura
para endereço zero. A correção de alocação permanece ativa.

O Windows registrou access violation `0xC0000005`, módulo `VCRUNTIME140.dll`,
offset `0x1CCA7`. O dump `superman_returns.exe.25988.dmp` identifica a thread
19256 (a mesma do mixer XMA) e a leitura inválida `0x1F348D0BB79`.
Os retornos consecutivos da pilha correspondem a:

```
memcpy (VCRUNTIME140+1CCA7)
rex::memory::RingBuffer::Write + 0x3B
rex::audio::XmaContext::Consume + 0xE0
rex::audio::XmaContext::Work + 0x2BD
```

Os endereços foram associados às exportações e instruções do runtime original,
preservado antes da recompilação. O timestamp PE do runtime coincide com o do
módulo no dump. Não havia WinDbg/CDB instalado; foi usado o parser local de
minidump, com leitura do contexto da exceção, pilha, PE e disassembly.

O `this` de Consume é `0x1F248D09EF0`; as instruções dão offset `0x2089` para
raw_frame, resultando em `0x1F248D0BF79`. O contexto local de áudio indica mono
e quatro blocos por consumo. A diferença entre a leitura inválida e o início
do PCM é exatamente `0xFFFFFC00`: `(4 − 8) * 256` convertido para uint32.
Logo, PCM pendente de oito blocos foi interpretado no layout mono de quatro.
O int8 negativo virou um deslocamento de quase 4 GB na multiplicação unsigned.

O log também registra o flush estéreo do contexto `FFCA8E00`, que aparece na
pilha da falha. Isso é consistente com reconfiguração/reuso do contexto enquanto
resta PCM de outro layout; não é necessário assumir falha do driver ou DLSS.

Originais preservados localmente em `logs/mongul_crash_20261030/`: logs atuais
e rotacionados, dump, executável e runtime. Dumps e binários não entram no Git.

## Correção

O patch do SDK compara o layout recebido com o formato do decoder antes de
consumir. Em mudança de canais ou contagem incompatível, usa ResetDecoderState
para descartar PCM/carry/loops antigos e forçar a reabertura do codec. Não muda
o layout da classe nem a ABI. Consume também valida limites antes da subtração
e mantém offset e tamanho unsigned e limitados ao frame. Entradas válidas
mantêm o caminho normal, inclusive drenagem, padding, loops e orçamento do ring.

O instalador reconhece apenas a versão anterior exata do patch 0002, mantida
em `previous/`. A migração só começa após comparar os arquivos completos com
o conteúdo canônico construído em índice Git temporário. Alterações locais,
inclusive ocultas por assume-unchanged/skip-worktree, continuam sendo recusadas.

## Validação e limite

Antes da correção, o novo teste mono → estéreo falhou com saída antiga indevida
e carry não descartado. A ordem evita disparar primeiro o AV estéreo → mono.
Depois, os testes verificam ambos os sentidos, descarte seguro, reabertura do
codec e entrega de PCM novo; contagens inválidas e o guard independente de
Consume também são cobertos. A retomada usa PCM controlado na fixture, não
uma nova decodificação de uma gravação da luta.

Suite XMA: 12 testes, zero checks falhos, mais os cenários separados de stall
e troca de canais. CTest do runtime/tracking: 7/7. Testes nativos: 5/5.
O executável normal foi recompilado com o runtime corrigido.
Os testes completos do instalador também passaram, incluindo migração da
versão anterior, VerifyOnly e rejeição de edições ocultas sem sobrescrita.
SHA-256 do runtime corrigido instalado ao lado do executável:
`A3B01E59741C92AC00C331014798F9D20703C0E02FF73A5EBC715DF2B04E4119`.

Em 30/10/2026, o usuário confirmou que a correção funcionou após retestar o jogo.
Um benchmark da tela de título não comprova estabilidade dessa luta.
O smoke de inicialização com o binário corrigido terminou válido e sem crash
(`logs/mongul_audio_fix_smoke_20261030/result.json`); as preferências gráficas
do usuário foram preservadas.
