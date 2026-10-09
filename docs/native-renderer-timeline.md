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

1. **`gpu` não é tempo ocupado puro.** No Vulkan a linha `gpu` vai do `TOP_OF_PIPE` do primeiro command buffer de upload ao `BOTTOM_OF_PIPE` do fim do quadro, com 2 quadros em voo; ela pode incluir fila atrás do quadro anterior. Trate o `util.` da GPU do Vulkan como **limite superior**, não como tempo ocupado [atualização da Fase 2.0: a parte "fila atrás do quadro anterior" desta ressalva não se confirmou; em 12.716 quadros medidos nas duas APIs a sobreposição entre quadros foi zero e o `gpu` é igual ao `gpu_real`; o texto original fica como foi escrito, ver "Tempo real de GPU (Fase 2.0)" no fim deste documento]. No D3D12 a linha vem dos timestamps do próprio renderer (do primeiro ao último dentro do quadro) e pode ter ressalva parecida (lacunas entre passes entram na conta).
2. **`game` inclui a lógica do próprio jogo**, não só o renderer. Ele é o intervalo entre a saída de um `OnSwap` e a entrada do seguinte, então também inclui qualquer espera da thread do jogo fora do renderer. Nas tabelas, `game` + `front_wait` fecha com o intervalo médio do quadro (por exemplo, no D3D12: 32,76 + 0,72 ≈ 33,5 ms), logo o `util.` do `game` fica perto de 100% sempre que o `OnSwap` é curto. Só o `game_other` (`game` menos `capture`, apenas no Vulkan) separa a captura do resto. No Vulkan também vale `game` + `front_wait` ≈ intervalo por construção, então o `util.` e o `limitante` do `game` ali, sozinhos, não provam que a thread do jogo é o gargalo. No D3D12 o limitador de 30 FPS segura o intervalo em ~33,4 ms, então o `game` e o seu `limitante` **não provam** que a thread do jogo é o gargalo.
3. **`limitante` é a fração dos QUADROS** em que o estágio teve o maior tempo ocupado, não uma fração ponderada pelo tempo.
4. **Uma máquina, cena variável.** Tudo vem de um notebook (Intel UHD, i5-13420H) e de uma cena que muda de uma execução para outra. Compare as razões entre as duas execuções de cada API, não os valores absolutos.
5. **Janela.** O relatório usa `--last 1100`, que é a janela dos últimos 1100 quadros (cerca de 37 s a 30 FPS; no Vulkan, com intervalos de 36–38 ms, cerca de 40 s), que cobre a fase andando e o fim da fase parado, e pode alcançar antes da fase parado. Por isso o intervalo médio do relatório pode diferir do FPS médio impresso pelo bench, que mede só a janela de 20 s de cada cenário.
6. **O tempo ocupado do `worker` pode esconder esperas.** Esperas que não estão envolvidas por um bloco de escopo próprio entram como trabalho (ocupado), não em `bloq.`. Casos conhecidos: a aquisição de `mutex_`, o `RefreshGuestOutput` do apresentador e, no Vulkan, o `WaitSlot` depois do submit, que conta como ocupado do `record`. Por isso um `bloq.` de 0,00 ms (como no D3D12) não prova que o estágio nunca espera.
7. **O aninhamento dos estágios `fs_*` e `fb_*` depende do modo do resolve do intervalo de vértices.** Com o resolve preguiçoso (padrão desde a Fase 1.1c; `SR_NATIVE_LAZY_RESOLVE=0` restaura o modo antigo), o `fs_resolve` fica **dentro** do `fs_buffer`; no modo antigo (ansioso) os dois eram irmãos dentro do `fs_plan`. Logo o `fs_buffer` não é comparável entre os dois modos. Os estágios `fb_refresh` e `fb_hash` aninham em vários pais e não se somam a nada (ver a Fase 1.1c no fim deste documento). As seções anteriores descrevem o aninhamento do modo ansioso e ficam como estão.

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

**Vulkan, medições.** O intervalo médio ficou em 35,9 e 37,7 ms (27,8 e 26,6 FPS), com 1% low de 19,2 e 18,0 FPS. O `game` mede média de 34,99 e 36,67 ms (acima do orçamento de 33,3 ms) e p99 de 48,3 e 49,1 ms (folga p99 de -15,0 e -15,7 ms), com `util.` de 97% e `limitante` em 73% e 72% dos quadros. O `front_wait` é de 0,92 e 0,95 ms. A captura é só uma parte do `game` (6,59 e 7,16 ms, 18% e 19% de `util.`, p99 de 14,5 e 14,3 ms) e o `game_other` (28,40 e 29,51 ms de média, 79% e 78%, p99 de 37,85 e 37,69 ms) é o maior bloco que existe. Os outros estágios estão abaixo do orçamento na média, mas com p99 acima dele: `worker` 29,32 e 31,55 ms (82% e 84%, limitante em 14% e 13%, folga p99 de -10,4 e -10,5 ms), `record` 25,27 e 27,72 ms (70% e 74%, limitante em 5% e 6%, folga p99 de -10,0 e -16,4 ms) e `gpu` 29,75 e 31,93 ms (83% e 85%, limitante em 8% e 9%, folga p99 de -6,1 e -3,9 ms; limite superior, ver ressalva 1 e a atualização da Fase 2.0 no fim do documento). Nenhum dos quatro estágios principais do Vulkan tem folga de p99.

**Vulkan, interpretação.** O `game` é o estágio de ritmo: ele vai da saída de um `OnSwap` à entrada do seguinte, então `game` + `front_wait` fecha com o intervalo do quadro (34,99 + 0,92 ≈ 35,9 ms; 36,67 + 0,95 ≈ 37,7 ms) por construção (ressalva 2). Com o `front_wait` em ~1 ms, o `game` é o maior dos quatro estágios na maioria dos quadros. O `util.` de 97% é o número quase tautológico (`game` + `front_wait` ≈ intervalo), e o `limitante` de 72% a 73% fica inflado pela mesma construção; nenhum dos dois prova que a thread do jogo é o gargalo. O que os dados sustentam: o `front_wait` de ~1 ms mostra que a thread do jogo não é segurada pelo renderer esperando, e o `game_other` é o maior bloco. Sem a captura, a média do `game` (~28 a 30 ms) ficaria abaixo do orçamento de 33,3 ms, e só o p99 (37,85 e 37,69 ms no `game_other`) o estouraria. Não está decidido se o `game_other` é trabalho ou espera fora do renderer (ver "O que os dados não decidem").

**D3D12.** O quadro fecha no limitador (intervalo médio de 33,5 e 33,4 ms, 29,8 e 29,9 FPS), então o `game` (98% e 92% de `util.`, limitante em 74% e 42%) não identifica o gargalo (ressalva 2). O estágio de trabalho real mais ocupado é o `worker`: 29,17 e 32,37 ms de tempo ocupado, 87% e 97% de `util.`, `bloq.` de 0,00 ms, limitante em 26% e 57% dos quadros e p99 de 47,6 e 43,5 ms (folga p99 de -14,3 e -10,2 ms). A GPU tem folga: 21,47 e 25,02 ms (64% e 75%), limitante em 0% e 1% dos quadros e folga p99 de +7,1 e +3,4 ms. O 1% low ficou em 21,7 e 22,5 FPS e o mínimo em 11,9 e 10,5 FPS.

**GPU: no Vulkan ela mede 1,28 a 1,39x a do D3D12 (limite superior, por outro método); não está provada como gargalo.** A GPU do Vulkan (29,75 e 31,93 ms) é 1,39x e 1,28x a do D3D12 (21,47 e 25,02 ms) nas duas execuções, e só no Vulkan o p99 da GPU passa do orçamento (39,38 e 37,20 ms). Mas as duas APIs medem a GPU de formas diferentes: no Vulkan vai do `TOP_OF_PIPE` até o `BOTTOM_OF_PIPE` em uma submissão nova, com 2 quadros em voo, e pode incluir a espera atrás do quadro anterior; no D3D12 vai do primeiro ao último timestamp dentro da própria command list. A razão é, portanto, um limite superior obtido por método diferente, e a GPU do Vulkan é limitante em só 8% e 9% dos quadros. A cena varia entre execuções, então vale a razão, não uma comparação de cena a cena (que não foi feita). Recomendação para a Fase 1, antes do passo 2: registrar os dois ticks de início e fim da GPU do Vulkan, para subtrair a sobreposição com o quadro anterior (ocupada(N) = fim(N) − max(início(N), fim(N−1))). [Atualização da Fase 2.0: isso foi feito (`gpu_real`, `gpu_idle`); a sobreposição medida foi zero e a diferença de `gpu_real` entre as APIs sobreviveu, 7,6 a 10,3 ms; ver "Tempo real de GPU (Fase 2.0)" no fim deste documento.]

**Ordem recomendada para a Fase 1 (passos da tabela da seção 5 do design).**

1. **Passo 1, hash paralelo exato: primeiro.** A captura é compartilhada pelas duas APIs. No Vulkan ela está dentro do `game`, o estágio de ritmo (`util.` quase tautológico; `limitante` inflado pela mesma construção, ressalva 2) e o único com média acima do orçamento. Mas o teto do ganho é pequeno: a captura mede 6,59 e 7,16 ms por quadro (menos que os 8 a 13 ms de hash citados no design, e o relatório não separa o hash dentro dela), e restariam `game_other` de 28,40 e 29,51 ms, com p99 de 37,85 e 37,69 ms. O passo 1 sozinho não leva o Vulkan à meta (o p99 do `game_other` continua acima de 33,3 ms), embora a média sem a captura ficasse abaixo do orçamento. No D3D12 o hash roda dentro do `worker`, que a tabela não decompõe, então o ganho ali precisa ser medido.
2. **Passo 3, pipeline entre estágios: segundo.** No D3D12 o `worker` (87% e 97% de `util.`) é o estágio de trabalho mais ocupado, e o design prevê separar preparo e gravação ali. No Vulkan o `game` sozinho passa de 33 ms na média (a regra do design manda o trabalho ir para dentro do estágio, não para o pipeline), mas esse valor inclui o ritmo do quadro (ressalva 2); `worker` e `record` têm p99 acima do orçamento e já são threads separadas, então aumentar a profundidade entre replay e gravação ataca a cauda deles. O `front_wait` médio é de 0,92 e 0,95 ms no Vulkan e o bench reporta cerca de 7 threads lógicas em uso (campo `cores`, 7 a 7,2 nas execuções do Vulkan) de 12 (o processador tem 8 núcleos, 4P+4E), o que deixa threads livres. No D3D12 a soma serial NÃO foi medida: o `game` do D3D12 inclui a espera do limitador de 30 FPS (ressalva 2), então não dá para somá-lo ao `worker`. O `worker` sozinho fica em 29,17 e 32,37 ms de média (de 29 a 32 ms; p99 de 47,6 e 43,5 ms). A metade "núcleos ociosos" da condição do design ("soma serial > 33 ms com núcleos ociosos") vale: o bench reporta 6,1 a 6,4 threads lógicas de 12 em uso nas execuções do D3D12. A separação preparo/gravação do passo 3 ali se apoia no `worker` de 29 a 32 ms, não numa soma serial medida.
3. **Passo 2, itens de GPU bit a bit: terceiro, e só no Vulkan.** No D3D12 a GPU não está entre os limitantes (0% e 1%, folga p99 positiva), então o passo não entra lá. No Vulkan a GPU tem p99 acima do orçamento, mas é limitante em só 8% a 9% dos quadros, o `util.` é limite superior (a Fase 2.0 levantou esta parte, ver o fim do documento; a falta de medição por passe continua), e esta fase não mediu por grupo de passes, então não dá para escolher os itens do passo 2 sem uma medição por passe.
4. **Passo 4, estabilidade do 1% low: necessário qualquer que seja a ordem.** O 1% low ficou entre 18,0 e 22,5 FPS e o mínimo entre 1,8 e 11,9 FPS nas quatro execuções; `game`, `worker`, `record` e `gpu` no Vulkan e `game` e `worker` no D3D12 têm p99 acima de 33,3 ms. Mesmo no D3D12, com a média em 29,3 a 30 FPS, os mínimos do bench ficaram entre 25,8 e 29,7 FPS.

**O que os dados não decidem.** Entre os passos 2 e 3 no Vulkan, `worker` (13% a 14% dos quadros), `gpu` (8% a 9%) e `record` (5% a 6%) estão próximos e todos têm p99 acima do orçamento; colocar o 3 antes do 2 vem da ressalva de limite superior da GPU (levantada pela Fase 2.0, ver o fim do documento) e da falta de dados por passe (continua), não de uma diferença clara de custo. Também não está medido quanto do `game_other` (28,40 e 29,51 ms) é lógica do jogo e quanto é espera da thread do jogo fora do renderer (por exemplo vblank ou outra sincronização; a Fase 0.5, no fim deste documento, mediu a parte que é código do renderer medido por hook na thread do jogo, cerca de um terço do `game_other` no Vulkan, e o restante, o `game_guest`, continua sem separação entre lógica, driver e espera); a execução de D3D12 limitada pela GPU mostra que o `game` pode ter boa parte de espera (23,47 ms ali, contra 32,76 ms quando o limitador de 30 FPS manda). Por isso o `game` do Vulkan, com `util.` e `limitante` inflados por construção, não prova que a thread do jogo é o gargalo; só a média do `game` sem a captura (~28 a 30 ms, abaixo de 33,3 ms) e o p99 (37,85 e 37,69 ms no `game_other`, acima) estão medidos.

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

*Nota da Fase 1.1c (o texto acima descreve o modo ansioso, o único que existia na 1.1b):* desde o resolve preguiçoso o `fs_resolve` aninha dentro do `fs_buffer`, não ao lado dele; ver a ressalva 7 do início do documento e a seção "Resolve preguiçoso (Fase 1.1c)".

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

**Resultado.** Os cronômetros em nanossegundos somam 2,20 a 2,50 ms a mais que o `capture` em microssegundos (29% a 33% do `capture`), nas quatro execuções, e o `fe_end_rest` fica em 0,70 a 0,80 ms (0,26 a 0,29 µs por draw). Essa diferença corresponde a, no máximo, 75% a 77% do resíduo antigo de 2,89 a 3,24 ms (que, nestas execuções, é a mesma grandeza dos ≈2,8 a 3,0 ms da Fase 0.5 e dos 2,76 a 3,02 ms da 1.1a); sobram 0,70 a 0,80 ms sem atribuição. O resultado é consistente nas quatro execuções (a razão varia de 1,29 a 1,33).

**O que isso sustenta e o que não decide.** Os dados sustentam que o `capture` em microssegundos **subestimava** a captura em **no máximo** 2,2 a 2,5 ms por quadro: a diferença entre os cronômetros em ns e em µs é esse valor, mas parte dela pode ser custo das leituras de relógio do modo detalhado dentro dos cronômetros em nanossegundos, e então a subestimação por truncação é menor. Com o teto de relógio estimado abaixo (até 0,70 ms; estimativa, não medida), se o teto de relógio estimado valer, a truncação seria ≳1,6 a 1,8 ms (por execução: 1,80, 1,58, 1,70 e 1,58 ms, que são a diferença de 2,50, 2,20, 2,40 e 2,20 ms menos o teto de cada execução, 0,70, 0,62, 0,70 e 0,62 ms, em `tl_fp1` a `tl_fp4`). Duas fontes podem produzir uma diferença desse sinal e os números sozinhos não as separam: (a) a truncação para microssegundos inteiros em cada chamada (perda de até 1 µs, em média cerca de 0,5 µs se as frações forem distribuídas de modo uniforme, o que não foi verificado) e (b) o custo das leituras de relógio do modo detalhado, que entram nos cronômetros em nanossegundos. Estimativas, não medidas:

- *Truncação.* Há no máximo uma região de textura por draw e uma de PM4 por comando, então as regiões cronometradas são no máximo `draws=` + `packets=`: 5133 a 5854 por quadro (5853, 5182, 5854 e 5133 em `tl_fp1` a `tl_fp4`). Isto é uma estimativa que trata o `packets=` do worker como a contagem de comandos do front-end; o número real de regiões não foi contado. Com perda média de 0,5 µs por região, daria 2,6 a 2,9 ms; a diferença observada de 2,20 a 2,50 ms implica uma perda média de 0,41 a 0,43 µs por região, supondo que todas essas regiões foram cronometradas (com menos regiões, a perda média por região teria de ser maior, e não pode passar de 1 µs). Isso é compatível com a hipótese da truncação (e com a estimativa de ≈2,6 ms da 1.1a), mas não a prova. Se o relógio contribuir com até ≈0,7 ms (o teto estimado abaixo), a perda implícita por região cai para ≈0,29 a 0,31 µs (1,58 a 1,80 ms ÷ 5133 a 5854 regiões, por execução), abaixo dos 0,5 µs esperados para frações uniformes; então "compatível" é a leitura mais forte que os números permitem, e uma perda média menor que 0,5 µs também pode refletir regiões não cronometradas ou frações não uniformes.
- *Relógio.* Pela leitura do código, as duas medições contêm cada uma cerca de uma leitura de relógio da outra (o intervalo em microssegundos contém a leitura de início do escopo em nanossegundos; o intervalo em nanossegundos contém a leitura de fim do cronômetro em microssegundos), então esse custo em grande parte se cancela na diferença. Mesmo tomando o teto de 2 a 4 leituras de relógio por região a 20 a 30 ns cada (custo de leitura **não medido**), o efeito seria de 0,21 a 0,35 ms (2 leituras) a 0,41 a 0,70 ms (4 leituras), menos de um terço da diferença observada. Uma parte desse mesmo efeito, cerca de uma leitura de relógio por região (0,10 a 0,18 ms nas mesmas contas), é a leitura que cai **fora** dos escopos em nanossegundos `fe_pm4` e `fe_textures` (no pai, `fe_end`) e portanto dentro do `fe_end_rest`; isso é a mesma contabilidade de ≈1 leitura dentro e ≈1 fora por escopo, definida na divisão do `fs_plan`, aplicada a esses dois escopos em ns (não aos cronômetros em µs).

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
9. **Tetos pequenos, nos quais a sonda pesa uma fração apreciável** (`fs_prep`, `fs_other`, `fe_push`), **com `fe_end_rest` acima dele:** `fs_prep` 0,41 ms (≈0,15 µs/draw), `fs_other` 0,37 ms (≈0,14 µs/draw) e `fe_push` 0,305 ms (≈0,11 µs/draw; as quatro tabelas dão 0,31, 0,28, 0,33 e 0,30). Pela contabilidade desta seção (≈1 leitura de relógio dentro do intervalo do próprio escopo e ≈1 fora, no pai; 20 a 30 ns por leitura, **não medido**), por escopo cronometrado: o `fs_prep` (2530 a 2890 draws por quadro) custa ≈0,10 a 0,17 ms no total (2 leituras × 2530 a 2890 × 20 a 30 ns), dos quais ≈0,05 a 0,09 ms dentro do `fs_prep` (cerca de um oitavo a um quinto do seu 0,41 ms: 12% a 21%) e ≈0,05 a 0,09 ms fora dele, no `fs_other`. O `fs_other` também recebe a leitura de fora do `fs_plan` (≈1 por avaliação lenta, 2098 a 2480 por quadro, ≈0,04 a 0,07 ms), o que dá ≈0,09 a 0,16 ms de sonda no `fs_other` (25% a 44% do seu 0,37 ms; sem contar as leituras do próprio `fe_streams`, que não foram contadas). O `fe_push` tem um escopo por comando; tomando o `packets=` dos logs (2647 a 3038 na última linha de cada execução) como a contagem de comandos, o que não foi verificado, são ≈0,05 a 0,09 ms dentro do `fe_push` (17% a 30% do seu 0,305 ms) e outro tanto fora dele, no `fe_end_rest`. Esses números sustentam apenas que, por essa estimativa, a sonda pode ser de cerca de um oitavo a pouco mais de dois quintos (12% a 44%) de cada um dos três tetos, e que o restante (a maior parte, mas não medida em cada um) é trabalho não decomposto; como cada teto é de no máximo 0,41 ms, eles não são candidatos antes dos itens acima. O `fe_end_rest` (0,75 ms, 0,28 µs/draw) está acima dela (a parte de leituras de relógio que ele contém é ≈0,10 a 0,18 ms nas contas da pergunta 2, 13% a 24% do seu valor, mais as leituras de fora do `fe_push` acima); só vale abri-lo se o item 2 mostrar que o relógio explica pouco dele.

