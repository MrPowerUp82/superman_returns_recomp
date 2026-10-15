# Pacotes de draw: guardar apenas os registradores usados (2026-10-14)

Aplicado o desenho limitado aprovado: substituir `mirrored_registers[0x400]`
por `polygon_offset_registers[4]`. O decoder copiava 1.024 registradores PM4
por draw, mas a busca em todo o repositório identificou apenas um consumidor
de produção: `FinalizeDrawConstants`, que lê `0x2380..0x2383`.

Os quatro valores continuam sendo capturados a cada draw, com seus bits
originais. Índices antigos `0x380/0x382` viraram `0/2`; seleção de face,
culling, fallback, tratamento de valores não finitos e cálculos não mudaram.
O bloco separado `registers`, usado pelo estado do dispositivo e pelo backend,
permanece completo. O pacote interno não é um layout de constantes GPU.

`sizeof(DrawPacket)` caiu de **25.952 para 21.872 bytes**: 4.080 bytes por draw,
15,7% do payload, ou cerca de 11,7 MiB de memória host por batch de 3.008 draws.
O teste de orçamento de alocações passou de 91.991 para 87.911 bytes/comando
nesse cenário, confirmando a redução exata. Não é uma redução medida de VRAM.

O construtor comum permanece fornecido explicitamente (`DrawPacket() {}`),
com os inicializadores dos membros. A primeira candidata usava `= default`;
isso poderia acrescentar zero-initialization do objeto inteiro antes dos
membros em `make_shared<DrawPacket>()`. Corrigido antes da entrega. O caminho
Vulkan medido usa o construtor com tag de replay, que não foi alterado por
essa correção. Os defaults dos quatro novos registradores são zero.
As correções anteriores de descritores e do sufixo `shared` foram preservadas.

## Diagnóstico isolado

Release, 3.008 draws, duas rodadas de aquecimento e oito medidas por processo;
arena real, replay completo e pacotes retidos como na fila. Batch sem streams
ou device; não representa todo o jogo. Três pares alternados, sem builds ou
testes concorrentes. Medianas por processo em ms:

| Par | Executável | Arena | Decode | Total |
| --- | --- | ---: | ---: | ---: |
| 1 | Anterior | 26,2308 | 12,6382 | 38,9873 |
| 1 | Compactado | 26,3398 | 11,2733 | 37,4984 |
| 2 | Anterior | 28,9088 | 13,4664 | 42,3770 |
| 2 | Compactado | 26,1142 | 11,1341 | 37,5194 |
| 3 | Anterior | 28,5746 | 13,2256 | 41,4325 |
| 3 | Compactado | 25,7645 | 11,5527 | 37,1572 |

Todos os checksums: `45255360`. Há redução do custo nessa amostra isolada;
a etapa de arena não melhorou em todos os pares. As primeiras sequências
não alternadas também foram preservadas: total anterior 39,8513/46,6546/43,4676,
compactado 37,7861/35,6728/38,2377 ms. Uma execução isolada final, após preservar
o construtor comum, marcou arena 21,3142 / decode 10,6306 / total 32,0482 ms;
sem um par adjacente, não usá-la para atribuir ganho adicional ao ajuste.

Logs: `logs/replay_registers_paired_{old,new}*.log`,
`logs/replay_registers_{before,after}_micro*.log` e
`logs/replay_registers_final_micro.log`. Benchmarks anteriores preservados em
`build/tests-native/replay_registers_{before,after}_bench.exe`.

## Gameplay

RTX 2060 / i7-10750H, renderer nativo Vulkan, 1280x720, scale 1, MSAA 1,
vsync ligado, limite 30 FPS, frame generation desligado. Mesmo caminho/nome
do executável, detecção de gameplay/HUD, 20 segundos parado e 20 segurando W.
Sem builds, testes pesados ou restauração de fontes durante as janelas de FPS.
Instrumentação desligada nas rodadas abaixo:

| Rodada | Parado FPS | Movimento FPS | CPU cores parado/movimento |
| --- | ---: | ---: | --- |
| `cpu_replay_registers_old1` | 14,3 | 14,0 | 7,1 / 6,9 |
| `cpu_replay_registers_new1` | 13,2 | 14,0 | 7,0 / 7,0 |
| `cpu_replay_registers_final` | 12,9 | 13,6 | 7,1 / 6,8 |
| `cpu_replay_registers_old2` | 12,2 | 14,7 | 7,0 / 7,0 |

