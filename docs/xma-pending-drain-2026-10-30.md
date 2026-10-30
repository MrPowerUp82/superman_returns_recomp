# Drenagem de PCM XMA pendente — 30/10/2026

Quarta candidata da [pesquisa Canary](xenia-canary-opportunities-2026-10-29.md).
A correção está no runtime utilizado pelo build padrão do projeto, como patch
canônico separado do tracking. Não altera o codec FFmpeg nem substitui o áudio.

## Falha reproduzida e mudança

O `Work()` síncrono desliga o contexto ao começar. `Consume()` entrega no máximo
`max(1, subframe_decode_count)` blocos de 256 bytes por passagem. O loop antigo
saía assim que os inputs deixavam de ser válidos ou `error_status == 4`, mesmo
com o resto de um frame PCM já decodificado e espaço disponível na saída.
Esperar/pollar não entrega esse resto: seria necessário outro kick do guest.

O novo loop termina por falta de entrada/erro 4 somente depois de drenar o frame
pendente ou atingir o limite de espaço. Mantém a primeira tentativa de Decode
do kick, mesmo com erro 4 previamente registrado, preservando recuperação.
O caminho consume-only (ambas as contagens de pacotes zero) também drena apenas
enquanto a admissão de espaço permitir. Saída de capacidade zero retorna antes
da operação de módulo no RingBuffer.

Foi adaptada a ideia de
`xma_context_new.cc` do Canary fixado em
`48b318a2ad7c36f8f3901a94e290ba70ae0ffec5`, sem copiar sua condição de stall por
offset. No nosso SDK, trocar de buffer pode manter o mesmo offset numérico.
O guard compara offset, buffer corrente, valid flags, loop count, erro, blocos
pendentes, orçamento de saída, write offset e estado de carry/skip/limites.
Uma passagem sem mudança nesses campos retorna, evitando spin síncrono.

`Consume()`, realinhamento de start-padding, carry final, truncamento/skip de
loop e `StoreContextMerged()` permanecem como antes. O orçamento inclui padding
e não é reconstruído do ring a cada passagem: wrap completo pode igualar read
e write sem significar espaço livre. Stereo continua contando blocos de 256
bytes, com oito por frame contra quatro em mono. A admissão conserva
`max(1, SDC) + padding`, mesmo se o restante do frame for menor que SDC.

O guard reconhece estado completamente inalterado; não prova término de todo
ciclo malformado com múltiplos estados. Não foi adicionado limite arbitrário
que interromperia trabalho produtivo e exigiria outro kick.

O [flush final existente](xma-audio-takeoff-fix-2026-10-27.md) continua necessário:
o carry que ainda aguarda o próximo frame é diferente dos blocos PCM prontos
para consumo. Esta mudança não introduz espera/yield no mixer.

## Distribuição e setup

- `tools/patches/rexglue/v0.10.0/0002-xma-drain-pending.patch` aplica-se ao SDK
  `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`; inclui a mudança de Work e o
  comentário de contrato em `include/rex/audio/xma/context.h`, sem mudar ABI.
- O setup agora valida ambos os patches por conteúdo e por caminhos distintos,
  com preflight de todas as entradas antes de aplicar/copiar. Migra o checkout
  com apenas 0001 e atualiza o manifesto com as duas hashes; mantém o campo
  legado `patch_sha256` para 0001. Rejeita patches parciais e mudanças locais
  desconhecidas, sem sobrescrevê-las.
- A comparação do diff fixa `suppressBlankEmpty=false` e `interHunkContext=0`.
  Além do diff, um índice temporário reconstruído de HEAD aplica as fontes
  canônicas para obter os blobs completos esperados. Os arquivos reais são
  comparados antes de qualquer alteração e antes do manifesto, independentemente
  de `assume-unchanged`/`skip-worktree`; o índice do usuário não é alterado.
  Apenas CRLF/LF é normalizado nessa comparação de texto UTF-8.