**O que os dados não decidem.** Quantos buffers distintos cada slot vê por quadro e se os mesmos buffers voltam (item 1); o desfecho do `PlanBuffer` nos desvios lentos, quantos deles chegam a um `tracked_` sujo (a parcela de buffers sujos entre as avaliações `address` é desconhecida) e quantos `Resolve` são consumidos (item 1); quanto da diferença de 2,2 a 2,5 ms entre os cronômetros em µs e em ns é truncação e quanto é relógio (item 2), embora a estimativa de relógio fique abaixo de um terço dela; onde está o tempo dentro de `fe_textures` e `fe_pm4` (item 3); quanto do `fe_end_rest` é cada parte; quanto do `fe_index` é o `PlanBuffer` de índices; e quanto uma redução do `frontend` vira FPS (depende de `worker` e `gpu`). Também continuam sem medida o custo de uma leitura de relógio e o custo do hash de conteúdo de buffers de até 32 KiB dentro do `RefreshTrackedBuffer`.

## Resolve preguiçoso (Fase 1.1c)

Plano: `superpowers/plans/2026-10-08-lazy-vertex-range.md`. A Fase 1.1b mostrou que o `fs_resolve` (≈0,95 ms por quadro) varria os índices de todo desvio lento com `VertexRange`, mas que o `PlanBuffer` só lê o intervalo quando acha o buffer em `tracked_` **e** ele está sujo. Esta fase muda o código: o `PlanBuffer` recebe o `VertexRange` e chama o `Resolve()` só nesse ramo (commit `0e458e9`). O desligador `SR_NATIVE_LAZY_RESOLVE=0` restaura o `Resolve()` ansioso no `PlanStreams` no mesmo binário, o que permite o A/B abaixo. A mesma fase acrescenta (commit `bed8aa8`) os estágios `fb_refresh` (todo o `RefreshTrackedBuffer`) e `fb_hash` (só o hash de conteúdo dentro dele) e, na linha `native front-end stream plan`, os contadores `buffer refresh: calls= hashes= hash_kb=`. Máquina e condições são as da Fase 0 (Intel UHD, i5-13420H, 1280x720, vsync, limite de 30 FPS), 2026-10-08, build release, Vulkan.

**Procedência da build.** Não houve recompilação nesta tarefa. O `superman_returns.exe` tem data de modificação 22:14:27; os arquivos C++ dos commits `0e458e9` e `bed8aa8` têm data anterior a ela (o mais novo, `native_renderer.cpp`, 22:14:02; `frame_timeline.h`, 22:13:33), o commit `bed8aa8` foi criado às 22:16:48, a árvore de trabalho estava limpa no início e o executável contém as cadeias `fb_refresh`, `buffer refresh: calls=` e `SR_NATIVE_LAZY_RESOLVE`. A árvore do executável foi assumida idêntica à do commit, sem teste de árvore limpa (mesma ressalva das fases anteriores).

### Medições (A/B no mesmo binário, ordem OFF, ON, ON, OFF)

OFF é `SR_NATIVE_LAZY_RESOLVE=0` (resolve ansioso, o comportamento da 1.1b); ON é o padrão (preguiçoso). Quatro benches Vulkan, um por vez, sem compilar nada em paralelo (outra carga na máquina não foi medida nem controlada), todos com `-Timeline -TimelineDetail -Profile` (`lz_off1`, `lz_on1`, `lz_on2`, `lz_off2`). Nenhuma falha de infraestrutura e nenhuma repetição. FPS do bench, média (mínimo), e intervalo médio do relatório:

| Execução | Modo | parado | andando | Intervalo médio do relatório |
| --- | --- | --- | --- | --- |
| `lz_off1_vulkan` | OFF | 26,4 (22,7) | 27,1 (26,1) | 37,4 ms (26,7 FPS) |
| `lz_on1_vulkan` | ON | 26,1 (21,4) | 25,1 (23,9) | 38,9 ms (25,7 FPS) |
| `lz_on2_vulkan` | ON | 24,7 (20,9) | 23,8 (21,5) | 41,3 ms (24,2 FPS) |
| `lz_off2_vulkan` | OFF | 22,5 (18) | 24,6 (22,9) | 42,8 ms (23,4 FPS) |

As quatro tabelas do relatório (janela dos últimos 1100 quadros):

`lz_off1_vulkan` (OFF):

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 37.4 ms = 26.7 FPS | 1% low 19.6 FPS | mínimo 1.8 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio           média     p50     p99   bloq.   util.  folga p99  limitante
game              36.85   36.74   49.68    0.00     99%     -16.35        80%
game_guest        18.63   18.40   26.18    0.00     50%       7.16          -
game_other        30.13   29.96   38.92    0.00     81%      -5.59          -
capture            6.72    6.51   13.15    0.00     18%      20.18          -
frontend          18.23   18.17   26.67    0.00     49%       6.66          -
fe_begin           0.45    0.44    0.65    0.00      1%      32.68          -
fe_ring            0.27    0.25    0.60    0.00      1%      32.74          -
fe_device          1.51    1.52    2.06    0.00      4%      31.27          -
fe_index           1.45    1.43    2.18    0.00      4%      31.15          -
fe_streams         4.11    4.14    5.87    0.00     11%      27.47          -
fe_end             9.84    9.67   16.74    0.00     26%      16.60          -
fe_flush           0.06    0.05    0.23    0.00      0%      33.11          -
fd_shaders         0.43    0.43    0.63    0.00      1%      32.71          -
fs_prep            0.36    0.35    0.51    0.00      1%      32.82          -
fs_plan            3.36    3.40    4.94    0.00      9%      28.39          -
fe_push            0.26    0.26    0.38    0.00      1%      32.95          -
fe_pm4             3.71    3.65    5.50    0.00     10%      27.83          -
fe_textures        5.19    4.83   10.66    0.00     14%      22.67          -
fs_resolve         0.87    0.85    1.35    0.00      2%      31.98          -
fs_buffer          2.33    2.41    3.60    0.00      6%      29.73          -
fb_refresh         2.71    2.78    3.87    0.00      7%      29.47          -
fb_hash            1.42    1.45    2.23    0.00      4%      31.10          -
frontend_other     0.61    0.60    0.94    0.00      2%      32.40          -
fe_end_rest        0.67    0.66    0.97    0.00      2%      32.36          -
front_wait         0.49    0.00    0.00    0.00      1%      33.33          -
worker            30.91   31.12   43.30    1.65     83%      -9.97        12%
record            26.87   26.34   44.72    1.96     72%     -11.38         4%
gpu               30.14   31.21   38.49    0.00     81%      -5.16         4%
```

`lz_on1_vulkan` (ON):

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 38.9 ms = 25.7 FPS | 1% low 19.7 FPS | mínimo 2.0 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio           média     p50     p99   bloq.   util.  folga p99  limitante
game              38.41   38.61   49.45    0.00     99%     -16.12        78%
game_guest        19.67   19.64   25.78    0.00     51%       7.55          -
game_other        31.16   31.25   38.80    0.00     80%      -5.47          -
capture            7.25    7.00   13.96    0.00     19%      19.38          -
frontend          18.74   18.96   26.70    0.00     48%       6.64          -
fe_begin           0.48    0.49    0.70    0.00      1%      32.63          -
fe_ring            0.29    0.27    0.64    0.00      1%      32.70          -
fe_device          1.67    1.73    2.20    0.00      4%      31.13          -
fe_index           1.45    1.43    2.14    0.00      4%      31.19          -
fe_streams         3.56    3.76    4.95    0.00      9%      28.39          -
fe_end            10.61   10.49   17.59    0.00     27%      15.74          -
fe_flush           0.06    0.05    0.18    0.00      0%      33.15          -
fd_shaders         0.48    0.48    0.69    0.00      1%      32.64          -
fs_prep            0.39    0.40    0.54    0.00      1%      32.79          -
fs_plan            2.75    2.92    3.93    0.00      7%      29.40          -
fe_push            0.29    0.29    0.41    0.00      1%      32.93          -
fe_pm4             4.05    4.08    5.85    0.00     10%      27.48          -
fe_textures        5.56    5.20   11.89    0.00     14%      21.45          -
fs_resolve         0.00    0.00    0.01    0.00      0%      33.33          -
fs_buffer          2.59    2.74    3.73    0.00      7%      29.60          -
fb_refresh         2.88    2.96    4.17    0.00      7%      29.16          -
fb_hash            1.56    1.62    2.44    0.00      4%      30.90          -
frontend_other     0.67    0.67    1.00    0.00      2%      32.33          -
fe_end_rest        0.72    0.72    0.98    0.00      2%      32.35          -
front_wait         0.46    0.00    0.00    0.00      1%      33.33          -
worker            32.61   33.25   45.97    1.58     84%     -12.64        12%
record            28.15   27.90   43.64    1.97     72%     -10.31         5%
gpu               31.71   33.40   38.31    0.00     81%      -4.97         5%
```

`lz_on2_vulkan` (ON):

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 41.3 ms = 24.2 FPS | 1% low 17.9 FPS | mínimo 1.9 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio           média     p50     p99   bloq.   util.  folga p99  limitante
game              40.53   40.76   53.51    0.00     98%     -20.18        78%
game_guest        20.53   20.64   26.54    0.00     50%       6.79          -
game_other        32.60   32.83   41.61    0.00     79%      -8.28          -
capture            7.94    7.57   14.75    0.00     19%      18.58          -
frontend          20.00   20.11   28.73    0.00     48%       4.60          -
fe_begin           0.52    0.52    0.76    0.00      1%      32.58          -
fe_ring            0.31    0.28    0.61    0.00      1%      32.72          -
fe_device          1.78    1.84    2.29    0.00      4%      31.05          -
fe_index           1.56    1.54    2.27    0.00      4%      31.07          -
fe_streams         3.71    3.90    5.07    0.00      9%      28.26          -
fe_end            11.39   11.15   18.57    0.00     28%      14.76          -
fe_flush           0.06    0.05    0.20    0.00      0%      33.13          -
fd_shaders         0.53    0.53    0.72    0.00      1%      32.61          -
fs_prep            0.43    0.43    0.59    0.00      1%      32.75          -
fs_plan            2.82    2.97    3.97    0.00      7%      29.37          -
fe_push            0.32    0.32    0.45    0.00      1%      32.88          -
fe_pm4             4.38    4.37    6.17    0.00     11%      27.16          -
fe_textures        5.91    5.40   12.49    0.00     14%      20.85          -
fs_resolve         0.00    0.00    0.01    0.00      0%      33.33          -
fs_buffer          2.64    2.78    3.76    0.00      6%      29.58          -
fb_refresh         2.96    3.07    4.09    0.00      7%      29.24          -
fb_hash            1.55    1.59    2.33    0.00      4%      31.00          -
frontend_other     0.73    0.72    1.10    0.00      2%      32.24          -
fe_end_rest        0.78    0.79    1.10    0.00      2%      32.24          -
front_wait         0.71    0.00    2.91    0.00      2%      30.42          -
worker            34.97   35.46   49.60    1.94     85%     -16.27        14%
record            30.24   29.26   47.14    2.08     73%     -13.81         5%
gpu               32.73   34.50   40.24    0.00     79%      -6.91         2%
```

`lz_off2_vulkan` (OFF):

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 42.8 ms = 23.4 FPS | 1% low 17.6 FPS | mínimo 2.0 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio           média     p50     p99   bloq.   util.  folga p99  limitante
game              42.11   42.01   54.60    0.00     98%     -21.27        82%
game_guest        20.46   20.43   25.79    0.00     48%       7.54          -
game_other        33.80   33.68   42.20    0.00     79%      -8.87          -
capture            8.31    7.86   16.12    0.00     19%      17.21          -
frontend          21.65   21.35   30.64    0.00     51%       2.70          -
fe_begin           0.53    0.53    0.73    0.00      1%      32.60          -
fe_ring            0.32    0.29    0.61    0.00      1%      32.72          -
fe_device          1.80    1.80    2.22    0.00      4%      31.11          -
fe_index           1.74    1.70    2.45    0.00      4%      30.88          -
fe_streams         4.69    4.64    6.09    0.00     11%      27.24          -
fe_end            11.84   11.41   19.89    0.00     28%      13.45          -
fe_flush           0.07    0.05    0.24    0.00      0%      33.10          -
fd_shaders         0.53    0.53    0.71    0.00      1%      32.62          -
fs_prep            0.43    0.43    0.58    0.00      1%      32.76          -
fs_plan            3.79    3.74    4.98    0.00      9%      28.35          -
fe_push            0.32    0.32    0.42    0.00      1%      32.92          -
fe_pm4             4.47    4.44    6.29    0.00     10%      27.05          -
fe_textures        6.24    5.64   13.45    0.00     15%      19.89          -
fs_resolve         1.00    0.96    1.69    0.00      2%      31.64          -
fs_buffer          2.59    2.53    3.55    0.00      6%      29.78          -
fb_refresh         3.11    3.10    4.04    0.00      7%      29.29          -
fb_hash            1.55    1.53    2.22    0.00      4%      31.11          -
frontend_other     0.74    0.72    1.06    0.00      2%      32.28          -
fe_end_rest        0.81    0.81    1.09    0.00      2%      32.24          -
front_wait         0.63    0.00    5.73    0.00      1%      27.60          -
worker            36.41   36.47   47.77    1.84     85%     -14.44        13%
record            31.01   30.10   52.15    2.06     72%     -18.82         4%
gpu               32.98   32.66   38.95    0.00     77%      -5.62         1%
```

### Draws por quadro e contadores

Os draws por quadro vêm do campo `draws=` das linhas `Vulkan profile (ms/frame over 120)` dos arquivos `logs/bench_lz_off1_vulkan.log`, `bench_lz_on1_vulkan.log`, `bench_lz_on2_vulkan.log` e `bench_lz_off2_vulkan.log` (cópias do `game.log` feitas com `-Profile`), e os contadores das linhas `native front-end stream plan (per frame over 120)` do mesmo log. Como nas Fases 1.1a e 1.1b, usei as **últimas 9 janelas de 120 quadros** de cada execução (1080 quadros, quase a janela de 1100 do relatório), pareando a janela do contador com a do perfil pela posição. `lz_off1` e `lz_on1` imprimiram 20 janelas (a primeira janela de jogo, 2577 e 2553 draws, fica fora); `lz_on2` e `lz_off2` imprimiram 19, e as 9 últimas são todas as janelas de jogo (a primeira delas entra). Draws por janela:

- `lz_off1`: 2844, 2987, 2853, 2154, 1914, 2435, 2694, 2532, 2565 (média 2553).
- `lz_on1`: 2881, 3018, 3059, 2486, 1934, 2553, 2994, 2951, 2946 (média 2758).
- `lz_on2`: 2425, 2926, 3075, 2921, 2239, 1929, 2862, 2971, 2957 (média 2701).
- `lz_off2`: 2282, 2882, 2994, 2781, 3004, 2806, 2659, 2526, 2566 (média 2722).

A média de draws por modo é 2638 em OFF e 2729 em ON. As quatro execuções diferem em até 205 draws por quadro (2553 a 2758) e as janelas de uma mesma execução variam de 1914 a 3075, então a cena muda bastante; por isso a comparação entre os modos é **por draw**. Contadores (médias por quadro nas mesmas 9 janelas; são divisões inteiras por 120 feitas pelo jogo):

| Execução | `evaluated` | `fast` | `slow` | `slow`/`evaluated` | `address` | `buffer refresh: calls` | `hashes` | `hash_kb` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `lz_off1` | 3450 | 1305 | 2144 | 62,2% | 2143 | 4857 | 1850 | 6602 |
| `lz_on1` | 3789 | 1409 | 2379 | 62,8% | 2378 | 5269 | 2060 | 7079 |
| `lz_on2` | 3707 | 1372 | 2334 | 63,0% | 2333 | 5154 | 2014 | 6958 |
| `lz_off2` | 3687 | 1402 | 2285 | 62,0% | 2283 | 5183 | 1959 | 7080 |

Os motivos `untracked`, `size`, `stride` e `phase` foram 0 em todas as janelas usadas, `decl` ficou em 0,2 a 0,4 e `dirty` em 0,9 a 1,0 por quadro: o mesmo quadro da Fase 1.1b (a fração de avaliações lentas, 62,0% a 63,0%, não depende do modo, como esperado, porque o resolve não entra na condição do cache).

### Comparação por draw, OFF contra ON, e regra de aceitação

µs por draw = média do estágio (ms) × 1000 ÷ `draws=` **da mesma execução**; a coluna de cada modo é a média dos valores das suas duas execuções (OFF: `off1` e `off2`; ON: `on1` e `on2`). Os estágios por comando (`frontend`, `fe_index`) contam também comandos que não são draws (cerca de 3% dos draws, ver a 1.1a): o divisor é sempre `draws=`. Os pares são adjacentes na ordem de execução: par 1 = `on1` menos `off1`, par 2 = `on2` menos `off2`.

Todos os valores desta tabela são do modo detalhado (`-TimelineDetail`); a sonda do `fs_resolve` não é simétrica entre os modos e afeta `fs_plan`, `fe_streams`, `frontend` e `game`, ver "A sonda do `fs_resolve` é assimétrica" abaixo.

