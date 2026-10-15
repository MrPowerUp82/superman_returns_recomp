# Cache por versão dos bancos VS/PS — experimento descartado (2026-10-14)

O desenho aprovado foi implementado, revisado e medido. **Não foi mantido no
código de produção.** O cache reduziu o custo isolado de upload e o perfil de
bindings, mas não demonstrou benefício de FPS nas quatro rodadas limpas.
As duas rodadas ligadas ficaram abaixo de suas respectivas referências.
Como o desenho foi autorizado condicionado às medições, o código e o
executável anteriores foram restaurados, preservando os ajustes já aceitos.

As cenas não são replays determinísticos e as cargas de draws variam. Esses
resultados não provam uma regressão causal universal; tampouco justificam
ativar o novo cache por padrão ou afirmar ganho no gameplay. A execução
restaurada também variou em FPS. O experimento fica disponível para análise,
sem acrescentar identidade, metadados ou flags ao produto atual.

## Desenho testado

- Identidade única de cada Pm4Mirror, renovada ao copiar/mover/atribuir/resetar.
  As versões existentes de VS/PS continuavam refletindo mudanças nos bancos.
- Proveniência fora do ConstantSnapshot: mirror ID e duas versões por draw,
  válidos somente para bancos imutáveis publicados pelo decoder. Edição dos
  bancos exigia limpar a proveniência. Nenhum consumidor de produção foi
  encontrado alterando VS/PS depois do decode.
- Cache de metadados, sem memcmp dos 8 KiB ou cópia auxiliar dos bancos na CPU.
  Reutilização limitada ao mesmo mirror, versões, serial de submissão e chunk.
  Offsets VS/PS independentes; shared reconstruído em todo draw. Reservas de
  12 KiB por draw e flush/lifetime existentes foram preservados.
- Proveniência ausente ou `SR_VULKAN_VERSIONED_BANK_CACHE=0` usavam o caminho
  anterior. O cache exato antigo ficou desligado em todas as comparações.
- O GPU ABI manteve ConstantSnapshot com 12.288 bytes. A proveniência aumentou
  o DrawPacket de 21.872 para 21.896 bytes; esse aumento também foi revertido.

## Diagnóstico isolado

Mesmo binário de testes, 3.000 payloads distintos, 16 rodadas. Padrão de
repetição aproximado de 30% VS e 60% PS, memória mapeada real do ResourceStore,
comparando upload normal, cache exato e cache por versão. Tempos em µs/draw:

| Processo | Normal | Exato | Por versão |
| --- | ---: | ---: | ---: |
| 1 | 7,56101 | 7,80115 | 7,29073 |
| 2 | 7,70485 | 8,00349 | 7,16068 |
| 3 | 7,57074 | 7,83629 | 7,41474 |

Redução de cerca de 2–7% nessa amostra. Checksum igual nos três caminhos:
`14212856112228415104`. Por processo: 14.384 hits VS e 28.752 PS em 48.000
draws; mudanças de chunk reduzem a reutilização em relação à repetição bruta.
O checksum do benchmark cobre os bindings; a correção dos bancos e offsets
também foi verificada por testes e readbacks de pixels, não por esse checksum.

Referência prévia em executável separado, antes da mudança de layout:
normal 7,40836/7,68594/7,56921; exato 7,67307/7,84875/7,62616 µs/draw.
Não substituir a comparação no mesmo binário por esses processos separados.
Logs: `logs/versioned_banks_{before,after}_micro*.log`.

## Perfil e FPS

RTX 2060 / i7-10750H, Vulkan nativo, 1280x720, scale 1, MSAA 1, vsync ligado,
limite 30 FPS e frame generation desligado. Mesmos caminho, nome e hash do
executável experimental, alternando só o flag. Sem builds, testes pesados ou
restauração de fontes nas janelas de FPS. Cada rodada verifica gameplay/HUD
e mede 20 segundos parado e 20 segurando W.

Últimas janelas instrumentadas de 120 frames:

| Cache | Draws/frame | Bindings ms/frame | Record ms/frame | Reutilizações VS/PS |
| --- | ---: | ---: | ---: | --- |
| OFF | 2.854 | 16,0 | 62,6 | 0 / 0 |
| OFF | 2.942 | 15,9 | 56,2 | 0 / 0 |
| ON | 2.432 | 11,0 | 52,0 | 712 / 1.461 |
| ON | 3.000 | 12,8 | 54,0 | 861 / 1.912 |

O cache foi usado no gameplay e o perfil apontou menor tempo em bindings.
Cargas diferentes impedem tratar a diferença bruta como um A/B causal exato.
FPS instrumentados OFF 13,0/13,4; ON 12,9/13,4. Timeline média record OFF
37,28 ms, ON 36,36 ms; GPU aproximadamente
7,7 ms. Estágios se sobrepõem e não devem ser somados.

Rodadas limpas, sem instrumentação, na ordem executada:

