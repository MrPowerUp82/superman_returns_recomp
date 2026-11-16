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

# Segunda rodada: frustum e LOD de objeto único (16/11/2026)

## Ghidra 12.1.3 + XEXLoaderWV

O `default.xex` foi importado e analisado em modo headless (sem interface)
com a extensão XEXLoaderWV 12.1.3. O loader escolheu a linguagem
`PowerPC:BE:64:A2ALT-32addr`, e a análise terminou sem erros. O projeto do
Ghidra fica fora do repositório (`%LOCALAPPDATA%\Programs\ghidra\projects`).
O script de exportação está em `tools/analysis/ghidra/ExportDecomp.java`.

O Ghidra **não serviu como referência para as funções quentes**, por dois motivos:

- **Corpos truncados:** a análise trata como "não retorna" os helpers
  `__savegprlr_N`/`__restgprlr_N` (`0x8279E000..0x8279EFFF`), e a função termina
  logo após o primeiro `bl`. O script desmarca esses helpers, mas o corpo de
  `82305B88` continuou com 8 bytes.
- **Sem VMX128:** a linguagem `A2ALT` não decodifica as instruções vetoriais
  do Xbox 360 (`lvx128`, `vrlimi128`, `vmulfp128`...). Elas aparecem como
  "Unable to resolve constructor" / "bad instruction data", e a descompilação
  para nelas. Foi o que aconteceu com `82319108`, que trunca na primeira
  instrução VMX128.

As funções quentes são justamente as vetoriais. Por isso, a referência
continua sendo o C++ gerado pelo recompilador, que decodifica VMX128
corretamente.

## Alvos

Ranking pelo perfil de CPU guest (`SR_NATIVE_GUEST_CPU_PROFILE=1`, sobrevoo,
original). "Próprio" inclui descendentes não rastreados.

| Função | Próprio (ms/s) | Chamadas/s | Situação |
| --- | ---: | ---: | --- |
| `822F5290` (envio por draw RenderWare) | 81–91 | 20–30 mil | não atacada (chama código XDK/D3D) |
| `82783948` | 66–80 | 3–4 mil | não atacada |
| `823055A8` (LOD por instância) | 40–57 | 7–10 mil | rodada 1 |
| `82316958` (render por nó) | 24–32 | 22–33 mil | não atacada (próprio pequeno) |
| `82305B88` (LOD de objeto único) | 15–21 | 14–20 mil | **nova reescrita** |
| `82319108` (esfera × frustum) | 10–15 | 200–300 mil | **portada da demo** |

## `82319108`: teste de esfera contra o frustum

Portada de `native_frustum_cull.cpp` da demo (`82319340`). O corpo é idêntico,
exceto por uma constante: `0x820219A0` passa a ser `0x82021360`.
`SR_NATIVE_FRUSTUM_CULL`; o wrapper do perfil chama `RunFrustumCull`.

## `82305B88`: LOD de objeto único (reescrita nova)

Não tem equivalente na demo. Foi reconstruída do C++ gerado e documentada no
início de `object_lod` em `native_guest_rewrites.cpp`. É o irmão de objeto
único da `823055A8`:

- aloca sob demanda o auxiliar em `o+220` e faz as duas chamadas virtuais dele
  (`vt[28]` e `vt[48]`, que preenche a matriz em `o+128`);
- transforma a esfera de bounds e chama o teste de frustum;
- percorre a máscara de luzes com `8231A5F8`;
- grava as flags em `o+48`, chama `vt[88]` do objeto e marca o bit `0x200`.

As chamadas guest mantêm argumentos, frame de 256 bytes e endereços de retorno.
As chamadas virtuais usam o mesmo despacho do `REX_CALL_INDIRECT_FUNC` gerado.
`SR_NATIVE_OBJECT_LOD`; o wrapper do perfil chama `RunObjectLod`.

O modo shadow não pode rodar as duas versões, porque as chamadas virtuais têm
efeitos colaterais. Em vez disso, roda o original e depois recalcula em código
nativo o centro (`f+112`), o splat do raio (`f+144`), o ponto local (`f+176`),
as flags de luz (`o+48`) e o bit `0x80` de `o+16`. Esses valores são comparados
com o que o original deixou no frame morto e no objeto. Os argumentos das
chamadas virtuais e da alocação não entram na comparação.

Erro encontrado e corrigido durante a validação: o ponteiro dos planos do frustum
é `[0x829A9390]` (`lis -32101` = `0x829B0000`, −27760), e não `0x82999390`. O
endereço errado causava leitura de guest `0` nos modos 1 e 2.

## Validação (shadow, quatro reescritas juntas)

`--suite guest_rewrites_shadow --blocks 1` (logs em
`logs/rewrites4_shadow2_20261116/`), todas com **0 divergências**:

| Reescrita | Chamadas comparadas | Cobertura |
| --- | ---: | --- |
| teste de luz `8231A5F8` | 23.855.104 | todos os caminhos |
| frustum `82319108` | 23.068.672 | — |
| LOD de objeto `82305B88` | 786.432 | 723 mil escondidos, 176 mil com luz |
| LOD por instância `823055A8` | 393.216 | — |

As quatro em modo nativo (`=1`) rodaram 30 s sem falha. Gameplay e
cooperativo não foram validados.

## Custo por chamada (perfil, 2 blocos alternados)

| Função | Original (ns) | Nativo (ns) | Diferença |
| --- | ---: | ---: | ---: |
| `82305B88` | 1.203 / 1.243 | 1.048 / 1.099 | −12% |
| `823055A8` | 7.528 / 7.916 | 5.182 / 5.218 | −32% |
| `82319108` | 60 / 62 | 41 / 39 | −34% |

Somando as chamadas por segundo, dá cerca de 25 a 30 ms de CPU a menos por
segundo na thread de render. As funções não reescritas `82316958` e `822F5290`
ficaram de 18% a 25% mais caras por chamada nas rodadas nativas. Elas submetem
draws; numa máquina limitada pela GPU, o tempo economizado provavelmente vira
espera dentro delas, mas isso não foi confirmado.

## A/B de FPS (sem perfil, 5 blocos)

`--suite guest_rewrites --blocks 5` (`logs/rewrites4_ab_20261116/`):

- FPS mediano: 51,1 (original) contra 54,4 (nativo);
- nativo − original por bloco: +6,7 / −0,6 / −4,4 / +7,9 / +3,5 FPS;
- CPU do processo: mediana igual (152,0 contra 154,0 s).

O nativo venceu 3 de 5 pares e não houve regressão, mas a dispersão de cerca de
±10% não permite afirmar ganho de FPS.

## Conclusão da segunda rodada

As quatro reescritas são exatas no sobrevoo e cortam de 12% a 34% do custo por
chamada das funções reescritas. O efeito no FPS desta máquina, limitada pela
GPU, não é mensurável. Ficam opt-in (`SR_NATIVE_LIGHT_TEST`,
`SR_NATIVE_INSTANCE_LOD`, `SR_NATIVE_FRUSTUM_CULL`, `SR_NATIVE_OBJECT_LOD`)
até a validação em gameplay e cooperativo.