| Estágio | OFF: µs/draw (`off1`; `off2`; média) | ON: µs/draw (`on1`; `on2`; média) | Δ média (ON − OFF) | Δ par 1 | Δ par 2 |
| --- | --- | --- | ---: | ---: | ---: |
| `fs_resolve` (alvo) | 0,341; 0,367; **0,354** | 0,000; 0,000; **0,000** | −0,354 (−100%) | −0,341 | −0,367 |
| `fs_buffer` (escopo diferente, ver abaixo) | 0,913; 0,951; 0,932 | 0,939; 0,978; 0,958 | +0,026 (+2,8%) | +0,026 | +0,026 |
| `fs_plan` | 1,316; 1,392; **1,354** | 0,997; 1,044; **1,021** | **−0,333 (−24,6%)** | −0,319 | −0,348 |
| `fe_streams` | 1,610; 1,723; **1,666** | 1,291; 1,374; **1,332** | **−0,334 (−20,0%)** | −0,319 | −0,349 |
| `frontend` | 7,140; 7,953; 7,547 | 6,795; 7,406; 7,100 | −0,446 (−5,9%) | −0,346 | −0,547 |
| `game` (média do jogo) | 14,433; 15,469; 14,951 | 13,927; 15,008; 14,467 | −0,484 (−3,2%) | −0,507 | −0,461 |
| controle: `game_guest` | 7,297; 7,516; 7,406 | 7,132; 7,602; 7,367 | −0,039 (−0,5%) | −0,165 | +0,086 |
| controle: `fe_index` | 0,568; 0,639; 0,604 | 0,526; 0,578; 0,552 | −0,052 (−8,6%) | −0,042 | −0,062 |
| controle: `fs_prep` | 0,141; 0,158; 0,149 | 0,141; 0,159; 0,150 | +0,001 (+0,6%) | 0,000 | +0,001 |

As médias em ms por modo (OFF; ON), só para referência, porque as cenas diferem: `fs_resolve` 0,935; 0,00. `fs_buffer` 2,46; 2,615. `fs_plan` 3,575; 2,785. `fe_streams` 4,40; 3,635. `frontend` 19,94; 19,37. `game` 39,48; 39,47. Os controles (`game_guest`, `fe_index`, `fs_prep`) são estágios que a mudança não toca; eles mostram o tamanho do ruído de um par: o `game_guest` por draw moveu −0,165 e +0,086 µs/draw nos dois pares (diferença de 0,25 entre os pares) e o `fe_index`, −0,042 e −0,062.

**A sonda do `fs_resolve` é assimétrica entre os modos.** O `DetailScope` do `fs_resolve` (duas leituras de relógio, só com `-TimelineDetail`) é construído em OFF em toda avaliação lenta com `range` não nulo e, em ON, só no ramo sujo do `PlanBuffer` (`dirty` de 0,9 a 1,0 por quadro, ou seja, ≈1 vez). Em OFF isso é no máximo o número de avaliações lentas, 2144 (`off1`) e 2285 (`off2`) por quadro (teto: o `range` é nulo com busca dinâmica de vértices, caso que não foi contado). Pela contabilidade deste documento (2 leituras por escopo, 20 a 30 ns por leitura, **custo não medido**): 2 × 2144 a 2285 × 20 a 30 ns = 0,086 a 0,137 ms por quadro (≈0,09 a 0,14 ms), que existem só em OFF e só no modo detalhado. É 9% a 15% dos ≈0,91 ms da economia aparente (0,333 µs/draw × 2729 draws). Cerca de metade, 0,043 a 0,069 ms (5% a 7% dos 0,935 ms do `fs_resolve` OFF), cai dentro do intervalo do próprio `fs_resolve`; a outra metade cai fora dele, mas dentro do `fs_plan` (e do `fe_streams`). Portanto o `fs_plan` e o `fe_streams` OFF contêm um custo de sonda que o ON não tem, e os −0,333 µs/draw, −24,6% e −20,0% acima, bem como os ≈0,91 ms, são **valores do modo detalhado** que superestimam a economia fora dele. Descontada a sonda (0,086 a 0,137 ms ÷ 2638 draws do OFF = 0,033 a 0,052 µs/draw), a economia estimada fica em −0,28 a −0,30 µs/draw, ≈0,77 a 0,82 ms por quadro a 2729 draws. É uma estimativa: depende do custo da leitura de relógio (não medido) e supõe que o resto da diferença é trabalho de fato removido. O A/B só é simétrico nos escopos `fb_*` (ambos os modos os têm), não no `fs_resolve`.

**Regra de aceitação: passou. O estágio-alvo caiu por draw, e o efeito líquido também.** (1) O `fs_resolve` foi de 0,354 µs/draw (0,87 e 1,00 ms nas execuções OFF) para 0,00 ms nas duas execuções ON (o relatório imprime 0,00 de média e 0,01 de p99, então o `Resolve` essencialmente não roda nessas cenas; quantas vezes ele roda no ramo sujo não foi contado). Isso é consistente com a leitura da 1.1b de que o resultado do `Resolve` quase nunca era consumido (a 1.1b só tinha a leitura do código). (2) O efeito líquido está em `fs_plan` e `fe_streams`: −0,333 e −0,334 µs/draw (−24,6% e −20,0%; **valores do modo detalhado**, ver a sonda assimétrica acima), com o mesmo sinal e tamanho parecido nos dois pares (−0,319 e −0,348 no `fs_plan`; −0,319 e −0,349 no `fe_streams`), e maior que o movimento dos controles nos mesmos pares (até 0,17 µs/draw em módulo, ou seja, o efeito é ≈2 vezes o ruído dos controles). O `fe_streams` cai o mesmo que o `fs_plan`, então o que `fe_streams` tem fora do `fs_plan` (`fs_prep` mais `fs_other`: 0,75 a 0,90 ms em OFF e 0,81 a 0,89 ms em ON, por execução 0,75, 0,90, 0,81 e 0,89 ms em `off1`, `off2`, `on1` e `on2`, ou 0,29 e 0,33 µs/draw em OFF e 0,29 e 0,33 em ON; o `fs_other` sozinho é 0,39 a 0,47 ms em OFF e 0,42 a 0,46 ms em ON) não mudou além da variação. A queda líquida (0,333) é ligeiramente menor que o `fs_resolve` removido (0,354): a diferença, 0,02 µs/draw, é pequena e fica dentro do ruído dos controles. Em ms por quadro, a 2729 draws (a média ON), 0,333 µs/draw são ≈0,91 ms **em modo detalhado**; descontada a sonda, ≈0,77 a 0,82 ms (−0,28 a −0,30 µs/draw, estimativa), que ainda é 1,7 a 1,8 vez o maior movimento dos controles (0,165) e é o que a construção da mudança explica (o `Resolve` deixa de rodar em ≈2200 avaliações lentas por quadro). **O veredito de aceitação não muda**: a sonda reduz o tamanho do efeito medido, não o sinal nem a explicação. Não há motivo para reverter a mudança.

O `frontend` (−0,446 µs/draw) e o `game` (−0,484) caem mais que o `fs_resolve` removido (0,354) e com o mesmo sinal nos dois pares, mas a parte além de ≈0,35 (≈0,1 µs/draw) tem o tamanho do ruído dos controles, então **não** atribuo a queda além do `fs_resolve` à mudança. Os valores por modo vêm de duas execuções cada, com cenas diferentes. `frontend` e `game` também contêm a sonda assimétrica do `fs_resolve` (0,09 a 0,14 ms em OFF, estimada), que é parte pequena dessa queda.

**`fs_buffer` não é comparável entre os modos.** No modo ansioso o `fs_resolve` e o `fs_buffer` eram irmãos dentro do `fs_plan` (`fs_plan` ≈ `fs_resolve` + `fs_buffer` + resto); no modo preguiçoso o `Resolve` roda dentro do `PlanBuffer`, ou seja, dentro do `fs_buffer`, e o `fs_plan` ≈ `fs_buffer` + resto. O resto (`fs_plan` − `fs_resolve` − `fs_buffer`) é 0,16 e 0,20 ms em OFF e 0,16 e 0,18 ms em ON. O `fs_buffer` ON inclui, portanto, o `Resolve` do ramo sujo (≈0 ms observado). A diferença de +0,026 µs/draw (+2,8%; ≈0,07 ms por quadro), com o mesmo valor nos dois pares, é pequena e **não foi investigada**; por isso o efeito líquido se lê em `fs_plan` e `fe_streams`, não no `fs_buffer`. O nome do estágio no relatório não muda, e a tabela do relatório não avisa disso (a ressalva 7 do início do documento passa a avisar).

### Gate de imagem

Não existia referência em `artifacts/golden`; gravei uma com o modo OFF nesta sessão (`lz_gate_off_vulkan`, `-Gate record`) e conferi o modo ON contra ela (`lz_gate_on_vulkan`, `-Gate check`). Para ter uma base do ruído do próprio gate, conferi também uma segunda execução OFF contra a mesma referência (`lz_gate_off2_vulkan`). O gate compara o screenshot do instante em que o HUD é detectado, e o ângulo da câmera na abertura da fase varia entre execuções; só o emblema do HUD é pixel a pixel.

| Execução | Modo | Veredito | PSNR global | Histograma | logo (PSNR dB / dif. média / cromaticidade) | barra azul | barra vermelha | capa (`char`) |
| --- | --- | --- | ---: | ---: | --- | --- | --- | --- |
| `lz_gate_on_vulkan` | ON | PASS | 38,3 dB | 0,005 | 99,0 / 0,0 / 0,000 | 29,3 / 1,2 / 0,002 | 28,1 / 0,5 / 0,001 | 24,1 / 4,2 / 0,034 |
| `lz_gate_off2_vulkan` | OFF (base do ruído) | PASS | 36,3 dB | 0,007 | 99,0 / 0,0 / 0,000 | 25,6 / 8,7 / 0,014 | 25,4 / 6,1 / 0,011 | 24,0 / 1,7 / 0,014 |

Os dois modos passam, e o ON não fica pior que o OFF contra a mesma referência no PSNR global (38,3 contra 36,3 dB) nem no histograma (0,005 contra 0,007). O emblema (`logo`) é idêntico nos dois (99,0 dB, diferença 0,0). Na capa, o ON tem diferença média maior (4,2 contra 1,7) e cromaticidade 0,034 contra 0,014 (limite do gate: 0,05); a diferença média de 4,2 fica dentro da faixa dos PASS anteriores em `logs/bench_gate.csv` (0,64 a 7,71) e a cromaticidade de 0,034 passa só um pouco acima da maior dos PASS anteriores (0,030). Essas linhas anteriores são de 2026-10-06 e foram pontuadas contra **outra referência**: existia então um golden que não existe mais (o `artifacts/` foi recriado depois), de modo que as faixas são contra uma referência diferente da desta fase e servem só como ordem de grandeza. A capa depende do ângulo da câmera e da pose; estes valores não separam diferença de imagem de diferença de cena. **O gate é grosseiro** (limites: PSNR global ≥ 17 dB, histograma ≤ 0,40, PSNR do logo ≥ 30 dB, capa com diferença média ≤ 20): pega falhas grosseiras, não prova imagem bit a bit. A equivalência da mudança vem do argumento estrutural do plano (o `Resolve` só altera o `VertexRange` local do draw e é idempotente, e o modo preguiçoso calcula o intervalo com a mesma fórmula do `PlanStreams`), mais o desligador no mesmo binário, mais este gate. A referência `artifacts/golden/start.png` passa a existir localmente (ignorada pelo Git), gravada pelo modo OFF desta build.

### Efeito em FPS e variação de cena

O FPS **não** muda de forma distinguível. Intervalo médio do relatório: OFF 37,4 e 42,8 ms (média 40,1); ON 38,9 e 41,3 ms (média 40,1). FPS do bench, média das duas execuções: parado 24,45 (OFF) contra 25,4 (ON); andando 25,85 contra 24,45. As direções são opostas entre os dois cenários e entre os pares (OFF1 → ON1 pior em 1,5 ms de intervalo, ON2 → OFF2 pior em 1,5 ms). Os estágios que limitam o quadro também ficam iguais na média: `worker` 33,66 (OFF) contra 33,79 ms (ON) e `gpu` 31,56 contra 32,22 ms.

Ressalvas: (1) **a cena varia entre execuções** (draws por quadro de 2553 a 2758 e intervalo de 37,4 a 42,8 ms); o `game_guest`, que a mudança não toca, subiu de 18,63 ms (`off1`) para 19,67, 20,53 e 20,46 ms nas execuções seguintes, o que indica deriva de cena ou de máquina ao longo da sequência. A ordem OFF, ON, ON, OFF cancela uma deriva linear nas médias por modo, mas não outras formas. (2) Dois resultados por modo, medidos com o detalhe e o perfil ligados. (3) Uma economia de ≈0,9 ms por quadro no `PlanStreams` (a 2729 draws; valor do modo detalhado, ≈0,77 a 0,82 ms descontada a sonda assimétrica do `fs_resolve`, estimada, ver acima) pode não virar FPS: o `worker` (30,9 a 36,4 ms) e a `gpu` (30,1 a 33,0 ms) estão perto do orçamento de 33,3 ms e não foram medidos em função da redução; **os dados não decidem** quanto da redução viraria FPS.

**Custo do modo detalhado com os escopos novos.** `fb_refresh` roda ≈5100 vezes e `fb_hash` ≈1970 vezes por quadro (médias de ON e OFF, tabela de contadores); cada escopo lê o relógio duas vezes. Isso dá ≈14 200 leituras por quadro, que a 20 a 30 ns por leitura (suposição, o custo de uma leitura **não foi medido**) são ≈0,28 a 0,43 ms por quadro, distribuídos pelos pais que contêm esses escopos (`fs_buffer`, `fs_other`, `fe_index` e, acima, `fs_plan`, `fe_streams`, `frontend`, `game`). Isso é só um teto estimado, não uma medida do custo, e **infla os valores absolutos** de `fb_refresh` e `fb_hash` e dos estágios que os contêm, em ambos os modos. Pela contabilidade da 1.1b (≈1 leitura dentro do intervalo do próprio escopo e ≈1 fora), o `fb_refresh` contém ≈1 leitura por chamada mais as 2 leituras de cada `fb_hash` aninhado, ou seja, ≈9100 leituras (≈0,18 a 0,27 ms, 6% a 9% dos 2,92 ms), e o `fb_hash` contém ≈1 leitura por hash, ≈2000 leituras (≈0,04 a 0,06 ms, 3% a 4% dos 1,52 ms). O A/B OFF contra ON é simétrico nos escopos `fb_*` (ambos os modos os têm), então a comparação por draw não é afetada por eles; **não** é simétrico no `fs_resolve` (≈2200 escopos por quadro em OFF contra ≈1 em ON, ver "A sonda do `fs_resolve` é assimétrica"). As tabelas desta seção não são comparáveis em valor absoluto com as da 1.1b (por exemplo, `fs_buffer` OFF de 2,33 e 2,59 ms contra 2,12 a 2,51 ms na 1.1b, com cenas diferentes).

### Mudança de aninhamento

Modo ansioso (OFF, o que a 1.1b descreve): `fs_resolve` e `fs_buffer` são irmãos dentro do `fs_plan`. Modo preguiçoso (ON, padrão): `fs_resolve` ⊂ `fs_buffer` ⊂ `fs_plan` ⊂ `fe_streams`. Consequências: (a) o `fs_buffer` ON inclui o `fs_resolve` e não é comparável ao OFF (acima); (b) a comparação entre modos se faz em `fs_plan` e `fe_streams`; (c) a afirmação da 1.1b "`fs_resolve` e `fs_buffer` dentro do `fs_plan`" continua verdadeira nos dois modos, mas a de que são partes que se somam só vale no modo ansioso. A ressalva 7 do início do documento e a nota no fim da seção de execuções da 1.1b registram isso sem reescrever o texto original.

### Decomposição do `fs_buffer`: `fb_refresh` e `fb_hash`

O que cada estágio mede, pelo código (`RefreshTrackedBuffer`): `fb_refresh` é a chamada inteira (consulta de escrita de página pelo write-watch, hash quando necessário, marcação de sujo); `fb_hash` é só o `TextureHash` do conteúdo do buffer dentro dela, que roda quando o buffer é de até 32 KiB (`kHashedBufferMax`) e o `BufferContent::Refresh` decide hashear: na primeira chamada de cada quadro para aquele buffer (o "reforço uma vez por quadro" para escritas por aliases virtuais não observados) ou quando o write-watch acusou escrita de página. (O buffer novo é hasheado no cadastro, fora do `fb_hash`, e isso já deixa o conteúdo válido para aquele quadro.) **Os contadores e os estágios `fb_*` cobrem todos os buffers rastreados: de vértices e de índices**, não só os de vértices. Um stream sujo chama o `RefreshTrackedBuffer` duas vezes (na condição do ramo rápido e dentro do `PlanBuffer`), mas isso é raro (`dirty` ≈ 1 por quadro). O hash que o `PlanBuffer` faz ao cadastrar um buffer **novo** é outro trecho de código e não passa por `fb_hash`.

**Onde `fb_refresh` aninha.** Há três pontos de chamada: (a) a condição do ramo rápido do `PlanStreams`, que fica **fora** do `fs_plan` e dentro do `fe_streams` (entra no `fs_other`); (b) o `PlanBuffer` dos streams, dentro do `fs_buffer` (ramo lento); (c) o `PlanBuffer` do buffer de índices, dentro do `fe_index`. O `fb_hash` aninha no `fb_refresh`. Como o primeiro `RefreshTrackedBuffer` de cada quadro para um buffer é o que paga o hash, o hash cai no ponto de chamada que o buffer atingir primeiro no quadro; essa divisão não foi medida. Não afirmo uma divisão do `fs_buffer` em `fb_refresh` e resto. O que os números sustentam é um limite: a média de `fb_refresh` (2,92 ms) supera a do `fs_buffer` ON (2,615 ms), então pelo menos ≈0,3 ms do `fb_refresh` está **fora** do `fs_buffer` (em `fs_other` e `fe_index`); no OFF, 2,91 contra 2,46 ms, ≥ ≈0,45 ms. O restante e a parte do `fs_buffer` que não é `fb_refresh` (chave XXH3, busca em `tracked_`, `align_range`, varredura de `clean`, captura de bytes) não foram separados.

