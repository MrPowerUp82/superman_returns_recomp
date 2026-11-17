# Terceira rodada: seis reescritas portadas da demo (16/11/2026)

Seis reescritas funcionais do decomp da demo (`supermanReturns-demo-decomp`,
`port/demo_runtime/`) portadas para o retail. Antes de portar, comparei o C++
gerado de cada par sem mascarar endereços de dados (só nomes de função e
endereços de retorno). Na tabela, "idêntico" quer dizer que o corpo é igual
inclusive nos dados.

| Retail | Demo | Função | Arquivo retail | Variável | Demo × retail |
| --- | --- | --- | --- | --- | --- |
| `822F5290` (+ `8261CED0`) | `822F54C8` (+ `8261D0A8`) | envio dos segmentos de draw de um modelo (RenderWare) | `native_draw_segments.cpp` | `SR_NATIVE_DRAW_SEGMENTS` | idêntico; LR `822F5644/56DC` → `822F540C/54A4` |
| `82783948` | `82783B08` | percurso de elementos de camada, protegido por lock | `native_layer_walk.cpp` | `SR_NATIVE_LAYER_WALK` | só o argumento do lock: `0x82099498` → `0x82098FB8` |
| `820F4920` / `820F4A30` | iguais | cópia de constantes VS/PS (inclusive fontes desalinhadas) | `native_constant_copy.cpp` | `SR_NATIVE_CONSTANT_COPY` | idêntico |
| `82107760` | igual | upload de grupos de 6 dwords no anel de comandos | `native_constant_upload_b.cpp` | `SR_NATIVE_CONSTANT_UPLOAD_B` | idêntico |
| `82107520` | igual | upload irmão | `native_constant_upload_c.cpp` | `SR_NATIVE_CONSTANT_UPLOAD_C` | idêntico |
| `826092B8` | `82609490` | registros de estado RenderWare (máscara + tabela) | `native_record_bits.cpp` | `SR_NATIVE_RECORD_BITS` | idêntico |

Todas aceitam `0` = original traduzido, `1` = nativo e `2` = shadow.

## O que mudou em relação à demo

- **Mecânico:** as cvars viraram variáveis de ambiente, os endereços foram
  remapeados (script de port) e cada port ficou num `.cpp` próprio para não
  misturar os helpers de cada um.
- **Donos dos hooks:**
  - `822F5290`, `826092B8` e `82783948` já tinham wrappers do perfil de CPU
    em `guest_cpu_profile.cpp`; esses wrappers agora chamam
    `RunDrawSegments` / `RunRecordBits` / `RunLayerWalk`.
  - O wrapper de `82783948` continua chamando `NoteCameraProbeGroup` do
    cooperativo.
  - `820F4920` / `820F4A30` já tinham hooks em `native_hooks.cpp`
    (`TryCopyConstantBank` só para fontes alinhadas e `TryCopyConstantPrefix`,
    que copia um prefixo e deixa o resto para o original). O hook chama
    `RunConstantCopy` primeiro; com `SR_NATIVE_CONSTANT_COPY=0`, mantém o
    caminho anterior.
  - `82107760`, `82107520` e `8261CED0` (só o traço do shadow dos segmentos)
    não tinham hook no retail.
- **Removido:** o `memcpy` `825F6B38` de `native_leaf_copies.cpp`. Em retail
  esse endereço é outra função. Também saíram o atalho e o traço de
  `native_ps_params.cpp`, que não foi portado.
- **Percurso de camadas:** a demo valida essa função com um *lockstep shadow*
  (`lockstep_shadow.h`, portado sem mudanças), porque as chamadas virtuais têm
  efeitos colaterais e não podem rodar duas vezes:
  - o original roda uma vez numa cópia instrumentada, e o nativo roda junto
    numa fibra sobre uma visão copy-on-write da memória;
  - a cada chamada, os dois precisam pedir o mesmo alvo, LR e argumentos, e ter
    escrito os mesmos bytes desde a chamada anterior.

  A cópia instrumentada vem de `tools/analysis/trace_functions.py`, a mesma
  transformação da demo apontada para o código gerado do retail. Ela é
  derivada do jogo e por isso fica fora do git (`port/generated/traced/`).
  Sem ela, o modo 2 roda só o original. Esse port vai no alvo principal (que
  enxerga `port/generated/default`), não no `sr_native`.
