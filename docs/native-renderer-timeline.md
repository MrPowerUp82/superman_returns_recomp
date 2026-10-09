# Linha do tempo por quadro do renderer nativo

Ferramenta da Fase 0 do ciclo 4 (design: `superpowers/specs/2026-10-08-native-renderer-cpu-gpu-bottlenecks-design.md`).

## Como usar

- `SR_FRAME_TIMELINE=<arquivo.csv>` liga a gravação; sem ela nada é registrado.
- `tools\bench\bench_api.ps1 -Api vulkan|d3d12 -Name <nome> -Timeline` roda o bench e imprime a tabela.
- `python tools\analysis\frame_timeline_report.py <arquivo.csv> [--last N] [--json]` analisa um CSV.
- Colunas, estágios e o significado de `busy_ns`/`blocked_ns`: `docs/superpowers/plans/2026-10-08-frame-timeline-phase0.md`.
- `-Timeline -TimelineDetail` (variável `SR_FRAME_TIMELINE_DETAIL=1`, só junto de `SR_FRAME_TIMELINE`) liga também os contadores finos do front-end da thread do jogo (estágios `frontend`, `fe_*` e os derivados `game_guest`, `frontend_other`); ver "Decomposição do front-end da thread do jogo (Fase 0.5)" no fim deste documento.

## Como ler a tabela

`média/p50/p99` são ms de trabalho do estágio por quadro (sem a espera); `bloq.` é a espera média por outro estágio; `util.` é média ÷ intervalo médio do quadro; `folga p99` é o orçamento de 33,3 ms menos o p99; `limitante` é a fração dos quadros em que o estágio teve o maior tempo ocupado entre `game`, `worker`, `record` e `gpu`. `capture` está aninhado em `game` (só existe no Vulkan, onde a captura roda na thread do jogo); `game_other` é `game` menos `capture` (também só no Vulkan). O aninhamento não é estrito: a captura PM4 do próprio comando de swap roda depois do carimbo de entrada do swap (≈4 µs), o que pode deixar o `game_other` levemente negativo em um quadro isolado (nas duas janelas analisadas do Vulkan nenhum quadro ficou negativo; o mínimo é 18,10 e 20,53 ms). O `1% low` do cabeçalho do relatório é 1000 ÷ p99 do intervalo entre quadros, não a média dos piores 1% dos quadros.

### Ressalvas (leia antes de usar os números)

1. **`gpu` não é tempo ocupado puro.** No Vulkan a linha `gpu` vai do `TOP_OF_PIPE` do primeiro command buffer de upload ao `BOTTOM_OF_PIPE` do fim do quadro, com 2 quadros em voo; ela pode incluir fila atrás do quadro anterior. Trate o `util.` da GPU do Vulkan como **limite superior**, não como tempo ocupado. No D3D12 a linha vem dos timestamps do próprio renderer (do primeiro ao último dentro do quadro) e pode ter ressalva parecida (lacunas entre passes entram na conta).
2. **`game` inclui a lógica do próprio jogo**, não só o renderer. Ele é o intervalo entre a saída de um `OnSwap` e a entrada do seguinte, então também inclui qualquer espera da thread do jogo fora do renderer. Nas tabelas, `game` + `front_wait` fecha com o intervalo médio do quadro (por exemplo, no D3D12: 32,76 + 0,72 ≈ 33,5 ms), logo o `util.` do `game` fica perto de 100% sempre que o `OnSwap` é curto. Só o `game_other` (`game` menos `capture`, apenas no Vulkan) separa a captura do resto. No Vulkan também vale `game` + `front_wait` ≈ intervalo por construção, então o `util.` e o `limitante` do `game` ali, sozinhos, não provam que a thread do jogo é o gargalo. No D3D12 o limitador de 30 FPS segura o intervalo em ~33,4 ms, então o `game` e o seu `limitante` **não provam** que a thread do jogo é o gargalo.
3. **`limitante` é a fração dos QUADROS** em que o estágio teve o maior tempo ocupado, não uma fração ponderada pelo tempo.
4. **Uma máquina, cena variável.** Tudo vem de um notebook (Intel UHD, i5-13420H) e de uma cena que muda de uma execução para outra. Compare as razões entre as duas execuções de cada API, não os valores absolutos.
5. **Janela.** O relatório usa `--last 1100`, que é a janela dos últimos 1100 quadros (cerca de 37 s a 30 FPS; no Vulkan, com intervalos de 36–38 ms, cerca de 40 s), que cobre a fase andando e o fim da fase parado, e pode alcançar antes da fase parado. Por isso o intervalo médio do relatório pode diferir do FPS médio impresso pelo bench, que mede só a janela de 20 s de cada cenário.
6. **O tempo ocupado do `worker` pode esconder esperas.** Esperas que não estão envolvidas por um bloco de escopo próprio entram como trabalho (ocupado), não em `bloq.`. Casos conhecidos: a aquisição de `mutex_`, o `RefreshGuestOutput` do apresentador e, no Vulkan, o `WaitSlot` depois do submit, que conta como ocupado do `record`. Por isso um `bloq.` de 0,00 ms (como no D3D12) não prova que o estágio nunca espera.

## Custo da instrumentação (ligada contra desligada)

FPS médio do bench (mínimo entre parênteses), mesma build, ordem desligada/ligada/ligada/desligada:

| API | Cenário | OFF 1 | ON 1 | ON 2 | OFF 2 |
| --- | --- | --- | --- | --- | --- |
| Vulkan | parado | 26,4 (19,2) | 27,2 (23) | 25,5 (18,8) | 26,4 (21,9) |
| Vulkan | andando | 29,1 (27,1) | 28,6 (23,1) | 28,1 (25) | 27,5 (26,1) |
| D3D12 | parado | 30 (29,6) | 29,9 (29,5) | 29,6 (27,8) | 29,7 (28,3) |
| D3D12 | andando | 29,6 (26) | 29,8 (27,6) | 30 (29,7) | 29,3 (25,8) |

As médias dos FPS ligados (26,35 / 28,35 / 29,75 / 29,9, na ordem das linhas) ficam a até cerca de 0,5 FPS das médias dos desligados (26,4 / 28,3 / 29,85 / 29,45), sem direção sistemática (diferenças de -0,05, +0,05, -0,10 e +0,45 FPS). Os valores individuais se sobrepõem, mas não ficam todos dentro da faixa dos desligados (por exemplo, Vulkan parado ON 1 em 27,2 está acima dos dois desligados, 26,4 e 26,4). Os mínimos do Vulkan andando são menores nas duas execuções ligadas (23,1 e 25) que nas duas desligadas (27,1 e 26,1); isso é compatível com a variação de cena entre execuções, mas fica registrado. Outras células em que um valor ligado fica abaixo dos dois desligados: Vulkan parado, média do ON 2 (25,5 contra 26,4 e 26,4) e mínimo do ON 2 (18,8 contra 19,2 e 21,9); D3D12 parado, média do ON 2 (29,6 contra 30 e 29,7) e mínimo do ON 2 (27,8 contra 29,6 e 28,3). Um custo de instrumentação não é distinguível da variação de cena com duas execuções por célula. O teste unitário `timeline_recording_a_frame_costs_microseconds` limita só o custo de gravar um quadro (`Record`); ele não cobre o `Flush`, que escreve o arquivo na thread do jogo a cada 60 `swap`s. O critério do design (< 0,3 ms por quadro, ligado contra desligado) portanto não é demonstrado pelo A/B acima (inconclusivo) nem pelo teste unitário sozinho. O custo do `Flush` foi medido a partir dos CSVs.

### Custo do Flush (medido nos CSVs)

Para cada quadro N com N % 60 == 0, o intervalo entre o fim do `front_wait` do quadro N e o início do `game` do quadro N+1 é o stall causado pelo `Flush` mais o carimbo de saída (o `Flush` roda antes desse carimbo, então o stall não entra em nenhum estágio, só no intervalo do quadro). O mesmo intervalo nos demais quadros mede a linha de base. Arquivos inteiros dos CSVs `tl_on1_*` e `tl_on2_*` (ms):

| Execução | Quadros com Flush | Stall médio | p50 | Máximo | Amortizado (médio ÷ 60) |
| --- | --- | --- | --- | --- | --- |
| Vulkan ON 1 | 41 | 1,478 | 1,420 | 2,438 | 0,025 |
| Vulkan ON 2 | 40 | 1,282 | 1,081 | 3,708 | 0,021 |
| D3D12 ON 1 | 65 | 1,062 | 1,077 | 1,684 | 0,018 |
| D3D12 ON 2 | 65 | 1,049 | 1,016 | 1,749 | 0,018 |

Nos demais quadros o mesmo intervalo tem média de 0,0002 a 0,0003 ms (máximo de 0,005 a 0,014 ms). Restrito à janela dos últimos 1100 quadros usada nas tabelas (18 quadros com Flush por execução), o stall médio é 1,70 e 1,48 ms no Vulkan e 1,14 e 1,24 ms no D3D12, com máximo de 2,44 e 3,71 ms no Vulkan e 1,42 e 1,57 ms no D3D12. O custo amortizado fica em ≈0,02 ms por quadro, bem abaixo de 0,3 ms, mas ele chega como um engasgo de 1 a 4 ms uma vez a cada 60 quadros. Esse tempo cai no intervalo entre quadros e em nenhum estágio, então alimenta o 1% low e o mínimo (1 quadro em 60 é 1,7% dos quadros) sem aparecer em `game`, `worker`, `record` ou `gpu`. O custo de `Record` (microssegundos, pelo teste unitário) não está incluído.

## Resultados (Intel UHD, i5-13420H, 1280x720, vsync, limite de 30 FPS, 2026-10-08)

### Vulkan

Execução `tl_on1_vulkan` (bench: parado 27,2 FPS, andando 28,6 FPS):

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 35.9 ms = 27.8 FPS | 1% low 19.2 FPS | mínimo 1.8 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio        média     p50     p99   bloq.   util.  folga p99  limitante
game           34.99   34.77   48.31    0.00     97%     -14.98        73%
game_other     28.40   28.09   37.85    0.00     79%      -4.52          -
capture         6.59    6.21   14.47    0.00     18%      18.86          -
front_wait      0.92    0.00    9.83    0.00      3%      23.50          -
worker         29.32   29.11   43.71    2.45     82%     -10.37        14%
record         25.27   24.70   43.34    2.82     70%     -10.00         5%
gpu            29.75   30.36   39.38    0.00     83%      -6.05         8%
```

Execução `tl_on2_vulkan` (bench: parado 25,5 FPS, andando 28,1 FPS):

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 37.7 ms = 26.6 FPS | 1% low 18.0 FPS | mínimo 1.9 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio        média     p50     p99   bloq.   util.  folga p99  limitante
game           36.67   36.28   49.06    0.00     97%     -15.73        72%
game_other     29.51   29.32   37.69    0.00     78%      -4.36          -
capture         7.16    6.78   14.29    0.00     19%      19.05          -
front_wait      0.95    0.00   16.14    0.00      3%      17.20          -
worker         31.55   31.55   43.80    3.13     84%     -10.46        13%
record         27.72   26.92   49.73    2.90     74%     -16.40         6%
gpu            31.93   32.17   37.20    0.00     85%      -3.86         9%
```

### D3D12

Execução `tl_on1_d3d12` (bench: parado 29,9 FPS, andando 29,8 FPS):

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 33.5 ms = 29.8 FPS | 1% low 21.7 FPS | mínimo 11.9 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio        média     p50     p99   bloq.   util.  folga p99  limitante
game           32.76   32.91   42.42    0.00     98%      -9.08        74%
front_wait      0.72    0.00   18.43    0.00      2%      14.90          -
worker         29.17   29.11   47.63    0.00     87%     -14.30        26%
gpu            21.47   22.69   26.27    0.00     64%       7.07         0%
```

Execução `tl_on2_d3d12` (bench: parado 29,6 FPS, andando 30 FPS):

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 33.4 ms = 29.9 FPS | 1% low 22.5 FPS | mínimo 10.5 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio        média     p50     p99   bloq.   util.  folga p99  limitante
game           30.86   30.71   43.34    0.00     92%     -10.00        42%
front_wait      2.56    0.00   17.52    0.00      8%      15.81          -
worker         32.37   31.89   43.54    0.00     97%     -10.21        57%
gpu            25.02   24.70   29.90    0.00     75%       3.43         1%
```

### Verificação do caminho bloqueado do worker (D3D12 limitado pela GPU)