| Execução | `fb_refresh` ms (µs/draw) | `fb_hash` ms (µs/draw) | chamadas/quadro | hashes/quadro | `hash_kb`/quadro | µs por chamada | µs por hash | KiB por hash |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `lz_off1` | 2,71 (1,061) | 1,42 (0,556) | 4857 | 1850 | 6602 | 0,56 | 0,77 | 3,6 |
| `lz_on1` | 2,88 (1,044) | 1,56 (0,566) | 5269 | 2060 | 7079 | 0,55 | 0,76 | 3,4 |
| `lz_on2` | 2,96 (1,096) | 1,55 (0,574) | 5154 | 2014 | 6958 | 0,57 | 0,77 | 3,5 |
| `lz_off2` | 3,11 (1,142) | 1,55 (0,569) | 5183 | 1959 | 7080 | 0,60 | 0,79 | 3,6 |
| média | **2,92 (1,086)** | **1,52 (0,566)** | **5116** | **1971** | **6930** | 0,57 | 0,77 | 3,5 |

Leitura dos números (média das quatro execuções; o `fb_*` não depende do modo do resolve, e os valores dos dois modos coincidem dentro da variação de cena):

- **`fb_refresh` ≈ 2,92 ms por quadro** (1,09 µs/draw, ≈15% do `frontend` médio de 19,66 ms), em ≈5100 chamadas por quadro (≈1,9 por draw). A média por chamada é 0,57 µs, **inclusive** o relógio do escopo. As chamadas superam as avaliações de stream (`evaluated` de 3450 a 3789) em 1407 a 1496 por quadro; isso é compatível com uma chamada por draw indexado no bloco do buffer de índices, mas o número de draws indexados **não foi contado**, então é uma inferência.
- **`fb_hash` ≈ 1,52 ms por quadro** (0,57 µs/draw, ≈7,7% do `frontend`, ≈52% do `fb_refresh`): ≈1970 hashes de conteúdo por quadro (0,73 por draw), de ≈3,5 KiB em média (buffers de até 32 KiB), ≈6930 KiB (≈6,8 MiB) hasheados por quadro, ≈0,77 µs por hash, o que equivale a ≈4,6 a 4,8 GB/s contando o relógio e a tradução de endereço. O `fb_refresh` sem o `fb_hash` é ≈1,40 ms (0,27 µs por chamada), dos quais ≈0,15 a 0,2 ms são, por estimativa, leituras de relógio (≈7100 leituras a 20 a 30 ns, não medido); o que ele contém além disso (consulta de escrita de página, early-return, marcação de sujo) não foi decomposto.
- Depois desta mudança, o `fb_hash` (1,52 ms) é a maior causa isolada já identificada dentro dos estágios de buffer: maior que o `fs_resolve` removido (0,935 ms em OFF). Ele se distribui entre `fs_buffer`, `fs_other` e `fe_index` (a divisão não foi medida), então não se compara com o `fs_plan` (2,79 ms em ON) como uma fração dele. Os blocos `fe_textures` (5,72 ms) e `fe_pm4` (4,15 ms) são maiores, mas continuam sem decomposição.

**O `fb_hash` é um mecanismo de correção e esta fase não o altera.** Ele é o reforço de uma vez por quadro do hash de conteúdo de buffers rastreados de até 32 KiB: confirma o conteúdo contra escritas feitas por aliases virtuais que o write-watch de páginas não observa (é o que o comentário do código diz sobre esse reforço; o comentário da função cita separadamente os vértices da capa, reescritos sem `Unlock` e pegos pelo write-watch físico). É da mesma família do hash de fallback das texturas, para o qual o usuário aceitou uma política de backoff (watch com backoff, para o Vulkan, em 2026-10-08, ver "Atualização: o hash quase não pesa na captura do Vulkan atual"); **para buffers não existe decisão equivalente**. Qualquer mudança de quando ou se o hash roda (backoff, pular, só sob suspeita) pode deixar vértices velhos na tela por quadros e **exige decisão do usuário**; este documento não recomenda removê-lo nem reduzi-lo. A medida de custo (1,52 ms) é o teto do que uma decisão dessas poderia economizar; o ganho real seria menor, e mudanças que não alteram quando nem se o hash roda (por exemplo, o trabalho do `fb_refresh` fora do hash) não dependem dela, mas ainda não foram medidas (ver o item 2 da lista abaixo).

### Candidatos da próxima fase (reordenados com os dados da 1.1c)

O item 5 da lista anterior (adiar o `Resolve`) foi feito nesta fase; o item 8 (repetições dentro do ramo lento) segue rebaixado. Tetos: média do próprio estágio nas quatro execuções (ms por quadro e µs por draw, cada execução dividida pelos seus draws); para os estágios afetados pela mudança, a média ON. Só seriam alcançados se o estágio inteiro sumisse; tetos aninhados não se somam (`fb_refresh` está parcialmente dentro de `fs_buffer`, `fs_other` e `fe_index`; `fb_hash` dentro de `fb_refresh`); incluem o custo dos relógios dos escopos; e quanto vira FPS depende de `worker` e `gpu`, perto do orçamento (ver acima). Todo item que muda comportamento tem de preservar a **imagem bit a bit**, a **semântica do rastreamento de buffer sujo** (`dirty`/`clean`, write-watch, `InvalidateGuestRange`) e a **precedência de leitura dos bytes capturados**; verificação comum: o estágio cai por draw no A/B de quatro execuções (mesma tabela de µs por draw, com controles), `tests/native` e `python -m pytest tests/tools -q` passam, e o gate de imagem (`-Gate check` contra a referência local gravada nesta fase; o gate é grosseiro).

1. **Medir: decomposição de `fe_textures` e `fe_pm4`** (estágios `fe_textures`, teto 5,72 ms, 2,13 µs/draw; `fe_pm4`, teto 4,15 ms, 1,55 µs/draw; os maiores blocos cronometrados do front-end). Sub-regiões no modo detalhado, **sem** alterar a política de watch e de hash das texturas. Risco: nenhum. Verificação: as sub-regiões fecham com o estágio. Sem decisão do usuário.
2. **Medir: decomposição do `fb_refresh` por ponto de chamada e por desfecho** (estágios `fb_refresh`, teto 2,92 ms, 1,09 µs/draw; `fb_hash`, teto 1,52 ms, 0,57 µs/draw). Contar por quadro: chamadas por ponto de chamada (condição do ramo rápido, `PlanBuffer` de stream, `PlanBuffer` de índices); hashes por motivo (buffer visto pela primeira vez, primeira chamada do quadro, escrita de página detectada pelo write-watch); quantas vezes o hash dá "mudou" (`stats_.buffer_hash_dirty`, que já existe, contra `buffer_watch_dirty`); e o hash do buffer novo no `PlanBuffer`. Cronometrar o `fb_refresh` sem o hash por ponto de chamada. Risco: nenhum no comportamento. Verificação: a soma por ponto de chamada fecha com `calls` e a soma por motivo com `hashes`. Sem decisão do usuário. É o que diz se existe algo a ganhar em `fb_refresh` fora do `fb_hash` e quanto do `fb_hash` é o reforço de uma vez por quadro contra escritas de página.
3. **Medir: repetição de buffers por slot e desfecho do `PlanBuffer`** (estágio `fs_plan`, teto agora 2,79 ms, 1,02 µs/draw; `fs_buffer`, 2,62 ms, 0,96 µs/draw). É o item 1 da 1.1c anterior, ainda aberto: endereços-base distintos por slot, simulação de caches de 2 e 4 entradas, `action` 0, 1 ou 2, e se o `tracked_` achado está sujo. Risco: nenhum. Verificação: as contagens fecham com `evaluated`, `fast` e `slow`. Sem decisão do usuário.
4. **Medir: `fe_index` e `fd_other`** (`fe_index`, teto 1,55 ms, 0,58 µs/draw; `fd_other` = `fe_device` − `fd_shaders`, teto 1,20 ms, 0,45 µs/draw). Itens 3 e 4 da 1.1b, ainda sem medida; o `fe_index` inclui agora o `fb_refresh` do buffer de índices (a divisão não foi medida). Risco: nenhum. Verificação: as sub-regiões e as contagens fecham com `fe_index` e com `fd_other`, como na 1.1b. Sem decisão do usuário.
5. **Medir: custo do relógio e contagem de regiões** (todos os `fe_*`; `fe_end_rest`, 0,745 ms, 0,28 µs/draw). Substitui os 20 a 30 ns supostos por um número e dá o custo medido (estimado hoje em ≈0,3 a 0,4 ms) dos escopos `fb_*` e o da sonda assimétrica do `fs_resolve` (estimada em 0,09 a 0,14 ms em OFF), o que fixa quanto da economia da 1.1c existe fora do modo detalhado. Risco: nenhum na imagem. Verificação: o `capture` do relatório coincide com `fe_pm4` + `fe_textures` dentro da variação, e a economia fora do modo detalhado passa a ter um valor medido em vez de uma faixa estimada. Sem decisão do usuário.
6. **Ampliar o acerto do cache de stream, ou consulta direta em `tracked_`** (estágio `fs_plan`, teto 2,79 ms, frouxo; só se o item 3 mostrar o que converter). Risco **alto** no rastreamento de sujo (um acerto que pule a captura quando devia capturar quebra a imagem; o acerto precisa passar por `RefreshTrackedBuffer`). Verificação: gate de imagem, testes de dirty-tracking, contagem de capturas (`action` 1 e 2) idêntica. Uma avaliação convertida em acerto continua pagando o `fb_refresh`, com o hash da primeira chamada do quadro. Não propor antes do item 3. Sem decisão do usuário (preserva a semântica do rastreamento de sujo; se algum desenho mudasse essa semântica, passaria a exigir).
7. **Política do hash de conteúdo de buffers rastreados** (estágio `fb_hash`, teto 1,52 ms, 0,57 µs/draw). **Exige decisão do usuário** e não é proposta aqui. Antes de levá-la ao usuário: o item 2 (quantos hashes são o reforço de uma vez por quadro e quantos são por escrita de página); um teste que force uma escrita por alias virtual e conte os quadros com dado velho (o gate de imagem compara uma única captura e é grosseiro); e a definição, pelo usuário, de quantos quadros de atraso são aceitáveis para buffers (para texturas, a decisão foi até 16 quadros). Ideias não avaliadas, como mover o cálculo do hash para fora da thread do jogo, **podem mudar a latência** (o hash decide a captura de forma síncrona) e por isso também exigem decisão do usuário; não foram medidas nem estudadas, e o teto de qualquer uma é menor que 1,52 ms.
8. **Reduzir `CaptureDevice` fora dos shaders e as buscas de shader** (`fd_other`, 1,20 ms, 0,45 µs/draw; `fd_shaders`, 0,49 ms, 0,18 µs/draw; `fs_prep`, 0,40 ms, 0,15 µs/draw; médias das quatro execuções). Só depois do item 4. Risco médio (precedência das faixas e invalidação de shaders; ver a 1.1b). Verificação: como na 1.1b: o estágio cai por draw no A/B de quatro execuções, e `BufferPlan` e bytes capturados idênticos (`tests/native` e gate de imagem). Sem decisão do usuário.
9. **Tetos pequenos** (`fs_prep`, `fs_other`, `fe_push`): nesta fase, 0,40 ms (0,15 µs/draw), 0,44 ms (0,16 µs/draw) e 0,30 ms (0,11 µs/draw), médias das quatro execuções. O item 9 da seção "Candidatos da Fase 1.1c (reordenados com os novos dados)", com os dados da 1.1b, estimou que a sonda de relógio é cerca de 12% a 44% de cada um desses tetos (estimativa, custo da leitura não medido); essa conta não foi refeita com os dados desta fase. Não são candidatos. Verificação, se algum virar candidato depois do item 5: o estágio cai por draw no A/B de quatro execuções. Sem decisão do usuário.

Itens que precisam de decisão de política do usuário: **o 7** (hash de buffers). Os demais são medição ou mudanças que preservam a semântica e se verificam por imagem e por contagem.

**O que os dados não decidem.** Quanto da economia de ≈0,9 ms no `PlanStreams` (valor do modo detalhado) vira FPS (o intervalo médio não mudou: 40,1 ms nos dois modos); quanto dela é sonda do modo detalhado (o `fs_resolve` lê o relógio em ≈2200 avaliações por quadro em OFF e em ≈1 em ON; estimado em 0,09 a 0,14 ms, ou seja, ≈0,77 a 0,82 ms fora do modo detalhado, com o custo da leitura não medido), de modo que a economia real fora do modo detalhado não foi medida; por que o `fs_buffer` ON está +0,026 µs/draw acima do OFF; quantas vezes o `Resolve` roda no ramo sujo; como as ≈5100 chamadas e os ≈1970 hashes por quadro se dividem entre os três pontos de chamada, e quantos hashes são o reforço do quadro e quantos são por escrita de página; quanto do `fb_refresh` além do `fb_hash` é trabalho e quanto é relógio; quantas chamadas vêm de draws indexados; e o custo de uma leitura de relógio. A cena variou de uma execução para outra (2553 a 2758 draws por quadro; `game_guest` de 18,6 a 20,5 ms), com duas execuções por modo, em uma máquina.

### Suítes

`python -m pytest tests/tools -q`: 67 passaram. `build\tests-vulkan\sr_vulkan_tests.exe`: 88 testes, 0 falhas (o executável é de 16:30, não foi recompilado nesta tarefa; a mudança não toca o código dele). `ctest --test-dir build/tests-native`: 2 de 2 passaram. Nenhuma dessas suítes exercita o front-end do `PlanStreams` (que precisa do SDK); a segurança vem da equivalência estrutural, do desligador no mesmo binário e do gate de imagem (grosseiro).

## Sondas do PM4 e experimento rejeitado (Fase 1.2)

Plano: `docs/superpowers/plans/2026-10-09-texture-fetch-and-pm4-probe.md`. Esta fase fez duas coisas. (1) Acrescentou sondas que decompõem o `fe_pm4` (commit `ed6355c`) e as mediu em três execuções do Vulkan. (2) Testou, por A/B no mesmo binário, uma leitura preguiçosa dos fetch constants de textura (`SR_NATIVE_LAZY_FETCH`); o resultado foi **inconclusivo e a mudança não foi adotada**: o código dela não está na árvore. **Nenhuma otimização foi adotada nesta fase**; ela só mediu.

### O que cada sonda mede

Pelo código de `CapturePm4Dependencies` (`port/src/graphics/guest/pm4_capture.cpp`) e do `EndCmd`. As sondas só existem com `-TimelineDetail` (sem o detalhe, o ponteiro da sonda é nulo e nenhum relógio extra é lido).

| Estágio | O que mede |
| --- | --- |
| `fe_pm4` | a chamada inteira de `CapturePm4Dependencies` dentro do `EndCmd`, uma por chamada do `EndCmd` com verificação de pacotes e bytes de anel (já existia) |
| `fp_primary` | a cópia dos bytes do anel do comando para um `std::vector` novo (`primary`), uma por chamada |
| `fp_read` | cada chamada do leitor de memória do guest (`ReadCommittedGuest`: tradução do endereço e `CheckedGuestReads::Read`, que aloca um vetor e chama `ReadProcessMemory`, ou seja, uma chamada de sistema por dependência lida) |
| `fp_copy` | a cópia da leitura para `sources` (um `std::deque` de vetores), mais o `batch.bytes.insert` e o `batch.ranges.push_back` |
| `fp_scan` | **derivado** pelo relatório, por quadro: `max(0, fe_pm4 − fp_primary − fp_read − fp_copy)`. É um resíduo, não uma medida direta |

Além dos estágios, a linha de log `native front-end stream plan (per frame over 120)` ganhou `pm4 deps: reads=… kb=…`: leituras de dependência bem-sucedidas e KiB copiados para o arena, médias por quadro em janelas de 120 (divisões inteiras feitas pelo jogo, então o KiB é truncado).

### Medições

Três execuções do Vulkan com o mesmo binário (`superman_returns.exe` de 09/10, 09:07, mais novo que todos os fontes do commit `ed6355c`), todas com `-Timeline -TimelineDetail -Profile`, uma por vez, sem outra compilação em paralelo (outra carga na máquina não foi medida nem controlada): `pp_smoke_vulkan` (a execução de fumaça da tarefa anterior), `pp1_vulkan` e `pp2_vulkan`. Nenhuma das duas executadas aqui falhou e nenhuma foi repetida. Nas três: 1100 quadros analisados, 0 descartados.

| Execução | parado (mínimo) | andando (mínimo) | Intervalo médio do relatório |
| --- | --- | --- | --- |
| `pp_smoke_vulkan` | 23,7 (18,1) | 27,5 (25,6) | 39,3 ms (25,4 FPS) |
| `pp1_vulkan` | 27 (23,1) | 24,9 (21,2) | 38,2 ms (26,1 FPS) |
| `pp2_vulkan` | 29,3 (27,4) | 27,6 (24,2) | 34,9 ms (28,6 FPS) |

(FPS do `bench_results.csv`, média e mínimo.) A `pp2` é uma cena visivelmente mais leve (ver os draws abaixo), com FPS mais alto; as três não são comparáveis em valor absoluto.

Tabelas impressas pelo relatório (`logs/timeline_pp_smoke_vulkan.txt`, `logs/timeline_pp1_vulkan.txt`, `logs/timeline_pp2_vulkan.txt`):

