# Upload nativo de constantes `821078C0` (16/11/2026)

Backport da reescrita funcional do decomp da demo
(`supermanReturns-demo-decomp`, `port/demo_runtime/native_constant_upload.cpp`)
para o retail, em `port/src/native_renderer/xdk_constant_upload.h`. O wrapper de
`sub_821078C0` em `guest_cpu_profile.cpp`, que já existia para o perfil de CPU,
chama a reescrita. Assim, não há um segundo hook no mesmo símbolo.

## Equivalência com a demo

O C++ gerado de `821078C0` é idêntico na demo e no retail: mesmo endereço,
mesmas 131 linhas e nenhum deslocamento diferente. A reescrita foi portada sem
alterações; só mudaram os nomes e a configuração.

`SR_NATIVE_CONSTANT_UPLOAD`: `0` = original traduzido (padrão), `1` = nativo,
`2` = shadow (roda o nativo e o original com as mesmas entradas, compara os
bytes do anel e os registradores e mantém o efeito do original).

## Validação (shadow)

`python tools/bench/cpu_web_sweep.py --suite constant_upload_shadow --blocks 1 --cpu-profile 0`
(sobrevoo da tela de título, D3D12 nativo, 1280x720, sem limite de FPS):

- 1.048.576 chamadas comparadas, **0 divergências**;
- 9.389 fallbacks para o original (cerca de 0,9%: caminho de flush do anel,
  `sub_82107058`, que fica com o original).

Logs em `logs/upload_shadow_20261116/`.

## A/B de desempenho

`python tools/bench/cpu_web_sweep.py --suite constant_upload --blocks N --cpu-profile 0`,
com a ordem alternada a cada bloco, no i5-13420H + Intel UHD.

| Rodada | Blocos | Original (mediana FPS) | Nativo (mediana FPS) | Nativo − original por bloco |
| --- | --- | --- | --- | --- |
| `upload_ab_20261116` | 3 | 63,32 | 56,86 | −6,46 / −11,23 / +4,64 |
| `upload_ab2_20261116` | 5 | 53,72 | 52,59 | −10,60 / −1,39 / −4,28 / −0,29 / +0,61 |

O tempo de CPU do processo ficou igual nas duas variantes (cerca de 140 a 158 s
por rodada). O FPS variou muito entre execuções (50 a 65 FPS) e caiu ao longo da
sessão, o que é compatível com aquecimento ou turbo. Mesmo assim, o nativo não
venceu de forma consistente.

## Conclusão

A reescrita está correta, mas **não houve ganho mensurável** nesta máquina,
que é limitada pela GPU integrada (README, seção de desempenho). O +6,8% medido
na demo não se repetiu aqui. Por isso o modo fica opt-in (`0` por padrão). Vale
repetir o A/B numa máquina limitada pela CPU, ou numa cena de gameplay, antes
de ativá-lo por padrão.