| Rodada | Parado FPS | Movimento FPS | CPU cores parado/movimento |
| --- | ---: | ---: | --- |
| `cpu_versioned_banks_off_clean` | 15,3 | 15,0 | 6,9 / 6,9 |
| `cpu_versioned_banks_on_clean` | 13,6 | 14,7 | 7,0 / 7,0 |
| `cpu_versioned_banks_on_clean2` | 14,0 | 13,7 | 7,0 / 6,9 |
| `cpu_versioned_banks_off_clean2` | 14,6 | 14,7 | 7,0 / 6,9 |

A repetição com ordem invertida não sustentou benefício de FPS. A economia
local não foi suficiente para justificar manter esse caminho no jogo.

## Verificação e restauração

O experimento passou 183 testes nativos e 93 Vulkan, orçamento de alocações,
CTest nativo 2/2 e Vulkan 1/1. RED nativo 13 checks falharam; RED Vulkan 10,
antes de implementar identidade/proveniência/reutilização. Uma primeira
compilação do RED Vulkan tinha o alias `guest` abaixo dos testes novos; foi
corrigido e o RED executado com falhas comportamentais esperadas.

Readbacks com `--production-bindings` e `--versioned-bindings` passaram nas
três variantes cada. O caminho por versão verificava restauração independente
de bancos consumidos pelo shader, troca de origem com versões iguais, hits
posteriores e offsets. Fixture `--game-record-versioned` confirmou hits dos
dois bancos no GameRenderer. Fixtures normal/merged, lifetime, frame assíncrono
e IDs de descritores passaram. Revisão independente não encontrou problema de
correção ou lifetime. Camadas de validação indisponíveis; `validation_errors=0`
não representa execução com validation layers.

Após retirar o experimento, passaram novamente 181 testes nativos, 91 Vulkan,
orçamento de alocações, CTest 2/2 e 1/1, três readbacks explícitos de
`--production-bindings` e fixtures merged/lifetime/IDs. A busca nos fontes
confirmou ausência da API, metadados e cache experimentais. Arquivos que eram
limpos antes do experimento voltaram sem diff; mudanças anteriores preservadas.

Gameplay do executável restaurado, sem instrumentação:
`cpu_versioned_banks_restored` **13,6/14,4 FPS**. Não interpretar essa variação
como benefício da restauração. Cena, personagem e HUD conferidos visualmente.
Seis gates contra `off_clean` passaram sem relaxar limites: perfil OFF
34,75 dB / histograma 0,01831; perfil ON 35,74 / 0,00764; ON 1 38,44 / 0,00637;
ON 2 37,02 / 0,00541; OFF 2 33,61 / 0,02392; restaurado 34,46 / 0,01364.
A cobertura é dos cenários testados, não de todo o jogo ou do gate histórico C7.

A sequência dos perfis inicialmente parou entre OFF e ON porque o processo
anterior ainda aparecia durante o encerramento. OFF já havia concluído suas
medições; ON não havia começado. O helper recebeu espera limitada e a medição
ON foi feita novamente. Não é uma rodada perdida de gameplay nem evidência de
falha do cache. O helper agora rejeita o executável restaurado, que não conhece
esse flag, para impedir comparações futuras rotuladas incorretamente.

## Artefatos

Atual/restaurado: `port/out/build/win-amd64-release/superman_returns.exe`, igual
a `artifacts/native-renderer-performance/versioned_banks_before.exe`.
SHA-256: `FC4A158A5CCE7B9E5576AC449BB2BCC59448C46D21ADF274269183C626A6E1EC`.
Esse é o executável já construído e validado do ajuste anterior, não uma nova
build do jogo após a restauração de fontes.

Experimento: `artifacts/native-renderer-performance/versioned_banks_candidate.exe`,
SHA-256 `05383649BF9131CEC6B2705899BE35B76B86D61202F21B5918DE3D796D920499`.
Snapshot dos 13 arquivos alterados em
`artifacts/native-renderer-performance/versioned_banks_trial/`, mantendo caminhos
relativos. Não é um projeto independente: usa o restante deste repositório e
as dependências do runtime Release. Código experimental retirado do produto.

Builds: `build/versioned_banks_*build.log`. Testes e restauração:
`logs/versioned_banks_*tests.log`, `logs/versioned_banks_*ctest.log`,
`logs/versioned_banks_pixels_*.log`, `logs/versioned_banks_fixture_*.log` e
`logs/versioned_banks_restored_*`. Gameplay: `logs/bench_cpu_versioned_banks_*`,
CSV `logs/bench_results.csv` e timelines correspondentes. Gates:
`logs/versioned_banks_image_gates.json`, helper Windows PowerShell 5.1
`build/compare_versioned_banks.ps1`.

Até o encerramento desta medição, não havia commit ou publicação.
O executável de distribuição não foi substituído.
Os 30 FPS e a eliminação completa dos gargalos continuam pendentes.
