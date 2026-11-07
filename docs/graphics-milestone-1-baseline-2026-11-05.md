# Baseline anterior ao Milestone 1 — 05/11/2026

Estado: preparação/medição da implementação existente. Nenhum dos cinco
milestones novos está concluído; não há resultado depois da mudança.

## Ambiente e procedimento

Checkout `2b07a68`, sem alterações prévias. RTX 2060, driver Windows
32.0.16.1742 (617.42). Executável existente de 04/11, SHA256
`9f6db039a577fb8df152c8eaab1925b25a3d52237975ed6221dc37516b2039ad`.
Não foi recompilado nesta preparação.

Três execuções sequenciais do benchmark embutido com
`tools/bench/bench_dlss.py --mode quality --name <nome>`. O harness executa
diretamente o mesmo EXE usado por `run.cmd`, com `--sr_benchmark=true`,
`--sr_benchmark_headless=true`, aquecimento 5 s e medição 30 s. Não foi invocado
o wrapper `run.cmd` para essas três medições. Sem profiling, dump, build ou
teste concorrente. D3D12, DLSS Quality, render 1280x720, saída NGX 1920x1080,
MSAA 1, escala 1, VSync false, limite FPS 0, LSFG Off e RT Off.
O texto de settings do benchmark informa janela 0x0 no modo headless; dimensões
de render/NGX foram verificadas separadamente nas mensagens do adapter.

Cada execução confirmou Evaluate, composição, conclusão por fence e estado
`applied=2`. Há rejeições de inputs durante a entrada na cena; os números abaixo
medem o benchmark inteiro após seu aquecimento, não apenas frames NGX ativos.
Não comparar esse conjunto com outro que altere aquecimento/cena/dimensões.

## Resultados

| Rodada | FPS | Média ms | Desvio padrão ms | p50 ms | p95 ms | p99 ms | p99,9 ms | 1% low FPS | 0,1% low FPS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 38,6293 | 25,8871 | 18,6628 | 23,0534 | 45,4022 | 71,5655 | 302,029 | 7,1318 | 2,9688 |
| 2 | 43,1250 | 23,1884 | 10,1165 | 21,6037 | 35,3494 | 45,3931 | 115,976 | 13,1739 | 5,3019 |
| 3 | 39,1242 | 25,5596 | 22,7083 | 23,3604 | 50,5663 | 68,0936 | 369,398 | 6,0182 | 2,5691 |
| Média das três | **40,2928** | **24,8784** | **17,1625** | **22,6725** | **43,7726** | **61,6841** | **262,4677** | **8,7746** | **3,6133** |

Desvio padrão populacional calculado a partir de cada CSV; percentis por
nearest-rank. Lows são os fornecidos pelo benchmark: inverso da média dos
tempos dos frames mais lentos, ceil(1%)/ceil(0,1%). A última linha é média
aritmética das métricas por rodada, não percentil de uma população agrupada.

Evidências locais (fora do Git):

- `logs/graphics_m1_baseline_quality_20261105_r{1,2,3}/`: `result.json`,
  `benchmark.json`, `frame_times.csv`, `game.log`, stdout e stderr.
- `logs/graphics_m1_baseline_quality_20261105_summary.json`: métricas e hash.
- JSONs originais: `benchmark-20261105-093054`, `-093150`, `-093246`, no
  diretório `benchmarks` ao lado do EXE.

O precompile registrou 813 pipelines (376 embedded; 6199 descrições locais na
primeira execução). Ainda houve 108/113/117 mensagens de criação de PSO nas
três rodadas, contadas em todo o log, não somente na janela de medição.
Isso prova que o caminho atual ainda cria PSOs no runtime observado; não prova
que cada criação compilou DXIL nem atribui os outliers exclusivamente a PSO.
Zero mensagens de shader não reconhecido nessa cena de título não demonstram
100% de cobertura nas fases principais do jogo.

## Regressão da base

`ctest --test-dir build/tests-native --output-on-failure`, com o CTest instalado
no Visual Studio BuildTools: 5/5 passaram em 4,32 s. São os binários de teste
já existentes, sem rebuild: contrato DLSS sem SDK, testes nativos, hook XMA,
buffer heap pool e capture budget. Isso não valida implementação nova.

## Capturas e limites

O ensaio `graphics_m1_before_quality_20261105_r1` tentou capturar no swap 1200.
O benchmark terminou válido, mas o harness rejeitou a ausência da saída DLSS,
que começou depois desse swap. Não entra na tabela acima.

Capturas separadas Off/Quality concluídas no swap 2100, com gamma de
apresentação desligado para conferir a contribuição dos pixels do NGX. Ambas
passaram seus respectivos harnesses e produziram PNGs inspecionados:

- [Quality, base camera-only](../logs/graphics_m1_before_quality_capture_20261105/capture/presented.png).
- [DLSS Off, base](../logs/graphics_m1_before_off_capture_20261105/capture/presented.png).

O teste `tools/analysis/check_dlss_capture.py` aprovou a captura Quality:
1920x1080, 100% dos pixels apresentados dentro de 2/255 da saída NGX,
erro médio 0,06836/255. Isso comprova contribuição do NGX no framebuffer,
não correção dos vetores de objetos. Raw buffers e `pixel_check.json` ficam
nos mesmos diretórios dos PNGs.

Essas execuções com readback não entram nas métricas de desempenho. Elas
registram a cena automática de título; há um personagem pequeno no céu,
mas não constituem percurso de gameplay com NPCs/veículos. Mesmo swap não
garante mesma pose/câmera entre processos, pois a animação depende do tempo.
Não são referência pareada para diferença pixel a pixel. A comparação de
ghosting em gameplay e o conjunto depois da implementação continuam pendentes.

Desenho técnico: [Milestone 1](graphics-milestone-1-object-motion-design-2026-11-05.md).