Execução `pp_smoke_vulkan`:

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 39.3 ms = 25.4 FPS | 1% low 19.0 FPS | mínimo 1.8 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio           média     p50     p99   bloq.   util.  folga p99  limitante
game              38.78   38.47   50.94    0.00     99%     -17.61        80%
game_guest        19.30   19.21   25.31    0.00     49%       8.02          -
game_other        31.00   30.83   39.49    0.00     79%      -6.16          -
capture            7.77    7.18   15.26    0.00     20%      18.07          -
frontend          19.47   18.98   27.97    0.00     50%       5.36          -
fe_begin           0.46    0.45    0.68    0.00      1%      32.66          -
fe_ring            0.29    0.27    0.64    0.00      1%      32.70          -
fe_device          1.67    1.67    2.13    0.00      4%      31.20          -
fe_index           1.66    1.63    2.35    0.00      4%      30.98          -
fe_streams         3.56    3.54    4.78    0.00      9%      28.56          -
fe_end            11.17   10.58   19.23    0.00     28%      14.11          -
fe_flush           0.07    0.05    0.21    0.00      0%      33.13          -
fd_shaders         0.47    0.46    0.65    0.00      1%      32.68          -
fs_prep            0.39    0.39    0.53    0.00      1%      32.81          -
fs_plan            2.76    2.74    3.81    0.00      7%      29.52          -
fe_push            0.29    0.29    0.40    0.00      1%      32.93          -
fe_pm4             4.20    4.15    6.05    0.00     11%      27.28          -
fe_textures        5.94    5.20   13.24    0.00     15%      20.09          -
fs_resolve         0.00    0.00    0.01    0.00      0%      33.33          -
fs_buffer          2.61    2.58    3.63    0.00      7%      29.71          -
fb_refresh         3.05    3.02    4.05    0.00      8%      29.28          -
fb_hash            1.59    1.57    2.26    0.00      4%      31.07          -
fp_primary         0.69    0.67    1.06    0.00      2%      32.28          -
fp_read            0.72    0.70    1.09    0.00      2%      32.25          -
fp_copy            0.15    0.14    0.32    0.00      0%      33.02          -
frontend_other     0.67    0.65    0.98    0.00      2%      32.35          -
fe_end_rest        0.74    0.73    1.02    0.00      2%      32.32          -
fp_scan            2.64    2.61    3.75    0.00      7%      29.58          -
front_wait         0.49    0.00    0.00    0.00      1%      33.33          -
worker            33.38   33.05   45.20    1.72     85%     -11.87        13%
record            29.44   28.82   44.44    1.95     75%     -11.10         5%
gpu               31.34   31.17   37.74    0.00     80%      -4.40         2%
```

Execução `pp1_vulkan`:

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 38.2 ms = 26.1 FPS | 1% low 19.2 FPS | mínimo 1.9 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio           média     p50     p99   bloq.   util.  folga p99  limitante
game              37.59   37.83   49.02    0.00     98%     -15.69        83%
game_guest        19.51   19.43   25.62    0.00     51%       7.71          -
game_other        30.40   30.55   38.17    0.00     79%      -4.84          -
capture            7.19    6.99   13.83    0.00     19%      19.51          -
frontend          18.08   18.47   26.64    0.00     47%       6.69          -
fe_begin           0.44    0.44    0.64    0.00      1%      32.69          -
fe_ring            0.28    0.27    0.63    0.00      1%      32.71          -
fe_device          1.56    1.62    2.02    0.00      4%      31.32          -
fe_index           1.48    1.46    2.15    0.00      4%      31.18          -
fe_streams         3.35    3.54    4.87    0.00      9%      28.46          -
fe_end            10.34   10.29   17.77    0.00     27%      15.56          -
fe_flush           0.06    0.05    0.19    0.00      0%      33.15          -
fd_shaders         0.44    0.44    0.63    0.00      1%      32.70          -
fs_prep            0.38    0.38    0.54    0.00      1%      32.79          -
fs_plan            2.58    2.72    3.92    0.00      7%      29.41          -
fe_push            0.28    0.28    0.39    0.00      1%      32.94          -
fe_pm4             3.95    3.98    5.61    0.00     10%      27.72          -
fe_textures        5.43    5.08   11.93    0.00     14%      21.40          -
fs_resolve         0.00    0.00    0.01    0.00      0%      33.33          -
fs_buffer          2.44    2.56    3.75    0.00      6%      29.58          -
fb_refresh         2.78    2.88    3.99    0.00      7%      29.35          -
fb_hash            1.47    1.52    2.33    0.00      4%      31.00          -
fp_primary         0.64    0.64    1.00    0.00      2%      32.33          -
fp_read            0.69    0.69    1.04    0.00      2%      32.29          -
fp_copy            0.14    0.14    0.22    0.00      0%      33.12          -
frontend_other     0.63    0.64    0.95    0.00      2%      32.39          -
fe_end_rest        0.68    0.69    0.95    0.00      2%      32.38          -
fp_scan            2.48    2.49    3.57    0.00      6%      29.77          -
front_wait         0.59    0.00    6.23    0.00      2%      27.11          -
worker            31.18   31.97   43.34    1.68     82%     -10.00         9%
record            27.67   27.33   45.11    2.02     72%     -11.78         4%
gpu               30.01   31.34   37.66    0.00     78%      -4.33         3%
```