Nas execuções acima a GPU do D3D12 fica abaixo do orçamento, então a espera de fence do worker (`bloq.`) nunca aparece (0,00 ms). Para provar que o caminho funciona, uma execução extra rodou o D3D12 com `--sr_native_render_scale=2` (GPU como gargalo; o FPS cai de propósito, parado 17,5 e andando 16,8 FPS, e o gate de HUD aceitou a execução):

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 59.0 ms = 17.0 FPS | 1% low 11.8 FPS | mínimo 6.4 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio        média     p50     p99   bloq.   util.  folga p99  limitante
game           23.47   23.40   32.45    0.00     40%       0.88         0%
front_wait     35.50   35.58   59.46    0.00     60%     -26.12          -
worker         30.17   29.47   53.49   28.35     51%     -20.15         1%
gpu            58.88   59.81   68.63    0.00    100%     -35.29        99%
```

O `bloq.` do worker sobe de 0,00 para 28,35 ms, o `front_wait` do jogo sobe para 35,50 ms, a GPU fica em 100% de `util.` e é o limitante em 99% dos quadros. O caminho de espera de fence está instrumentado e fica visível quando a GPU limita.

## Conclusão: estágio limitante de cada API

**Vulkan, medições.** O intervalo médio ficou em 35,9 e 37,7 ms (27,8 e 26,6 FPS), com 1% low de 19,2 e 18,0 FPS. O `game` mede média de 34,99 e 36,67 ms (acima do orçamento de 33,3 ms) e p99 de 48,3 e 49,1 ms (folga p99 de -15,0 e -15,7 ms), com `util.` de 97% e `limitante` em 73% e 72% dos quadros. O `front_wait` é de 0,92 e 0,95 ms. A captura é só uma parte do `game` (6,59 e 7,16 ms, 18% e 19% de `util.`, p99 de 14,5 e 14,3 ms) e o `game_other` (28,40 e 29,51 ms de média, 79% e 78%, p99 de 37,85 e 37,69 ms) é o maior bloco que existe. Os outros estágios estão abaixo do orçamento na média, mas com p99 acima dele: `worker` 29,32 e 31,55 ms (82% e 84%, limitante em 14% e 13%, folga p99 de -10,4 e -10,5 ms), `record` 25,27 e 27,72 ms (70% e 74%, limitante em 5% e 6%, folga p99 de -10,0 e -16,4 ms) e `gpu` 29,75 e 31,93 ms (83% e 85%, limitante em 8% e 9%, folga p99 de -6,1 e -3,9 ms; limite superior, ver ressalva 1). Nenhum dos quatro estágios principais do Vulkan tem folga de p99.

**Vulkan, interpretação.** O `game` é o estágio de ritmo: ele vai da saída de um `OnSwap` à entrada do seguinte, então `game` + `front_wait` fecha com o intervalo do quadro (34,99 + 0,92 ≈ 35,9 ms; 36,67 + 0,95 ≈ 37,7 ms) por construção (ressalva 2). Com o `front_wait` em ~1 ms, o `game` é o maior dos quatro estágios na maioria dos quadros. O `util.` de 97% é o número quase tautológico (`game` + `front_wait` ≈ intervalo), e o `limitante` de 72% a 73% fica inflado pela mesma construção; nenhum dos dois prova que a thread do jogo é o gargalo. O que os dados sustentam: o `front_wait` de ~1 ms mostra que a thread do jogo não é segurada pelo renderer esperando, e o `game_other` é o maior bloco. Sem a captura, a média do `game` (~28 a 30 ms) ficaria abaixo do orçamento de 33,3 ms, e só o p99 (37,85 e 37,69 ms no `game_other`) o estouraria. Não está decidido se o `game_other` é trabalho ou espera fora do renderer (ver "O que os dados não decidem").

**D3D12.** O quadro fecha no limitador (intervalo médio de 33,5 e 33,4 ms, 29,8 e 29,9 FPS), então o `game` (98% e 92% de `util.`, limitante em 74% e 42%) não identifica o gargalo (ressalva 2). O estágio de trabalho real mais ocupado é o `worker`: 29,17 e 32,37 ms de tempo ocupado, 87% e 97% de `util.`, `bloq.` de 0,00 ms, limitante em 26% e 57% dos quadros e p99 de 47,6 e 43,5 ms (folga p99 de -14,3 e -10,2 ms). A GPU tem folga: 21,47 e 25,02 ms (64% e 75%), limitante em 0% e 1% dos quadros e folga p99 de +7,1 e +3,4 ms. O 1% low ficou em 21,7 e 22,5 FPS e o mínimo em 11,9 e 10,5 FPS.

**GPU: no Vulkan ela mede 1,28 a 1,39x a do D3D12 (limite superior, por outro método); não está provada como gargalo.** A GPU do Vulkan (29,75 e 31,93 ms) é 1,39x e 1,28x a do D3D12 (21,47 e 25,02 ms) nas duas execuções, e só no Vulkan o p99 da GPU passa do orçamento (39,38 e 37,20 ms). Mas as duas APIs medem a GPU de formas diferentes: no Vulkan vai do `TOP_OF_PIPE` até o `BOTTOM_OF_PIPE` em uma submissão nova, com 2 quadros em voo, e pode incluir a espera atrás do quadro anterior; no D3D12 vai do primeiro ao último timestamp dentro da própria command list. A razão é, portanto, um limite superior obtido por método diferente, e a GPU do Vulkan é limitante em só 8% e 9% dos quadros. A cena varia entre execuções, então vale a razão, não uma comparação de cena a cena (que não foi feita). Recomendação para a Fase 1, antes do passo 2: registrar os dois ticks de início e fim da GPU do Vulkan, para subtrair a sobreposição com o quadro anterior (ocupada(N) = fim(N) − max(início(N), fim(N−1))).

**Ordem recomendada para a Fase 1 (passos da tabela da seção 5 do design).**

1. **Passo 1, hash paralelo exato: primeiro.** A captura é compartilhada pelas duas APIs. No Vulkan ela está dentro do `game`, o estágio de ritmo (`util.` quase tautológico; `limitante` inflado pela mesma construção, ressalva 2) e o único com média acima do orçamento. Mas o teto do ganho é pequeno: a captura mede 6,59 e 7,16 ms por quadro (menos que os 8 a 13 ms de hash citados no design, e o relatório não separa o hash dentro dela), e restariam `game_other` de 28,40 e 29,51 ms, com p99 de 37,85 e 37,69 ms. O passo 1 sozinho não leva o Vulkan à meta (o p99 do `game_other` continua acima de 33,3 ms), embora a média sem a captura ficasse abaixo do orçamento. No D3D12 o hash roda dentro do `worker`, que a tabela não decompõe, então o ganho ali precisa ser medido.
2. **Passo 3, pipeline entre estágios: segundo.** No D3D12 o `worker` (87% e 97% de `util.`) é o estágio de trabalho mais ocupado, e o design prevê separar preparo e gravação ali. No Vulkan o `game` sozinho passa de 33 ms na média (a regra do design manda o trabalho ir para dentro do estágio, não para o pipeline), mas esse valor inclui o ritmo do quadro (ressalva 2); `worker` e `record` têm p99 acima do orçamento e já são threads separadas, então aumentar a profundidade entre replay e gravação ataca a cauda deles. O `front_wait` médio é de 0,92 e 0,95 ms no Vulkan e o bench reporta cerca de 7 threads lógicas em uso (campo `cores`, 7 a 7,2 nas execuções do Vulkan) de 12 (o processador tem 8 núcleos, 4P+4E), o que deixa threads livres. No D3D12 a soma serial NÃO foi medida: o `game` do D3D12 inclui a espera do limitador de 30 FPS (ressalva 2), então não dá para somá-lo ao `worker`. O `worker` sozinho fica em 29,17 e 32,37 ms de média (de 29 a 32 ms; p99 de 47,6 e 43,5 ms). A metade "núcleos ociosos" da condição do design ("soma serial > 33 ms com núcleos ociosos") vale: o bench reporta 6,1 a 6,4 threads lógicas de 12 em uso nas execuções do D3D12. A separação preparo/gravação do passo 3 ali se apoia no `worker` de 29 a 32 ms, não numa soma serial medida.
3. **Passo 2, itens de GPU bit a bit: terceiro, e só no Vulkan.** No D3D12 a GPU não está entre os limitantes (0% e 1%, folga p99 positiva), então o passo não entra lá. No Vulkan a GPU tem p99 acima do orçamento, mas é limitante em só 8% a 9% dos quadros, o `util.` é limite superior, e esta fase não mediu por grupo de passes, então não dá para escolher os itens do passo 2 sem uma medição por passe.
4. **Passo 4, estabilidade do 1% low: necessário qualquer que seja a ordem.** O 1% low ficou entre 18,0 e 22,5 FPS e o mínimo entre 1,8 e 11,9 FPS nas quatro execuções; `game`, `worker`, `record` e `gpu` no Vulkan e `game` e `worker` no D3D12 têm p99 acima de 33,3 ms. Mesmo no D3D12, com a média em 29,3 a 30 FPS, os mínimos do bench ficaram entre 25,8 e 29,7 FPS.

**O que os dados não decidem.** Entre os passos 2 e 3 no Vulkan, `worker` (13% a 14% dos quadros), `gpu` (8% a 9%) e `record` (5% a 6%) estão próximos e todos têm p99 acima do orçamento; colocar o 3 antes do 2 vem da ressalva de limite superior da GPU e da falta de dados por passe, não de uma diferença clara de custo. Também não está medido quanto do `game_other` (28,40 e 29,51 ms) é lógica do jogo e quanto é espera da thread do jogo fora do renderer (por exemplo vblank ou outra sincronização; a Fase 0.5, no fim deste documento, mediu a parte que é código do renderer medido por hook na thread do jogo, cerca de um terço do `game_other` no Vulkan, e o restante, o `game_guest`, continua sem separação entre lógica, driver e espera); a execução de D3D12 limitada pela GPU mostra que o `game` pode ter boa parte de espera (23,47 ms ali, contra 32,76 ms quando o limitador de 30 FPS manda). Por isso o `game` do Vulkan, com `util.` e `limitante` inflados por construção, não prova que a thread do jogo é o gargalo; só a média do `game` sem a captura (~28 a 30 ms, abaixo de 33,3 ms) e o p99 (37,85 e 37,69 ms no `game_other`, acima) estão medidos.

## Decomposição do front-end da thread do jogo (Fase 0.5)

Plano: `superpowers/plans/2026-10-08-frame-timeline-frontend-breakdown.md`. A pergunta que a Fase 0 deixou aberta: de que é feito o `game_other` (≈29 a 30 ms no Vulkan nas execuções com detalhe: 29,78 e 29,38 ms; nas execuções da Fase 0 foram 28,40 e 29,51 ms)? Esta fase mede quanto dele é trabalho do próprio renderer dentro da thread do jogo e deixa o resto como `game_guest`. Não otimiza nada.

### Como ligar e o que cada estágio mede

`tools\bench\bench_api.ps1 -Api vulkan|d3d12 -Name <nome> -Timeline -TimelineDetail`. A opção define `SR_FRAME_TIMELINE_DETAIL=1`, que só tem efeito junto de `SR_FRAME_TIMELINE`; sem as duas variáveis nenhum relógio extra é lido. Os estágios abaixo são acumulados em nanossegundos por quadro, na thread do jogo, e gravados no `OnSwap`.

| Estágio | O que mede |
| --- | --- |
| `frontend` | tempo total dentro dos hooks `DrawVertices`, `DrawIndexedVertices`, `DrawInlineVertices`, `Resolve`, `BeginTiling`, `Clear`, `EndTiling` e `OnPassEnd` (inclui a espera do `front_mutex_`; no `DrawIndexedVertices` inclui também o spin de depuração `sr_native_debug_spin_us`, desligado por padrão e só de depuração, porque o escopo do hook começa antes dele) |
| `fe_begin` | `BeginCmd` |
| `fe_ring` | `CaptureRing` |
| `fe_device` | `CaptureDevice` |
| `fe_index` | bloco do buffer de índices no `DrawIndexedVertices` |
| `fe_streams` | `PlanStreams` |
| `fe_end` | `EndCmd` inteiro (aninha a captura PM4, as texturas e quase todo o `fe_flush`) |
| `fe_flush` | `FlushBatch` |
| `frontend_other` | derivado: `frontend` menos `fe_begin`, `fe_ring`, `fe_device`, `fe_index`, `fe_streams` e `fe_end` (resíduo, ver ressalvas) |
| `game_guest` | derivado: `game` menos `frontend`; não é "sem código do renderer" (contém também trabalho do renderer fora dos escopos cronometrados, ver ressalva 1) |

O relatório imprime `game_guest` e `frontend_other` quando o CSV tem os estágios de detalhe. Todos os valores abaixo são médias em ms por quadro (janela dos últimos 1100 quadros, como na Fase 0).

### Tabelas impressas pelo relatório

Execução `tl_fe1_vulkan` (bench: parado 25,5 FPS, andando 28,1 FPS):

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 37.5 ms = 26.7 FPS | 1% low 20.3 FPS | mínimo 1.9 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio        média     p50     p99   bloq.   util.  folga p99  limitante
game           36.97   36.60   48.19    0.00     99%     -14.85        78%
game_guest     18.81   18.64   24.07    0.00     50%       9.26          -
game_other     29.78   29.48   37.08    0.00     79%      -3.75          -
capture         7.20    6.79   14.13    0.00     19%      19.20          -
frontend       18.16   17.76   26.05    0.00     48%       7.29          -
fe_begin        0.41    0.41    0.59    0.00      1%      32.74          -
fe_ring         0.29    0.27    0.60    0.00      1%      32.73          -
fe_device       1.48    1.46    1.84    0.00      4%      31.49          -
fe_index        1.44    1.41    2.05    0.00      4%      31.29          -
fe_streams      3.68    3.64    4.99    0.00     10%      28.35          -
fe_end         10.17    9.78   17.32    0.00     27%      16.02          -
fe_flush        0.06    0.05    0.22    0.00      0%      33.12          -
frontend_other    0.69    0.67    0.96    0.00      2%      32.38          -
front_wait      0.47    0.00    0.00    0.00      1%      33.33          -
worker         31.68   31.55   42.27    1.79     85%      -8.94        10%
record         26.46   25.82   43.32    2.49     71%      -9.99         3%
gpu            32.26   31.71   38.81    0.00     86%      -5.47         9%
```

Execução `tl_fe2_vulkan` (bench: parado 26 FPS, andando 27,9 FPS):

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 36.9 ms = 27.1 FPS | 1% low 19.2 FPS | mínimo 1.9 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio        média     p50     p99   bloq.   util.  folga p99  limitante
game           36.19   35.74   49.11    0.00     98%     -15.78        84%
game_guest     19.14   18.79   27.20    0.00     52%       6.13          -
game_other     29.38   28.95   38.97    0.00     80%      -5.64          -
capture         6.82    6.48   14.73    0.00     18%      18.60          -
frontend       17.05   16.85   26.12    0.00     46%       7.21          -
fe_begin        0.39    0.38    0.62    0.00      1%      32.72          -
fe_ring         0.26    0.25    0.56    0.00      1%      32.77          -
fe_device       1.37    1.37    1.91    0.00      4%      31.42          -
fe_index        1.34    1.31    2.01    0.00      4%      31.32          -
fe_streams      3.45    3.47    5.05    0.00      9%      28.28          -
fe_end          9.59    9.30   17.90    0.00     26%      15.44          -
fe_flush        0.07    0.05    0.25    0.00      0%      33.09          -
frontend_other    0.64    0.63    0.97    0.00      2%      32.36          -
front_wait      0.65    0.00    3.75    0.00      2%      29.58          -
worker         29.89   29.87   43.18    1.52     81%      -9.84         8%
record         25.37   24.57   43.26    2.05     69%      -9.93         4%
gpu            29.89   30.62   38.73    0.00     81%      -5.39         4%
```

Execução `tl_fe1_d3d12` (bench: parado 30 FPS, andando 29,7 FPS):

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 33.5 ms = 29.8 FPS | 1% low 22.1 FPS | mínimo 12.9 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio        média     p50     p99   bloq.   util.  folga p99  limitante
game           32.52   32.55   42.73    0.00     97%      -9.40        57%
game_guest     24.98   25.06   34.20    0.00     75%      -0.87          -
frontend        7.53    7.52   10.85    0.00     22%      22.49          -
fe_begin        0.41    0.40    0.58    0.00      1%      32.75          -
fe_ring         0.29    0.26    0.58    0.00      1%      32.75          -
fe_device       0.58    0.58    0.73    0.00      2%      32.60          -
fe_index        1.72    1.64    3.07    0.00      5%      30.26          -
fe_streams      3.46    3.41    5.09    0.00     10%      28.24          -
fe_end          0.34    0.34    0.47    0.00      1%      32.86          -
fe_flush        0.03    0.02    0.14    0.00      0%      33.20          -
frontend_other    0.74    0.73    1.03    0.00      2%      32.31          -
front_wait      0.96    0.00   14.79    0.00      3%      18.54          -
worker         31.78   31.40   47.06    0.00     95%     -13.72        43%
gpu            23.23   23.76   26.36    0.00     69%       6.97         0%
```

