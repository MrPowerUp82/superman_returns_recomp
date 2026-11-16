# Reescritas nativas: teste de luz e LOD por instância (16/11/2026)

Backport de duas reescritas funcionais do decomp da demo
(`supermanReturns-demo-decomp`, `port/demo_runtime/native_light_test.cpp` e
`native_instance_lod.cpp`) para o retail, em
`port/src/native_renderer/native_guest_rewrites.cpp`.

| Retail | Demo | Função |
| --- | --- | --- |
| `8231A5F8` (+ `8231A408`) | `8231A830` (+ `8231A640`) | teste de influência de luz e teste de caixa |
| `823055A8` | `823057E0` | visibilidade, LOD e máscara de luzes por instância |

## Mapeamento demo → retail

Comparei o C++ gerado sem mascarar endereços de dados:

- `8231A5F8` / `8231A408`: corpos idênticos, endereços de dados incluídos.
  Só os valores de LR das chamadas diferem, e a reescrita não grava nenhum.
- `823055A8`: idêntico exceto pelas constantes de limiar do LOD e pelos
  destinos de chamada:
  - `0x82913CD0..CF8` → `0x82913CC0..CE8` (−0x10);
  - `0x82020A28..A30` → `0x820203E0..3E8`;
  - chamadas `82319340` → `82319108` (frustum) e `8231A830` → `8231A5F8`;
  - LR de retorno `82305B14` → `823058DC` e `82305D6C` → `82305B34`.

  Os acessores de câmera que a reescrita embute (`82775BD8` / `82775BF0`) são
  idênticos aos da demo (`82775D98` / `82775DB0`), e nenhum hook do retail os
  substitui.

O símbolo `823055A8` continua no wrapper do perfil de CPU
(`guest_cpu_profile.cpp`), que chama `RunInstanceLod`. `8231A5F8` ganhou um
hook próprio. A LOD chama `sub_8231A5F8`, então usa o teste de luz no modo
selecionado.

`SR_NATIVE_LIGHT_TEST` e `SR_NATIVE_INSTANCE_LOD`: `0` = original traduzido
(padrão), `1` = nativo, `2` = shadow (mantém o resultado do original e compara).

## Validação (shadow)

`python tools/bench/cpu_web_sweep.py --suite guest_rewrites_shadow --blocks 1 --cpu-profile 0`
(sobrevoo da tela de título, D3D12 nativo, 1280x720):

- teste de luz: 27.262.976 chamadas, **0 divergências**, com todos os caminhos
  exercitados (23,7 mi rejeições pelo cilindro, 2,5 mi pela caixa 1,
  364 mil aceitas sem caixa, 673 mil testes da caixa 2, 3,5 mi com índice de
  luz diferente de zero);
- LOD por instância: 458.752 chamadas, **0 divergências**.

Logs em `logs/rewrites_shadow_20261116/`. Gameplay e cooperativo não foram
validados.

## Custo por chamada (perfil de CPU guest)

`SR_NATIVE_GUEST_CPU_PROFILE=1` com `--suite guest_rewrites --blocks 2`, ordem
alternada. A tabela mostra o tempo inclusivo de `823055A8`, que inclui os
testes de luz que ela chama:

| Rodada | Original (ns/chamada) | Nativo (ns/chamada) |
| --- | ---: | ---: |
| bloco 1 | 6.920 | 5.208 |
| bloco 2 | 6.704 | 5.420 |

São cerca de 21% a menos por chamada, ou aproximadamente 1,5 µs. Com cerca de
200 chamadas por quadro, isso dá **cerca de 0,3 ms por quadro**, por volta de 1%
de um quadro de 25 ms. As funções de controle (`82319108`, `82316958`,
`821078C0`) ficaram estáveis entre as variantes.

O FPS dessas rodadas instrumentadas (44,7 / 49,1 no original; 44,6 / 43,8 no
nativo) está dentro do ruído de cerca de ±10% entre execuções desta máquina, que é
limitada pela GPU integrada. Um ganho de cerca de 1% não aparece no A/B de FPS.

## Conclusão

As duas reescritas são exatas no sobrevoo e mais baratas por chamada, mas o
ganho absoluto é pequeno. Ficam opt-in até serem validadas em gameplay. Junto
com `SR_NATIVE_CONSTANT_UPLOAD=1`
([upload de constantes](native-constant-upload-2026-11-16.md)), formam o
conjunto de reescritas guest disponível.
