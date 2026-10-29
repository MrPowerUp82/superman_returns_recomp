# Benchmark: alocação de buffers D3D12 e stuttering — 28/10/2026

## Resultado aquecido

Três pares intercalados no mesmo executável, com
`--sr_native_buffer_heap_pool=false|true`. Nativo, D3D12, RTX 2060, i7-10750H,
1280×720, escala 1, sombra 1, MSAA 1, FXAA/geração/VSync desligados, sem limite.
Rodadas válidas de 5 s de aquecimento + 60 s medidos, perfis detalhados desligados.

| Medida (mediana das três rodadas) | Committed, antes | Placed heaps, depois |
| --- | ---: | ---: |
| FPS médio | 41,71 | 45,31 |
| 1% low, FPS | 7,16 | 21,61 |
| 0,1% low, FPS | 3,78 | 17,22 |
| p99 do intervalo guest, ms | 87,70 | 40,18 |
| Pior quadro por rodada, ms | 307,75 | 63,15 |
| CPU efetiva do worker por quadro, ms | 16,64 | 14,71 |

O p99 caiu **54,2%** e a CPU do worker por quadro caiu **11,6%** nas medianas.
Nenhuma das três rodadas novas teve um quadro acima de 100 ms; as antigas tiveram
14/16/16 aproximadamente por minuto. Todas as comparações pareadas melhoraram p99
em mais de 15% e CPU do worker por quadro em mais de 10%.

Isso **não é uma redução de 12% da CPU total do processo**: o alvo medido é o
worker do renderer por quadro. O processo passou a produzir mais quadros. A thread
do jogo continua próxima de ocupar um núcleo; 60 FPS constantes não foram obtidos.

| Par | FPS antes/depois | p99 ms antes/depois | 1% low antes/depois | Worker CPU ms/quadro antes/depois |
| --- | --- | --- | --- | --- |
| 1 | 41,71 / 45,31 | 79,77 / 59,28 | 7,16 / 15,89 | 16,565 / 14,713 |
| 2 | 38,15 / 44,40 | 87,70 / 40,18 | 6,16 / 22,15 | 17,918 / 14,946 |
| 3 | 42,67 / 48,01 | 88,24 / 39,54 | 8,03 / 21,61 | 16,635 / 13,550 |

## Causa observada

O perfil `ref_profile1` mostrou GPU real média de aproximadamente 4,1 ms, p99 de
7,2 ms, enquanto o worker chegou a p99 de 103 ms. São diagnósticos com overhead,
não números de aceitação. O estágio ApplyBuffer aumentava nas janelas de streaming.

Uma instrumentação mais estreita (`allocation_profile`) confirmou milhares de
`CreateCommittedResource` durante o sobrevoo: janelas de 120 swaps tiveram
3.995/2.312/2.446/4.834 chamadas, com 9,080/5,592/5,913/11,139 ms por quadro
somente nessa API. O custo por chamada era frequentemente pequeno, mas somava
grandes pausas quando muitos buffers apareciam juntos. Os PSOs criados nas três
referências aquecidas anteriores tiveram máximos de 1,10/0,86/0,49 ms; não explicam
sozinhos os maiores picos. Isso não descarta outras causas de stutter.

## Mudança

- Buffers de vértices/índices pequenos passam a ser placed resources em heaps
  D3D12 DEFAULT de 16 MiB, em vez de um heap committed por recurso.
- `GetResourceAllocationInfo` determina tamanho e alinhamento; planejamento verifica
  overflow, capacidade e não sobreposição. Recursos preservam estados, GPU VAs,
  identidades, descritores e conteúdo independentes.
- Alocação append-only: nenhum intervalo é reciclado enquanto houver recursos
  vivos. O pool é declarado antes dos detentores de recursos para morrer depois
  deles, incluindo os aposentados por fence. Não há reutilização que exija aliasing
  barriers. A política de retenção de `buffers_` já existia; não foi ampliada.
- Recursos maiores que uma página ou erros usam o caminho committed. Uma falha
  real desliga novas tentativas do pool nessa instância. O primeiro HRESULT é
  registrado, e os perfis distinguem placements de fallbacks.
