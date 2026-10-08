# Linha do tempo por quadro do renderer nativo

Ferramenta da Fase 0 do ciclo 4 (design: `superpowers/specs/2026-10-08-native-renderer-cpu-gpu-bottlenecks-design.md`).

## Como usar

- `SR_FRAME_TIMELINE=<arquivo.csv>` liga a gravação; sem ela nada é registrado.
- `tools\bench\bench_api.ps1 -Api vulkan|d3d12 -Name <nome> -Timeline` roda o bench e imprime a tabela.
- `python tools\analysis\frame_timeline_report.py <arquivo.csv> [--last N] [--json]` analisa um CSV.
- Colunas, estágios e o significado de `busy_ns`/`blocked_ns`: `docs/superpowers/plans/2026-10-08-frame-timeline-phase0.md`.

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

**O que os dados não decidem.** Entre os passos 2 e 3 no Vulkan, `worker` (13% a 14% dos quadros), `gpu` (8% a 9%) e `record` (5% a 6%) estão próximos e todos têm p99 acima do orçamento; colocar o 3 antes do 2 vem da ressalva de limite superior da GPU e da falta de dados por passe, não de uma diferença clara de custo. Também não está medido quanto do `game_other` (28,40 e 29,51 ms) é lógica do jogo e quanto é espera da thread do jogo fora do renderer (por exemplo vblank ou outra sincronização); a execução de D3D12 limitada pela GPU mostra que o `game` pode ter boa parte de espera (23,47 ms ali, contra 32,76 ms quando o limitador de 30 FPS manda). Por isso o `game` do Vulkan, com `util.` e `limitante` inflados por construção, não prova que a thread do jogo é o gargalo; só a média do `game` sem a captura (~28 a 30 ms, abaixo de 33,3 ms) e o p99 (37,85 e 37,69 ms no `game_other`, acima) estão medidos.
