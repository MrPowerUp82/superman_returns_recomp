# Pesquisa de CPU e benchmarks — 30/10/2026

Estado final: agrupamento de Arm ligado por padrão no Windows; retorno por
`SR_TRACKING_BATCH_ARM=0`. Admissão de queries, cache shared e demais opções
experimentais mantêm os padrões anteriores. O ganho comprovado é reduzir
consultas/proteções do monitor e suas pausas; FPS normal ainda depende da cena.

Fontes primárias consultadas:

- [AMD RDNA Performance Guide](https://gpuopen.com/learn/rdna-performance-guide/):
  custo CPU de gravação de comandos, estados redundantes, batching/submissões.
- [Microsoft: Map](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-map):
  evitar leituras CPU de heaps Upload/write-combine; manter comparações em RAM.
- [Microsoft: CPU Efficiency](https://microsoft.github.io/DirectX-Specs/d3d/CPUEfficiency.html):
  responsabilidades explícitas de gerenciamento de estado e recursos.
- [Clang User Manual](https://clang.llvm.org/docs/UsersManual.html#profile-guided-optimization):
  PGO com cargas representativas; candidato posterior, não ganho assumido.

Inferência local: os logs recentes mostram o worker D3D12 em cerca de 20 ms,
com custo relevante de constantes e textura; aumentar threads ou mexer em flags
do compilador antes de medir não identifica o trabalho removível.

O runner `tools/bench/cpu_web_sweep.py` grava `results.json` após cada execução,
confere o hash do executável entre runs e encerra a bateria se uma medição
falhar. Três blocos alternam baseline, cache shared e tracking Off. Cada run
usa 5 s de aquecimento e 30 s de benchmark headless, 1280×720, sem limite,
VSync ou LSFG; todos os modos usam o mesmo profiling. Não houve alterações no
plano de energia, prioridade do sistema, afinidade ou drivers.

Tracking Off também desliga provas de buffers; é um diagnóstico de custo do
subsistema, não um A/B isolado de uma função. A comparação de cache shared usa
RAM normal. Os números de FPS e percentis devem ser lidos junto dos tempos de
CPU; instrumentação, ordem de carregamento e trabalho variável de cena afetam
as medições.

Outra hipótese surgiu na fonte: queries de reutilização não podem aceitar
uploads sem watch, versão válida ou baseline coberto. O teste proposto omite
somente a query pré-reuso nesses casos, preservando hash, backoff, física e
as samples de auditoria antes/depois do hash. Não é autorização para promover
uma textura desconhecida a conhecida ou deixar de validar seus pixels.

## Primeira bateria: opções existentes

Hash do EXE: `5786f7bc0ef65a222c54bce1fa9a303c349599b93617e6c469cc2217384f55e9`.
9/9 execuções válidas e exit code 0, três por variante:

| Variante | FPS mediano | Faixa de FPS | p99 mediano (ms) |
| --- | ---: | ---: | ---: |
| Atual | 40,583 | 26,343–43,690 | 79,874 |
| Cache shared | 36,957 | 33,997–38,237 | 54,548 |
| Tracking Off | 34,098 | 27,656–34,528 | 62,180 |

Os percentis menores de algumas variantes não acompanharam aumento consistente
de throughput. A dispersão e três amostras não sustentam promoção de opções.
O benchmark já espera o filtro da cena 3D e invalida interrupção por vídeo;
não se deve interpretar a variação como inclusão automática de telas de carga.
O hardware identificado é i7-10750H (6c/12t) e RTX 2060. Não foi comprovada a
causa da variação (carga externa, frequência, aquecimento ou cena).

Dados: `logs/cpu_web_20261030_01/results.json`.

## Hipótese de admissão da query

Implementada como experimento reiniciado por processo:
`SR_NATIVE_SKIP_UNPUBLISHED_TEXTURE_QUERY=1` (padrão Off).
Somente a tentativa inicial de reuso em modo On passa por admissão. Uploads
publicados continuam consultando estado atual e runtimes sem fast view
continuam usando a consulta com lock. Modo Compare e samples da auditoria de
hash não mudam. O contador `unpublished_queries_skipped` distingue trabalho
eliminado de hashes realmente dispensados.

Teste vermelho confirmou helper ausente; depois da implementação, suite nativa
com 620 testes e CTest 5/5 passaram. A revisão independente confirmou que
`Sample` não rearma cobertura e não pode reparar o baseline armazenado.

Comparação concluída: `logs/cpu_query_profile_20261030_01/results.json`.
O runner também registra CPU total do processo (inclui boot/aquecimento,
não é CPU isolada da janela medida) e hashes de EXE/runtime. Uma segunda
comparação sem profiling é necessária para separar desempenho e instrumentação.

A bateria sem profiling teve 6/6 execuções válidas, saída 0 e durações totais
de 50–52 s. Medianas: controle 38,202 FPS / p99 56,216 ms; admissão 40,779 FPS /
p99 50,239 ms. O terceiro controle teve somente 13,765 FPS, com pausa longa
durante a medição, enquanto sua mediana de frames foi 17,266 ms. Esse resultado
não foi descartado: é um evento real a investigar. Os dois primeiros pares
tinham throughput mediano praticamente igual. A admissão continua Off por
padrão; estes dados não isolam um ganho geral de FPS.

Uma execução Compare com admissão On manteve o contador de consultas omitidas
em zero e a auditoria ativa, mas atingiu só 0,392 FPS (12 frames em 30,65 s,
maior pausa 22,002 s). O watchdog amostrou o renderer em `NtQueryVirtualMemory`
→ `VirtualQuery` → `RexGetMemoryTrackingApi+0x1430`. O símbolo relativo à exportação
não identifica sozinho toda a pilha: é evidência de uma hipótese, não atribuição
exclusiva. Dados: `logs/cpu_query_compare_20261030_01/results.json`.

## Arm do monitor por regiões (Windows)

A fonte revelou duas consultas de proteção por página em Arm, mais uma chamada
de proteção por página gravável. [VirtualQuery](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualquery)
informa trechos contíguos de atributos homogêneos; [VirtualProtect](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotect)
permite mudar o trecho comprometido dentro do mesmo mapeamento. A alternativa
`SR_TRACKING_BATCH_ARM=1` (Off durante os primeiros A/B, On no resultado final) agrupa consultas e proteção por trecho,
sempre limitado ao watch e ao tamanho de cada alias. Não guarda mapas entre
chamadas, mantém o lock do SDK e mantém flush/publicação de cobertura.

Somente RW exato vira RO; RO e NoAccess continuam como estavam. Guard, COW,
executável, modificadores desconhecidos, tipo/identidade errada, aritmética
inválida e falhas de syscall rejeitam cobertura. Em falha parcial, a posse dos
trechos já protegidos continua registrada para tratamento de faults e cleanup.
Nenhum page vizinho é incluído para aumentar artificialmente o lote.

O helper tem testes de quantidade de chamadas, corte de regiões e alias curto,
direitos mistos, rejeições e cleanup parcial. O runtime real testa dois pages
centrais de uma alocação de 64 KiB, bytes não alinhados, vizinhos intocados,
escrita/invalidação por aliases físico/A/C/E e restauração no Unwatch.
Suite nativa: 624 testes. Runtime CTest 4/4 com agrupamento On e 4/4 Off.
SDK verificado pelo instalador; cópia do runtime e artefato de build têm SHA256
`afa8abc18c863e05e0a1760e4a1c72f30b0559641f42c5ded29bdb81d146d91e`.

`SR_TRACKING_ARM_PROFILE=1` registra calls, páginas solicitadas em todos os
aliases, consultas, proteções e tempo de Arm (não o lock inteiro, nem decode,
flush, Unwatch ou Restore). A bateria On usa a mesma instrumentação nas duas
variantes: `logs/cpu_arm_profile_20261030_01/results.json`.

A revisão também encontrou invalidação permanente da geração de uma alocação
após eventos de perda de cobertura, inclusive alterações de proteção do SDK.
Isso ajuda a explicar muitos baselines inutilizáveis, mas não foi relaxado.
Agrupar Arm não resolve essa questão nem os custos de decode extra de Compare.

### Resultado de Arm com profiling

Em On normal, três pares do mesmo EXE/runtime (`cpu_arm_profile_20261030_01`)
resultaram em medianas de 35,585 FPS legado e 34,502 agrupado, p99 94,061 /
65,986 ms. Os efeitos em FPS mudaram de sinal entre pares. Não há ganho de
throughput normal comprovado. Esse primeiro build não coletou os totais de Arm:
o benchmark usa `TerminateProcess`, então o log no Stop não era alcançado.

A coleta foi corrigida para snapshots em Stats e após Arm maior que 100 ms.
Com um novo par de hashes fixo (sem combinar executáveis dentro da bateria),
Compare teve os seguintes resultados:

| Bloco | Legado FPS | Agrupado FPS | Arm legado (ms) | Arm agrupado (ms) |
| --- | ---: | ---: | ---: | ---: |
| 1 | 8,162 | 43,587 | 25.837,541 | 367,780 |
| 2, ordem inversa | 26,783 | 34,655 | 25.749,509 | 442,579 |

Os últimos snapshots do legado cobriram 97.304 / 99.724 páginas solicitadas,
194.608 / 199.448 consultas e 67.626 / 69.237 proteções. O agrupado registrou
109.220 / 110.828 páginas, 4.756 / 4.880 consultas e 2.985 / 3.042 proteções.
São totais até o snapshot, incluindo inicialização, não CPU exclusivamente da
janela medida. O custo observado por página solicitada caiu de ~258–266 µs
para ~3,4–4,0 µs. Isso confirma redução de trabalho no caminho diagnosticado,
sem atribuir toda a variação de FPS exclusivamente a ele.

Auditoria ativa, zero queries pré-reuso omitidas em Compare e zero mismatch.
Dados: `logs/cpu_arm_compare_20261030_01/results.json`.
EXE `5258d9e4a93cfb110fd8d7452652478036949b013434470a802eb2a32cb2bd53`;
runtime `258c48c89acfee1bb7ce503df7be2bff35215a59e5e182b16c5e824e683ea6ad`.

Uma bateria On sem profiling verifica o comportamento normal:
`logs/cpu_arm_unprofiled_20261030_01/results.json`.

### Resultado normal sem profiling e promoção

| Bloco | Legado FPS | Agrupado FPS | p99 legado (ms) | p99 agrupado (ms) |
| --- | ---: | ---: | ---: | ---: |
| 1 | 44,858 | 50,357 | 79,170 | 48,223 |
| 2, ordem inversa | 37,478 | 35,593 | 69,285 | 63,624 |
| Mediana (n=2) | 41,168 | 42,975 | 74,227 | 55,924 |

O FPS mudou de sinal entre pares: a mediana +4,4% não estabelece ganho geral
nem precisão estatística. O p99 melhorou em ambos. A redução de syscalls e
custo de Arm, a melhora repetida de Compare e a validação de direitos/cobertura
sustentaram promover o agrupamento ao padrão. O retorno ao legado continua
disponível por variável de ambiente reiniciada. Não há promessa de 60 FPS.

Revisão independente final sem defeitos identificados; os dois reforços
sugeridos também foram adicionados: região que ultrapassa o alias e falha de
proteção no segundo trecho após progresso. Suite final: 624 testes, zero checks
falhos. Runtime final, sem override de Arm e com instrumentação ativa: CTest
6/6 (tracking, perdas/falhas de cobertura, inicialização e drenagem/stall XMA).
Os testes funcionais também passaram nos caminhos explícitos legado e agrupado.

Build final: EXE
`4ba0818215edde3a0f0edb98c1e1884bb60951f0d0c8445242f30bf9d235b2b2`;
runtime `42df8315f81d4ee67e8c3a3248db9824b512df465432349926b7b7a4958f2a87`.
Instalador em VerifyOnly aprovado. Validação do executável com a variável
`SR_TRACKING_BATCH_ARM` realmente ausente:
`logs/cpu_arm_default_20261030_01/results.json`.

Validação final aprovada: JSON válido, exit 0, 36,505 FPS, p99 56,605 ms;
último snapshot com 54.116 páginas solicitadas, 43.248 consultas, 6.063
proteções e 149,022 ms acumulados em Arm. Não é outro par A/B. Ao todo foram
37 execuções em oito baterias; uma foi excluída da comparação por wall time
excessivo do host. Os resultados brutos dessa rodada foram preservados.

Para repetir, sem paralelizar jogo ou compilação:

```powershell
python tools/bench/cpu_web_sweep.py --suite arm --blocks 3 --cpu-profile 0
python tools/bench/cpu_web_sweep.py --suite arm_compare --blocks 2 --cpu-profile 1
python tools/bench/summarize_cpu_sweep.py logs/<nome-da-bateria>/results.json
```

Cada bateria usa pasta única e verifica hashes antes de cada rodada. Não
reutilizar resultados de hashes distintos como se fossem um único A/B.

O controle do bloco 3 levou 12.239 s de wall time, embora a janela interna de
benchmark tenha sido 30 s. Isso sugere suspensão/atraso do host, cuja causa não
foi confirmada. O JSON original permanece intacto; `analysis.json` marca essa
rodada como excluída. O runner agora rejeita duração total maior que 150 s,
além do timeout de espera do processo. O timeout do Windows sozinho não detectou
o intervalo observado. A nova bateria sem profiling começou do zero.

Entre rodadas não excluídas, a cauda de três janelas de tracking indicou
1,004 ms/frame de consultas no controle (n=2) e 0,228 na admissão (n=3).
Isso mede o subsistema, não significa 77% de redução da CPU inteira. As medianas
de FPS foram 40,369 e 40,447, com dispersão suficiente para não provar ganho.
Os tempos de consultas são inclusivos de cronômetros quando profiling está On.

## Outros caminhos encontrados

- O build local já usa `-O3`, `-DNDEBUG` e alvo x86-64-v2. Não há um ganho novo
  demonstrado por simplesmente selecionar Release ou repetir `-O3`.
- A gravação normal já protege assinatura e heaps com `frame_state_bound_`;
  a recomendação de eliminar esses estados redundantes não exige outra mudança.
- A cópia adicional do bloco shared para seu cache em RAM pode explicar parte
  do custo da opção, mas não foi isolada experimentalmente. Um par de buffers
  de montagem em CPU poderia dispensá-la; permanece hipótese.
- PGO precisa de build instrumentado e cargas representativas, incluindo jogo
  real; otimizar apenas o título pode enviesar o binário. Não foi implementado.
- Gravação em múltiplas threads exige separar estado de recursos, alocações e
  comandos dependentes. O guia da AMD é uma direção arquitetural, não prova de
  que adicionar threads ao worker atual será correto ou mais rápido.