- Padrão: ligado. Para comparar ou reverter localmente:
  `--sr_native_buffer_heap_pool=false`.
- Resolução, draws, shaders, sombras, simulação, duração e contagem do benchmark
  não foram reduzidos/alterados para produzir o ganho. A mudança vale para o
  renderer D3D12 também fora do benchmark; Vulkan não usa esse alocador.

## Custo e limites

O uso de VRAM **aumentou**. Os máximos registrados por rodada foram
607/622/641 MiB antes e 1.864/1.982/1.976 MiB depois, com orçamento disponível
de 5.187 MiB. O alinhamento dos placed resources e as sobras das páginas explicam
por que o custo observado pode superar bastante os pequenos committed buffers
geridos internamente pelo driver. Isso precisa ser considerado em GPUs com menos
memória e sessões longas; o teste não demonstra consumo limitado em toda a campanha.

O voo e a cena variam entre aberturas. Não houve controle rígido de temperatura
ou clocks. A execução estava em alimentação externa e sem builds/testes concorrentes.
Por isso os números são evidência local dos três pares, não uma previsão para
qualquer PC ou fase. O ganho de cauda se repetiu em todos os pares e é maior que
a variação observada entre eles; não foi necessário ampliar para cinco pares.

Os percentis/lows são de presents guest, a métrica que o benchmark já exporta.
A timeline também mostrou trabalho real de GPU baixo e gaps grandes de GPU na
referência, coerentes com falta de submissão. Não foi feita captura de scanout/
PresentMon: não se afirma que cada intervalo guest equivale a um quadro exibido.

Nos últimos 2.000 frames de cada diagnóstico, o intervalo de conclusão na GPU
(duração + gap sem trabalho entre frames, sem somar sobreposição) teve p99 de
103,65 ms antes e 43,33 ms depois; máximos de 341,13 e 80,35 ms. Isso corrobora a
redução das pausas na entrega do trabalho ao host. As câmeras e o overhead dos
perfis diferem, portanto esse é um diagnóstico, não outro A/B de aceitação.

### Primeiro plano e travadas restantes

As rodadas de conferência visual expuseram uma condição mais lenta. Foram
preservadas: `visual_final` (45 s, p99 85,51 ms) e `visual_full_final` (60 s,
22,43 FPS, p99 87,31 ms, máximo 512,63 ms). Tiveram interação de foco e capturas,
por isso não substituem a série de aceitação sem essas operações. Elas mostram
que **não é correto dizer que todo stuttering foi eliminado**.

`visual_timed_final` fez a conferência em três momentos do sobrevoo, também
separada da aceitação: 25,10 FPS, p99 90,34 ms e máximo 625,79 ms. Seus arquivos
foram preservados; essa execução reforça a existência dos picos restantes.

Uma referência em primeiro plano também ficou lenta (`foreground_old`: 19,98 FPS,
p99 196,97 ms; consultas WMI/nvidia-smi durante a medição). Em seguida foram
rodadas, sem captura ou consultas durante a janela, `foreground_pool_clean`
e `foreground_old_clean`; a janela foi trazida à frente antes de medir:

| Primeiro plano, um par adicional | Antes | Depois |
| --- | ---: | ---: |
| FPS médio | 25,40 | 27,63 |
| 1% low, FPS | 3,22 | 8,35 |
| p99, ms | 154,67 | 90,70 |
| Máximo, ms | 940,89 | 345,46 |

Esse par também favorece o pool, mas não tem três repetições e não esclarece a
causa da diferença entre foco/condições do sistema. Não misturar seus números
com a mediana da série original. Alimentação externa permaneceu ativa; uma
amostra durante a referência mostrou GPU a 76 °C/1.725 MHz/36% de uso e CPU com
130% da frequência base no contador de performance. Não há telemetria contínua
para atribuir a variação ao foco, ao sistema, à cena ou a limites térmicos.
O limite restante inclui custo da thread do jogo e precisa de investigação
separada; não será mascarado mudando a qualidade ou o limite de FPS.

## Protocolo, reprodução e arquivos