Ordem da tabela é a ordem de execução. Entre `new1` e `final`, fora das janelas
de medição, houve a correção do construtor comum, builds e testes. `new1` usa
a primeira candidata; `final` usa o executável entregue. A carga não é um
replay determinístico: últimas amostras de `old1` tinham 2.568/2.578 pacotes,
enquanto `final` tinha 3.002/2.977/2.993. Os dois executáveis variaram em FPS.

**Não há ganho estável de FPS demonstrado.** A candidata não superou a
referência nesse conjunto. Essa amostra não isola o impacto no gameplay nem
descarta regressão em outras cargas. Mantido pela economia exata de memória,
remoção da cópia não consumida e melhoria no diagnóstico isolado, sem afirmar
30 FPS ou eliminação de todos os gargalos.

Separadamente, perfil `cpu_replay_registers_profile`, primeira candidata:
14,5/14,0 FPS instrumentados. Últimas janelas de 120 frames: arena
10,46/10,05 ms, decode 23,86/21,81 ms, draws Vulkan 2.702/2.560. A referência
instrumentada do trabalho anterior (`cpu_replay_shared_profile`) tinha arena
10,73/11,91 ms, decode 27,59/27,65 ms e 2.974/2.923 draws. Cargas diferentes;
esses números são diagnóstico, não uma comparação causal.

O perfil novo ainda apresenta record 59,3/51,0 ms, bindings 15,2/14,1 ms e
descritores 13,7/11,6 ms. Timeline média: worker 24,85 ms, record 38,32 ms,
GPU 7,04 ms. Há sobreposição entre estágios; não somar seus tempos.

## Verificação

- RED: 181 casos, falhou apenas o orçamento de tamanho do DrawPacket; os
  testes de comportamento já passavam no layout anterior. GREEN e execução
  final: 181 casos, zero falhas. Tamanho anterior 25.952, final 21.872 bytes.
- Replay normal e com arena preservam os bits dos quatro registradores.
  Testes cobrem frente/verso, todas as primitivas usadas pelo cálculo, culling,
  fallback com frente zerada, NaN e infinito. Os testes existentes de falhas
  precoces/tardias e equivalência de pacotes continuam passando.
- Orçamento de alocações passou. CTest final nativo 2/2, Vulkan 1/1;
  91 casos Vulkan, zero falhas. Três variantes de pixels de produção passaram
  após o ajuste final do construtor.
- Fixtures de lifetime, gravação normal/merged, frame assíncrono e IDs de
  descritores passaram após a compactação. Camadas de validação indisponíveis;
  `validation_errors=0` não representa uma execução com validation layers.
- Revisão independente sem problema funcional; apontou atualização do texto
  histórico de arena, agora explicitamente marcado como desenho de 13/10.
  Revisão pontual confirmou a necessidade de preservar o construtor comum.
- Quatro gates de imagem contra `old1` passaram, sem relaxar limites:
  perfil 37,78 dB / histograma 0,00454; `new1` 33,19 / 0,02069;
  `final` 37,52 / 0,00497; `old2` 36,39 / 0,01328.
  Cena/personagem/HUD de `final` conferidos visualmente. A cobertura é dos
  cenários registrados; não resolve o gate histórico C7 nem cobre todo o jogo.

Builds: `build/replay_registers_*build.log`. Testes:
`logs/replay_registers_{red,green}.log`, `logs/replay_registers_final_*`,
`logs/replay_registers_fixture_*.log`. Gameplay: `logs/bench_cpu_replay_registers_*`,
CSV `logs/bench_results.csv` e `logs/timeline_cpu_replay_registers_profile.{csv,txt}`.
Gates: `logs/replay_registers_image_gates.json`; helper
`build/compare_replay_registers.ps1` executado com Windows PowerShell 5.1.

Executável final: `port/out/build/win-amd64-release/superman_returns.exe`.
Cópia: `artifacts/native-renderer-performance/replay_registers_candidate.exe`.
SHA-256: `FC4A158A5CCE7B9E5576AC449BB2BCC59448C46D21ADF274269183C626A6E1EC`.
Anterior: `replay_registers_before.exe`, SHA-256
`B565B661791C37AE543E3019915DF5DA92E290AFBC669B4DE0B4FA4F3ABD1D2E`.
Primeira candidata: `replay_registers_pre_ctor_review.exe`, SHA-256
`C97B70FDF58065257A43684FA758DEC7C593021A6116A1BAC176298788031CF7`.
As cópias ficam em `artifacts/native-renderer-performance/`.
O helper restaurou o executável final depois de `old2`. Até o encerramento
desta medição, não havia commit ou publicação. O executável de distribuição
não foi substituído.