Execução `tl_fe2_d3d12` (bench: parado 29,8 FPS, andando 29,7 FPS):

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 33.6 ms = 29.7 FPS | 1% low 20.4 FPS | mínimo 8.4 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio        média     p50     p99   bloq.   util.  folga p99  limitante
game           32.15   32.43   42.28    0.00     96%      -8.95        62%
game_guest     25.07   25.36   33.91    0.00     75%      -0.58          -
frontend        7.07    6.94   10.74    0.00     21%      22.59          -
fe_begin        0.38    0.37    0.63    0.00      1%      32.71          -
fe_ring         0.26    0.23    0.59    0.00      1%      32.74          -
fe_device       0.54    0.55    0.73    0.00      2%      32.60          -
fe_index        1.73    1.59    3.50    0.00      5%      29.84          -
fe_streams      3.14    3.07    4.97    0.00      9%      28.36          -
fe_end          0.32    0.32    0.47    0.00      1%      32.86          -
fe_flush        0.04    0.03    0.12    0.00      0%      33.21          -
frontend_other    0.69    0.68    1.09    0.00      2%      32.24          -
front_wait      1.43    0.00   18.06    0.00      4%      15.28          -
worker         30.11   29.79   48.07    0.00     90%     -14.74        38%
gpu            21.22   22.37   27.80    0.00     63%       5.53         0%
```

### Resumo (ms médios por quadro e fração do `game`)

Cada célula é `ms (fração do game)`. As colunas `capture`, `fe_device`, `fe_streams`, `fe_end`, `fe_begin`, `fe_ring` e `fe_index` são partes do `frontend` (e `capture` está dentro de `fe_end`), então as frações dessas colunas não somam com `game_guest`. `capture` só existe no Vulkan (n/d no D3D12).

| Execução | game | frontend | game_guest | capture | fe_device | fe_streams | fe_end | fe_begin | fe_ring | fe_index | frontend_other |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `tl_fe1_vulkan` | 36,97 | 18,16 (49,1%) | 18,81 (50,9%) | 7,20 (19,5%) | 1,48 (4,0%) | 3,68 (10,0%) | 10,17 (27,5%) | 0,41 (1,1%) | 0,29 (0,8%) | 1,44 (3,9%) | 0,69 (1,9%) |
| `tl_fe2_vulkan` | 36,19 | 17,05 (47,1%) | 19,14 (52,9%) | 6,82 (18,8%) | 1,37 (3,8%) | 3,45 (9,5%) | 9,59 (26,5%) | 0,39 (1,1%) | 0,26 (0,7%) | 1,34 (3,7%) | 0,64 (1,8%) |
| `tl_fe1_d3d12` | 32,52 | 7,53 (23,2%) | 24,98 (76,8%) | n/d | 0,58 (1,8%) | 3,46 (10,6%) | 0,34 (1,0%) | 0,41 (1,3%) | 0,29 (0,9%) | 1,72 (5,3%) | 0,74 (2,3%) |
| `tl_fe2_d3d12` | 32,15 | 7,07 (22,0%) | 25,07 (78,0%) | n/d | 0,54 (1,7%) | 3,14 (9,8%) | 0,32 (1,0%) | 0,38 (1,2%) | 0,26 (0,8%) | 1,73 (5,4%) | 0,69 (2,1%) |

Conferências a partir das tabelas: `game_guest` + `frontend` = `game`; `frontend` = soma de `fe_begin`, `fe_ring`, `fe_device`, `fe_index`, `fe_streams`, `fe_end` e `frontend_other`, onde o `frontend_other` é o resíduo (por construção), positivo nas quatro execuções (0,64 a 0,74 ms). A fração do `fe_end` no `frontend` do Vulkan é 56,0% e 56,2% (10,17 ÷ 18,16 e 9,59 ÷ 17,05). `fe_streams` + `fe_device` + `fe_index` somam 6,60 e 6,16 ms no Vulkan (36,3% e 36,1% do `frontend`) e 5,76 e 5,41 ms no D3D12 (76,5% nas duas execuções). O `fe_flush` fica em 0,03 a 0,07 ms e está quase todo dentro do `fe_end` (o `FlushBatch` do `OnSwap` fica fora; ressalva 3).

### Quanto do `game_other` do Vulkan é trabalho do renderer

O `game_other` da Fase 0 é `game` menos `capture`. Como `capture` está dentro do `frontend`, o `frontend` menos o `capture` é o trabalho do renderer na thread do jogo que o `game_other` já continha: 10,96 e 10,23 ms nas duas execuções, que são 36,8% e 34,8% do `game_other` (29,78 e 29,38 ms). (O aninhamento do `capture` no `frontend` ignora a captura do próprio comando de swap, que roda depois do carimbo do `OnSwap`, e a do `SyncRing`; por isso estes números são um limite inferior ligeiramente baixo, ver a ressalva 3.) O restante, o `game_guest` (18,81 e 19,14 ms), é 63,2% e 65,1% do `game_other`. Ou seja, cerca de um terço do `game_other` é código do renderer medido por hook, e cerca de dois terços não são código medido por hook (parte desses dois terços ainda pode ser código do renderer sem cronômetro, ver ressalva 1). Isso responde só a metade da pergunta da Fase 0: o que o `game_guest` contém continua sem medida (ver "O que os dados ainda não decidem" abaixo). A divisão entre `capture` e o resto do `frontend` depende da ressalva 6; o `game_guest` (63% a 65%) não depende dela.

### Custo do modo detalhado

Comparação do `game` médio das execuções com detalhe (`tl_fe1/2`) com o das execuções da Fase 0 (`tl_on1/2`, mesma API). As execuções da Fase 0 vieram de uma build anterior aos escopos de detalhe, então a diferença abaixo inclui tudo o que mudou entre as builds, não só o modo detalhado ligado. A ordem das execuções não foi alternada e a cena varia entre execuções, então a diferença também não separa o custo da sonda da variação de cena:

| API | `game` Fase 0 (on1, on2 / média) | `game` detalhe (fe1, fe2 / média) | Diferença da média (por par) | Intervalo médio Fase 0 → detalhe | FPS do bench, médias das 2 execuções (Fase 0 → detalhe) |
| --- | --- | --- | --- | --- | --- |
| Vulkan | 34,99 e 36,67 / 35,83 | 36,97 e 36,19 / 36,58 | +0,75 ms (+1,98 e -0,48) | 36,8 → 37,2 ms | parado 26,35 → 25,75; andando 28,35 → 28,0 |
| D3D12 | 32,76 e 30,86 / 31,81 | 32,52 e 32,15 / 32,34 | +0,53 ms (-0,24 e +1,29) | 33,45 → 33,55 ms | parado 29,75 → 29,9; andando 29,9 → 29,7 |

A diferença média do `game` é de +0,75 ms (Vulkan) e +0,53 ms (D3D12), abaixo do limite de ~1,5 ms por quadro que o plano usa para uma ressalva forte. Mas a precisão é baixa: entre as duas execuções da Fase 0 de uma mesma API o `game` já variava 1,68 ms (Vulkan) e 1,90 ms (D3D12), mais que a diferença medida, e um dos pares do Vulkan (fe1 contra on1) passa de 1,5 ms (+1,98). No D3D12 o `game` inclui a espera do limitador de 30 FPS (ressalva 2 da Fase 0), que absorve parte de qualquer custo extra, então a diferença do `game` ali pode subestimar o custo da sonda; o FPS não mudou de forma distinguível (diferenças de -0,2 a +0,15 FPS). No Vulkan o FPS médio ficou 0,35 a 0,6 FPS menor com o detalhe ligado, na direção de um custo, mas dentro da variação entre execuções. A diferença observada é +0,75 ms (Vulkan) e +0,53 ms (D3D12) na média do `game`, não separáveis da variação de cena (um par Vulkan deu −0,48 ms). O custo da sonda não foi medido diretamente: os números finos incluem a própria sonda (por exemplo, o `frontend_other` de 0,64 a 0,74 ms é o resíduo do `frontend`, e contém o custo dos relógios do escopo do hook além de qualquer código dos hooks fora das partes cronometradas).

### Ressalvas

1. **`game_guest` é "game menos hooks cronometrados", não "lógica do jogo" nem "sem código do renderer".** Ele mistura quatro coisas: a lógica do próprio jogo, o código do driver XDK recompilado que monta o PM4 antes de o nosso hook rodar, as esperas do kernel (vblank, semáforos etc.) e o trabalho do renderer que roda na thread do jogo fora dos escopos cronometrados. Esta quarta categoria (efeitos laterais do renderer, sem cronômetro) inclui, conforme o código: `SyncRing`/`ResyncRing`, chamados nos hooks `RingMakeSpace` e `RingAllocLarge` (`native_hooks.cpp`; ambos tomam o `front_mutex_`, e o `SyncRing` ainda faz `BeginCmd`/`CaptureRing`/`EndCmd`, ver ressalva 3); `InvalidateGuestRange`, chamado a cada unlock de buffer de vértices ou de índices, que toma o `front_mutex_` e percorre as páginas do intervalo; as falhas de página do write-watch de texturas e os callbacks `OnPhysicalWrite` (o write-watch é controlado por `sr_native_texture_watch`, com padrão `true`, e `tools\bench\bench_api.ps1` não o altera; o log da `tl_fe1_d3d12` tem "write watch enabled"); e o bookkeeping de `OnDeviceCall`/`NoteHookCall` que cada hook faz antes de chegar a um escopo. (`NoteRingConstants` e `ApplyLoadAluConstants` retornam logo no início com o padrão `sr_native_pm4_mirror=true`, então não contam.) Nenhum desses efeitos tem escopo próprio nem entra no `frontend` (o `SyncRing` aparece em `fe_*` por causa do `BeginCmd`/`EndCmd`, mas fora do `frontend`), e o tamanho da soma não é conhecido. Este detalhamento não separa essas quatro coisas. No D3D12 ele também inclui a espera do limitador de 30 FPS (ressalva 2 da Fase 0), então os 24,98 e 25,07 ms do D3D12 não são todos trabalho; no Vulkan o intervalo (36,9 a 37,5 ms) é maior que 33,3 ms, mas isso por si só não prova que o `game_guest` do Vulkan não contém esperas.
2. **As partes aninham e não se somam.** `capture` está dentro de `fe_end`, e `fe_flush` está quase todo dentro de `fe_end` (o `FlushBatch` do `OnSwap` fica fora; ver ressalva 3); nenhum dos dois acrescenta algo a ele. Somar as colunas do resumo duplicaria tempo. Só `fe_begin`, `fe_ring`, `fe_device`, `fe_index`, `fe_streams`, `fe_end` e `frontend_other` fecham com o `frontend`.
3. **"Partes ≤ `frontend`" não vale quadro a quadro.** `fe_begin`, `fe_ring` e `fe_end` também incluem o `BeginCmd`/`CaptureRing`/`EndCmd` do próprio comando de swap no `OnSwap`, que não está dentro do `frontend` (a ordem é de alguns µs por quadro). O mesmo vale para o `BeginCmd`/`CaptureRing`/`EndCmd` do `SyncRing` (hooks `RingMakeSpace` e `RingAllocLarge`), inclusive a captura PM4 dele, que alimenta o `capture`: esse tempo cai em `fe_*` e em `capture` mas não em `frontend` (e portanto fica no `game_guest`). Como o `capture` o inclui e o `frontend` não, a diferença `frontend` menos `capture` (10,96 e 10,23 ms) é um limite inferior (ligeiramente baixo) do código do renderer medido por hook; quantas trocas de segmento do anel ocorrem por quadro não foi medido, então o tamanho da subestimação também não. O `FlushBatch` chamado no `OnSwap` cai no `fe_flush` do quadro seguinte (um flush entre os vários por quadro, desprezível). Nas médias o resíduo `frontend_other` ficou positivo nas quatro execuções. Calculado direto dos CSVs, sem o piso em zero que o relatório aplica, ele não é negativo em nenhum dos 1100 quadros de cada uma das quatro execuções (mínimo de 0,36 a 0,49 ms por execução); nos arquivos inteiros, fora dessa janela, aparecem 5 quadros levemente negativos (o menor com −0,057 ms), então num quadro isolado ele pode sair um pouco abaixo de zero.
4. **Modo direto contra modo worker.** Se o renderer rodasse em modo direto (sem worker), o `fe_end` incluiria também a renderização (`Execute`). O modo é decidido em `BeginCmd` (`native_renderer.cpp`): `worker_mode_ = bool(packet_sink_) || (sr_native_worker && sr_native_pm4_mirror)`. **Vulkan:** o `packet_sink_` está definido, então o worker vale sempre e o `fe_end` do Vulkan não inclui `Execute` (as linhas `worker` e `record` das tabelas existem pelo mesmo motivo); a base é o código, não um log. **D3D12:** o modo depende de `sr_native_worker` e `sr_native_pm4_mirror`, ambas com padrão `true` (`REXCVAR_DEFINE_BOOL` em `native_renderer.cpp`), e `tools\bench\bench_api.ps1` não passa nenhuma das duas, então valem os padrões (salvo alguma configuração do usuário que as altere, o que não foi conferido). O que o log verificou: o `game.log` da execução `tl_fe1_d3d12` tem a linha "native: recording worker enabled" (junto de "write watch enabled (api=d3d12)"), e o `fe_end` do D3D12 é de só 0,34 e 0,32 ms nas duas execuções, compatível com isso. O log só foi conferido para essa execução (o `game.log` é sobrescrito a cada execução); para `tl_fe2_d3d12` o modo worker segue dos padrões, não de um log.
5. **Cronômetros por thread.** Os contadores são `thread_local` na thread do jogo que emite os draws. Draws emitidos por outras threads do guest não seriam contados em lugar nenhum (os acumuladores `thread_local` dessas threads nunca são gravados nem zerados); só a contenção delas no `front_mutex_` apareceria dentro do `frontend` desta thread. O `frontend` ser consistentemente ≈48% do `game` (49,1% e 47,1% no Vulkan) indica que a maior parte do caminho de draws cronometrado de draws roda na mesma thread do `OnSwap`. Não foi verificado se existe emissão de draws fora dela.
6. **O `capture` (Fase 0) trunca para µs por chamada; os `fe_*` não.** O `capture` soma `pm4_us` + `textures_us`, acumulados em microssegundos inteiros por chamada, enquanto os `fe_*` são em nanossegundos. No Vulkan o `fe_end` menos o `capture` dá 2,97 e 2,77 ms (no D3D12 o `fe_end` inteiro é 0,34 e 0,32 ms). Esses ~2,8 a 3,0 ms do Vulkan não foram atribuídos: parte pode ser o `capture` subestimado pela truncação (até ≈1 µs por chamada cronometrada, vezes o número de comandos por quadro, que não foi medido aqui; chegar a ≈2,8 a 3,0 ms exigiria da ordem de 3 000 a 6 000 chamadas cronometradas por quadro, com 0,5 a 1 µs de truncação cada), parte trabalho do `EndCmd` fora desses dois cronômetros e parte as próprias leituras de relógio da sonda aninhadas dentro do `fe_end`; os dados não decidem qual. Isso muda a divisão entre `capture` e "resto do `fe_end`", não o `frontend` total.
7. **Uma máquina, cena variável, duas execuções por API.** Notebook Intel UHD, i5-13420H; a cena muda entre execuções (as duas execuções de uma API diferem em `game` por 0,78 ms no Vulkan e 0,37 ms no D3D12). Vale comparar razões e frações, não os valores absolutos.
8. **Custo do modo detalhado**: ver a seção acima (+0,75 ms no Vulkan e +0,53 ms no D3D12 na média do `game`, não separáveis da variação de cena; um par Vulkan deu −0,48 ms).

### Conclusão da Fase 0.5

**Vulkan.** O `frontend` (hooks do renderer na thread do jogo) é 18,16 e 17,05 ms por quadro, 49,1% e 47,1% do `game`; o `game_guest` é 18,81 e 19,14 ms, 50,9% e 52,9%. A parte dominante do `frontend` é o `fe_end` (10,17 e 9,59 ms, 56% do `frontend` e 27,5% e 26,5% do `game`), dentro do qual a captura (`capture`) mede 7,20 e 6,82 ms (19,5% e 18,8% do `game`). Depois vêm `fe_streams` (3,68 e 3,45 ms, 10,0% e 9,5% do `game`), `fe_device` (1,48 e 1,37 ms) e `fe_index` (1,44 e 1,34 ms); `fe_begin` + `fe_ring` somam 0,70 e 0,65 ms. Nenhum bloco do renderer sozinho se aproxima do `game_guest` (o maior, `fe_end`, é cerca de metade dele), mas o `frontend` inteiro tem tamanho parecido com o `game_guest`.

**D3D12.** O `frontend` é 7,53 e 7,07 ms, 23,2% e 22,0% do `game`. O `fe_end` é desprezível (0,34 e 0,32 ms, compatível com a renderização no worker) e a parte dominante é o `fe_streams` (3,46 e 3,14 ms, 45,9% e 44,4% do `frontend`, 10,6% e 9,8% do `game`), seguido do `fe_index` (1,72 e 1,73 ms) e do `fe_device` (0,58 e 0,54 ms); `fe_streams` + `fe_device` + `fe_index` são 76,5% do `frontend`. O `game_guest` (24,98 e 25,07 ms, 76,8% e 78,0% do `game`) é o maior bloco, mais de três vezes o `frontend`, mas inclui a espera do limitador de 30 FPS e não deve ser lido como trabalho.

**Primeiro alvo da Fase 1.**

- **No D3D12 o maior bloco do `game` não é o front-end do renderer; no Vulkan os dois têm tamanho parecido.** No Vulkan o `game_guest` (18,81 e 19,14 ms) e o `frontend` (18,16 e 17,05 ms) diferem por só 0,65 ms (fe1) e 2,1 ms (fe2), e um lado é um bloco medido por hook enquanto o outro é um resíduo misto: tamanho parecido; o resíduo mistura lógica, driver XDK e esperas. No D3D12 o `game_guest` (24,98 e 25,07 ms) é mais de três vezes o `frontend`, mas inclui a espera do limitador de 30 FPS e não deve ser lido como trabalho. As mudanças previstas na Fase 1 (hash paralelo, pipeline entre estágios, itens de GPU) não visam o `game_guest` como um todo (lógica do jogo, driver XDK e esperas ficam fora delas), mas ele também contém trabalho do renderer sem cronômetro (ressalva 1), que essas mudanças podem tocar; o tamanho dessa parte não está medido.
- **No Vulkan, o passo 1 (hash paralelo exato da captura) continua sendo o primeiro da ordem da Fase 0, mas esta medição não o apoia mais que o candidato seguinte.** A captura (`capture`) é o maior bloco isolado do renderer na thread do jogo (6,8 a 7,2 ms, ≈19% do `game`) e está dentro do `fe_end`. O grupo de estado por draw (`fe_streams` + `fe_device` + `fe_index`) soma 6,16 a 6,60 ms no Vulkan, ≈90% do `capture` em tamanho, e está atribuído por hook por inteiro. O hash, ao contrário, é só uma parte NÃO medida do `capture`, e um hash paralelo remove apenas uma fração dessa parte. Só pelo tamanho, os dois candidatos são indistinguíveis nestes dados. A escolha do passo 1 como primeiro vem do raciocínio de projeto da Fase 0 (a restrição de exatidão, compartilhada com o D3D12), não desta medição. A medida que falta é a parcela do hash dentro do `capture`. O teto é um limite superior que a paralelização não alcança: tirar a captura inteira deixaria o `game` em ≈29 a 30 ms (o `game_other`), e tirar o `frontend` inteiro o deixaria em ≈19 ms (o `game_guest`; limite teórico, não meta). Como `worker` (31,68 e 29,89 ms) e `gpu` (32,26 e 29,89 ms, limite superior) já estão perto do orçamento de 33,3 ms nas execuções com detalhe, o ganho no intervalo do quadro não deve passar do que esses estágios permitem; os dados não dizem quanto de uma redução do `game` viraria FPS.
- **Segundo candidato, compartilhado pelas duas APIs: a captura de estado por draw (`fe_streams`, `fe_device`, `fe_index`).** Ele não corresponde a nenhum dos quatro passos da ordem recomendada da Fase 0; seria um passo novo, a ser avaliado. A soma das três partes é 6,60 e 6,16 ms no Vulkan (≈36% do `frontend`) e 5,76 e 5,41 ms no D3D12 (76,5% do `frontend`). No D3D12 é a única parte do front-end com tamanho relevante, mas o D3D12 está preso ao limitador de 30 FPS e o estágio de trabalho mais ocupado é o `worker` (31,78 e 30,11 ms, 95% e 90% de `util.`), então reduzir só o `frontend` do D3D12 não é suportado pelos dados como ganho de FPS.
- **O passo 3 (pipeline) para o D3D12 não muda**: o `worker` continua sendo o estágio de trabalho mais ocupado (esta fase só mediu a thread do jogo).

**O que os dados ainda não decidem.**

- O que o `game_guest` contém: lógica do jogo, código do driver XDK recompilado, esperas do kernel ou trabalho do renderer sem cronômetro (os efeitos laterais `SyncRing`/`ResyncRing`, `InvalidateGuestRange`, falhas de página do write-watch com `OnPhysicalWrite` e o bookkeeping de `OnDeviceCall`/`NoteHookCall`; ressalva 1). Este detalhamento só diz que ele é 51% a 53% do `game` no Vulkan e 77% a 78% no D3D12 (neste último com a espera do limitador). O próximo passo para separá-lo seria um profiler de amostragem na thread do jogo (por exemplo, ETW/WPA ou outro profiler de CPU, atribuindo o tempo às funções recompiladas do jogo, às do driver e às esperas) ou cronometrar as chamadas D3D e as esperas do kernel no nível das camadas de compatibilidade do guest; a parte do renderer poderia ser medida cronometrando os efeitos laterais acima. Nenhum dos três foi feito.
- Para onde vão os ~2,8 a 3,0 ms do `fe_end` do Vulkan que não são `capture` (ressalva 6): truncação do `capture`, trabalho não cronometrado ou as leituras de relógio da própria sonda.
- Quanto de uma redução do `frontend` vira FPS: depende de `worker` e `gpu`, que não foram medidos aqui em função da redução.
- Se há draws emitidos por outras threads do guest (ressalva 5) e se alguma configuração do usuário alterou os padrões de `sr_native_worker`/`sr_native_pm4_mirror` na `tl_fe2_d3d12`, cujo `game.log` não foi conferido (ressalva 4).

## Atualização: o hash quase não pesa na captura do Vulkan atual

Amostras de um quadro a cada 120 (`frame >= 600`) em cinco logs do Vulkan com `SR_VULKAN_PROFILE` das builds de 08/10 (`bench_release_8b1f78c_vulkan`, `bench_watch_scan_{on1,on2,off1,off2}_vulkan`; 74 amostras, ms inteiros truncados por quadro, então as médias estão subestimadas em até ~0,5 ms):

| Campo | Média (ms) | Máximo (ms) |
| --- | ---: | ---: |
| `pm4_ms` | 1,78 | 5 |
| `textures_ms` | 3,89 | 16 |
| `hash_ms` | 0,28 | 5 |
| `watch_ms`, `read_ms`, `copy_ms` | ~0 | 0 a 1 |

O `hash_ms` é ~7% do `textures_ms`. O resto (~3,6 ms) é trabalho por draw e por slot de textura que não é hash, leitura nem cópia. A causa do hash pequeno é a política padrão: com `sr_native_texture_watch=true`, texturas sem mudança são revalidadas com backoff de 2 a 16 quadros. Isso corrige a leitura anterior deste documento de que o hash paralelo exato seria o primeiro alvo da Fase 1: no Vulkan atual o teto desse ganho é ~0,3 a 1 ms. Em 2026-10-08 o usuário aceitou manter essa política (watch com backoff) para o Vulkan; o risco aceito é o atraso de até 16 quadros em texturas escritas por alias virtual (vídeo, por exemplo). O hash do D3D12 (7,5 a 8 ms no `worker`, medido antes) não foi reavaliado.

Ressalvas: são amostras de um quadro por 120 quadros, de builds anteriores à instrumentação da Fase 0 (a lógica de captura é a mesma), e não substituem uma medição com os contadores finos de hash por quadro.

## Decomposição fina do front-end (Fase 1.1a)

Plano: `superpowers/plans/2026-10-08-frontend-diet-measurement.md`. A Fase 0.5 deixou três blocos grandes sem decomposição no Vulkan: `fe_streams` (3,45 a 3,68 ms), o resto do `fe_end` além da captura (2,77 a 2,97 ms) e `fe_device` (1,37 a 1,48 ms). Esta fase acrescentou quatro estágios, todos na thread do jogo e só com `SR_FRAME_TIMELINE_DETAIL=1` (os quatro aninham nos estágios existentes `fe_streams`, `fe_device` e `fe_end`; `fs_prep` e `fs_plan` ficam os dois dentro de `fe_streams`): `fd_shaders` (as buscas de shader dentro do `CaptureDevice`), `fs_prep` (o preparo do `PlanStreams`: `DynamicVertexFetch` e a leitura da declaração de vértices), `fs_plan` (o ramo lento do `PlanStreams`, ver abaixo) e `fe_push` (só o `push_back` do `WorkCmd` em `EndCmd`). Não otimiza nada. Máquina e condições são as da Fase 0 (Intel UHD, i5-13420H, 1280x720, vsync, limite de 30 FPS), 2026-10-08, build release do front-end instrumentado (commit `efccf4f`). Procedência da build: o executável foi compilado a partir da árvore do commit `efccf4f` (compilado cerca de 2 minutos antes de o commit ser criado); a árvore foi assumida idêntica à do commit, sem conferência com um teste de árvore limpa.

### Medições

Quatro benches, todos Vulkan, um por vez, sem outra carga: `tl_fd1` e `tl_fd2` com `-Timeline -TimelineDetail`; `tl_fd3` e `tl_fd4` com `-Timeline -TimelineDetail -Profile` (o perfil acrescenta a linha `Vulkan profile`, que tem `draws=` por quadro). Nenhuma falha de infraestrutura. FPS do bench (média; mínimo entre parênteses):

| Execução | Modo | parado | andando |
| --- | --- | --- | --- |
| `tl_fd1_vulkan` | detalhe | 26,7 (21,5) | 26,6 (25,7) |
| `tl_fd2_vulkan` | detalhe | 27,1 (23,5) | 25,6 (23) |
| `tl_fd3_vulkan` | detalhe + perfil | 23,2 (18,8) | 24,4 (23) |
| `tl_fd4_vulkan` | detalhe + perfil | 25,6 (22,3) | 25,6 (24,5) |

As quatro tabelas do relatório (janela dos últimos 1100 quadros, como nas fases anteriores):

`tl_fd1_vulkan`:

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 37.5 ms = 26.7 FPS | 1% low 19.7 FPS | mínimo 1.9 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio           média     p50     p99   bloq.   util.  folga p99  limitante
game              36.92   36.98   50.15    0.00     99%     -16.82        73%
game_guest        19.34   19.35   26.75    0.00     52%       6.58          -
game_other        30.36   30.51   38.05    0.00     81%      -4.71          -
capture            6.56    6.20   14.36    0.00     18%      18.97          -
frontend          17.58   17.71   27.54    0.00     47%       5.79          -
fe_begin           0.40    0.41    0.62    0.00      1%      32.72          -
fe_ring            0.28    0.25    0.63    0.00      1%      32.70          -
fe_device          1.60    1.66    2.10    0.00      4%      31.23          -
fe_index           1.27    1.26    1.87    0.00      3%      31.46          -
fe_streams         3.73    3.84    5.51    0.00     10%      27.82          -
fe_end             9.65    9.36   17.79    0.00     26%      15.54          -
fe_flush           0.05    0.04    0.20    0.00      0%      33.14          -
fd_shaders         0.46    0.46    0.65    0.00      1%      32.68          -
fs_prep            0.37    0.37    0.51    0.00      1%      32.82          -
fs_plan            3.03    3.11    4.66    0.00      8%      28.68          -
fe_push            0.27    0.28    0.38    0.00      1%      32.95          -
frontend_other     0.64    0.65    0.99    0.00      2%      32.35          -
front_wait         0.50    0.00    0.01    0.00      1%      33.33          -
worker            30.88   31.61   43.30    2.09     82%      -9.96        12%
record            25.51   24.57   41.94    3.04     68%      -8.61         3%
gpu               31.79   33.71   39.01    0.00     85%      -5.68        12%
```