Execução `pp2_vulkan`:

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 34.9 ms = 28.6 FPS | 1% low 20.3 FPS | mínimo 3.5 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio           média     p50     p99   bloq.   util.  folga p99  limitante
game              34.36   34.17   47.14    0.00     98%     -13.81        80%
game_guest        20.25   19.49   33.49    0.00     58%      -0.16          -
game_other        28.82   28.81   37.67    0.00     83%      -4.33          -
capture            5.54    5.40   12.66    0.00     16%      20.67          -
frontend          14.11   14.36   24.28    0.00     40%       9.05          -
fe_begin           0.34    0.35    0.61    0.00      1%      32.72          -
fe_ring            0.22    0.21    0.56    0.00      1%      32.78          -
fe_device          1.26    1.31    1.97    0.00      4%      31.37          -
fe_index           1.07    1.05    1.91    0.00      3%      31.42          -
fe_streams         2.64    2.77    4.58    0.00      8%      28.75          -
fe_end             8.08    8.08   16.10    0.00     23%      17.24          -
fe_flush           0.05    0.04    0.18    0.00      0%      33.15          -
fd_shaders         0.35    0.36    0.61    0.00      1%      32.73          -
fs_prep            0.28    0.29    0.49    0.00      1%      32.84          -
fs_plan            2.04    2.14    3.74    0.00      6%      29.60          -
fe_push            0.22    0.22    0.36    0.00      1%      32.97          -
fe_pm4             3.03    3.07    5.47    0.00      9%      27.86          -
fe_textures        4.29    4.06   10.93    0.00     12%      22.40          -
fs_resolve         0.00    0.00    0.01    0.00      0%      33.33          -
fs_buffer          1.93    2.01    3.56    0.00      6%      29.78          -
fb_refresh         2.11    2.13    3.71    0.00      6%      29.62          -
fb_hash            1.16    1.19    2.26    0.00      3%      31.07          -
fp_primary         0.51    0.51    0.96    0.00      1%      32.38          -
fp_read            0.46    0.44    0.93    0.00      1%      32.40          -
fp_copy            0.09    0.09    0.19    0.00      0%      33.15          -
frontend_other     0.51    0.51    0.87    0.00      1%      32.46          -
fe_end_rest        0.54    0.55    0.95    0.00      2%      32.38          -
fp_scan            1.96    2.03    3.52    0.00      6%      29.81          -
front_wait         0.49    0.00    6.56    0.00      1%      26.77          -
worker            24.78   25.48   40.58    2.50     71%      -7.25         7%
record            23.95   24.06   46.84    2.19     69%     -13.51         8%
gpu               25.87   26.92   35.33    0.00     74%      -2.00         5%
```

### Draws por quadro, µs por draw, leituras e KiB

Os draws vêm do campo `draws=` das linhas `Vulkan profile (ms/frame over 120)` de `logs/bench_pp_smoke_vulkan.log`, `logs/bench_pp1_vulkan.log` e `logs/bench_pp2_vulkan.log` (cópias do `game.log` feitas com `-Profile`), **últimas 9 janelas de 120 quadros** (1080 quadros, quase a janela de 1100 do relatório), como nas fases anteriores; as linhas `pm4 deps` do mesmo log são pareadas com as janelas pela posição. Draws por janela:

- `pp_smoke`: 2882, 2945, 2932, 2791, 2975, 2675, 2525, 2414, 2427 (média 2729,6).
- `pp1`: 2835, 2959, 2794, 2143, 1882, 2374, 2625, 2550, 2563 (média 2525,0).
- `pp2`: 2171, 2498, 2228, 2096, 2067, 1955, 1195, 1573, 2893 (média 2075,1).

As janelas variam muito dentro de uma mesma execução (1195 a 2893 na `pp2`), então os µs por draw abaixo (média do estágio em ms × 1000 ÷ média dos draws da mesma execução) herdam essa variação de cena. Os estágios por comando contam também comandos que não são draws (cerca de 3% dos draws, ver a Fase 1.1a); o divisor é sempre `draws=`.

| Execução | `fe_pm4` ms (µs/draw) | `fp_primary` | `fp_read` | `fp_copy` | `fp_scan` (derivado) | `fp_scan` ÷ `fe_pm4` |
| --- | --- | --- | --- | --- | --- | ---: |
| `pp_smoke` (÷2729,6) | 4,20 (1,539) | 0,69 (0,253) | 0,72 (0,264) | 0,15 (0,055) | 2,64 (0,967) | 62,9% |
| `pp1` (÷2525,0) | 3,95 (1,564) | 0,64 (0,253) | 0,69 (0,273) | 0,14 (0,055) | 2,48 (0,982) | 62,8% |
| `pp2` (÷2075,1) | 3,03 (1,460) | 0,51 (0,246) | 0,46 (0,222) | 0,09 (0,043) | 1,96 (0,945) | 64,7% |
| média das três | **3,73 (1,521)** | **0,61 (0,251)** | **0,62 (0,253)** | **0,13 (0,051)** | **2,36 (0,965)** | 63,5% |

Os valores em ms são as médias do relatório, arredondadas a duas casas (a soma das quatro partes fecha com o `fe_pm4` dentro desse arredondamento, por construção do `fp_scan`). `fe_pm4` é 21,5% a 21,8% do `frontend` e 8,8% a 10,8% do `game` nas três execuções. Em ms o `fp_scan` varia de 1,96 a 2,64 entre execuções (cenas diferentes); por draw varia de 0,945 a 0,982, mais estável que o valor por quadro, o que é compatível com um custo que acompanha o número de comandos, mas três execuções e janelas de draws de outras partes da mesma execução não bastam para afirmar proporcionalidade.

Leituras e KiB das dependências do PM4 (linhas `pm4 deps`, média e faixa das 9 janelas):

| Execução | leituras por quadro | KiB por quadro | leituras por draw | bytes por leitura | `fp_read` por leitura | `fp_copy` por leitura |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| `pp_smoke` | 694,6 (569 a 801) | 53,3 (43 a 63) | 0,254 | 78,6 | 1,04 µs | 0,22 µs |
| `pp1` | 651,2 (545 a 766) | 51,0 (43 a 60) | 0,258 | 80,2 | 1,06 µs | 0,22 µs |
| `pp2` | 426,4 (274 a 763) | 31,8 (20 a 59) | 0,205 | 76,4 | 1,08 µs | 0,21 µs |

(Bytes por leitura = KiB médios × 1024 ÷ leituras médias; os KiB são truncados por janela, então subestimam em até 1 KiB por janela, ≈2% de 50. As colunas por leitura dividem a média do estágio pela média das leituras, não leitura por leitura. O `fp_read` por leitura divide o tempo total de `fp_read` só pelas leituras bem-sucedidas, mas o relógio do `read_ns` também cobre as leituras que falharam (memória não confirmada ou curta, que o contador de leituras não conta); quantas falham **não foi contado**, e o efeito sobre o µs por leitura é desprezível se elas são raras e desconhecido caso contrário.)

### O que as sondas dizem, e o que não dizem

- **As dependências do PM4 são poucas e pequenas.** ≈430 a ≈695 leituras por quadro (≈0,2 a ≈0,26 por draw) de ≈76 a ≈80 bytes em média, ou seja, 32 a 53 KiB por quadro.
- **`fp_read`, `fp_copy` e `fp_primary` somam 1,06 a 1,56 ms** (média 1,36 ms) dos 3,03 a 4,20 ms do `fe_pm4`; **o resíduo `fp_scan` é 63% a 65% do `fe_pm4`** (1,96 a 2,64 ms; média das três 2,36 ms, 0,965 µs/draw), nas três execuções. Os dados **não dividem** o `fp_scan` entre o analisador e o resto.
- **O que o `fp_scan` contém, pelo código:** a varredura do `Pm4Mirror::ScanCopyUsing` (o analisador), o despacho por `std::function` de cada leitura, a construção e a destruição do `std::deque` `sources` e do `unique_ptr` do espelho local, a destruição do `primary` e de cada vetor de `sources` no fim da função (cerca de uma liberação por leitura mais uma por chamada), e o custo de relógio das próprias sondas que cai fora dos intervalos (cada leitura de relógio cai em parte dentro e em parte fora do intervalo que ela delimita). **Não** é o custo do analisador sozinho.
- **Quanto do `fp_primary` é overhead das sondas não foi separado.** São dois relógios por chamada de `CapturePm4Dependencies`; o número de chamadas por quadro **não foi contado** (o campo `packets=` do perfil mostrou 2484 a 3059 nas 9 janelas da `pp_smoke`, mas não foi contado quantos desses comandos têm bytes de anel). Se for da ordem de 2500 chamadas, o `fp_primary` de 0,5 a 0,7 ms dá ≈0,2 a ≈0,28 µs por chamada, e os dois relógios por chamada (a 20 a 30 ns, suposição) seriam ≈0,10 a ≈0,15 ms do `fp_primary` (de ≈0,61 ms), o resto sendo a alocação do vetor e a cópia, que **não foram separadas**. É uma conta de ordem de grandeza sobre duas suposições; não permite dizer que o `fp_primary` está inflado.
- **Custo do modo detalhado.** Cada leitura inclui ≈4 leituras de relógio das sondas (início e fim de `fp_read` e de `fp_copy`) e cada chamada, 2 (`fp_primary`), ou seja, algo como 7000 a 8000 leituras por quadro na `pp_smoke` se as chamadas forem ≈2500 (suposição); a 20 a 30 ns por leitura (suposição de seções anteriores, **não medida**) são ≈0,14 a ≈0,24 ms por quadro. O valor absoluto do `fe_pm4` e dos `fp_*` no modo detalhado é maior que o de produção por essa conta; o valor fora do modo detalhado **não foi medido**. Há uma comparação indicativa, com a ressalva de que as cenas e os binários diferem (ver a proveniência do A/B): nas quatro execuções `lf_*`, sem as sondas, o `fe_pm4` ficou em 1,48 a 1,66 µs/draw (média 1,59; 4,03 a 4,71 ms), e nas três `pp_*`, com elas, em 1,46 a 1,56 µs/draw (média 1,52; 3,03 a 4,20 ms). A diferença entre as duas faixas é menor que o aumento do `fe_pm4` OFF dentro das `lf_*` (de 1,482 para 1,630, +0,148 µs/draw), então o overhead das sondas ficou **abaixo do ruído** desta comparação, e ela não separa overhead de ruído nem de cena. O que isso implica para o `fp_scan`: se o overhead é pequeno, o resíduo é mais provavelmente trabalho real (o analisador e a configuração por chamada) do que relógio, mas isso **não está decidido**: o estimado de 0,14 a 0,24 ms é só ≈4% a ≈6% dos 3,73 ms do `fe_pm4`, e a comparação acima não tem resolução para confirmá-lo. Os relógios do `fe_pm4` (duas leituras por chamada) e de `capture_timings.pm4_us` (duas por chamada, sempre ligado) já existiam antes das sondas.
- **Nenhuma sonda cobre o tamanho do anel.** Quantos bytes e quantas palavras do anel o analisador varre por draw e por quadro é a pergunta que falta para interpretar o `fp_scan` (a varredura do anel é o que o analisador faz; a leitura das dependências é só uma parte dele).

### Correção sobre o número antigo de leituras e bytes por quadro

O texto do plano desta fase citava "≈490 leituras e ≈5,6 MB por quadro (máx. 42 MB)" dos logs de captura. Esses números vêm das linhas `native Vulkan capture frame=… reads=… bytes=…`, impressas **a cada 120 quadros (e em quadros com mais de 100 ms de captura) e com os valores de um único quadro** (o `capture_timings` é zerado a cada quadro). O `reads` e o `bytes` desse contador são incrementados em dois lugares: em `ReadCommittedGuest` (que serve a captura PM4, a varredura que mantém o espelho atualizado nos comandos sem verificação de pacotes e a cópia de texturas que mudaram) e no laço de hash de `CaptureTextures` (`++capture_timings.reads; capture_timings.bytes += range.length`, uma vez por faixa hasheada). Ou seja, **não são só PM4**; os bytes são dominados pela leitura das texturas.

Refeito com as linhas das três execuções (63 amostras de um quadro; média simples, indicativa): 475 leituras e 5,77 MB (MB = 10^6 bytes) por amostra (por execução: `pp_smoke` 588 leituras e 12,0 MB, com máximo de 43,8 MB; `pp1` 408 e 1,98 MB; `pp2` 421 e 2,84 MB). Isso reproduz a ordem do número antigo e mostra que ele varia de 0,03 MB a 43,8 MB por amostra; não é um valor estável por quadro. Nas mesmas 9 janelas usadas acima (uma amostra de quadro contra a média de 120 quadros do `pm4 deps`, então a comparação é só indicativa): `pp_smoke` 913,8 leituras e ≈25 600 KiB contra 694,6 leituras e 53,3 KiB do PM4; `pp1` 656 e ≈1700 KiB contra 651,2 e 51,0; `pp2` 469,8 e ≈3480 KiB contra 426,4 e 31,8. Em **leituras**, o PM4 é a maior parte do contador (na `pp1` quase tudo); em **bytes**, o PM4 é da ordem de 0,2% a 3% do contador. A conclusão da tarefa anterior ("o 5,6 MB era leitura de textura, não PM4") se sustenta, com a ressalva de que a comparação é de uma amostra contra uma média.

### O que as sondas dizem sobre mudanças candidatas

- **Remover a cópia redundante `sources` ou a alocação intermediária não vale a pena.** O `fp_copy` inteiro é 0,09 a 0,15 ms (0,043 a 0,055 µs/draw), e ele contém também o `batch.bytes.insert` e o `batch.ranges.push_back`, que são necessários; o que daria para remover é só uma parte disso, abaixo da resolução do A/B (ver o experimento rejeitado abaixo). Pela leitura do código, o `CheckedGuestReads` mantém cada cópia viva até o `Reset()` do início do próximo `EndCmd`, o que sugere que `sources` seja redundante hoje, mas o contrato de `GuestMemoryReader` (que é uma `std::function` genérica que não promete o tempo de vida do `span`) é a razão da cópia; não verifiquei se algum outro leitor depende dela, e o teto não paga esse risco.
- **O `fp_read` (0,46 a 0,72 ms; ≈1,04 a 1,08 µs por leitura, incluindo duas leituras de relógio) é o único bloco de leitura com teto visível**, mas contém a leitura necessária; o que seria evitável é a chamada de sistema e a alocação por dependência, e o contrato (`CheckedGuestReads` existe para devolver vazio em vez de falhar quando a página não está confirmada) tem de ser mantido. A parte evitável **não foi medida**.
- **A pergunta aberta é o resíduo `fp_scan`** (média 2,36 ms, 0,965 µs/draw, 63% do `fe_pm4`): o analisador, a configuração por chamada ou o overhead das sondas. O que falta saber é como os 3,73 ms médios do `fe_pm4` se dividem entre o trabalho do analisador e do resto da função, de um lado, e o overhead dos relógios das sondas, do outro: os ≈1,36 ms medidos diretamente (`fp_primary` + `fp_read` + `fp_copy`) e os ≈2,36 ms do resíduo `fp_scan` incluem, cada um, uma parte de overhead desconhecida. Sem essa divisão, o ganho possível de qualquer mudança no `fe_pm4` é desconhecido; o teto do estágio inteiro (3,73 ms) é só um limite, não uma estimativa.

**Próxima medição (sem alterar comportamento).** (a) Contar por quadro as chamadas de `CapturePm4Dependencies` e os bytes e palavras do anel varridos (por draw e por quadro). (b) Cronometrar o **bloco PM4 inteiro com um par de relógios por quadro** em vez de um por chamada (por exemplo, amostrando uma chamada em N, ou re-executando a varredura dos anéis do quadro num espelho descartável, fora do caminho normal, num laço cronometrado uma vez; o desenho exato **não foi decidido** e tem de não alterar o estado do espelho nem o arena), e comparar com a soma dos escopos por chamada: a diferença é o overhead de relógio; o que sobra é o custo do analisador e do resto. Só depois disso faz sentido escolher uma mudança.

### Não adotado: A/B inconclusivo da leitura preguiçosa dos fetch constants (`SR_NATIVE_LAZY_FETCH`)

**O que foi tentado.** Em `Renderer::CaptureTextures` (chamada a cada draw), o laço percorre os 32 slots de textura e monta, para cada um, as 6 palavras do fetch constant (`capture_mirror_.written(reg) ? capture_mirror_.reg(reg) : Load32(...)`), e só depois testa `IsTextureBound(fetch[0])`: 192 leituras de registrador por chamada, a maior parte para slots não ligados e descartadas. A mudança lia só `fetch[0]` antes do teste e as outras 5 palavras só depois dele (`SR_NATIVE_LAZY_FETCH=0` restaurava a leitura completa, para o A/B no mesmo binário).

**Verificação da premissa (feita e correta).** Reli o laço na árvore: depois do `continue` do teste de ligado, a primeira utilização do array é a chave de `captured_textures_[fetch]`, e `fetch[1..5]` de um slot não ligado nunca são consumidos; `written()` e `reg()` do espelho são leituras sem efeito colateral, `Load32` também (uma busca nas faixas capturadas da thread, um `memcpy` e um `bswap`), e `IsTextureBound` é puro. Logo a mudança não alteraria `cur_.textures`, `cur_.texture_errors` nem `captured_textures_`: seria equivalente por construção. O que ficou em aberto é só o ganho.

**Proveniência do binário do A/B.** As execuções `lf_*` usaram um binário anterior às sondas do PM4: o `superman_returns.exe` compilado a partir do HEAD `4ce4744` com a edição preguiçosa na árvore de trabalho (o relatório da tarefa registra o executável das 08:51:40, mais novo que o `native_renderer.cpp` das 08:51:13, sem recompilação entre as quatro execuções; pelos horários dos arquivos, as quatro execuções ocorreram entre 08:57 e 09:04). Essa edição **nunca foi commitada** e foi revertida da árvore com `git checkout` logo depois (o relatório avisa que o executável ainda a continha). Por isso os timelines `lf_*` **não têm linhas `fp_*`** (0 ocorrências de `fp_` em `logs/timeline_lf_*_vulkan.txt`, 4 em cada `logs/timeline_pp*_vulkan.txt`). Esse executável foi **sobrescrito** pela recompilação das 09:07:19, feita depois da reversão, a partir dos fontes que viraram o commit das sondas `ed6355c` (commitado às 09:10:06), sem o código preguiçoso (`grep` de `lazy_fetch` no fonte = 0); é o binário das execuções `pp_*`. Consequências: (1) o A/B **não pode ser reproduzido** (o código do modo ON não está em nenhum commit e o executável que o continha não existe mais; recriá-lo exigiria reescrever a mudança descrita acima); (2) os números `lf_*` e `pp_*` **não devem ser comparados entre si** (binários, cenas e momentos diferentes), com a única exceção, explícita e só indicativa, da comparação do `fe_pm4` com e sem sondas na seção "O que as sondas dizem, e o que não dizem" (acima). As fontes são os logs e os relatórios locais em `.superpowers/sdd/` (`lf-task-1-report.md`, `lf-task-1b-report.md`, `pp-task-2-report.md`), todos **git-ignorados**: um leitor do repositório não os tem, e este parágrafo é o registro.

**A/B (Vulkan, mesmo binário, ordem OFF, ON, ON, OFF, todas com `-Timeline -TimelineDetail -Profile`).** Números refeitos agora a partir de `logs/timeline_lf_*_vulkan.txt` e `logs/bench_lf_*_vulkan.log` (draws = média das últimas 9 janelas `Vulkan profile`; µs por draw = ms × 1000 ÷ draws):

| Execução | Modo | `fe_textures` ms | draws | `fe_textures` µs/draw | Intervalo médio | FPS do bench: parado (mín.) / andando (mín.) |
| --- | --- | ---: | ---: | ---: | --- | --- |
| `lf_off1` | OFF (leitura completa) | 6,02 | 2719,4 | **2,214** | 39,8 ms (25,1 FPS) | 25,1 (19,2) / 25,1 (24,1) |
| `lf_on1` | ON (preguiçoso) | 6,24 | 2759,6 | **2,261** | 41,5 ms (24,1 FPS) | 22,7 (17,7) / 25,7 (24,4) |
| `lf_on2` | ON | 6,56 | 2843,9 | **2,307** | 43,6 ms (22,9 FPS) | 22,8 (17,7) / 23,2 (22,4) |
| `lf_off2` | OFF | 6,75 | 2822,9 | **2,391** | 43,1 ms (23,2 FPS) | 22,4 (16,2) / 23,9 (22,4) |

Os estágios maiores que contêm o `fe_textures`, por draw (mesma divisão, tabelados a partir dos mesmos `logs/timeline_lf_*_vulkan.txt`; µs/draw):

| Execução | `fe_end` | `frontend` | `game` |
| --- | ---: | ---: | ---: |
| `lf_off1` | 4,067 | 7,075 | 14,433 |
| `lf_on1` | 4,262 | 7,414 | 14,828 |
| `lf_on2` | 4,378 | 7,627 | 15,078 |
| `lf_off2` | 4,432 | 7,627 | 15,020 |
| par 1 (`on1` − `off1`) | +0,195 | +0,339 | +0,395 |
| par 2 (`on2` − `off2`) | −0,054 | 0,000 | +0,058 |
| deriva OFF (`off2` − `off1`) | +0,365 | +0,552 | +0,587 |

**Modo detalhado.** As quatro execuções usam `-TimelineDetail`. O `fe_textures` é medido por um único `DetailScope` por chamada de `EndCmd` de draw (em volta de `CaptureTextures`, no `EndCmd`), igual nos dois modos; o custo de relógio dele é, portanto, simétrico e não cria diferença entre OFF e ON, mas entra no valor absoluto de `fe_textures` (maior que o de produção) e dilui, em termos relativos, qualquer efeito. O valor fora do modo detalhado não foi medido.

- **Os pares discordam no sinal.** Par 1 (`on1` − `off1`): **+0,048 µs/draw** (+2,2%, pior). Par 2 (`on2` − `off2`): **−0,084 µs/draw** (−3,5%, melhor). Média ON 2,284 contra OFF 2,302 µs/draw (−0,018, −0,8%).
- **Os controles se moveram tanto quanto o efeito.** Nos mesmos pares, os 14 estágios que a mudança não toca (`fe_begin`, `fe_ring`, `fe_device`, `fe_index`, `fe_streams`, `fe_flush`, `fd_shaders`, `fs_prep`, `fs_plan`, `fe_push`, `fe_pm4`, `fs_buffer`, `fb_refresh` e `fb_hash`; ficam de fora os agregados que contêm o `fe_textures`, como `fe_end`, `frontend` e `game`, e os resíduos; alguns desses estágios se sobrepõem entre si, ver `fb_hash` ⊂ `fb_refresh`) variaram de −0,054 (`fs_buffer`; `fs_plan` ficou em −0,051) a +0,113 µs/draw (`fe_pm4`) no par 1 (faixa de 0,167) e de −0,011 (`fs_plan` e `fs_buffer`) a +0,038 (`fe_index`) no par 2 (faixa de 0,049); o `fe_index` foi de +0,090 e +0,038. Na deriva OFF (`off2` − `off1`) os mesmos 14 estágios foram de +0,003 (`fe_flush`) a +0,148 (`fe_pm4`). No par 1 a faixa dos controles (0,167) é maior que o efeito (+0,048); no par 2 o −0,084 fica além da faixa dos controles (0,049), mas o par 1 tem o sinal contrário e a deriva do mesmo modo no `fe_textures` (+0,177) é maior que os dois efeitos.
- **Deriva.** O intervalo médio foi de 39,8 ms (`off1`) para 43,1 ms (`off2`), um aumento de +8,3% ao longo das quatro execuções, no **mesmo modo**; o `fe_textures` por draw OFF subiu de 2,214 para 2,391 (+0,177 µs/draw, +8,0%, ≈0,48 ms por quadro a ≈2700 draws), mais que qualquer par. A ordem OFF, ON, ON, OFF cancela uma deriva linear nas médias por modo, mas não outras formas, e dois pares são pouca coisa.
- **Decisão: rejeitar como inconclusivo.** A regra de aceitação (o estágio caiu por draw, nos dois pares) não foi atendida. Não afirmo que a mudança não ajuda; afirmo que este A/B não mostra efeito.

**O tamanho esperado do efeito não é conhecido, e uma estimativa anterior dele não se sustenta.** O relatório do implementador do A/B (`.superpowers/sdd/lf-task-1b-report.md`, local e git-ignorado, não está no repositório) estimou, sem medida, ≈0,01 µs/draw ("dezenas de microssegundos" por quadro), o que pareceria dezenas de vezes abaixo do ruído. Refazendo a conta pelo código: cada chamada lê 192 palavras e a mudança pula 5 por slot não ligado, ou seja, ≥120 leituras por draw se ao menos 24 dos 32 slots estiverem desligados (**o número de slots ligados por draw não foi contado**). Em ≈2700 draws por quadro são ≥324 000 leituras por quadro; a 1 a 3 ns por leitura (suposição, **não medida**) são ≈0,12 a ≈0,5 µs/draw (o limite superior supõe os 32 slots desligados, 160 leituras), ou ≈0,3 a ≈1,3 ms por quadro, **possivelmente próximo de 0**: as duas premissas (pelo menos 24 slots desligados por draw; 1 a 3 ns por leitura) não foram medidas, e leituras abaixo de 1 ns (o mirror é um teste de limites e um acesso a array) não estão excluídas. O topo da faixa está na ordem da resolução do A/B, não dezenas de vezes abaixo dela. Os números do A/B (média −0,018, pares +0,048 e −0,084) **argumentam contra o topo da faixa** (um efeito de ≈0,5 µs/draw aparecia, no mínimo, como um par bem mais negativo, mesmo com ruído de ±0,1), mas **não excluem a parte de baixo** (≈0,1 µs/draw ou menos), que a deriva e o desvio dos controles escondem. Conclusão honesta: **o ganho é desconhecido** (entre ≈0 e ≈0,5 µs/draw), não "desprezível".

**A lição.** Com um aumento de ≈8% ao longo das quatro execuções (no intervalo, 39,8 → 43,1 ms, e no `fe_textures` OFF, 2,214 → 2,391 µs/draw), os controles que se moveram até +0,113 µs/draw dentro de um par (faixa de 0,167 no par 1) e o `fe_textures` do mesmo modo que subiu +0,177 µs/draw (≈0,48 ms por quadro), a **resolução realista** deste A/B é de **≈0,1 a ≈0,2 µs/draw (≈0,3 a ≈0,5 ms por quadro a ≈2700 draws)**: efeitos por draw dessa grandeza ou menores **não se resolvem** por A/B de bench, com ou sem pares adjacentes (a resolução vale para esta máquina, esta cena e quatro execuções; não é uma propriedade medida do método). Candidatos cujo teto é desse tamanho precisam de uma **micro-medida** (um escopo cronometrado só em volta do trecho, como foi feito com as sondas do PM4) ou de um desenho pareado mais longo (mais pares, execuções mais longas, intercaladas), antes de se escrever a mudança. Aqui isso significa um `DetailScope` em volta do laço de montagem dos fetch constants (um par de relógios por chamada, igual nos dois modos): se der menos de ≈0,3 ms por quadro, a ideia está encerrada; se der mais, a mudança (já conhecida, equivalente por construção) volta a valer um A/B mais longo. **A mudança não foi adotada.**

### Candidatos da próxima fase (ordem proposta)

Tetos: média das três execuções desta fase (ms por quadro e µs por draw, cada execução dividida pelos seus draws; a `pp2` é uma cena mais leve, então as médias não são as de um único cenário). Só seriam alcançados se o estágio inteiro sumisse; incluem o custo dos relógios das sondas; tetos aninhados não se somam (`fp_*` ⊂ `fe_pm4`; `fb_hash` ⊂ `fb_refresh`, que está espalhado por `fs_buffer`, `fs_other` e `fe_index`); e o quanto vira FPS depende de `worker` e `gpu`, perto do orçamento. **Resolução realista do A/B de bench: ≈0,3 a ≈0,5 ms por quadro (≈0,1 a ≈0,2 µs/draw)** (ver "A lição"); abaixo disso a mudança não se verifica por bench e exige micro-medida. Um teto de estágio inteiro acima dessa resolução **não diz** se uma fração realista dele seria detectável: um ganho de, por exemplo, 20% a 50% do teto de um estágio de 0,6 ms (0,12 a 0,3 ms) já cai na faixa da resolução ou abaixo dela; só os tetos de ≈1 ms ou mais deixam folga para ganhos parciais detectáveis. Todo item que muda comportamento tem de preservar a **imagem bit a bit**, a **semântica do rastreamento de buffer sujo**, a **precedência de leitura dos bytes capturados** e, nos do PM4, o **contrato de `GuestMemoryReader`** (tempo de vida dos `span`, falha por memória não legível sem travar). Verificação comum: `tests/native` e `python -m pytest tests/tools -q` passam e o gate de imagem (`-Gate check` contra a referência local; é grosseiro).

Esta lista reordena a da Fase 1.1c: o item 1 dela ("decomposição de `fe_textures` e `fe_pm4`") foi feito para o `fe_pm4` (as sondas acima) e segue aberto para o `fe_textures` (item 6 abaixo); os itens 2, 3, 4 e 7 dela seguem válidos e são os itens 7, 8, 9 e 10 abaixo; o item 5 dela (custo do relógio) segue aberto e é o item 2 abaixo; o item 6 dela (ampliar o acerto do cache de stream ou consultar `tracked_` direto, risco alto no rastreamento de sujo) segue **bloqueado pelo item 8 abaixo** (a medição de `PlanBuffer`) e não vira item desta lista antes dele; o item 8 dela (reduzir o `CaptureDevice` fora dos shaders e as buscas de shader: `fd_other`, hoje 1,08 ms nas três execuções desta fase contra 1,20 ms na 1.1c, `fd_shaders` e `fs_prep`) segue **bloqueado pelo item 9 abaixo** (a medição de `fe_index` e `fd_other`), que é o sucessor dele, e o item 11 dá `fs_prep` como teto pequeno. Nenhum dos dois é um candidato de mudança agora.

1. **Medir: o analisador do PM4 e o overhead das sondas** (estágio `fp_scan`, teto 2,36 ms, 0,965 µs/draw; acima da resolução). O desenho está na seção das sondas: contar chamadas e bytes/palavras do anel por draw e por quadro; cronometrar o bloco PM4 com um par de relógios por quadro. Risco: nenhum no comportamento. Verificação: as chamadas contadas são compatíveis com o campo `packets=` do perfil (que também conta comandos sem bytes de anel), e a diferença entre a soma dos escopos por chamada e o bloco cronometrado uma vez estima o overhead. Sem decisão do usuário.
2. **Medir: custo de uma leitura de relógio** (sem teto próprio; item 5 da 1.1c). Várias estimativas deste documento usam 20 a 30 ns por leitura sem medida (as sondas do PM4, os escopos `fb_*`, a sonda do `fs_resolve`). Risco: nenhum. Verificação: um microbenchmark isolado da leitura de `steady_clock`. Sem decisão do usuário.
3. **Reaproveitar os buffers por chamada do PM4, só depois do item 1** (estágio `fp_primary`, teto 0,61 ms, 0,251 µs/draw; **no limite da resolução**: o teto de estágio inteiro fica só um pouco acima de ≈0,3 a ≈0,5 ms, e uma fração realista dele cairia dentro da resolução; o estágio contém a cópia, que é necessária, e dois relógios por chamada, então o ganho esperado **não é conhecido** e pode ser bem menor que o teto, sem que os dados digam quanto). Ideia: reutilizar um vetor por thread em vez de um vetor novo por chamada. Risco médio: o `primary` tem de ficar estável durante toda a varredura, inclusive nos callbacks aninhados, porque o arena cresce (é a razão do comentário no código); a função não pode ser reentrante com o mesmo buffer. Verificação: o teste de equivalência existente (`pm4_capture_probe_counts_reads_and_changes_nothing`, que compara `batch.bytes` e `batch.ranges` com e sem sonda) estendido a chamadas repetidas de tamanhos diferentes e ao caso de falha com reversão; micro-medida antes do A/B. Sem decisão do usuário.
4. **Reduzir o custo por dependência lida** (estágio `fp_read`, teto 0,62 ms, 0,253 µs/draw, ≈1,05 µs por leitura; **no limite da resolução**, como o item 3: o teto é só um pouco maior que a faixa de ≈0,3 a ≈0,5 ms e contém a leitura necessária; só depois do item 1). Alvo: a chamada de sistema e a alocação por dependência. Risco **alto** no contrato: `CheckedGuestReads` existe para devolver vazio (e não falhar) com páginas não confirmadas; qualquer alternativa tem de manter isso e o tempo de vida dos `span` durante os callbacks aninhados. Verificação: os testes de memória ilegível existentes, gate de imagem, contagem de leituras idêntica. Sem decisão do usuário se o contrato for preservado; **não foi desenhado**.
5. **Remover a cópia `sources`** (estágio `fp_copy`, teto 0,13 ms, 0,051 µs/draw; **abaixo da resolução**). Não é candidato: o teto é pequeno, contém trabalho necessário e o risco é o do contrato de tempo de vida. Fica registrado para não ser reproposto sem dados novos.
6. **Medir: decomposição do `fe_textures` fora do hash** (estágio `fe_textures`, teto 5,22 ms, 2,13 µs/draw; acima da resolução). A primeira sub-região é o laço de montagem dos fetch constants (encerra ou revive o experimento não adotado); as demais, a busca em `captured_textures_`, a consulta de escrita (watch) e o resto do trabalho por slot ligado, **sem alterar a política de watch e de hash das texturas** (a "Atualização: o hash quase não pesa" mostrou o hash em ≈7% do `textures_ms`; o resto, ≈3,6 ms nas amostras daquela seção, não foi decomposto). Risco: nenhum. Verificação: as sub-regiões fecham com o estágio. Sem decisão do usuário (a política de texturas já foi decidida).
7. **Medir: `fb_refresh` por ponto de chamada e desfecho** (estágios `fb_refresh`, teto 2,65 ms, 1,078 µs/draw; `fb_hash`, 1,41 ms, 0,575 µs/draw; acima da resolução). Como o item 2 da 1.1c: chamadas por ponto de chamada, hashes por motivo, quantas vezes o hash dá "mudou", e o `fb_refresh` sem o hash por ponto de chamada. Risco: nenhum. Verificação: as somas fecham com `calls` e `hashes`. Sem decisão do usuário.
8. **Medir: repetição de buffers por slot e desfecho do `PlanBuffer`** (estágios `fs_plan`, teto 2,46 ms, 1,005 µs/draw; `fs_buffer`, 2,33 ms, 0,951 µs/draw; acima da resolução). É o item 3 da 1.1c, ainda aberto: endereços-base distintos por slot, simulação de caches de 2 e 4 entradas, `action` 0, 1 ou 2 e se o `tracked_` achado está sujo. Risco: nenhum. Verificação: as contagens fecham com `evaluated`, `fast` e `slow`. Sem decisão do usuário.
9. **Medir: `fe_index` e `fd_other`** (`fe_index`, teto 1,40 ms, 0,570 µs/draw; `fd_other` = `fe_device` − `fd_shaders`, 1,08 ms, 0,441 µs/draw; acima da resolução). Item 4 da 1.1c. Risco: nenhum. Verificação: as sub-regiões fecham com os estágios. Sem decisão do usuário.
10. **Política do hash de conteúdo de buffers rastreados** (estágio `fb_hash`, teto 1,41 ms, 0,575 µs/draw; acima da resolução). **Exige decisão do usuário** e não é proposta aqui: é o mecanismo de correção contra escritas por aliases virtuais, e para buffers **não existe** decisão equivalente à das texturas (até 16 quadros). Antes de levá-la ao usuário: o item 7, um teste que force uma escrita por alias virtual e conte os quadros com dado velho, e a definição de quantos quadros de atraso são aceitáveis. Qualquer ideia que mude quando ou se o hash roda, ou onde ele roda, está nessa categoria.
11. **Tetos pequenos, não candidatos** (`fs_prep`, 0,35 ms, 0,143 µs/draw; `fe_push`, 0,26 ms, 0,108 µs/draw; `fe_end_rest`, 0,65 ms, 0,267 µs/draw): o `fs_prep` (0,35 ms) está dentro da faixa da resolução (≈0,3 a ≈0,5 ms) e o `fe_push` (0,26 ms), abaixo dela; o relógio do próprio escopo é parte relevante deles. O `fe_end_rest` é um resíduo sem escopo próprio, não um alvo.

**Teto de estágio inteiro bem acima da resolução (≳1 ms):** 1, 6, 7, 8, 9, 10; isso é só o teto: não diz se uma fração realista dele seria detectável (ver a ressalva acima), e os itens 1, 6, 7, 8 e 9 são medições, que não dependem de detectar um ganho por A/B. **No limite da resolução (teto ≈0,6 ms, ≈0,25 µs/draw):** 3 e 4. **Abaixo ou dentro da faixa da resolução:** 5 (0,13 ms) e 11 (`fs_prep` 0,35 ms; `fe_push` 0,26 ms; `fe_end_rest` 0,65 ms, um resíduo). **Sem teto próprio:** 2. Precisam de decisão de política do usuário: **só o 10**. Os itens 1, 2, 6, 7, 8 e 9 só medem.

**O que os dados não decidem.** O custo do analisador do PM4 sozinho (o `fp_scan` é um resíduo); quantas chamadas de `CapturePm4Dependencies` e quantos bytes de anel há por quadro; o custo de uma leitura de relógio na máquina (as estimativas desta seção usam 20 a 30 ns, não medido); o tamanho do ganho do experimento não adotado (a estimativa anterior, ≈0,01 µs/draw, não se sustenta; a faixa de ≈0,12 a ≈0,5 µs/draw acima depende de 1 a 3 ns por leitura e de pelo menos 24 slots desligados por draw, ambos não medidos, e o ganho pode estar **próximo de 0**; o A/B argumenta contra o topo da faixa, mas não exclui a parte de baixo); quanto de qualquer redução vira FPS. Três execuções do PM4 em uma máquina, uma delas de cena diferente das outras; quatro execuções do A/B, com um aumento de ≈8% ao longo delas, e em um binário que não existe mais (ver a proveniência).

### Suítes desta fase

`python -m pytest tests/tools -q`: 69 passaram. Esta fase não altera código (só este documento); as suítes de C++ foram executadas na tarefa das sondas (`ctest --test-dir build/tests-native`: 2 de 2) e não foram repetidas aqui.

## Tempo real de GPU (Fase 2.0)

Plano: `superpowers/plans/2026-10-09-gpu-real-time.md`. A pergunta que as seções anteriores deixaram aberta: o estágio `gpu` do Vulkan (≈30 a 32 ms) é só um limite superior (ressalva 1 da lista no início do documento) porque pode incluir a fila atrás do quadro anterior; quanto dele é tempo real de execução, e a GPU é de fato um limitante? Esta fase não muda renderização, ordem de comandos nem sincronização: ela só lê os timestamps que já existiam, nas duas APIs, e grava dois estágios novos.

| Estágio | O que mede (por quadro) |
| --- | --- |
| `gpu_real` | duração do quadro na GPU menos a sobreposição com o quadro anterior |
| `gpu_idle` | intervalo (≥ 0) entre o fim do quadro anterior na GPU e o início deste |
| `gpu_overlap` | derivado no relatório: `gpu` menos `gpu_real`; não é gravado |

`gpu` continua como era (do primeiro timestamp ao último do quadro). Os três estágios novos não entram no `limitante`. Por quadro: `sobreposição = max(0, fim_anterior − início)`, `ocioso = max(0, início − fim_anterior)` e `real = duração − min(sobreposição, duração)`.

### Medição

Binário release de 2026-10-09 (`port\out\build\win-amd64-release\superman_returns.exe`, gerado às 09:50, depois da última edição dos fontes do commit `a2d4f46`). Quatro execuções, uma por vez, sem build nem outra carga em paralelo, na ordem Vulkan, D3D12, D3D12, Vulkan (intercaladas para diluir a deriva): `tools\bench\bench_api.ps1` com `-Timeline -Profile` (sem `-TimelineDetail`) e os nomes `gr1_vulkan`, `gr2_d3d12`, `gr3_d3d12` e `gr4_vulkan`. Janela do relatório: os últimos 1100 quadros, como nas fases anteriores. O `-Profile` foi usado para ter os draws por quadro. Todos os valores abaixo vêm das tabelas do relatório, dos CSVs `logs\timeline_gr*.csv`, de `logs\bench_results.csv` e dos `logs\bench_gr*.log`.

### Tabelas impressas pelo relatório

Execução `gr1_vulkan` (bench: parado 26,3 FPS, andando 29,3 FPS):

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 36.1 ms = 27.7 FPS | 1% low 20.4 FPS | mínimo 2.1 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio           média     p50     p99   bloq.   util.  folga p99  limitante
game              35.58   35.28   46.59    0.00     99%     -13.26        72%
game_other        28.46   28.38   35.55    0.00     79%      -2.22          -
capture            7.12    6.76   14.02    0.00     20%      19.31          -
front_wait         0.50    0.00    3.89    0.00      1%      29.45          -
worker            30.66   30.38   41.92    2.37     85%      -8.59        14%
record            27.69   27.09   42.01    2.20     77%      -8.68         6%
gpu               30.70   30.48   36.60    0.00     85%      -3.26         7%
gpu_real          30.70   30.48   36.60    0.00     85%      -3.26          -
gpu_idle           5.39    2.83   19.93    0.00     15%      13.40          -
gpu_overlap        0.00    0.00    0.00    0.00      0%      33.33          -
```

