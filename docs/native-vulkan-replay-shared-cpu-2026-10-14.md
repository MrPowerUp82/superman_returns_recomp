# Replay Vulkan: reduzir a inicialização redundante (2026-10-14)

Aplicado o desenho aprovado: o construtor de `ConstantSnapshot` usado pelo
storage de replay inicializa somente os primeiros 512 bytes de `shared`.
O decoder já copia os 3.584 bytes restantes por inteiro antes de publicar o
pacote, inclusive quando não há atributos de vértice. Evita 3,5 KiB de zeragem
por draw; os bancos de constantes continuam com o comportamento de replay
anterior. Construtores comuns continuam zerando todos os campos.

Uma asserção de compilação prende o tamanho da cópia ao tamanho do sufixo.
Falhas precoces e tardias de decode não substituem o pacote publicado. O
decoder preenche ambos os bancos, registradores e metadados antes de leitura
ou publicação; a reserva do arena evita mover storage parcialmente preenchido.
A correção anterior do cache de descritores permanece no executável.

## Medição isolada

Diagnóstico Release `sr_replay_storage_bench`: 3.008 draws com arena e replay
completos, mantendo os pacotes vivos como na fila Vulkan. Duas rodadas de
aquecimento e oito medidas por processo; tabela mostra medianas em ms.
O benchmark usa um batch sem streams/device e não reproduz todo o gameplay.

| Executável | Processo | Arena | Decode | Total |
| --- | --- | ---: | ---: | ---: |
| Anterior | 1 | 26,9230 | 12,4701 | 39,5480 |
| Anterior | 2 | 26,9585 | 12,5541 | 39,1424 |
| Anterior | 3 | 28,8441 | 13,3971 | 42,2413 |
| Anterior | 4 | 26,8687 | 13,8118 | 40,7002 |
| Novo | 1 | 26,0765 | 12,4770 | 38,6729 |
| Novo | 2 | 25,6911 | 12,8783 | 38,8984 |
| Novo | 3 | 25,9045 | 12,1891 | 38,0363 |

Checksum idêntico: `45255360`. Redução pequena, consistente nessa amostra
isolada; não converter esses tempos em previsão de FPS. Logs
`logs/replay_shared_{before,after}_micro*.log`; anterior preservado em
`build/tests-native/replay_shared_before_bench.exe`.

## Gameplay e perfil

RTX 2060 / i7-10750H, Vulkan nativo, 1280x720, scale 1, MSAA 1, vsync ligado,
limite 30 FPS, frame generation desligado. Medições de 20 segundos parado e
20 segundos segurando W, com detecção de gameplay/HUD. Nenhum build ou teste
pesado concorrente durante as janelas de FPS. Mesmos caminho e nome do exe.
Ordem: anterior 1, novo 1, novo 2, anterior 2. Instrumentação desligada:

| Rodada | Parado FPS | Movimento FPS | CPU cores parado/movimento |
| --- | ---: | ---: | --- |
| `cpu_replay_shared_old1` | 15,3 | 14,9 | 7,3 / 7,2 |
| `cpu_replay_shared_new1` | 13,3 | 15,7 | 7,0 / 7,0 |
| `cpu_replay_shared_new2` | 14,3 | 13,1 | 7,2 / 7,0 |
| `cpu_replay_shared_old2` | 13,6 | 15,3 | 7,2 / 7,0 |

Não há ganho estável de FPS demonstrado. As rodadas variam nos dois
executáveis; cenas e contagens de draws não são um replay determinístico.
Mantido o ajuste pela redução isolada e eliminação comprovada das escritas
redundantes, sem afirmar aceleração geral ou 30 FPS.

Separadamente, perfil instrumentado anterior `cpu_replay_next_baseline`:
últimas janelas de 120 frames: `arena_setup` 12,91/11,28 ms,
`decode` 25,94/23,21 ms, draws Vulkan 2.681/2.560. Perfil novo
`cpu_replay_shared_profile`: `arena_setup` 10,73/11,91 ms,
`decode` 27,59/27,65 ms, draws Vulkan 2.974/2.923. Contagens são do renderer,
e não uma equivalência exata ao número de slots inicializados. A carga variou;
essa comparação serve como diagnóstico, não como A/B causal. FPS
instrumentados anterior 14,6/14,0, novo 13,1/13,5.

O novo perfil ainda tem gravação Vulkan 58,3/57,1 ms, bindings 16,6/16,3 ms e
descritores 13,1/12,7 ms nas últimas janelas. Timeline média: worker 27,73 ms,
record 39,29 ms, GPU 7,47 ms. Esses estágios seguem relevantes; seus tempos
se sobrepõem e não devem ser somados para estimar o frame.

## Correção e evidências

- RED: teste WriteWatch falhou com o construtor anterior, por escrita no
  sufixo. GREEN: passou com o ajuste. Mede stores do construtor em páginas
  distintas, sem ler memória indeterminada.
- 178 testes nativos e orçamento de alocações passaram; 91 testes Vulkan
  passaram. CTest nativo 2/2 e Vulkan 1/1 passaram.
- Comparação completa de pacotes normais e replay com sufixo preenchido com
  `0xa5`, incluindo metadados não zero, índices normalizados e falhas de decode.
- Três variantes dos contratos de pixels de bindings de produção passaram.
  Fixtures de lifetime, gravação normal/merged, frame assíncrono e IDs do cache
  passaram. Camadas de validação indisponíveis: GPU validation não executada;
  `validation_errors=0` não representa uma execução com validation layers.
- Revisão independente sem problemas concretos de correção ou lifetime.
- Quatro gates de imagem contra `old1` passaram, sem alterar limites:
  perfil 33,15 dB / histograma 0,01020; novo 1 39,70 / 0,00595;
  novo 2 31,31 / 0,02273; anterior 2 34,04 / 0,01316.
  Cena/personagem/HUD de `new2` conferidos visualmente. A cobertura é dos
  cenários testados; não resolve o gate histórico C7 nem verifica todo o jogo.

Builds: `build/replay_shared_{release,vulkan}_build.log` e
`build/replay_shared_final_native_build.log`. Testes:
`logs/replay_shared_*tests.log`, `logs/replay_shared_*ctest.log`,
`logs/replay_shared_budget.log`, `logs/replay_shared_pixels.log` e
`logs/replay_shared_fixture_*.log`.
Gameplay: `logs/bench_cpu_replay_shared_*.log`, `logs/bench_results.csv`,
`logs/timeline_cpu_replay_shared_profile.{csv,txt}` e screenshots correspondentes.
Gates: `logs/replay_shared_image_gates.json`; helper
`build/compare_replay_shared.ps1` executado com Windows PowerShell 5.1.

Executável validado nesta etapa: `port/out/build/win-amd64-release/superman_returns.exe`.
Cópia preservada: `artifacts/native-renderer-performance/replay_shared_candidate.exe`.
SHA-256: `B565B661791C37AE543E3019915DF5DA92E290AFBC669B4DE0B4FA4F3ABD1D2E`.
Anterior: `artifacts/native-renderer-performance/replay_shared_before.exe`,
SHA-256 `657F9CAF036C1FFC5492054A84ACC46916E77EFC547EFD8CFD7154FD2B64AB70`.
O helper restaurou o novo executável ao encerrar a última rodada anterior.
Até o encerramento desta medição, não havia commit ou publicação.
O executável de distribuição não foi substituído.