`tl_fd2_vulkan`:

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 37.6 ms = 26.6 FPS | 1% low 19.9 FPS | mínimo 2.0 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio           média     p50     p99   bloq.   util.  folga p99  limitante
game              37.10   36.96   49.41    0.00     99%     -16.08        77%
game_guest        19.30   19.22   25.81    0.00     51%       7.52          -
game_other        30.38   30.34   38.53    0.00     81%      -5.20          -
capture            6.72    6.37   13.77    0.00     18%      19.57          -
frontend          17.80   17.85   26.79    0.00     47%       6.55          -
fe_begin           0.40    0.40    0.61    0.00      1%      32.73          -
fe_ring            0.27    0.25    0.62    0.00      1%      32.72          -
fe_device          1.59    1.65    2.13    0.00      4%      31.20          -
fe_index           1.28    1.27    1.87    0.00      3%      31.47          -
fe_streams         3.85    3.97    5.33    0.00     10%      28.00          -
fe_end             9.76    9.52   17.14    0.00     26%      16.20          -
fe_flush           0.06    0.05    0.21    0.00      0%      33.12          -
fd_shaders         0.45    0.45    0.65    0.00      1%      32.69          -
fs_prep            0.38    0.38    0.54    0.00      1%      32.80          -
fs_plan            3.13    3.21    4.50    0.00      8%      28.83          -
fe_push            0.28    0.28    0.41    0.00      1%      32.93          -
frontend_other     0.65    0.65    0.97    0.00      2%      32.36          -
front_wait         0.45    0.00    0.00    0.00      1%      33.33          -
worker            31.02   31.54   43.92    1.56     82%     -10.59        11%
record            25.93   25.22   40.55    2.30     69%      -7.22         3%
gpu               31.14   33.32   38.06    0.00     83%      -4.73         8%
```

`tl_fd3_vulkan` (com `-Profile`):

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 41.6 ms = 24.0 FPS | 1% low 17.3 FPS | mínimo 1.8 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio           média     p50     p99   bloq.   util.  folga p99  limitante
game              40.89   40.88   55.32    0.00     98%     -21.99        75%
game_guest        20.52   20.46   26.84    0.00     49%       6.49          -
game_other        32.92   32.83   42.50    0.00     79%      -9.17          -
capture            7.98    7.63   14.63    0.00     19%      18.70          -
frontend          20.37   20.23   29.66    0.00     49%       3.68          -
fe_begin           0.53    0.53    0.77    0.00      1%      32.56          -
fe_ring            0.32    0.29    0.62    0.00      1%      32.71          -
fe_device          1.81    1.81    2.32    0.00      4%      31.02          -
fe_index           1.61    1.57    2.45    0.00      4%      30.89          -
fe_streams         4.03    3.98    5.66    0.00     10%      27.67          -
fe_end            11.31   11.01   18.20    0.00     27%      15.14          -
fe_flush           0.06    0.05    0.20    0.00      0%      33.13          -
fd_shaders         0.53    0.53    0.74    0.00      1%      32.59          -
fs_prep            0.42    0.42    0.59    0.00      1%      32.74          -
fs_plan            3.23    3.17    4.65    0.00      8%      28.68          -
fe_push            0.31    0.31    0.44    0.00      1%      32.89          -
frontend_other     0.76    0.75    1.12    0.00      2%      32.21          -
front_wait         0.62    0.00    4.65    0.00      1%      28.68          -
worker            35.45   35.49   49.04    2.26     85%     -15.70        16%
record            30.54   30.12   46.81    2.46     73%     -13.48         5%
gpu               32.55   32.46   37.64    0.00     78%      -4.31         4%
```