Execução `gr2_d3d12` (bench: parado 28,7 FPS, andando 28,2 FPS):

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 34.9 ms = 28.6 FPS | 1% low 20.4 FPS | mínimo 10.8 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio           média     p50     p99   bloq.   util.  folga p99  limitante
game              26.21   25.40   38.34    0.00     75%      -5.00        22%
front_wait         8.70    9.11   21.85    0.00     25%      11.48          -
worker            32.93   33.26   46.09    0.00     94%     -12.75        78%
gpu               22.44   24.03   27.01    0.00     64%       6.32         0%
gpu_real          22.44   24.03   27.01    0.00     64%       6.32          -
gpu_idle          12.49   11.85   26.36    0.00     36%       6.97          -
gpu_overlap        0.00    0.00    0.00    0.00      0%      33.33          -
```

Execução `gr3_d3d12` (bench: parado 27,9 FPS, andando 28,2 FPS):

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 35.3 ms = 28.3 FPS | 1% low 20.1 FPS | mínimo 11.4 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio           média     p50     p99   bloq.   util.  folga p99  limitante
game              25.75   25.36   37.95    0.00     73%      -4.62        10%
front_wait         9.55    9.65   23.42    0.00     27%       9.91          -
worker            34.44   33.96   47.97    0.00     97%     -14.64        90%
gpu               23.07   23.70   26.24    0.00     65%       7.09         0%
gpu_real          23.07   23.70   26.24    0.00     65%       7.09          -
gpu_idle          12.25   11.23   26.02    0.00     35%       7.31          -
gpu_overlap        0.00    0.00    0.00    0.00      0%      33.33          -
```

Execução `gr4_vulkan` (bench: parado 24,2 FPS, andando 27,1 FPS):

```
Quadros analisados: 1100 (descartados por falta de estágio: 0)
Intervalo médio 39.3 ms = 25.4 FPS | 1% low 19.2 FPS | mínimo 2.1 FPS
Orçamento por quadro: 33.3 ms (tempos em ms)

estágio           média     p50     p99   bloq.   util.  folga p99  limitante
game              38.78   38.47   50.60    0.00     99%     -17.27        73%
game_other        30.67   30.51   38.98    0.00     78%      -5.65          -
capture            8.11    7.75   15.26    0.00     21%      18.07          -
front_wait         0.49    0.00    1.42    0.00      1%      31.92          -
worker            34.34   33.98   46.80    1.89     87%     -13.47        18%
record            29.50   28.76   43.05    2.06     75%      -9.72         4%
gpu               32.77   32.26   38.29    0.00     83%      -4.96         5%
gpu_real          32.77   32.26   38.29    0.00     83%      -4.96          -
gpu_idle           6.53    3.86   23.76    0.00     17%       9.57          -
gpu_overlap        0.00    0.00    0.00    0.00      0%      33.33          -
```

### Resumo por execução (ms médios por quadro)

O intervalo é o do relatório com duas casas (calculado dos CSVs; o relatório imprime uma). As duas últimas colunas são `gpu_real ÷ intervalo` e `gpu_idle ÷ intervalo`, a mesma conta do `util.` do relatório.

| Execução | Bench (parado / andando, FPS) | Intervalo | `game` + `front_wait` | `gpu` | `gpu_real` | `gpu_overlap` | `gpu_idle` | `gpu_real` ÷ intervalo | `gpu_idle` ÷ intervalo |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `gr1_vulkan` | 26,3 / 29,3 | 36,11 | 36,08 | 30,70 | 30,70 | 0,00 | 5,39 | 85,0% | 14,9% |
| `gr2_d3d12` | 28,7 / 28,2 | 34,92 | 34,91 | 22,44 | 22,44 | 0,00 | 12,49 | 64,3% | 35,8% |
| `gr3_d3d12` | 27,9 / 28,2 | 35,33 | 35,30 | 23,07 | 23,07 | 0,00 | 12,25 | 65,3% | 34,7% |
| `gr4_vulkan` | 24,2 / 27,1 | 39,31 | 39,27 | 32,77 | 32,77 | 0,00 | 6,53 | 83,4% | 16,6% |

Faixas por API nas duas execuções de cada uma: Vulkan, `gpu_real` 30,70 a 32,77 ms, `gpu_idle` 5,39 a 6,53 ms, intervalo 36,11 a 39,31 ms; D3D12, `gpu_real` 22,44 a 23,07 ms, `gpu_idle` 12,25 a 12,49 ms, intervalo 34,92 a 35,33 ms.

Conferência: `gpu_real + gpu_idle` dá 36,09, 34,93, 35,31 e 39,30 ms, contra os intervalos de 36,11, 34,92, 35,33 e 39,31 ms. Isso é esperado quando não há sobreposição (início a início da GPU é o período do quadro) e confere os dois estágios entre si e com o intervalo do jogo; não é uma evidência independente de que a GPU seja a causa de nada.

### Sobreposição entre quadros: não observada

Verificação quadro a quadro nos CSVs, com `gpu_real` contra `gpu`:

| Execução | Quadros da janela (1100) com `gpu_real` < `gpu` | Quadros do arquivo inteiro com `gpu_real` < `gpu` | Quadros com `gpu_real` > `gpu` |
| --- | ---: | ---: | ---: |
| `gr1_vulkan` | 0 | 0 de 2564 | 0 |
| `gr2_d3d12` | 0 | 0 de 3944 | 0 |
| `gr3_d3d12` | 0 | 0 de 3824 | 0 |
| `gr4_vulkan` | 0 | 0 de 2384 | 0 |

Em nenhum dos 12.716 quadros dos arquivos inteiros (4400 nas janelas) o início de um quadro na GPU veio antes do fim do anterior: `gpu_real` é igual a `gpu` em todos os quadros, nas duas APIs, e o `gpu_overlap` do relatório é 0,00 em média, p50 e p99. Em cada arquivo o único quadro com `gpu_idle` igual a 0 é o quadro 1 (o primeiro colhido, que não tem anterior); nas janelas nenhum quadro tem `gpu_idle` zero. Com `gpu_idle` acima de 1 ms, ficaram 85,5% e 83,4% dos quadros no Vulkan (`gr1`, `gr4`) e 100% nos dois do D3D12.

**Consequência para as seções anteriores.** A ressalva 1 dizia que o `gpu` do Vulkan podia incluir fila atrás do quadro anterior e devia ser tratado como limite superior. Nesta máquina, nesta configuração (Intel UHD, 1280x720, vsync, limite de 30 FPS, 2 quadros em voo) e nestas quatro execuções, essa parte da ressalva não se confirmou: o `gpu_real` é igual ao `gpu` em todos os quadros, ou seja, não houve sobreposição a descontar, e o `gpu` do Vulkan coincide com o `gpu_real`. Portanto, para esta configuração, o `gpu` do Vulkan deixa de ser "limite superior" no sentido de fila atrás do quadro anterior. Não foi investigado o que serializa os quadros na GPU (só que eles são serializados); em outra configuração (outra GPU, mais quadros em voo, sem vsync) isto precisa ser medido de novo. Continua valendo a ressalva de que um intervalo de timestamps (topo do pipe no começo, fundo do pipe no fim) mede o tempo de execução do quadro e não a ocupação: dentro do quadro a GPU pode ter lacunas entre passes, e passes podem se sobrepor entre si, o que o `gpu_real` não separa (ver "Ressalvas desta fase").

### Vulkan contra D3D12: o `gpu_real`

Vulkan: 30,70 e 32,77 ms; D3D12: 22,44 e 23,07 ms. As quatro razões possíveis entre uma execução de Vulkan e uma de D3D12 vão de 1,33x (30,70 ÷ 23,07) a 1,46x (32,77 ÷ 22,44), e a diferença em ms vai de 7,63 a 10,33 ms. Contra todas as execuções documentadas neste arquivo (o `gpu` das tabelas do relatório de cada uma, sem seleção): do Vulkan entram as 19 execuções das fases anteriores (`tl_on1` e `tl_on2`, `tl_fe1` e `tl_fe2`, `tl_fd1` a `tl_fd4`, `tl_fp1` a `tl_fp4`, `lz_off1`, `lz_on1`, `lz_on2` e `lz_off2`, `pp_smoke`, `pp1` e `pp2`) e as 2 desta fase; do D3D12 entram as 4 das fases anteriores (`tl_on1`, `tl_on2`, `tl_fe1` e `tl_fe2`) e as 2 desta fase. Fica de fora só a execução de D3D12 com `--sr_native_render_scale=2`, em que a GPU foi sobrecarregada de propósito. O `gpu` do Vulkan vai de 25,87 ms (`pp2_vulkan`, uma cena mais leve, com intervalo ≈34,9 ms) a 33,82 ms (`tl_fp3_vulkan`, Fase 1.1b) nas 21 execuções, e o do D3D12 vai de 21,22 a 25,02 ms nas 6. As faixas continuam sem se sobrepor, mas por só ≈0,85 ms (25,87 contra 25,02 ms); sem a `pp2`, o Vulkan começa em 29,75 ms. As médias simples das médias de cada execução são 31,25 ms (Vulkan) e 22,74 ms (D3D12), uma diferença de 8,51 ms; mas a variação de uma execução para outra dentro de uma mesma API é de ≈8,0 ms no Vulkan (25,87 a 33,82 ms) e de 3,8 ms no D3D12 (21,22 a 25,02 ms), do tamanho da diferença entre as APIs nestas execuções (7,63 a 10,33 ms). Os binários e as condições das fases anteriores são outros (não foi verificado quais usaram `-Profile`, ressalva 5). Portanto a diferença Vulkan contra D3D12 depende da cena e **não é uma constante de ≈9 ms**: entre os extremos documentados ela vai de ≈0,85 ms (25,87 contra 25,02) a 12,6 ms (33,82 contra 21,22). **A diferença de ≈9 ms do `gpu` antigo não era sobreposição (a sobreposição medida foi zero); a diferença de `gpu_real` entre as APIs existe nestas quatro execuções, de 7,6 a 10,3 ms, e o tamanho dela em outras cenas é variável.**

Por draw (draws por quadro: média das últimas 9 janelas de 120 quadros, 1080 quadros, do campo `draws=` das linhas `Vulkan profile (ms/frame over 120)` no Vulkan e das linhas `native CPU constants (ms/frame over 120, nested)` no D3D12):

| Execução | `gpu_real` (ms) | Draws por quadro | µs de GPU por draw |
| --- | ---: | ---: | ---: |
| `gr1_vulkan` | 30,70 | 2700 | 11,37 |
| `gr4_vulkan` | 32,77 | 2743 | 11,95 |
| `gr2_d3d12` | 22,44 | 2714 | 8,27 |
| `gr3_d3d12` | 23,07 | 2785 | 8,28 |