- Binários originais preservados em `artifacts/benchmark-cpu-stutter/reference`;
  instalação final em `artifacts/benchmark-cpu-stutter/variant`.
- `run.py` e `ab.py` nessa pasta registram argumentos, hashes, ambiente diagnóstico,
  caches presentes, início/fim, saída, logs e arquivos JSON/CSV do benchmark.
- Pares: `ab_old1..3`, `ab_pool1..3`. `ab-tested.exe` preserva exatamente o
  executável da comparação aquecida; a build final acrescenta recuperação testada,
  contadores e promove o padrão. Hashes estão nos manifestos de cada rodada.
- CPU: `tools/analysis/sample_thread_cpu.ps1`, logs `threads.csv`. `summary.py`
  usa apenas intervalos completos do SO contidos na janela medida e conta os
  frames cujos finais caem na mesma janela. Jogo/worker identificados pelo TID do
  log. Trata-se de média por janela, não atribuição de CPU a cada quadro.
- Percentis e contagens de picos vêm dos CSVs completos; não foram retirados
  outliers. O perfil detalhado foi feito em rodadas separadas.
- A primeira rodada `ref_cold` teve aviso de firewall e falha do sampler; foi
  preservada e excluída de conclusões de CPU/aceitação. As referências aquecidas
  `ref_warm1..3` reproduziram picos >200 ms antes do A/B.
- Caches originais não foram apagados. Nos testes frios finais, os caches locais
  de tradução e o arquivo de PSOs foram movidos para pastas preservadas sob
  `cache-before-*`. A biblioteca `.srsl` fornecida e os PSOs embutidos ficaram
  presentes nas duas condições. O cache do driver é desconhecido, não foi chamado
  de frio. `cold.py` documenta exatamente a preparação.

## Verificação

- TDD do layout: teste falhou com quatro verificações antes da implementação;
  depois os 541 casos nativos passaram.
- Teste real em D3D12 WARP: 258 buffers, virada da página, GPU VAs distintos,
  conteúdo independente lido de volta após cópia/transition/fence.
- Recuperação: injeção de falhas de CreateHeap, CreatePlacedResource na página
  nova e na existente; sucesso do committed fallback, latch de falha, preservação
  de recursos e rejeição de tamanho excessivo sem inutilizar o pool. Teste novo
  falhou com a ausência do fallback, depois passou com a implementação.
- CTest final: 4/4, incluindo integração D3D12 WARP.
- Revisão independente não achou defeitos confirmados de produção; a lacuna de
  cobertura de falhas e o readback após rollover foram corrigidos.
- Teste frio da build final (`cold_old_final`/`cold_pool_final`): 42,78 → 49,20 FPS,
  p99 84,65 → 42,82 ms, máximo 661,77 → 261,06 ms; quadros >100 ms: 19 → 4.
  Sem regressão observada nesse par. Caches locais vazios, bibliotecas e base
  embutida preservadas, cache do driver desconhecido.
- Perfil final: 25.318 placements, 102 heaps/1.632 MiB reservados no último bloco,
  zero fallbacks ou erros. A janela mais cara de criação caiu de 11,139 para
  0,968 ms/quadro; instrumentação desligada nas rodadas de aceitação.
- Conferência visual em quadros do sobrevoo: cidade, personagem, texturas e
  iluminação presentes, sem novos artefatos evidentes. Não foi feita comparação
  pixel a pixel; o voo varia e não houve cobertura de todas as fases/quadros.
- Smoke curto de inicialização do modo benchmark Vulkan: saída 0 e resultado
  válido; não é validação de desempenho, imagem ou estabilidade prolongada Vulkan.
- A revisão das correções de recuperação não deixou achados importantes pendentes.
  Não há publicação de release nesta tarefa.

## Referências

Os relatos das capturas serviram como pistas; esta solução deriva do custo medido
neste checkout. Não foi necessário decompilar o executável pelo REA nem portar
um mecanismo novo do Xenia Canary. Sem Evidence IDs de REA nesta etapa.

Contrato da API: [CreatePlacedResource](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createplacedresource).