`tl_fd4_vulkan` (com `-Profile`):

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 39.3 ms = 25.4 FPS | 1% low 18.1 FPS | mínimo 2.0 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio           média     p50     p99   bloq.   util.  folga p99  limitante
game              38.67   38.58   52.04    0.00     98%     -18.71        78%
game_guest        19.78   19.62   25.94    0.00     50%       7.39          -
game_other        31.25   31.12   41.63    0.00     79%      -8.30          -
capture            7.41    7.12   13.99    0.00     19%      19.35          -
frontend          18.88   18.90   28.59    0.00     48%       4.74          -
fe_begin           0.50    0.50    0.79    0.00      1%      32.54          -
fe_ring            0.29    0.27    0.63    0.00      1%      32.71          -
fe_device          1.64    1.67    2.26    0.00      4%      31.07          -
fe_index           1.42    1.41    2.18    0.00      4%      31.15          -
fe_streams         3.82    3.82    5.80    0.00     10%      27.53          -
fe_end            10.51   10.30   17.48    0.00     27%      15.85          -
fe_flush           0.06    0.04    0.21    0.00      0%      33.12          -
fd_shaders         0.48    0.48    0.71    0.00      1%      32.62          -
fs_prep            0.40    0.39    0.57    0.00      1%      32.76          -
fs_plan            3.07    3.07    4.76    0.00      8%      28.57          -
fe_push            0.29    0.29    0.42    0.00      1%      32.91          -
frontend_other     0.69    0.68    1.06    0.00      2%      32.27          -
front_wait         0.60    0.00    8.92    0.00      2%      24.41          -
worker            33.01   33.25   48.07    1.91     84%     -14.74        16%
record            28.67   27.97   48.86    1.95     73%     -15.52         5%
gpu               30.76   31.92   38.74    0.00     78%      -5.41         2%
```

### Resíduos explícitos (o que ainda não tem escopo próprio)

Todos são diferenças de médias de uma mesma tabela (ms por quadro, `tl_fd1`, `tl_fd2` / `tl_fd3`, `tl_fd4`):

| Resíduo | Definição | fd1 | fd2 | fd3 | fd4 |
| --- | --- | --- | --- | --- | --- |
| `fs_other` | `fe_streams` − `fs_prep` − `fs_plan` | 0,33 | 0,34 | 0,38 | 0,35 |
| `fd_other` | `fe_device` − `fd_shaders` | 1,14 | 1,14 | 1,28 | 1,16 |
| `fe_end` fora da captura | `fe_end` − `capture` | 3,09 | 3,04 | 3,33 | 3,10 |
| resto do `EndCmd` | `fe_end` − `capture` − `fe_push` | 2,82 | 2,76 | 3,02 | 2,81 |
| `fs_plan` / `fe_streams` | fração | 81,2% | 81,3% | 80,1% | 80,4% |
| `fd_shaders` / `fe_device` | fração | 28,8% | 28,3% | 29,3% | 29,3% |

- **`fs_other`** (≈0,33 a 0,38 ms) é o que `fe_streams` tem fora de `fs_prep` e de `fs_plan`: por stream, as leituras de registradores do dispositivo (`Load32`/`Load8`), a consulta ao objeto do buffer de vértices, o teste do cache `FrontStreamCache` incluindo a **primeira** chamada de `RefreshTrackedBuffer` (ela está na condição do `if`, fora do escopo de `fs_plan`, mesmo quando devolve "sujo" e o ramo lento é tomado em seguida), o preenchimento do `StreamPlan` no ramo limpo, o `push_back` em `batch_->streams` e os testes dos cvars de depuração. O ramo "limpo e em cache" mais as leituras por stream estão aqui; os dados não separam uns dos outros. Essa primeira chamada de `RefreshTrackedBuffer` também confere o conteúdo de buffers pequenos: para `t.size` até `kHashedBufferMax` (32 KiB), o `BufferContent::Refresh` calcula o hash de conteúdo (`TextureHash`) na primeira chamada de cada quadro para aquele buffer (e de novo se uma escrita de página foi detectada); as chamadas seguintes no mesmo quadro retornam antes do hash. Quando o buffer está limpo, esse hash cai em `fs_other`; quando está sujo, a chamada da condição também o faz em `fs_other` e a do `PlanBuffer` já o encontra feito. Quando o cache erra nos campos (`sc.tracked` nulo ou endereço, tamanho, declaração, stride ou fase diferentes), a condição nem chama `RefreshTrackedBuffer` e o hash cai dentro do `PlanBuffer`, em `fs_plan`. Frequência (quantos buffers por quadro) e custo não foram medidos (ver "O que os dados não decidem" ao fim da seção).
- **`fd_other`** (≈1,14 a 1,28 ms) é o que `fe_device` tem fora de `fd_shaders`: no código do `CaptureDevice` com `packet_check` ligado (sempre, no Vulkan), são 15 a 20 chamadas de `CaptureBytes` por draw (9 do bloco do `packet_check`: constantes de fetch, `vs_bools` até o registrador de sombra e 7 faixas de `kRegisterShadow`; mais 5 fixas; mais até 5 de render targets/depth-stencil; mais a declaração; a chamada de `kDevicePtrAddr` é pulada porque a constante é 0) e os laços dos render targets e da declaração. Cada `CaptureBytes` faz `bytes.resize` (que zera a memória nova), `memcpy` e `ranges.push_back`. Quantos bytes por draw isso copia não foi medido aqui.
- **`fe_end` − `capture`** (3,04 a 3,33 ms; 2,76 a 3,02 ms depois de tirar `fe_push`) continua **em grande parte sem explicação**: `fe_push` (0,27 a 0,31 ms) responde por só ≈9% desse resto. Candidatos a olhar no código, nenhum medido: `checked_guest_reads.Reset()`; o tratamento do `capture_mirror_` (no Vulkan o `packet_check` fica ligado, então o segundo `if` do `EndCmd`, o `ScanCopyUsing`, é pulado; o bloco do `kResolve` só roda nos resolves); a marcação `resolve_copy_draw`; a decisão e a chamada do `FlushBatch` (o `fe_flush` mede 0,05 a 0,06 ms); as leituras de relógio de `steady_clock` em volta de `CapturePm4Dependencies` e de `CaptureTextures`; e a truncação para µs de cada uma dessas duas medições (ressalva 6 da Fase 0.5). Com a contagem de draws abaixo (≈2630 por quadro) a aritmética dá só uma ordem de grandeza para a truncação, não uma verificação. Há até 2 intervalos cronometrados por draw (o do PM4 só quando o comando tem bytes de anel; o das texturas só nos draws), então até ≈5260 chamadas cronometradas por quadro (o número real não foi contado). (a) **Estimativa com perda média de 0,5 µs por chamada:** até ≈2,6 ms, da ordem do resto. Isso é uma estimativa, não um limite superior. O limite rígido é de 1 µs por chamada vezes ≈5260 chamadas, ≈5,3 ms, que excede o resíduo inteiro (≈2,8 ms) e portanto não limita nada. (b) O `capture` custa em média cerca de 2,5 µs por draw (≈6,6 ms ÷ ≈2630 draws), então, quando os dois cronômetros (PM4 e texturas) rodam no mesmo draw, o intervalo médio medido (já truncado) é de no máximo ≈1,3 µs. É exatamente o regime em que a perda média de 0,5 µs por chamada é duvidosa, porque a perda depende da distribuição das durações e não foi medida; a hipótese é plausível, mas não verificada. (c) O que a hipótese exige: no máximo de ≈5260 chamadas, explicar todo o resíduo de ≈2,8 ms pede uma perda média de ≈0,53 µs em cada chamada, ou seja, essencialmente todo draw com os dois intervalos cronometrados; com menos chamadas, a perda média por chamada teria de ser maior, e como cada chamada perde menos de 1 µs, abaixo de ≈2800 chamadas por quadro a hipótese não explicaria o resíduo inteiro. A verificação é acumular o `capture` em nanossegundos (item 2 dos candidatos abaixo). Os outros candidatos continuam possíveis e os dados não decidem.
- **`fe_index` − (parte do `PlanBuffer`)**: não foi separado. O `fe_index` (1,27 a 1,28 ms em fd1/fd2) cobre o bloco do buffer de índices dos draws indexados: o `PlanBuffer` do buffer de índices (que, diferente dos streams, não passa por um cache `FrontStreamCache`; ele sempre calcula a chave XXH3, busca em `tracked_` e chama `RefreshTrackedBuffer`), a leitura do cabeçalho do objeto de índices, o `reset_index` dos strips, e, nos `kQuadList`, a normalização dos índices em um `std::vector` com cópia para `batch_->bytes`. Quanto de cada parte, os dados não dizem.

### O que `fs_plan` mede, e o que os dados não dizem sobre ele

`fs_plan` é o **ramo lento** do `PlanStreams` (`native_renderer.cpp`): dentro do `else` do teste do cache de stream, o `range->Resolve()` (quando há `VertexRange`), o `PlanBuffer` (chave XXH3, busca em `tracked_`, `RefreshTrackedBuffer`, `align_range`, varredura de `clean`, `CaptureBytes` quando há captura nova), o segundo `tracked_.find(sp.buffer.key)` e o preenchimento do `FrontStreamCache`. Ele domina `fe_streams`: 3,03 a 3,23 ms de 3,73 a 4,03 ms, ou 80,1% a 81,3% nas quatro execuções (`fs_prep` fica em 0,37 a 0,42 ms).

O que **leva** um stream ao ramo lento, lendo o código (o ramo rápido exige todas estas condições juntas): o cvar de depuração `sr_native_debug_buffers_always_dirty` desligado; `sc.tracked` não nulo; `sc.address`, `sc.size`, `sc.decl`, `sc.stride` e `sc.phase` iguais aos do stream atual; e `RefreshTrackedBuffer(*sc.tracked)` devolvendo falso. Qualquer uma falhando manda o stream para o ramo lento. Pontos que o código mostra:

1. O `FrontStreamCache` tem **uma entrada por slot de stream** (17 entradas, indexadas pelo número do stream `s`). Se draws consecutivos usam buffers diferentes no mesmo slot, cada troca é um erro de cache (e a entrada é sobrescrita).
2. `RefreshTrackedBuffer` devolve `t.dirty`. Ele vira `true` por: escrita de página detectada pelo write-watch; mudança do hash do conteúdo para buffers de até 32 KiB (`kHashedBufferMax`), conferido uma vez por quadro por buffer; `InvalidateGuestRange` (hooks de Unlock dos buffers); e o estouro de mais de 64 faixas em `clean`. Na leitura do código, **não há atribuição `dirty = false` em `port/src`, e `tracked_` nunca tem entradas removidas**: um `TrackedBuffer` que já ficou sujo uma vez continua devolvendo `true` em todo draw seguinte que o usa, e cada um desses draws vai ao ramo lento, onde o `PlanBuffer` consulta `clean` e pode não capturar nada (`action = 0`). Isso é uma leitura do código, não uma medida: quantos streams por draw estão nesse caso, e se é isso que domina, os dados **não** dizem.
3. O `RefreshTrackedBuffer` roda **duas vezes** no ramo lento quando o cache acertou os campos mas o buffer estava sujo (uma na condição, outra dentro do `PlanBuffer`), e a busca em `tracked_` é repetida depois do `PlanBuffer` (`tracked_.find(sp.buffer.key)`; a chave já vem no plano).

Os dados também não dizem: quantos streams por draw passam pelo ramo lento, qual dos motivos acima é o dominante, nem quanto de `fs_plan` é `Resolve` e quanto é `PlanBuffer`. **O contador por motivo é a próxima medição** (ver candidato 1 abaixo): contar por quadro os streams que entram no ramo lento por motivo (`tracked` nulo, endereço/tamanho/declaração/stride/fase diferentes, `RefreshTrackedBuffer` verdadeiro) e, para o último, o resultado do `PlanBuffer` (`action` 0, 1 ou 2).

### Draws por quadro e µs por draw

Os draws por quadro vêm do campo `draws=` das linhas `Vulkan profile (ms/frame over 120)` de `logs/bench_tl_fd3_vulkan.log` e `logs/bench_tl_fd4_vulkan.log` (cópias do `game.log` feitas com `-Profile`). Cada janela é a média de 120 quadros. Valores por janela:

- `tl_fd3`: 1356, 1628, 1787, 1199, 68, 65, 65, 65, 65, 65 (menus e carregamento), depois **2218, 2903, 2979, 2780, 2898, 2914, 2593, 2534, 2547**.
- `tl_fd4`: 1186, 1569, 2072, 1002, 66, 65, 65, 65, 65, 65, depois **2378** (fora da conta), **2857, 3034, 2761, 2028, 1915, 2611, 2598, 2530, 2639**.

Usei as últimas 9 janelas de cada execução (1080 quadros, quase a janela de 1100 quadros do relatório): média de 2707 draws por quadro em `tl_fd3` e 2553 em `tl_fd4`, média das duas 2630. (Com só as últimas 3 janelas: 2558 e 2589.) As janelas individuais variam de 1915 a 3034 dentro dessa faixa, então a cena muda bastante; um µs por draw tirado de uma janela só poderia mudar em dezenas de por cento.

**Como ler a tabela.** O µs por draw é a média do estágio (ms) × 1000 ÷ os draws por quadro das janelas do `-Profile` correspondentes. Os draws vêm de execuções **diferentes** das de `tl_fd1`/`tl_fd2` (que não têm a linha `Vulkan profile`), então a coluna "fd1/fd2" divide a média dessas duas execuções pela média dos draws de fd3 e fd4 (2630), e as colunas fd3 e fd4 dividem a média de cada execução pelos próprios draws. Os estágios por comando (`fe_end`, `fe_begin`, etc.) contam também os comandos que não são draws (resolves, clears, o swap; o campo `packets=` do perfil é maior que o `draws=` em 72 a 83 nas últimas 9 janelas de `tl_fd3` e em 66 a 83 nas de `tl_fd4`, cerca de 3% dos draws), e `fe_index` só roda nos draws indexados: o divisor é sempre o `draws=`, então os µs por draw dos estágios por comando são ligeiramente superestimados e o de `fe_index` é por draw de qualquer tipo, não por draw indexado.

| Estágio | fd1/fd2: média (ms) | µs/draw (÷2630) | fd3: ms | µs/draw (÷2707) | fd4: ms | µs/draw (÷2553) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `fe_device` | 1,60 | 0,61 | 1,81 | 0,67 | 1,64 | 0,64 |
| `fd_shaders` | 0,46 | 0,17 | 0,53 | 0,20 | 0,48 | 0,19 |
| `fd_other` (`fe_device` − `fd_shaders`) | 1,14 | 0,43 | 1,28 | 0,47 | 1,16 | 0,45 |
| `fe_streams` | 3,79 | 1,44 | 4,03 | 1,49 | 3,82 | 1,50 |
| `fs_prep` | 0,38 | 0,14 | 0,42 | 0,16 | 0,40 | 0,16 |
| `fs_plan` | 3,08 | 1,17 | 3,23 | 1,19 | 3,07 | 1,20 |
| `fs_other` (`fe_streams` − `fs_prep` − `fs_plan`) | 0,34 | 0,13 | 0,38 | 0,14 | 0,35 | 0,14 |
| `fe_index` | 1,28 | 0,48 | 1,61 | 0,59 | 1,42 | 0,56 |
| `fe_push` | 0,28 | 0,10 | 0,31 | 0,11 | 0,29 | 0,11 |
| `fe_end` − `capture` | 3,07 | 1,17 | 3,33 | 1,23 | 3,10 | 1,21 |
| `fe_end` − `capture` − `fe_push` | 2,79 | 1,06 | 3,02 | 1,12 | 2,81 | 1,10 |
| `capture` (referência) | 6,64 | 2,52 | 7,98 | 2,95 | 7,41 | 2,90 |
| `frontend` (referência) | 17,69 | 6,73 | 20,37 | 7,52 | 18,88 | 7,40 |

(As médias de fd1/fd2 são as das duas tabelas, com os arredondamentos das tabelas: por exemplo `fs_prep` 0,37 e 0,38 → 0,375, mostrado como 0,38, e `fe_end` − `capture` 3,09 e 3,04 → 3,07 depois dos arredondamentos.)

Duas leituras com cuidado: (1) em `fs_prep` (0,14 a 0,16 µs por draw) e `fe_push` (0,10 a 0,11 µs por draw) as duas leituras de relógio do próprio escopo podem ser uma fração visível do valor; o custo de uma leitura de relógio não foi medido aqui, então esses dois são limites superiores do trabalho que eles cobrem. (2) As colunas fd3 e fd4 saem de execuções com `-Profile` ligado, cujo `game` é maior (ver abaixo); por isso elas dão µs por draw entre ≈2% e ≈23% acima da coluna fd1/fd2, conforme o estágio (`frontend`: 6,73, 7,52 e 7,40), diferença que não separa o custo do perfil da variação de cena.

### Custo do modo detalhado desta fase

Comparação do `game` médio de `tl_fd1/2` com o de `tl_fe1/2` (Fase 0.5: mesmo modo detalhado, mas **sem** os quatro escopos novos, que ficam todos dentro de `fe_streams` (`fs_prep` e `fs_plan`), `fe_device` (`fd_shaders`) e `fe_end` (`fe_push`)):

| Execução | `game` | `frontend` | Intervalo médio | FPS do bench (parado / andando) |
| --- | ---: | ---: | ---: | --- |
| `tl_fe1_vulkan` | 36,97 | 18,16 | 37,5 ms | 25,5 / 28,1 |
| `tl_fe2_vulkan` | 36,19 | 17,05 | 36,9 ms | 26,0 / 27,9 |
| média fe1/fe2 | 36,58 | 17,61 | 37,2 ms | 25,75 / 28,0 |
| `tl_fd1_vulkan` | 36,92 | 17,58 | 37,5 ms | 26,7 / 26,6 |
| `tl_fd2_vulkan` | 37,10 | 17,80 | 37,6 ms | 27,1 / 25,6 |
| média fd1/fd2 | 37,01 | 17,69 | 37,55 ms | 26,9 / 26,1 |

A diferença do `game` médio é +0,43 ms (fd contra fe) e a do `frontend` é +0,09 ms, abaixo do limite de ~1,5 ms por quadro que o plano usa como ressalva forte. Comparada à diferença entre as duas execuções de cada conjunto, os +0,43 ms ficam abaixo da do par fe (0,78 ms entre fe1 e fe2 no `game`) mas acima da do par fd (0,18 ms entre fd1 e fd2). O intervalo médio subiu 0,35 ms. O FPS do bench não aponta uma direção: o "parado" subiu 1,15 FPS e o "andando" caiu 1,9 FPS. Por estágio, os que ganharam escopo subiram `fe_streams` +0,22 ms (3,57 → 3,79) e `fe_device` +0,17 ms (1,43 → 1,60), mas o `fe_index`, que não ganhou escopo, caiu 0,12 ms (1,39 → 1,28) e o `fe_end`, que ganhou o `fe_push`, caiu 0,18 ms (9,88 → 9,71): variações de tamanho parecido nos dois sentidos. Por isso a conclusão de que o custo dos escopos novos não se separa da variação de cena nestes dados se apoia em três coisas, e só nelas: a diferença do par fe (0,78 ms), maior que os +0,43 ms; o movimento em sentidos opostos de `fe_index` (−0,12 ms) e `fe_end` (−0,18 ms); e a mudança de build entre `tl_fe` e `tl_fd`, que está embutida na diferença (o custo da sonda não foi medido diretamente; ver o fim desta seção). Como o par fd é mais estreito (0,18 ms) que a diferença, e cada conjunto tem só duas execuções, isto não prova que o custo seja nulo: o custo do modo detalhado com os escopos novos não foi medido com isolamento.

`fe_end` − `capture` nestas execuções é 3,04 a 3,09 ms (fd1 e fd2), contra 2,77 a 2,97 ms citados da Fase 0.5 (fe1 e fe2). A diferença vem sobretudo do `capture`, que é cerca de 0,4 ms menor nestas execuções (6,56 e 6,72 ms contra 7,20 e 6,82 ms; médias 6,64 e 7,01 ms). A afirmação da Fase 0.5 de "~2,8 a 3,0 ms" não é contradita, mas se refere a outras execuções.

As execuções com `-Profile` pesam mais: `game` médio de 40,89 e 38,67 ms (média 39,78) contra 37,01 ms em `tl_fd1/2`, +2,77 ms, e `frontend` de 20,37 e 18,88 ms contra 17,69, +1,94 ms. Isso é o custo do perfil mais a variação de cena e não foi separado (a faixa de draws por janela mostra que a cena variou). Por isso os µs por draw das colunas fd3/fd4 são um pouco maiores que os de fd1/fd2, e a coluna fd1/fd2 (sem perfil) é a que vale para ordenar candidatos.

### Resumo e ressalvas desta fase

- **`fe_streams`** (3,79 ms, 1,44 µs/draw): `fs_plan` 3,08 ms (81%), `fs_prep` 0,38 ms (10%), `fs_other` 0,34 ms (9%).
- **`fe_device`** (1,60 ms, 0,61 µs/draw): `fd_shaders` 0,46 ms (28% a 29%), `fd_other` 1,14 ms (71%).
- **`fe_end` fora da captura** (3,07 ms, 1,17 µs/draw): `fe_push` 0,28 ms (9%); o resto, 2,79 ms (1,06 µs/draw), sem atribuição; a truncação do `capture` é uma hipótese plausível mas não verificada (só uma ordem de grandeza por aritmética, acima), não uma medida.
- **`fe_index`** (1,28 ms, 0,48 µs/draw): não decomposto.
- Soma dos quatro blocos de captura de estado (`fe_device` + `fe_streams` + `fe_index` + `fe_end` − `capture`): 9,7 ms na média de fd1/fd2, contra 17,69 ms de `frontend`. `fs_plan` (3,08 ms) e o resto de `fe_end` (3,07 ms) são os dois maiores, e só o primeiro é um estágio medido por escopo; o segundo é uma diferença de médias.
- O front-end do Vulkan continua sendo cerca de metade do `game` (48% a 50% nas quatro execuções desta fase); o `game_guest` (19,3 a 19,4 ms em fd1/fd2) permanece sem decomposição, como na Fase 0.5. Nada aqui muda a conclusão da Fase 0.5 sobre ele.
- Os valores absolutos valem para esta máquina e para duas execuções por modo; a cena varia entre execuções. Comparar razões e frações é mais seguro que comparar valores absolutos.
- O custo da sonda não foi medido diretamente: cada escopo lê o relógio duas vezes (início e fim), e o número de escopos por draw não foi contado. Os estágios menores (`fs_prep`, `fe_push`) são os mais afetados.
- A ressalva 3 da Fase 0.5 (partes aninhadas não se somam; `fe_begin`, `fe_ring` e `fe_end` incluem o comando de swap e o `SyncRing`) continua valendo. Os novos estágios aninham assim: `fd_shaders` dentro de `fe_device`; `fs_prep` e `fs_plan` dentro de `fe_streams`; `fe_push` dentro de `fe_end`.

### Candidatos da Fase 1.1b (ordem proposta)

O teto de cada item é a média do próprio estágio em fd1/fd2 (ms por quadro e µs por draw, tabela acima), que só seria alcançado se o estágio inteiro sumisse; nenhum ganho desse tamanho é esperado, e quanto vira FPS depende de `worker` e `gpu`, que estão perto do orçamento (30,9 a 31,0 ms e 31,1 a 31,8 ms nas execuções fd1/fd2). Todo item que muda comportamento (não os de medição) tem de preservar a **imagem bit a bit**, a **semântica do rastreamento de buffer sujo** (`dirty`/`clean`, write-watch, `InvalidateGuestRange`) e a **precedência de leitura dos bytes capturados** (a sobreposição de faixas que `FindUnambiguousCapture` e o shim de captura aplicam). Verificação comum: o custo do estágio por draw cai (mesmos 4 benches, mesma tabela de µs por draw); a suíte `tests/native` e `python -m pytest tests/tools -q` continuam passando; e o gate de imagem do bench (`-Gate record` na build antiga, `-Gate check` na nova).

1. **Medir: contador por motivo do ramo lento do `PlanStreams` e sub-tempo do `fs_plan`** (estágio: `fs_plan`, teto 3,08 ms, 1,17 µs/draw). Contar, por quadro, os streams que entram no ramo lento por motivo (`sc.tracked` nulo; endereço, tamanho, declaração, stride ou fase diferentes; `RefreshTrackedBuffer` verdadeiro), o resultado do `PlanBuffer` (`action` 0, 1 ou 2) e subdividir o tempo em `Resolve` contra `PlanBuffer` contra o resto. Risco: nenhum no comportamento (só contadores e escopos no modo detalhado). Verificação: os contadores batem com os `draws=` do perfil; o `fs_plan` total não muda além da variação. Decide o tamanho real dos itens 5 e 6.
2. **Medir: sub-tempo do resto do `EndCmd`** (estágio: `fe_end` − `capture` − `fe_push`, teto 2,79 ms, 1,06 µs/draw). Escopos em volta de `checked_guest_reads.Reset()`, do tratamento do `capture_mirror_` e do `FlushBatch`, e acumular o `capture` em nanossegundos em vez de µs inteiros por chamada, para testar a hipótese da truncação (plausível mas não verificada; ver o bullet de `fe_end` − `capture` acima). Risco: nenhum no comportamento. Verificação: `capture` + resto fecha com `fe_end`; se o `capture` subir e o resto cair do mesmo tanto, a truncação explicava o resíduo.
3. **Medir: `fe_index`** (estágio: `fe_index`, teto 1,28 ms, 0,48 µs/draw por draw de qualquer tipo): separar o `PlanBuffer` do buffer de índices da normalização de `kQuadList`. Só depois decidir se o buffer de índices merece um cache como o dos streams. Risco da mudança futura: o mesmo dos itens 5 e 6.
4. **Medir: bytes capturados por draw no `CaptureDevice`** (estágio: `fd_other`, teto 1,14 ms, 0,43 µs/draw). Contar, por quadro, as chamadas de `CaptureBytes` e os bytes copiados dentro do `CaptureDevice` (no modo detalhado). Risco: nenhum no comportamento. Verificação: o número de chamadas por draw deve ser comparável à faixa de 15 a 20 lida no código, e o `fd_other` total não muda além da variação. Decide se o item 7 vale a pena (o ganho é limitado pelo `memcpy` inerente).
5. **Tirar trabalho repetido do ramo lento sem mudar o resultado** (estágio: `fs_plan`, teto 3,08 ms, 1,17 µs/draw; o ganho real depende do item 1). Exemplos que o código sugere: reaproveitar o iterador de `tracked_` que o `PlanBuffer` já obteve em vez de repetir a busca depois dele; evitar a segunda chamada de `RefreshTrackedBuffer` no mesmo draw. A segunda chamada provavelmente já é barata: no mesmo draw o `checked_seq` já foi atualizado pela primeira, então a consulta de escrita de página é pulada (salvo uma escrita física entre as duas chamadas), e o `BufferContent::Refresh` retorna antes do hash porque o quadro é o mesmo; sobra uma leitura atômica e algumas comparações. O teto desse exemplo deve ficar bem abaixo do `fs_plan` inteiro; o valor não foi medido. Risco: baixo, desde que o `PlanBuffer` devolva exatamente o mesmo `BufferPlan` (`key`, `action`, `begin`, `end`) e os mesmos bytes capturados; o ponto de atenção é não pular a chamada que arma o write-watch (`ArmTextureWatch`). Verificação: comparar o `BufferPlan` e as faixas capturadas contra a build antiga (testes de `tests/native` e o gate de imagem), e `fs_plan` por draw menor.
6. **Ampliar o acerto do cache de stream** (estágio: `fs_plan`, mesmo teto; só vale se o item 1 mostrar que a maioria das entradas no ramo lento é "buffer sujo mas faixa já limpa" ou troca de buffer no slot). Exemplo: um cache com mais de uma entrada por slot, ou um acerto direto quando a faixa necessária já está em `clean`. Risco: **alto** no rastreamento de sujo (a faixa necessária exige o `Resolve` dos índices; um acerto que pule a captura quando devia capturar quebra a imagem); verificação: gate de imagem, testes de dirty-tracking, e a contagem de capturas (`action` 1 e 2, bytes capturados) idêntica à da build antiga. Não propor antes do item 1.
7. **Reduzir o trabalho de `CaptureDevice` fora dos shaders** (estágio: `fd_other`, teto 1,14 ms, 0,43 µs/draw; só depois do item 4). Por exemplo juntar `CaptureBytes` de faixas vizinhas ou pular o que não mudou desde o draw anterior. Risco: médio. O worker e a captura de PM4 leem essas faixas por endereço, e a sobreposição de faixas tem uma regra de precedência; mudar o formato das faixas muda o que é lido. Verificação: os bytes que o worker enxerga por endereço têm de ser os mesmos (teste novo comparando as leituras por faixa), gate de imagem, `fd_other` por draw menor. O ganho é limitado pelo `memcpy` inerente, e o volume de bytes por draw não foi medido (item 4).
8. **Buscas de shader por draw** (estágio: `fd_shaders`, teto 0,46 ms, 0,17 µs/draw; as tomadas em `fs_prep`, teto 0,38 ms, entram no mesmo trabalho). Cada draw faz três tomadas do mutex global do registro de shaders (`TryRegisterInlineShaders` e dois `CaptureGuestShader`, estes com cópia de `shared_ptr`); o `DynamicVertexFetch`, em `fs_prep`, toma o mesmo mutex mais duas vezes por draw (`LookupGuestShader`). Se outra thread usa esse mutex, a contenção não foi medida. Uma tomada só ou uma memória do último par (vs, ps) reduziria isso. Risco: médio: endereços de objeto de shader podem ser reutilizados (o `OnCreateShader` apaga associações antigas), então a memória tem de ser invalidada em criação e remoção de shader; e o `CaptureGuestShader` tem de devolver o mesmo `ShaderCapture`. Verificação: `fd_shaders` por draw menor; imagem idêntica; um teste que recria um shader no mesmo endereço e confere que a captura nova é a devolvida.
9. **`fs_prep`, `fe_push`, `fs_other`** (0,38, 0,28 e 0,34 ms; 0,14, 0,10 e 0,13 µs/draw): tetos pequenos, na ordem de grandeza do custo dos relógios dos próprios escopos. Não são candidatos antes dos itens acima; listados só para registrar que foram medidos.

**O que os dados não decidem.** Qual motivo domina o ramo lento do `PlanStreams`; quanto do `fs_plan` é `Resolve`, `PlanBuffer` ou o segundo `find`; quanto do resto do `EndCmd` é truncação do `capture`, leituras de relógio ou outro trabalho; quanto do `fe_index` é o `PlanBuffer` de índices; quantos bytes por draw o `CaptureDevice` copia; e quanto uma redução do `frontend` vira FPS (depende de `worker` e `gpu`). Também não está medido quanto do `fs_other` e do `fs_plan` é o hash de conteúdo de buffers de até 32 KiB que o `RefreshTrackedBuffer` faz (na primeira chamada de cada quadro por buffer; ver o bullet de `fs_other`): a política de hash de texturas decidida na seção anterior diz respeito a texturas e não cobre o rastreamento de buffers de vértices, e este documento não propõe nem decide nada sobre esse hash. Os itens 1 a 4 existem para responder o que é de medição antes de mudar código (a conversão em FPS depende de `worker` e `gpu`).

## Motivos do ramo lento e resíduo do EndCmd (Fase 1.1b)

Plano: `superpowers/plans/2026-10-08-frontend-diet-measurement-2.md`. Esta fase responde às duas perguntas que a Fase 1.1a deixou abertas, sem otimizar nada: (1) por que o `PlanStreams` toma o ramo lento e quanto do `fs_plan` é `Resolve`, `PlanBuffer` ou o resto; (2) para onde vão os ≈2,8 ms do `fe_end` que não eram `capture`. O código novo (commits `7c3ce60` e `6ea3324`) só acrescenta, no modo detalhado, os estágios `fe_pm4` e `fe_textures` (as mesmas duas regiões que o `capture` soma, agora em nanossegundos), `fs_resolve` e `fs_buffer` (dentro do `fs_plan`), o derivado `fe_end_rest` (`fe_end` menos `fe_pm4`, `fe_textures` e `fe_push`) e os contadores por motivo do ramo lento, que o jogo escreve a cada 120 quadros na linha `native front-end stream plan`. Máquina e condições são as da Fase 0 (Intel UHD, i5-13420H, 1280x720, vsync, limite de 30 FPS), 2026-10-08, build release.

Procedência da build: a árvore de trabalho estava limpa no início; o `superman_returns.exe` tem data de modificação 21:09:15 e o único arquivo C++ do commit `6ea3324` (`native_renderer.cpp`) tem 21:08:48, anterior ao executável; o commit foi criado às 21:11:37, depois da compilação. A árvore do executável foi assumida idêntica à do commit, sem teste de árvore limpa (mesma ressalva da Fase 1.1a). Uma execução de fumaça anterior (`tl_fp_smoke`) não entra nas tabelas abaixo.

### Medições

Quatro benches Vulkan, um por vez, sem outra carga, todos com `-Timeline -TimelineDetail -Profile` (`tl_fp1` a `tl_fp4`). Nenhuma falha de infraestrutura e nenhuma repetição. Como as quatro têm o perfil ligado, a comparação de custo do modo detalhado é com `tl_fd3/fd4` (também com perfil), não com `tl_fd1/fd2`. FPS do bench, média (mínimo):

| Execução | parado | andando | Intervalo médio do relatório |
| --- | --- | --- | --- |
| `tl_fp1_vulkan` | 23,7 (17,1) | 25,4 (24,7) | 40,8 ms |
| `tl_fp2_vulkan` | 26,3 (21,8) | 25,2 (23,8) | 38,8 ms |
| `tl_fp3_vulkan` | 22,7 (15,8) | 22,8 (21) | 43,4 ms |
| `tl_fp4_vulkan` | 25,7 (22,6) | 24,8 (22,6) | 39,7 ms |

As quatro tabelas do relatório (janela dos últimos 1100 quadros):

`tl_fp1_vulkan`:

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 40.8 ms = 24.5 FPS | 1% low 19.0 FPS | mínimo 1.8 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio           média     p50     p99   bloq.   util.  folga p99  limitante
game              40.14   40.19   51.63    0.00     98%     -18.30        78%
game_guest        19.88   19.96   25.24    0.00     49%       8.09          -
game_other        32.64   32.61   39.80    0.00     80%      -6.47          -
capture            7.50    7.19   14.91    0.00     18%      18.43          -
frontend          20.27   20.10   28.15    0.00     50%       5.18          -
fe_begin           0.50    0.50    0.68    0.00      1%      32.65          -
fe_ring            0.32    0.30    0.65    0.00      1%      32.68          -
fe_device          1.76    1.76    2.15    0.00      4%      31.18          -
fe_index           1.47    1.44    2.17    0.00      4%      31.17          -
fe_streams         4.44    4.45    5.80    0.00     11%      27.53          -
fe_end            11.05   10.78   18.59    0.00     27%      14.75          -
fe_flush           0.07    0.05    0.23    0.00      0%      33.11          -
fd_shaders         0.50    0.50    0.66    0.00      1%      32.67          -
fs_prep            0.42    0.43    0.55    0.00      1%      32.79          -
fs_plan            3.65    3.64    4.87    0.00      9%      28.46          -
fe_push            0.31    0.30    0.40    0.00      1%      32.94          -
fe_pm4             4.24    4.24    5.73    0.00     10%      27.60          -
fe_textures        5.76    5.24   12.98    0.00     14%      20.35          -
fs_resolve         0.95    0.91    1.43    0.00      2%      31.90          -
fs_buffer          2.51    2.50    3.53    0.00      6%      29.80          -
frontend_other     0.72    0.71    1.03    0.00      2%      32.30          -
fe_end_rest        0.75    0.74    1.00    0.00      2%      32.33          -
front_wait         0.60    0.00    6.87    0.00      1%      26.47          -
worker            34.66   34.88   45.34    2.22     85%     -12.01        12%
record            30.28   29.77   50.65    2.25     74%     -17.31         5%
gpu               33.49   33.82   38.51    0.00     82%      -5.18         5%
```