Os draws por quadro são parecidos entre as APIs (2700 a 2743 no Vulkan; 2714 a 2785 no D3D12), então, nestas execuções, a diferença de `gpu_real` não vem de um número de draws visivelmente maior no Vulkan: o Vulkan gasta 11,37 a 11,95 µs de GPU por draw contra 8,27 e 8,28 µs no D3D12 (1,37x a 1,45x entre as quatro combinações). Como a diferença em ms depende da cena (parágrafo anterior), a comparação por draw é a mais robusta à cena das duas, mas são só quatro execuções, duas por API, e a razão por draw também varia com a cena: para a `pp2_vulkan` (cena mais leve, 2075,1 draws por quadro em média na Fase 1.2), 25,87 ÷ 2075,1 dá 12,47 µs por draw no Vulkan, na mesma ordem de grandeza mas acima de 11,37 e 11,95 µs, e não há D3D12 dessa cena para comparar. Cuidados: são dois contadores de linhas de log diferentes, cuja definição de "draw" não foi reconciliada; as janelas do log não coincidem exatamente com os 1100 quadros do relatório; as janelas individuais variam muito (Vulkan de 2433 a 3036; D3D12 de 1973 a 3093 nas últimas 9 janelas de cada execução), e o tempo de GPU não é proporcional ao número de draws (há custo fixo por passe e por resolve); um µs por draw é, portanto, uma ordem de grandeza e não uma constante. A causa da diferença (API, driver, estados de pipeline, sincronização, formato dos dados) **não está decidida** por estas medições.

Verificação cruzada no D3D12 com o log do próprio jogo: as linhas `native: GPU time ... ms/frame` das últimas 8 janelas (≈1140 quadros) dão 22,62 ms (`gr2_d3d12`) e 23,16 ms (`gr3_d3d12`), contra 22,44 e 23,07 ms do relatório. As janelas não são as mesmas, e o log vem do mesmo renderer, então isto é uma conferência de consistência, não uma medida independente. O perfil do Vulkan não tem linha equivalente.

### O que o `gpu_idle` diz

`gpu_idle` é o tempo em que a GPU não tinha trabalho do quadro em execução (entre o fim do quadro anterior e o início do seguinte). Quando ele é grande, o quadro não é limitado só pela GPU. Quando é zero em todos os quadros (sem lacuna entre quadros), a GPU passa a ser candidata a limitante, com a ressalva de que execução não é ocupação (ressalva 1 desta fase). Os dois casos aparecem:

- **Vulkan: a GPU em execução ocupa 83,4% a 85,0% do intervalo (`gpu_real` ÷ intervalo) e fica sem trabalho 14,9% a 16,6% (5,39 e 6,53 ms por quadro).** A GPU está perto de ser contínua, mas não é: o p50 do `gpu_idle` é 2,83 e 3,86 ms e o p99 é 19,93 e 23,76 ms (1% dos quadros tem ociosidade acima disso). Pelo lado da CPU, o `game` é o único estágio que chega ao intervalo, e isso vale por construção (`game` + `front_wait` = intervalo, ressalva 2): 35,58 e 38,78 ms; o que ele contém não está decidido (ver a conclusão abaixo). O `worker` (30,66 e 34,34 ms de média), o `record` (27,69 e 29,50 ms) e o `gpu_real` (30,70 e 32,77 ms) ficam todos entre 27,69 e 34,34 ms, abaixo do intervalo na média (somando o `bloq.`, o `worker` dá 33,03 e 36,23 ms e o `record` 29,89 e 31,56 ms, ainda abaixo de 36,11 e 39,31 ms); os p99 dos quatro estão acima do orçamento (`game` 46,59 e 50,60 ms; `worker` 41,92 e 46,80 ms; `record` 42,01 e 43,05 ms; `gpu_real` 36,60 e 38,29 ms). O que o `gpu_idle` mostra é que o ritmo de entrega de trabalho à GPU é mais lento do que a GPU, na média, consegue consumir; não mostra qual estágio da CPU causa a espera, nem exclui que parte dela seja espera por apresentação ou outra sincronização (não medida).
- **D3D12: a GPU em execução ocupa 64,3% e 65,3% do intervalo e fica sem trabalho 35,8% e 34,7% (12,49 e 12,25 ms por quadro).** O p50 do `gpu_idle` é 11,85 e 11,23 ms; mais de 10 ms de ociosidade em 65,9% e 63,9% dos quadros das janelas. O `gpu_real` tem folga de 10,89 e 10,26 ms (média contra 33,3 ms) e de 6,32 e 7,09 ms no p99. Pelo lado da CPU, o estágio de trabalho mais ocupado é o `worker`: 32,93 e 34,44 ms de média, limitante em 78% e 90% dos quadros, p99 de 46,09 e 47,97 ms; o `game` tem 26,21 e 25,75 ms de trabalho e espera 8,70 e 9,55 ms no `front_wait`. O intervalo médio (34,92 e 35,33 ms) ficou acima de 33,3 ms, o que é compatível com o limitador de 30 FPS não ser o que segura o quadro, mas a média sozinha não o prova: 10,6% e 11,9% dos intervalos caem no bin de 32,8 a 34,2 ms e 36,4% e 34,3% ficam abaixo de 32,8 ms (53,0% e 53,8% acima de 34,2 ms), então um limitador que age em parte dos quadros não está excluído. A evidência mais forte que o documento já tem é o deslocamento entre estágios: o `front_wait` subiu para 8,70 e 9,55 ms e o `game` caiu para 26,21 e 25,75 ms, contra 0,72 a 2,56 ms de `front_wait` e 30,86 a 32,76 ms de `game` nas quatro execuções anteriores de D3D12 (intervalo de 33,45 a 33,62 ms, o regime descrito na ressalva 2); ou seja, nestas execuções a thread do jogo parece ter esperado o `worker` em vez do limitador (o `front_wait` é a espera do `worker` dentro do `OnSwap`; ele não diz o que atrasou o `worker`). Isso contradiz, para ESTAS execuções, a ressalva 2 do início do documento ("o limitador segura o intervalo em ~33,4 ms"): é uma mudança de regime, com causa não decidida. A leitura compatível com os dados é que o D3D12 aqui é limitado pela CPU (o `worker`), com a GPU esperando; o `bloq.` de 0,00 ms do `worker` e a GPU com folga são consistentes com isso, mas o `bloq.` do `worker` não prova ausência de espera (ressalva 6).

**Uma parada isolada em cada janela do Vulkan.** Cada uma das duas janelas do Vulkan contém uma parada de ≈470 a 510 ms: na `gr1` o intervalo do quadro 1806 é de 486,5 ms (o maior `gpu_idle`, 510,1 ms, está no quadro 1804) e na `gr4` o do quadro 1732 é de 471,1 ms (maior `gpu_idle` de 507,5 ms, no quadro 1730). Elas entram nas médias e são o "mínimo 2,1 FPS" dos relatórios; sem esse único intervalo e o `gpu_idle` do quadro correspondente, o `gpu_idle` médio seria 4,93 e 6,07 ms (em vez de 5,39 e 6,53 ms) e o intervalo médio 35,70 e 38,91 ms (em vez de 36,11 e 39,31 ms; 1098 intervalos), e o mínimo passaria para 16,0 e 14,7 FPS (o segundo maior intervalo é de 62,7 e 67,8 ms). No `front_wait` a parada aparece como 448,0 e 441,1 ms nesses mesmos quadros, e a média do `front_wait` seria 0,10 e 0,09 ms (em vez de 0,50 e 0,49 ms); `game`, `worker` e `gpu` mudam em no máximo 0,05 ms (o `gpu` do quadro 1804 e do 1730 é de 84,7 e 76,8 ms, contra ≈32 ms nos vizinhos). A causa da parada não foi investigada; as conclusões abaixo usam as médias com a parada incluída, e a diferença é de ≈0,4 a 0,5 ms no intervalo e no `gpu_idle`.

**D3D12 mais lento que nas fases 0 e 0.5.** O intervalo do D3D12 ficou em 34,92 e 35,33 ms (28,6 e 28,3 FPS no relatório), contra 33,4 a 33,6 ms nas quatro execuções anteriores; o `worker` ficou em 32,93 e 34,44 ms contra 29,17 a 32,37 ms; e o `front_wait` (a espera do `worker` dentro do `OnSwap`) em 8,70 e 9,55 ms contra 0,72 a 2,56 ms. Os binários, a cena e a deriva de ≈8% entre execuções (observada em "Não adotado: A/B inconclusivo da leitura preguiçosa dos fetch constants") podem explicar isso; **a causa não está decidida** e esta fase não a investigou. Para as conclusões abaixo isto importa pouco: a GPU do D3D12 tem folga em qualquer das duas leituras, e a ociosidade (12,49 e 12,25 ms) é maior que a folga média (10,89 e 10,26 ms).

### Conclusão: a GPU do Vulkan é um limitante?

**Não há evidência de que ela feche o intervalo, mas ela está perto do limite e deixa pouca folga (0,56 a 2,63 ms na média) para estabilizar em 30 FPS.** Com os números:

- **Não há evidência de que seja o estágio que fecha o quadro.** O intervalo do Vulkan (36,11 e 39,31 ms) é maior que o `gpu_real` (30,70 e 32,77 ms) em 5,41 e 6,54 ms; essa diferença é o `gpu_idle` (5,39 e 6,53 ms, ≈0,02 ms): tempo em que a GPU não tem trabalho do quadro. A fração dos quadros em que o `gpu` é o maior tempo ocupado entre `game`, `worker`, `record` e `gpu` (`limitante`) é 7% e 5%; essa coluna favorece o `game` por construção (ressalva 2) e não prova nada sozinha.
- **Está perto do orçamento.** Contra os 33,3 ms de um quadro a 30 FPS, a média do `gpu_real` deixa 2,63 e 0,56 ms de folga. Em 12,4% (`gr1`) e 32,7% (`gr4`) dos quadros o `gpu_real` sozinho passa de 33,3 ms, e o p99 é 36,60 e 38,29 ms (3,27 e 4,96 ms acima do orçamento). A folga média de 0,56 a 2,63 ms é do tamanho da deriva de ≈8% entre execuções (8% de 30,70 ms são 2,46 ms), e a diferença entre `gr1` e `gr4` (2,07 ms, 6,7%) é dessa ordem.
- **Aritmética de folga da GPU para 30 FPS (não é uma meta de CPU).** O orçamento é 33,3 ms por quadro. Contra ele a GPU tem folga de 2,63 e 0,56 ms na média (33,3 − `gpu_real`), e o p99 do `gpu_real` (36,60 e 38,29 ms) passa do orçamento em 3,27 e 4,96 ms: mesmo que a CPU custasse zero, o intervalo cairia ao `gpu_real` (≈32,6 e ≈30,5 FPS), 30 FPS só na média e sem margem. Isso descreve a GPU; não diz o que a CPU precisa cortar.
- **Do lado da CPU, nenhum estágio que não seja o `game` alcança o intervalo em média, nem ocupado nem ocupado mais bloqueado.** No `gr1` (intervalo 36,11 ms) o `worker` soma 30,66 + 2,37 = 33,03 ms, o `record` 27,69 + 2,20 = 29,89 ms e o `gpu_real` 30,70 ms; no `gr4` (39,31 ms), o `worker` soma 34,34 + 1,89 = 36,23 ms, o `record` 29,50 + 2,06 = 31,56 ms e o `gpu_real` 32,77 ms. Só o `game` (35,58 e 38,78 ms) chega ao intervalo, mas isso é circular: `game` + `front_wait` = intervalo por construção (ressalva 2 do início do documento), então "o `game` acima de 33,3 ms" é só "o intervalo acima de 33,3 ms", e os cortes de 2,25 e 5,45 ms que o `game` precisaria para caber no orçamento são o excesso do intervalo (a menos de ≈0,5 ms de `front_wait`) sobre 33,3 ms, não um requisito de CPU. O que o `game` contém (trabalho do renderer na thread do jogo, lógica do jogo, código do driver recompilado ou espera fora do renderer) **não está decidido**; ver o `game_guest` e o `frontend` nas seções da Fase 0.5 e da Fase 1.1a e "O que os dados não decidem" acima. Que o intervalo passe da média ocupada de todos os outros estágios é compatível, como possibilidade não quantificada, com o encadeamento: há uma única passagem de trabalho em voo entre a thread do jogo e o gravador (`job_pending_`, em `port/src/graphics/vulkan/game_frame.cpp`: a thread do jogo espera o trabalho anterior terminar antes de entregar o seguinte) e um fence por slot de quadro em voo (o do quadro N−2), de modo que esperas que médias de estágios separados não somam podem se acumular.
- **Efeito de reduzir só a GPU: não medido, provavelmente pequeno, mas não nulo.** O `record` é às vezes contido pela GPU (contrapressão), então a redução do tempo de GPU não é irrelevante para ele. O `bloq.` do `record` (2,20 e 2,06 ms) reúne as esperas por shaders, pelo fence do slot e pelo lock da fila, e as linhas `Vulkan profile` mostram a espera do fence do slot em 1,0 a 2,3 ms (`gr1`) e 0,9 a 1,3 ms (`gr4`) nas últimas 9 janelas, o que é compatível com contrapressão da GPU sobre o `record`; o tamanho dela no intervalo não foi isolado. Estas contas são de médias e p99 de estágios separados; não prevêem o FPS, e **não está medido** se o `gpu_real` mudaria quando a CPU entregasse trabalho mais rápido (por exemplo por frequência de relógio ou pressão de memória compartilhada em uma GPU integrada).
- **No D3D12 a GPU não limita.** `gpu_real` de 22,44 e 23,07 ms (folga de 10,26 a 10,89 ms na média e de 6,32 a 7,09 ms no p99), nunca acima de 33,3 ms nas janelas (0% dos quadros), e ociosa por 12,25 a 12,49 ms por quadro. Para 30 FPS ali o que falta é a CPU (o `worker`, de 32,93 e 34,44 ms de média, e de 46,09 e 47,97 ms de p99).

**Para a Fase 1.** A ordem recomendada (passo 1, passo 3, passo 2) não muda por estes dados. O motivo dado para deixar o passo 2 em terceiro tinha duas partes: o `gpu` ser limite superior (levantado nesta configuração, ver acima) e a falta de dados por passe (continua). A GPU do Vulkan passa a ser um tempo de execução medido de 30,70 a 32,77 ms por quadro, que também precisa caber no orçamento para 30 FPS estáveis; isso é compatível com manter o passo 2 na lista, não prova que ele seja o próximo. De qual passe viria a redução, esta fase não responde.

### Próximos passos

1. **Tempo de GPU por passe, nas duas APIs, com uma chave de agrupamento pela assinatura do render target.** Hoje o Vulkan grava só dois timestamps por quadro (início e fim, `slot*2` e `slot*2+1`), e o D3D12 tem um corte opcional por passe (`sr_native_gpu_pass_timing`, desligado por padrão e não usado aqui) que depende do id de passe do guest. Neste perfil de jogo os ids de passe (`kPassShadowMaps`, `kPassOpaque`, `kPassEndTiling`, `kPassSorted`, `kPassUpscale`, `kPassHud`) são todos -1 (`port/src/native_renderer/game_profile.h`: "No pass table is known for SR"), então o corte por id não serve de chave. O que a medição precisa: (a) definir a assinatura (por exemplo formato, dimensões e endereço dos alvos de cor e profundidade ligados) e verificar que ela agrupa os draws do mesmo jeito no Vulkan e no D3D12; (b) mais timestamps por quadro no Vulkan (hoje 2 por quadro em voo; o D3D12 reserva até 64 timestamps por quadro) e a colheita em ordem cronológica, que a Fase 2.0 já exige para o `gpu_idle`; (c) a soma dos grupos tem de fechar com o `gpu_real` do mesmo quadro dentro de uma tolerância declarada, e o `gpu_idle` continua separado; (d) contagem de draws por grupo, para dar µs por draw por grupo; (e) o custo da própria instrumentação medido por A/B (os timestamps podem quebrar sobreposição entre passes dentro do quadro e mudar o tempo que se quer medir); (f) mais de uma execução por API e a mesma cena, porque a variação entre execuções (≈8%, vários ms de cena) é do tamanho do que se quer comparar; preferir razões por grupo entre as APIs a valores absolutos.
2. **Só depois escolher os itens do passo 2 (GPU bit a bit) no Vulkan**, e só nos grupos cujo `gpu_real` explique a diferença de 7,6 a 10,3 ms contra o D3D12 nestas execuções (que depende da cena, ver acima) (ou o excesso do p99 sobre 33,3 ms).
3. **Em paralelo, continuar na CPU.** O `gpu_idle` de 5,4 a 6,5 ms no Vulkan e de 12,3 a 12,5 ms no D3D12 diz que há ritmo de entrega a recuperar; os candidatos estão em "Candidatos da próxima fase (ordem proposta)" acima. Também continua sem medida a divisão do `game_guest` (≈63% a 65% do `game_other` do Vulkan na Fase 0.5).

### Ressalvas desta fase

1. **Execução, não ocupação.** Timestamps de topo de pipe no começo e de fundo de pipe no fim medem o intervalo em que o quadro esteve na GPU, não quanto tempo as unidades ficaram ocupadas: dentro do quadro pode haver lacunas, e passes podem executar sobrepostos. "A GPU executa 83% a 85% do intervalo" não quer dizer "ocupada a 83% a 85%", e muito menos "ocupada a 100%".
2. **Só a sobreposição entre quadros foi descontada.** `gpu_real` não corrige nada dentro do quadro. Ela foi zero nas quatro execuções; isso é uma observação desta máquina e desta configuração, não uma garantia.
3. **Uma máquina, cenas variáveis.** Notebook Intel UHD, i5-13420H; as quatro execuções percorrem a mesma sequência do bench mas a cena muda de uma execução para outra (as médias de draws por janela variam de 1973 a 3093). Dois valores por API não dão intervalo de confiança; as faixas acima são as dos quatro valores observados, não limites.
4. **Deriva.** Foi observado um aumento de ≈8% do tempo ao longo de quatro execuções em uma fase anterior (ver a proveniência do A/B das sondas do PM4). Aqui o intervalo do Vulkan foi 8,9% maior em `gr4` que em `gr1` (39,31 contra 36,11 ms, mesma API) e o `gpu_real` subiu 6,7% (32,77 contra 30,70 ms); a ordem das execuções não permite separar deriva de cena.
5. **`-Profile` e custo da instrumentação.** As quatro execuções usaram `-Profile` (`SR_VULKAN_PROFILE=1`); o custo do perfil e o do cálculo de `gpu_real` e `gpu_idle` (aritmética sobre timestamps já lidos) não foram medidos por A/B aqui, e não foi verificado quais execuções das fases anteriores usaram `-Profile`, então a comparação com elas pode misturar as duas condições.
6. **Os draws por quadro vêm de dois contadores de log diferentes** (ver acima) e de janelas que não coincidem com as do relatório.
7. **O intervalo do relatório é o do `game`** (fim a fim do `OnSwap`), não o da apresentação; o `gpu_real + gpu_idle` bateu com ele (conferência acima), mas o instante em que o quadro aparece na tela não foi medido.

### Suítes desta fase

`python -m pytest tests/tools -q`: 71 passaram. Esta tarefa só edita este documento; as alterações de código da fase (estágios `gpu_real`, `gpu_idle` e o derivado `gpu_overlap`, em `frame_timeline.h`, no relatório e nas duas APIs) são das tarefas anteriores da fase.