- O build padrão (`SR_MEMORY_TRACKING_SDK=ON`) aplica e verifica o patch.
  A opção de usar o SDK stock não inclui essa correção. Nenhuma variável de
  runtime adicional é necessária; os defaults gráficos anteriores permanecem.

## Verificação

Os testes chamam o **Work/Consume reais do runtime**, com contexto/ring em
memória guest e blocos PCM previamente marcados. Acesso de teste aos membros
privados usa a mesma técnica de member pointers do hook de flush, sem offsets
adivinhados, ABI nova, codec substituído ou cópia do loop de produção.

1. Antes do patch: cinco testes iniciais produziram **79 checks falhos** no
   runtime anterior do projeto; PCM, contagem pendente e write offsets mostraram
   o consumo parcial (`logs/xma_drain_red.log`).
2. Uma entrada com offset inválido e estado inalterado não retornou em 1 s no
   runtime stock Release; o processo de teste foi encerrado. A mesma fixture
   retornou com o runtime corrigido. Esse cenário depende dos asserts desligados
   em Release e roda em processo separado com timeout do CTest.
3. Nove testes XMA finais passaram, mais o processo isolado de stall. Cobrem
   mono/stereo, SDC 0/1/2, entrada esgotada, erro 4, espaço insuficiente, padding,
   wrap parcial/completo, retomada após liberar espaço, limites/skip de loop,
   campos guest preservados, consume-only e capacidade zero.
4. Troca de buffers com offset 32 em ambos continuou corretamente. Com erro 4
   prévio, a primeira tentativa de Decode ainda foi executada.
5. CTest integrado: **6/6**, incluindo os quatro testes de tracking existentes
   e dois processos XMA. Suíte nativa existente: **5/5** no CTest, sem falhas.
6. Setup: instalação limpa de dois patches, migração de manifesto antigo,
   estado misto inverso, VerifyOnly, idempotência de hashes/timestamps,
   atualização canônica autorizada e rejeição sem escrita de edições parciais,
   desconhecidas e caminhos/revisões inadequados passaram.
7. Revisão independente identificou duas lacunas do setup, reproduzidas antes
   do ajuste: preferências de diff podiam rejeitar fontes canônicas, e um edit
   oculto por assume-unchanged podia ser registrado como confiável. A suíte
   completa passou após a correção. Fixtures adicionais partem de instalação
   válida e exigem a rejeição exata por conteúdo para ambas as flags ocultas,
   no setup normal e no VerifyOnly, sem alterar fontes ou manifesto. A revisão
   final não registrou problemas restantes.

O primeiro teste real no jogo completou 5 s de aquecimento e 20 s de medição
com endpoint Realtek ativo, escrevendo `benchmark-20261030-063505.json`.
Houve um aviso de watchdog por ausência de swap durante a inicialização, antes
do início da medição, e o jogo prosseguiu até concluir. O resultado de 44,6 FPS
é somente evidência de execução: não houve par A/B nem atribuição de ganho de
FPS à correção de áudio.

Reprodução: build Release padrão; depois:

```powershell
$ctest='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
& $ctest --test-dir port/out/build/win-amd64-release -R '^(sr_memory_tracking_|sr_xma_)' --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File tests/tools/test_memory_tracking_setup.ps1
```

Logs de evidência em `logs/xma_drain_*`. O teste standalone deve usar o runtime
corrigido; o pacote stock antigo serve somente para a reprodução negativa.
As fixtures injetam PCM já decodificado: não validam por si só qualidade do
codec, conteúdo musical real ou continuidade após decolar. A audição e um
roteiro completo de decolagem continuam sem confirmação nesta etapa.

Builds finais SHA256:

| Artefato | SHA256 |
| --- | --- |
| `superman_returns.exe` | `3A188EDBC183197D92F1D94194E47221E55FF2592F1380BDE9F7CA53B370FFE4` |
| `rexruntime.dll` | `3D4681E7726FA28408CC68863D2713C2FB95F8FAC9F80DB1DB68EB03AFC7BB4A` |
| patch 0002 | `731A99A9FF31C96951AAA6DA6A744C0AA5F038F5007C9B54B5039C019AF8F6F1` |