`tl_fp2_vulkan`:

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 38.8 ms = 25.8 FPS | 1% low 19.9 FPS | mínimo 1.7 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio           média     p50     p99   bloq.   util.  folga p99  limitante
game              38.13   38.34   48.86    0.00     98%     -15.53        85%
game_guest        19.57   19.62   25.53    0.00     50%       7.80          -
game_other        31.06   31.23   38.28    0.00     80%      -4.95          -
capture            7.08    6.86   13.65    0.00     18%      19.68          -
frontend          18.57   18.88   26.60    0.00     48%       6.73          -
fe_begin           0.47    0.48    0.67    0.00      1%      32.67          -
fe_ring            0.28    0.27    0.61    0.00      1%      32.72          -
fe_device          1.58    1.64    2.08    0.00      4%      31.26          -
fe_index           1.38    1.37    2.06    0.00      4%      31.27          -
fe_streams         3.96    4.09    5.52    0.00     10%      27.81          -
fe_end            10.25   10.20   17.23    0.00     26%      16.10          -
fe_flush           0.07    0.06    0.21    0.00      0%      33.13          -
fd_shaders         0.47    0.47    0.66    0.00      1%      32.67          -
fs_prep            0.38    0.39    0.52    0.00      1%      32.81          -
fs_plan            3.24    3.34    4.63    0.00      8%      28.71          -
fe_push            0.28    0.28    0.39    0.00      1%      32.94          -
fe_pm4             3.86    3.91    5.55    0.00     10%      27.78          -
fe_textures        5.42    5.12   11.71    0.00     14%      21.63          -
fs_resolve         0.91    0.89    1.33    0.00      2%      32.01          -
fs_buffer          2.17    2.26    3.34    0.00      6%      29.99          -
frontend_other     0.65    0.66    0.93    0.00      2%      32.40          -
fe_end_rest        0.70    0.70    0.96    0.00      2%      32.38          -
front_wait         0.60    0.00    0.03    0.00      2%      33.30          -
worker            31.66   32.38   43.74    1.48     82%     -10.41        10%
record            27.88   27.65   43.54    1.91     72%     -10.21         3%
gpu               30.05   30.98   38.37    0.00     77%      -5.03         1%
```

`tl_fp3_vulkan`:

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 43.4 ms = 23.1 FPS | 1% low 16.7 FPS | mínimo 1.8 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio           média     p50     p99   bloq.   util.  folga p99  limitante
game              42.44   42.75   56.14    0.00     98%     -22.81        76%
game_guest        21.55   21.42   32.49    0.00     50%       0.84          -
game_other        34.43   34.56   44.10    0.00     79%     -10.76          -
capture            8.01    7.91   15.03    0.00     18%      18.30          -
frontend          20.89   21.34   30.36    0.00     48%       2.98          -
fe_begin           0.52    0.54    0.78    0.00      1%      32.56          -
fe_ring            0.33    0.31    0.68    0.00      1%      32.65          -
fe_device          1.83    1.89    2.40    0.00      4%      30.94          -
fe_index           1.52    1.53    2.41    0.00      4%      30.93          -
fe_streams         4.40    4.52    6.17    0.00     10%      27.17          -
fe_end            11.54   11.62   18.84    0.00     27%      14.50          -
fe_flush           0.06    0.04    0.21    0.00      0%      33.12          -
fd_shaders         0.54    0.55    0.76    0.00      1%      32.57          -
fs_prep            0.44    0.46    0.60    0.00      1%      32.73          -
fs_plan            3.56    3.63    5.08    0.00      8%      28.25          -
fe_push            0.33    0.34    0.45    0.00      1%      32.88          -
fe_pm4             4.40    4.52    6.61    0.00     10%      26.72          -
fe_textures        6.01    5.68   12.44    0.00     14%      20.89          -
fs_resolve         1.00    0.98    1.72    0.00      2%      31.61          -
fs_buffer          2.36    2.40    3.56    0.00      5%      29.77          -
frontend_other     0.75    0.76    1.18    0.00      2%      32.16          -
fe_end_rest        0.80    0.82    1.11    0.00      2%      32.23          -
front_wait         0.86    0.00   12.39    0.00      2%      20.94          -
worker            35.79   36.91   50.88    2.80     83%     -17.55        15%
record            31.53   31.14   55.84    2.32     73%     -22.50         6%
gpu               33.82   34.37   41.00    0.00     78%      -7.67         3%
```