- **Prefetch:** o percurso faz prefetch dos próximos elementos, reconhecendo
  nós de cena pela vtable. O endereço retail é `0x82021768`: os slots
  `+8/+12/+16` (`82316CA8` / `82316CB0` / `8230F638`, os thunks da demo
  − 0x238) estão em `0x82021770` na imagem retail.

## Validação (shadow, sobrevoo, 40 s)

| Port | Chamadas comparadas | Divergências | Observação |
| --- | ---: | ---: | --- |
| cópia de constantes | 26.214.400 | 0 | 848 mil (3%) voltaram ao original: fonte em faixa recusada |
| upload B `82107760` | 458.752 | 0 | 361 fallbacks (flush do anel) |
| upload C `82107520` | 917.504 | 0 | 29 fallbacks |
| segmentos de draw | 589.824 | 0 | — |
| registros de estado | 588.800 | 0 | — |
| percurso de camadas (lockstep) | 143.360 percursos, 1.737.960 chamadas | 0 | — |

**Controle negativo do lockstep:** com `SR_NATIVE_LAYER_WALK_SHADOW_FAULT=1`,
o nativo publica um endereço de iterador errado. O shadow acusou 31.911
divergências em 126.976 percursos, todas no byte `0x829A93B7` do iterador
global. Isso mostra que o mecanismo detecta erros de verdade.

Logs em `logs/isolate_20261116/ports5_shadow.log`, `layer_shadow.log` e
`layer_fault.log`.

## Custo (perfil de CPU guest, 6 pares alternados)

`SR_NATIVE_GUEST_CPU_PROFILE=1`, `--suite demo_ports`, logs em
`logs/ports_profile_20261116/` e `logs/ports_profile2_20261116/`. A tabela
mostra o custo inclusivo por chamada, nativo dividido pelo original, como
mediana dos 6 pares:

| Função | Nativo / original | Por par |
| --- | ---: | --- |
| `826092B8` registros de estado | **0,80 (−20%)** | 0,86 0,80 0,78 0,80 0,77 0,94 |
| `82783948` percurso de camadas | 0,95 | 1,12 0,75 0,91 0,99 0,86 1,09 |
| `820FC000` (draw que chama cópias e uploads) | 0,955 | 1,11 0,90 0,94 0,97 0,88 1,23 |
| `822F5290` segmentos de draw | 1,02 | 1,66 0,65 1,13 0,76 0,92 1,31 |
| `82316958` (controle, não reescrita) | 1,00 | 1,52 0,69 1,10 0,81 0,90 1,30 |

O controle `82316958` varia de 0,69 a 1,52 entre pares, então nesta máquina
só ganhos grandes e consistentes ficam visíveis. Só os registros de estado
mostraram ganho claro, em 6 de 6 pares. O percurso de camadas e as
cópias/uploads tendem a ganhar (4 de 6). Os segmentos de draw não mostraram
ganho mensurável, mas também nenhuma regressão.

Por quadro, o tempo de CPU do processo dividido pelos quadros renderizados ficou
em mediana 8% menor no nativo (razões de 0,64 a 1,60). O FPS por par variou de
−18,7 a +9,9 (mediana +4,0). Os dois são inconclusivos pelo mesmo ruído.

## Capturas e ativação

O sobrevoo rodou com janela visível com as 11 reescritas desligadas (`=0`) e
depois ligadas (`=1`), com capturas aos 30, 38, 46 e 54 s
(`logs/shots_20261116/all11_*.png`). As versões nativas renderizam corretamente:
Superman de perto (capa, iluminação), janelas com reflexo, sombras nos
prédios, montanhas e céu. Nenhum objeto faltou. Os dois benchmarks terminaram
válidos.

Desde então os seis ports vêm **ativos por padrão** (`1`), como as cinco
reescritas anteriores. Numa execução sem nenhuma variável definida, o custo por
chamada confirma o caminho nativo (`826092B8` ≈ 490 ns, `823055A8` ≈ 5,1 µs,
`82319108` ≈ 40 ns). Para voltar ao original, use `=0` na variável
correspondente. O cooperativo e o gameplay ainda não foram testados.