`tl_fp4_vulkan`:

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 39.7 ms = 25.2 FPS | 1% low 19.1 FPS | mínimo 1.9 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio           média     p50     p99   bloq.   util.  folga p99  limitante
game              39.00   39.06   51.73    0.00     98%     -18.39        85%
game_guest        19.66   19.67   25.31    0.00     50%       8.02          -
game_other        31.46   31.60   41.15    0.00     79%      -7.82          -
capture            7.54    7.28   14.41    0.00     19%      18.92          -
frontend          19.34   19.33   27.49    0.00     49%       5.84          -
fe_begin           0.48    0.48    0.72    0.00      1%      32.62          -
fe_ring            0.29    0.27    0.61    0.00      1%      32.72          -
fe_device          1.65    1.68    2.21    0.00      4%      31.12          -
fe_index           1.44    1.40    2.17    0.00      4%      31.16          -
fe_streams         4.01    4.03    5.68    0.00     10%      27.65          -
fe_end            10.78   10.54   18.48    0.00     27%      14.85          -
fe_flush           0.07    0.06    0.21    0.00      0%      33.12          -
fd_shaders         0.49    0.50    0.72    0.00      1%      32.62          -
fs_prep            0.40    0.40    0.57    0.00      1%      32.76          -
fs_plan            3.25    3.24    4.68    0.00      8%      28.66          -
fe_push            0.30    0.30    0.42    0.00      1%      32.91          -
fe_pm4             4.06    4.03    6.26    0.00     10%      27.07          -
fe_textures        5.68    5.33   12.36    0.00     14%      20.97          -
fs_resolve         0.95    0.92    1.49    0.00      2%      31.85          -
fs_buffer          2.12    2.14    3.31    0.00      5%      30.03          -
frontend_other     0.68    0.68    1.04    0.00      2%      32.29          -
fe_end_rest        0.74    0.74    1.04    0.00      2%      32.29          -
front_wait         0.59    0.00    2.30    0.00      1%      31.04          -
worker            33.14   33.27   47.05    1.41     84%     -13.72        10%
record            28.52   27.65   46.50    1.92     72%     -13.17         3%
gpu               30.60   31.72   38.61    0.00     77%      -5.28         1%
```

Como nas fases anteriores, `fd_shaders`, `fs_prep`, `fs_plan`, `fe_push`, `fe_pm4`, `fe_textures`, `fs_resolve` e `fs_buffer` aninham nos estágios maiores (`fe_pm4`, `fe_textures` e `fe_push` dentro de `fe_end`; `fs_resolve` e `fs_buffer` dentro de `fs_plan`, que está dentro de `fe_streams`) e não se somam às colunas maiores. `fe_end_rest` é um derivado do relatório (`fe_end` menos `fe_pm4`, `fe_textures` e `fe_push`, com piso em zero por quadro).

**Draws por quadro e janelas dos contadores.** Os draws vêm do campo `draws=` das linhas `Vulkan profile (ms/frame over 120)` de `logs/bench_tl_fp1_vulkan.log` a `bench_tl_fp4_vulkan.log` (cópias do `game.log` feitas com `-Profile`); os contadores vêm das linhas `native front-end stream plan (per frame over 120)` do mesmo log. Como na Fase 1.1a, usei as **últimas 9 janelas de 120 quadros** de cada execução (1080 quadros, quase a janela de 1100 do relatório), pareando a janela do contador com a do perfil pela posição. Média de `draws=` nessas janelas: 2886 (`fp1`), 2554 (`fp2`), 2887 (`fp3`) e 2529 (`fp4`); por janela, de 1869 a 3074, então a cena variou bastante dentro das execuções (por exemplo, 1869 e 2118 em `fp2`, 1971 e 1999 em `fp4`). Os contadores por quadro são divisões inteiras por 120 feitas pelo jogo, então os campos de uma linha podem não fechar com o total por 1 ou 2 e os valores abaixo (médias de 9 janelas) saem com decimais. Os logs e os CSVs ficam em `logs/` (não versionado); as tabelas acima são cópias.

### Pergunta 1: por que o `PlanStreams` toma o ramo lento

**O que "endereço diferente" significa, pelo código.** O `FrontStreamCache` (`front_stream_cache_[17]`) tem **uma entrada por slot de stream** `s`: a última combinação (endereço-base do buffer, tamanho, declaração, stride, fase, chave e ponteiro para o `TrackedBuffer`) vista **no último draw que usou aquele slot**. Para cada stream de cada draw, o `PlanStreams` calcula o `buffer_base` (o início do buffer de vértices inteiro, quando o objeto do buffer o descreve; o deslocamento do `SetStreamSource` dentro de buffers grandes compartilhados não conta) e o compara com `sc.address`. A condição do ramo rápido é uma cadeia de `&&` na ordem `sc.tracked` não nulo, `sc.address == buffer_base`, `sc.size`, `sc.decl`, `sc.stride`, `sc.phase` e só então `!RefreshTrackedBuffer(*sc.tracked)`. O contador classifica o primeiro campo diferente nessa mesma ordem. Logo o motivo `address` quer dizer: o slot, neste draw, aponta para **outro buffer de vértices** (outro endereço-base) que o do último draw que usou o slot; ele **não** diz se tamanho, declaração, stride e fase também diferiam (eles só são testados quando o endereço é igual). Nesse caso a condição falha antes de `RefreshTrackedBuffer` ser chamado, e o ramo lento faz: o `range->Resolve()` (se há `VertexRange`), o `PlanBuffer` (chave XXH3 de 7 campos, busca em `tracked_`; se achou: `RefreshTrackedBuffer`, e se está limpo devolve sem capturar; se está sujo, alinha a faixa necessária e a confere contra `clean`: se ela cabe inteira dentro de uma única faixa de `clean` (ou se a faixa alinhada é vazia), devolve sem capturar; senão captura a faixa necessária alinhada inteira, sem descontar o que `clean` já cobre em parte, e a acrescenta a `clean`; se não achou: cadastra o buffer, arma o write-watch, calcula o hash de conteúdo quando o buffer é pequeno e captura o buffer inteiro), a segunda busca em `tracked_` e a regravação da entrada do slot com o buffer novo. Como a entrada é sobrescrita, um slot que alterna entre dois buffers (A, B, A, B) erra em todo draw.

**O `Resolve` é memorizado por draw, mas chamado em todo ramo lento, e o resultado só é consumido em um caso.** `VertexRange::Resolve()` tem uma bandeira `resolved` que o memoriza por draw: um draw não indexado já nasce resolvido e a chamada retorna na hora; num draw indexado a primeira chamada varre os índices (mínimo e máximo) e as seguintes do mesmo draw retornam na hora. Só dois formatos de índice têm caminho SSE (16 bits big-endian com reset `0xFFFF`, e 32 bits com endian 2 e reset `0xFFFFFF`); os demais formatos, e a cauda de cada varredura, são escalares. O resultado vira `need_begin`/`need_end`, que o `PlanBuffer` só usa quando acha o buffer em `tracked_` **e** ele está sujo (`align_range` e a varredura de `clean`); um buffer limpo devolve antes de usá-los, e um buffer novo captura o buffer inteiro. O `Resolve` é chamado, de forma ansiosa, antes do `PlanBuffer` em todo ramo lento que tem `range`, sem saber se o resultado será usado; o comentário de cabeçalho de `VertexRange` em `native_renderer.h` ("computed only when a dirty dynamic buffer needs it") está desatualizado em relação a isso. Pela leitura do código, em desvios em que o buffer está limpo ou é novo o `Resolve` foi calculado sem o resultado ser consumido; quantos desvios são assim não foi medido (o desfecho do `PlanBuffer` não é contado).

**Motivos, por execução** (médias por quadro das últimas 9 janelas):

| Execução | draws/quadro | `evaluated` | `fast` | `slow` | `slow`/`evaluated` | `address` | `decl` | `dirty` | `untracked`, `size`, `stride`, `phase` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `tl_fp1` | 2886 | 3967 | 1486 | 2480 | 62,5% | 2479 | 0,2 | 1,0 | 0 |
| `tl_fp2` | 2554 | 3440 | 1309 | 2130 | 61,9% | 2128 | 0,3 | 1,0 | 0 |
| `tl_fp3` | 2887 | 3952 | 1486 | 2465 | 62,4% | 2464 | 0,1 | 0,9 | 0 |
| `tl_fp4` | 2529 | 3410 | 1312 | 2098 | 61,5% | 2096 | 0,3 | 1,0 | 0 |

Os quatro motivos da última coluna foram 0 em todas as 36 janelas usadas. Fração de cada motivo entre os lentos: `address` 99,94%, 99,91%, 99,95% e 99,93% (`tl_fp1` a `tl_fp4`); `dirty` cerca de 0,04%; `decl` até 0,02%. A fração de avaliações lentas fica em 61,5% a 62,5% nas quatro execuções (62,1% somando as quatro). Por draw: 1,35 a 1,37 avaliações de stream e 0,83 a 0,86 avaliações lentas. **Janelas descartadas e critério.** Cada execução imprime 20 janelas de 120 quadros (19 em `tl_fp3`) e o critério foi apenas posicional: as últimas 9, como na 1.1a. Ficam de fora (a) as 4 primeiras janelas, de aquecimento antes dos menus, que não estão vazias (`evaluated` de 1346 a 2720 nas quatro execuções); (b) as 6 janelas de menu, com `evaluated` 0 ou 1; e (c) em `tl_fp1`, `tl_fp2` e `tl_fp4`, a primeira janela de jogo (`evaluated` 3173, 3748 e 3564). Em `tl_fp3` há só 9 janelas de jogo e a primeira delas (3628) entra. Só as janelas de menu têm `evaluated` ≤ 1; as demais foram descartadas pela posição, não por serem inválidas, e o critério não filtra por conteúdo da cena.

**O que os dados mostram.** (a) Quase todo desvio para o ramo lento (>99,9%) é classificado como `address`: o slot aponta para um buffer de vértices diferente do último draw que usou o slot. Essa classificação é pelo primeiro campo diferente e não olha o estado de sujo do buffer alcançado; ela não diz que o buffer novo está limpo. (b) `dirty` só é o motivo quando **todos** os campos em cache valem e `RefreshTrackedBuffer` devolve verdadeiro, isto é, quando o mesmo buffer se repete no mesmo slot e está sujo; isso aparece em cerca de 1 avaliação por quadro (`decl` somado a `dirty` é de 1,0 a 1,3 por quadro contra 1309 a 1486 acertos por quadro). Logo, buffers que ficaram sujos e nunca voltam a limpo (`dirty` nunca é atribuído a falso; a hipótese da Fase 1.1a, que continua verdadeira como leitura do código) são raros **como repetição no mesmo slot** (≈1 por quadro). A parcela deles entre as avaliações `address` é **desconhecida**: um buffer que continua sujo mas é alcançado por troca de endereço é contado em `address`, não em `dirty`, e os contadores não dizem quantas das ≈2100 a 2500 avaliações `address` por quadro chegam a um `tracked_` sujo. Portanto os dados não sustentam nem refutam que o ramo lento seja em parte buffer sujo; mostram apenas que, como motivo de repetição no mesmo slot, é pouco. Consequência para o texto da 1.1a: a segunda chamada de `RefreshTrackedBuffer` no mesmo draw (condição mais `PlanBuffer`), que o item 3 daquela seção descrevia, só ocorre no motivo `dirty` (todos os campos iguais e `RefreshTrackedBuffer` verdadeiro), cerca de 1 por quadro; nos desvios por `address` (e nos demais motivos de campo) a condição nem chama `RefreshTrackedBuffer` e há uma única chamada, dentro do `PlanBuffer`.

**O que os dados não mostram.** Os contadores não são por slot e não guardam o endereço, então não dizem (1) quantos buffers distintos cada slot vê por quadro; (2) se os mesmos buffers voltam em draws seguintes do mesmo quadro (alternância A, B, A, B, que um cache de 2 a 4 entradas por slot converteria em acertos) ou se cada buffer aparece uma vez por quadro (caso em que um cache **por slot** maior não ajuda; uma consulta direta em `tracked_` por endereço, ver o candidato 6, não depende dessa repetição dentro do quadro); (3) o desfecho do `PlanBuffer` nos desvios lentos (`action` 0, 1 ou 2), isto é, quantos acham o buffer em `tracked_` e o devolvem sem capturar, quantos capturam bytes e quantos cadastram um buffer novo, e, entre os que acham, se o `tracked_` está sujo (a `action` 0 mistura buffer limpo com buffer sujo cuja faixa já está coberta por `clean`); (4) quantos `Resolve` varrem índices e quantos resultados são consumidos. Nada disso foi contado. Mesmo uma avaliação lenta convertida em acerto mantém o custo inerente do `RefreshTrackedBuffer` (e, na primeira chamada do quadro por buffer de até 32 KiB, o hash de conteúdo), que o ramo rápido atual também paga.

**Divisão do `fs_plan`** (ms por quadro; entre parênteses, fração do `fs_plan`; "resto" é `fs_plan` menos `fs_resolve` menos `fs_buffer`; µs por avaliação lenta = ms × 1000 ÷ `slow` por quadro da tabela acima):

| Execução | `fs_plan` | `fs_resolve` | `fs_buffer` | resto | µs por avaliação lenta: `fs_plan` / `fs_resolve` / `fs_buffer` / resto |
| --- | ---: | ---: | ---: | ---: | --- |
| `tl_fp1` | 3,65 | 0,95 (26,0%) | 2,51 (68,8%) | 0,19 (5,2%) | 1,47 / 0,38 / 1,01 / 0,08 |
| `tl_fp2` | 3,24 | 0,91 (28,1%) | 2,17 (67,0%) | 0,16 (4,9%) | 1,52 / 0,43 / 1,02 / 0,08 |
| `tl_fp3` | 3,56 | 1,00 (28,1%) | 2,36 (66,3%) | 0,20 (5,6%) | 1,44 / 0,41 / 0,96 / 0,08 |
| `tl_fp4` | 3,25 | 0,95 (29,2%) | 2,12 (65,2%) | 0,18 (5,5%) | 1,55 / 0,45 / 1,01 / 0,09 |

Média das quatro: `fs_plan` 3,42 ms (1,26 µs por draw), `fs_resolve` 0,95 ms (0,35 µs por draw), `fs_buffer` 2,29 ms (0,84 µs por draw). O `PlanBuffer` é cerca de dois terços do `fs_plan` (65% a 69%), o `Resolve` cerca de um quarto a três décimos (26% a 29%) e o resto, que contém a segunda busca em `tracked_`, a regravação da entrada do slot, os contadores e a parte das leituras de relógio dos escopos que cai fora de `fs_resolve` e `fs_buffer` (ver a contabilidade abaixo), é de 0,16 a 0,20 ms (5%). Contabilidade das leituras de relógio, a mesma em toda esta seção: cada escopo de detalhe faz 2 leituras (início e fim, `NowNs`); as duas ficam dentro do intervalo do escopo pai, mas o custo de cada leitura se divide em torno do ponto em que o relógio é amostrado, então ≈1 leitura de custo cai dentro do intervalo do próprio escopo e ≈1 fora dele (no pai). O custo de uma leitura **não foi medido**; os 20 a 30 ns usados aqui são uma suposição. Pela contabilidade, o resto do `fs_plan` (dentro do `fs_plan` e fora de `fs_resolve` e `fs_buffer`) contém ≈1 leitura de cada um de três escopos, o do próprio `fs_plan` e os dois novos: 3 × 2098 a 2480 avaliações lentas = 6300 a 7400 leituras por quadro (o `fs_resolve` só roda quando há `range`, então é um teto), que a 20 a 30 ns dá 0,13 a 0,22 ms, da mesma ordem do resto de 0,16 a 0,20 ms. Portanto os dados não mostram custo mensurável na segunda busca nem no preenchimento da entrada: o teto desses dois, somados a todo o resto e à sonda, é de 0,16 a 0,20 ms. As avaliações do ramo rápido não têm escopo próprio; o `fs_other` (`fe_streams` menos `fs_prep` menos `fs_plan`: 0,34 a 0,40 ms) cobre as leituras por stream de todas as avaliações (3410 a 3967 por quadro) e o trabalho do ramo rápido (que inclui a chamada de `RefreshTrackedBuffer`), o que dá no máximo ≈0,09 a 0,11 µs por avaliação para o ramo rápido, contra 1,44 a 1,55 µs por avaliação lenta (≈15 vezes); é um limite superior do custo médio dos acertos atuais, não do custo dos buffers que um cache maior converteria (um buffer visto pela primeira vez no quadro paga o hash de conteúdo do `RefreshTrackedBuffer`, se for de até 32 KiB).

### Pergunta 2: o resíduo do `EndCmd`

O `capture` das fases anteriores é a soma de duas regiões cronometradas em **microssegundos inteiros por chamada** (`duration_cast` de `std::chrono` para microssegundos): a captura PM4 (só nos comandos com bytes de anel) e a captura de texturas (só nos draws). Esta fase cronometra as mesmas duas regiões em nanossegundos (`fe_pm4`, `fe_textures`). Comparação por execução (ms por quadro):

| Execução | `capture` (µs) | `fe_pm4` | `fe_textures` | `fe_pm4` + `fe_textures` | diferença | razão | resíduo antigo (`fe_end` − `capture` − `fe_push`) | `fe_end_rest` | diferença ÷ resíduo antigo |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `tl_fp1` | 7,50 | 4,24 | 5,76 | 10,00 | +2,50 | 1,33 | 3,24 | 0,75 | 77% |
| `tl_fp2` | 7,08 | 3,86 | 5,42 | 9,28 | +2,20 | 1,31 | 2,89 | 0,70 | 76% |
| `tl_fp3` | 8,01 | 4,40 | 6,01 | 10,41 | +2,40 | 1,30 | 3,20 | 0,80 | 75% |
| `tl_fp4` | 7,54 | 4,06 | 5,68 | 9,74 | +2,20 | 1,29 | 2,94 | 0,74 | 75% |

**Resultado.** Os cronômetros em nanossegundos somam 2,20 a 2,50 ms a mais que o `capture` em microssegundos (29% a 33% do `capture`), nas quatro execuções, e o `fe_end_rest` fica em 0,70 a 0,80 ms (0,26 a 0,29 µs por draw). Essa diferença corresponde a 75% a 77% do resíduo antigo de 2,89 a 3,24 ms (que, nestas execuções, é a mesma grandeza dos ≈2,8 a 3,0 ms da Fase 0.5 e dos 2,76 a 3,02 ms da 1.1a); sobram 0,70 a 0,80 ms sem atribuição. O resultado é consistente nas quatro execuções (a razão varia de 1,29 a 1,33).

**O que isso sustenta e o que não decide.** Os dados sustentam que o `capture` em microssegundos **subestimava** a captura em **no máximo** 2,2 a 2,5 ms por quadro: a diferença entre os cronômetros em ns e em µs é esse valor, mas parte dela pode ser custo das leituras de relógio do modo detalhado dentro dos cronômetros em nanossegundos, e então a subestimação por truncação é menor. Com o teto de relógio estimado abaixo (até 0,70 ms; estimativa, não medida), a truncação seria de pelo menos ≳1,6 a 1,8 ms por execução (2,20 a 2,50 ms menos 0,62 a 0,70 ms). Duas fontes podem produzir uma diferença desse sinal e os números sozinhos não as separam: (a) a truncação para microssegundos inteiros em cada chamada (perda de até 1 µs, em média cerca de 0,5 µs se as frações forem distribuídas de modo uniforme, o que não foi verificado) e (b) o custo das leituras de relógio do modo detalhado, que entram nos cronômetros em nanossegundos. Estimativas, não medidas:

- *Truncação.* Há no máximo uma região de textura por draw e uma de PM4 por comando, então as regiões cronometradas são no máximo `draws=` + `packets=`: 5133 a 5854 por quadro (5853, 5182, 5854 e 5133 em `tl_fp1` a `tl_fp4`). Isto é uma estimativa que trata o `packets=` do worker como a contagem de comandos do front-end; o número real de regiões não foi contado. Com perda média de 0,5 µs por região, daria 2,6 a 2,9 ms; a diferença observada de 2,20 a 2,50 ms implica uma perda média de 0,41 a 0,43 µs por região, supondo que todas essas regiões foram cronometradas (com menos regiões, a perda média por região teria de ser maior, e não pode passar de 1 µs). Isso é compatível com a hipótese da truncação (e com a estimativa de ≈2,6 ms da 1.1a), mas não a prova.
- *Relógio.* Pela leitura do código, as duas medições contêm cada uma cerca de uma leitura de relógio da outra (o intervalo em microssegundos contém a leitura de início do escopo em nanossegundos; o intervalo em nanossegundos contém a leitura de fim do cronômetro em microssegundos), então esse custo em grande parte se cancela na diferença. Mesmo tomando o teto de 2 a 4 leituras de relógio por região a 20 a 30 ns cada (custo de leitura **não medido**), o efeito seria de 0,21 a 0,35 ms (2 leituras) a 0,41 a 0,70 ms (4 leituras), menos de um terço da diferença observada. Uma parte desse mesmo efeito, cerca de uma leitura de relógio por região (0,10 a 0,18 ms nas mesmas contas), fica **fora** dos escopos em nanossegundos (a leitura de início dos cronômetros em microssegundos) e portanto dentro do `fe_end_rest`; isso é a mesma contabilidade de ≈1 leitura dentro e ≈1 fora por escopo, definida na divisão do `fs_plan`, aplicada aos cronômetros em µs.

Conclusão para o resíduo: a truncação é a explicação compatível com os números para a maior parte dos ≈2,8 ms (75% a 77%, no máximo, pelo mesmo motivo); a estimativa do efeito do relógio (não medida) fica abaixo de um terço da diferença, mas esta fase não separou as duas fontes por medida, e os 0,70 a 0,80 ms do `fe_end_rest` não foram decompostos (contêm `checked_guest_reads.Reset()`, o tratamento do `capture_mirror_`, a marcação do resolve, a decisão e a chamada do `FlushBatch`, o restante do próprio `EndCmd` e parte das leituras de relógio acima; quanto de cada um não foi medido).

**Correções aos números das seções anteriores (sem reescrever o histórico).** As tabelas e textos anteriores usaram o `capture` em microssegundos, que tende a ser um limite inferior (subestima por no máximo 2,2 a 2,5 ms, ver acima). Nas quatro execuções desta fase, o `fe_pm4` + `fe_textures` é 9,28 a 10,41 ms (média 9,86), contra 7,08 a 8,01 ms (média 7,53) do `capture`; esse 9,28 a 10,41 ms é uma **estimativa superior** da captura no modo detalhado, porque os cronômetros em nanossegundos incluem parte do custo das leituras de relógio do próprio modo (a captura verdadeira fica entre o `capture` em µs mais a truncação e esse valor), e a fração do `game` ocupada pela captura é 24,3% a 25,0% pelo valor em ns (também um teto), contra 18,6% a 19,3% pelo `capture`. A diferença foi medida no modo detalhado, com perfil; aplicá-la às tabelas da Fase 0 e da 0.5 (sem cronômetros em ns, e fora do modo detalhado nas da Fase 0) é só uma estimativa, porque a truncação depende da distribuição das durações e da cena. Isto atinge as afirmações que dependem do valor truncado: o "teto" de 6,59 e 7,16 ms da captura (Fase 0, "Ordem recomendada"), o "19,5% e 18,8% do `game`" (Fase 0.5), o `capture` de 2,5 µs por draw (1.1a; agora, como estimativa superior em modo detalhado, 3,47 a 3,85 µs por draw, média 3,64, com `fe_pm4` 1,53 e `fe_textures` 2,11) e a frase "sem a captura, a média do `game` (~28 a 30 ms)": nestas execuções, `game` menos (`fe_pm4` + `fe_textures`), que seria um valor mínimo pelo mesmo motivo, é 28,85 a 32,03 ms, contra 31,06 a 34,43 ms de `game_other` (cujo `capture` é o truncado). Essas execuções têm perfil ligado, então o `game` delas é maior que o das tabelas da Fase 0, e os valores não são diretamente comparáveis. A conclusão da Fase 0.5 de que cerca de um terço do `game_other` é código do renderer medido por hook continua valendo com o valor em nanossegundos: (`frontend` − (`fe_pm4` + `fe_textures`)) ÷ (`game` − (`fe_pm4` + `fe_textures`)) dá 34,1%, 32,2%, 32,7% e 32,8% nas quatro execuções. O `game_guest` não depende do `capture` e fica em 19,57 a 21,55 ms (49% a 51% do `game`).

**Texturas.** O `fe_textures` é 5,42 a 6,01 ms (média 5,72), 2,11 µs por draw, o maior bloco cronometrado do front-end depois do `fe_end` inteiro, maior que o `fs_plan` (3,42 ms). A amostra da seção "O hash quase não pesa na captura do Vulkan atual" (`textures_ms` ≈ 3,89 ms, `pm4_ms` ≈ 1,78 ms) vem de outras builds, de uma amostra por 120 quadros e de milissegundos inteiros por quadro, e fica abaixo dos valores desta fase (o `fe_pm4` de 3,86 a 4,40 ms é mais de duas vezes o `pm4_ms` amostrado); a diferença não foi reconciliada e o sentido é o do limite inferior. **Esta fase não mediu o hash**: nenhuma afirmação sobre o custo do hash muda, e a política decidida (watch com backoff para texturas) continua como está.

### Custo do modo detalhado nesta fase e ressalvas

- **Custo do modo detalhado.** Esta fase acrescenta aos escopos da 1.1a quatro regiões em nanossegundos (duas leituras de relógio cada): `fe_pm4` e `fe_textures` por comando e por draw, e `fs_resolve` e `fs_buffer` por avaliação lenta. Comparação com `tl_fd3/fd4` (mesmo modo, também com perfil; médias das duas): `game` 39,93 ms (38,13 a 42,44 nas quatro) contra 39,78; `frontend` 19,77 contra 19,63; `fe_end` 10,90 contra 10,91; `fe_streams` 4,20 contra 3,93; `fs_plan` 3,42 contra 3,15; draws por quadro 2714 contra 2630. As diferenças do `game` (+0,15 ms) e do `frontend` (+0,14 ms) são muito menores que a variação entre as quatro execuções desta fase (4,31 ms no `game`), então o custo dos escopos novos não é separável da variação de cena. O `fs_plan` +0,27 ms (e o `fe_streams` +0,27 ms, o mesmo aumento) tem a ordem de grandeza do custo estimado das leituras dos dois escopos novos dentro dele (2 escopos × 2 leituras × 2098 a 2480 avaliações lentas = 8400 a 9900 leituras por quadro × 20 a 30 ns = 0,17 a 0,30 ms; as duas leituras de cada escopo novo caem dentro do intervalo do `fs_plan`), mas a cena também difere (+84 draws por quadro, +3%), então isso é compatibilidade, não medida. Estimativa do teto das leituras de relógio dos quatro escopos novos: 9,3 a 10,8 mil regiões por quadro (5133 a 5854 de PM4 e textura, estimativa que trata o `packets=` do worker como a contagem de comandos do front-end, mais o dobro das avaliações lentas, 4196 a 4960) × 2 leituras × 20 a 30 ns ≈ 0,37 a 0,65 ms por quadro. Pela contabilidade da divisão do `fs_plan`, as 2 leituras de cada região caem dentro do estágio pai que a contém (`fe_end`, `fs_plan`, `fe_streams`, `frontend`), ≈1 delas dentro do intervalo da própria região e ≈1 fora. O custo de uma leitura de relógio continua não medido.
- **Cena variável e draws por quadro.** As quatro execuções diferem em `game` por até 4,31 ms e em draws por quadro por até 358 (2529 a 2887); a fração `slow`/`evaluated` ficou estável (61,5% a 62,5%), mas é o mesmo roteiro de bench em quatro passadas, não cenas distintas, e a cena varia entre execuções. Os µs por draw usam os `draws=` do perfil das mesmas execuções, pareando as 9 últimas janelas de cada contador e de cada perfil pela posição; um deslocamento de alguns quadros entre as janelas do contador e as do perfil não foi verificado.
- **Execuções com perfil.** Como na 1.1a, estas execuções têm `-Profile` ligado e o `game` delas é maior (a média de 39,93 ms destas quatro execuções é cerca de +2,9 ms contra os 37,01 ms de `tl_fd1/fd2`), mas esse aumento é o custo do perfil **mais** a variação de cena e os dois não foram separados; valores absolutos desta seção não são comparáveis com as tabelas sem perfil. Valem as razões e frações.
- **Uma máquina e quatro execuções.** Notebook Intel UHD, i5-13420H; a classificação do motivo toma o primeiro campo diferente na ordem testada e o contador não guarda a identidade dos buffers; `dirty` inclui o desvio de depuração `sr_native_debug_buffers_always_dirty`, desligado por padrão.

### Candidatos da Fase 1.1c (reordenados com os novos dados)

O teto de cada item é a média do próprio estágio nas quatro execuções (ms por quadro e µs por draw com os `draws=` de cada execução), que só seria alcançado se o estágio inteiro sumisse; quanto vira FPS depende de `worker` e `gpu` (31,66 a 35,79 ms e 30,05 a 33,82 ms nestas execuções, perto do orçamento de 33,3 ms), e esses números incluem o custo das leituras de relógio dos escopos. Todo item que muda comportamento tem de preservar a **imagem bit a bit**, a **semântica do rastreamento de buffer sujo** (`dirty`/`clean`, write-watch, `InvalidateGuestRange`) e a **precedência de leitura dos bytes capturados**. Verificação comum: o custo do estágio por draw cai (os mesmos 4 benches e a mesma tabela de µs por draw); `tests/native` e `python -m pytest tests/tools -q` passam; gate de imagem (`-Gate record` na build antiga, `-Gate check` na nova). Nenhum item toca a política de hash ou de watch das texturas, já decidida.

1. **Medir: repetição de buffers por slot e desfecho do ramo lento** (estágio: `fs_plan`, teto 3,42 ms, 1,26 µs/draw). É a primeira medida do motivo `address`. Por slot e por quadro, contar os endereços-base distintos e simular, sobre a sequência de draws, o acerto de um cache hipotético de 2 e de 4 entradas por slot (a simulação com 1 entrada tem de reproduzir os `fast` medidos, 1309 a 1486 por quadro, o que valida a contagem); contar também o desfecho do `PlanBuffer` nos desvios (`action` 0, 1 ou 2, e se o `tracked_` achou o buffer), **e se o `tracked_` achado pelo ramo lento está sujo** (a `action` 0 mistura buffer limpo com buffer sujo cuja faixa já está coberta por `clean`; sem essa separação não se sabe quantas das avaliações `address` chegam a um buffer sujo, que nunca seria acerto de cache), e quantos `Resolve` de fato varrem índices e quantos resultados o `PlanBuffer` consome. Risco: nenhum no comportamento (só contadores no modo detalhado). Verificação: as contagens fecham com `evaluated`, `fast` e `slow`. Decide entre os itens 5 e 6; o teto de um cache maior não é o `fs_plan` inteiro, e sim `fs_plan` menos o custo dos novos acertos (≤ ≈0,1 µs por avaliação nos acertos atuais, ver acima), isto é, até ≈3,2 ms se todas as avaliações lentas virassem acertos. Esse é um limite superior **frouxo**: um alvo sujo nunca é acerto de cache (o ramo rápido exige `RefreshTrackedBuffer` falso) e a parcela de avaliações lentas que chegam a um buffer sujo é desconhecida, e o que sobra depende da repetição medida aqui.
2. **Medir: custo da sonda e contagem de regiões** (estágio: todos os `fe_*`; sem teto próprio). Contar as regiões cronometradas por quadro (PM4, textura, `fs_resolve`, `fs_buffer`) e medir o custo de uma leitura de relógio na máquina, para trocar as estimativas de 20 a 30 ns desta fase por um número e separar a truncação do relógio na diferença `capture` contra `fe_pm4` + `fe_textures`. Acumular em nanossegundos o tempo que alimenta o estágio `capture` do relatório (hoje são microssegundos inteiros por chamada, subestimados em no máximo 2,2 a 2,5 ms nestas execuções, que é a diferença bruta entre os cronômetros em ns e em µs, em modo detalhado), para que as tabelas fora do modo detalhado deixem de usar o limite inferior. Risco: nenhum na imagem; mexe só na instrumentação (fora do modo detalhado, acrescenta leituras de relógio por região). Verificação: `capture` do relatório passa a coincidir com `fe_pm4` + `fe_textures` dentro da variação.
3. **Medir: decomposição de `fe_textures` e de `fe_pm4`** (estágios: `fe_textures`, teto 5,72 ms, 2,11 µs/draw; `fe_pm4`, teto 4,14 ms, 1,53 µs/draw; juntos 9,86 ms, 3,64 µs/draw, os maiores blocos cronometrados do front-end). Nenhum dos dois foi aberto: não se sabe onde está o tempo por draw e por slot de textura (o que a seção do hash só diz que não é hash, leitura nem cópia) nem quanto do `fe_pm4` é varredura de pacotes contra cópia. Medir sub-regiões no modo detalhado, **sem** alterar a política de watch e hash. Risco: nenhum. Verificação: as sub-regiões fecham com o estágio.
4. **Medir: `fe_index` e bytes capturados por draw no `CaptureDevice`** (estágios: `fe_index`, teto 1,45 ms, 0,54 µs/draw; `fd_other` = `fe_device` − `fd_shaders`, teto 1,21 ms, 0,44 µs/draw). São os itens 3 e 4 da 1.1b, ainda sem medida: separar o `PlanBuffer` do buffer de índices (cuja ordem de grandeza, ≈1 µs por chamada nos streams, não contradiz o 0,54 µs/draw, mas a fração de draws indexados não foi contada) da normalização de `kQuadList`, e contar chamadas de `CaptureBytes` e bytes copiados. Risco: nenhum. Verificação: como na 1.1b.
5. **Adiar o `Resolve` para dentro do `PlanBuffer`, só quando a faixa é necessária** (estágio: `fs_resolve`, teto 0,95 ms, 0,35 µs/draw; só vale pelo que o item 1 mostrar sobre quantos resultados são consumidos). Hoje o `Resolve` roda antes do `PlanBuffer` em todo ramo lento com `range`, e o resultado só é lido quando o buffer achado está sujo (ver acima). Risco: baixo a médio: o `Resolve` só lê índices do guest, então calculá-lo mais tarde no mesmo draw dá o mesmo resultado, mas a faixa precisa chegar idêntica a `align_range` e ao `clean`. Verificação: `BufferPlan` (`key`, `action`, `begin`, `end`) e bytes capturados idênticos aos da build antiga (`tests/native` e gate de imagem); `fs_resolve` por draw menor.
6. **Ampliar o acerto do cache de stream** (estágio: `fs_plan`, mesmo teto de 3,42 ms, frouxo: um alvo sujo nunca é acerto de cache, e a parcela das avaliações lentas que chegam a um buffer sujo é desconhecida; só se o item 1 mostrar que há o que converter: por exemplo um cache de 2 a 4 entradas por slot, que depende de os buffers repetirem no quadro, ou uma consulta direta em `tracked_` por endereço, que não depende dessa repetição). Os dados atuais mostram que o motivo é troca de buffer no slot, mas não que a troca repita; "cache maior não ajuda" vale só para o tamanho do cache por slot, não para a consulta direta. Uma avaliação convertida continua pagando o `RefreshTrackedBuffer` (e, na primeira chamada do quadro por buffer de até 32 KiB, o hash de conteúdo), que é inerente e já está no custo do ramo rápido. Risco: **alto** no rastreamento de sujo (um acerto que pule a captura quando devia capturar quebra a imagem; o acerto precisa passar por `RefreshTrackedBuffer`, como o ramo rápido atual, e respeitar `clean` e a faixa necessária). Verificação: gate de imagem, testes de dirty-tracking e contagem de capturas (`action` 1 e 2, bytes capturados) idêntica à da build antiga. Não propor antes do item 1.
7. **Reduzir o trabalho de `CaptureDevice` fora dos shaders e as buscas de shader** (estágios: `fd_other`, teto 1,21 ms, 0,44 µs/draw; `fd_shaders`, teto 0,50 ms, 0,18 µs/draw, mais `fs_prep` 0,41 ms, 0,15 µs/draw, que usa o mesmo mutex). Só depois do item 4. Risco: médio (a precedência de leitura das faixas e a invalidação de shaders; ver os itens 7 e 8 da 1.1b). Verificação: como na 1.1b.
8. **Rebaixado: repetições dentro do ramo lento** (a segunda busca em `tracked_` e o preenchimento da entrada do slot). Os dados limitam os dois a no máximo ≈0,2 ms juntos (resto do `fs_plan` de 0,16 a 0,20 ms, que ainda contém as leituras de relógio dos escopos), e a segunda chamada de `RefreshTrackedBuffer` acontece só em cerca de 1 avaliação por quadro. O exemplo "reaproveitar o iterador" do item 5 da 1.1b não vale a mudança com esses tetos.
9. **Tetos pequenos, da ordem do custo da sonda** (`fs_prep`, `fs_other`, `fe_push`), **com `fe_end_rest` acima dele:** `fs_prep` 0,41 ms (≈0,15 µs/draw), `fs_other` 0,37 ms (≈0,14 µs/draw) e `fe_push` 0,305 ms (≈0,11 µs/draw; as quatro tabelas dão 0,31, 0,28, 0,33 e 0,30). Pela estimativa desta seção, só o escopo do `fs_prep` custa ≈2 leituras × 2530 a 2890 draws × 20 a 30 ns ≈ 0,10 a 0,17 ms (um quarto a dois quintos do seu 0,41 ms; ≈1 das 2 leituras fora do intervalo do `fs_prep`, no `fs_other`), então esses três não se distinguem com folga da sonda e não são candidatos antes dos itens acima. O `fe_end_rest` (0,75 ms, 0,28 µs/draw) está bem acima dela (a parte de leituras de relógio que ele contém é ≈0,10 a 0,18 ms nas contas da pergunta 2); só vale abri-lo se o item 2 mostrar que o relógio explica pouco dele.

**O que os dados não decidem.** Quantos buffers distintos cada slot vê por quadro e se os mesmos buffers voltam (item 1); o desfecho do `PlanBuffer` nos desvios lentos, quantos deles chegam a um `tracked_` sujo (a parcela de buffers sujos entre as avaliações `address` é desconhecida) e quantos `Resolve` são consumidos (item 1); quanto da diferença de 2,2 a 2,5 ms entre os cronômetros em µs e em ns é truncação e quanto é relógio (item 2), embora a estimativa de relógio fique abaixo de um terço dela; onde está o tempo dentro de `fe_textures` e `fe_pm4` (item 3); quanto do `fe_end_rest` é cada parte; quanto do `fe_index` é o `PlanBuffer` de índices; e quanto uma redução do `frontend` vira FPS (depende de `worker` e `gpu`). Também continuam sem medida o custo de uma leitura de relógio e o custo do hash de conteúdo de buffers de até 32 KiB dentro do `RefreshTrackedBuffer`.
