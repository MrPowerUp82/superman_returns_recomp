# Port do RT (sombras e reflexos) do Superman-Returns-PC

Origem: `github.com/gangg111/Superman-Returns-PC` (commits `644273f`..`a7f6513` e
a parte de RT de `6be83f6`), base comum com este repositório em `92c26e0`
(14/10/2026). O RT antigo deste projeto (cena DXR dos draws visíveis, reflexos
híbridos por radiância projetada, variantes HLSL de sombra RT em runtime) foi
removido em `refactor(rt): remove the visible-scene DXR...`.

## Como funciona

Os shaders do próprio jogo fazem o ray tracing. A tradução Xenos→HLSL
(XenosRecomp) ganhou os patches `0014`–`0034`:

| Patch | O que faz |
|---|---|
| 0014 | `tfetch3D` de texturas empilhadas como `Texture2DArray` (só D3D12; aqui o host usa volumes, o bit nunca é ligado) |
| 0015 | Corte de 750 unidades das cascatas segue `g_ShadowRange` (0/1 = original) |
| 0016 | `g_ColorOutputScale` (c31): a normalização fixed16 deste projeto, mantida no lugar do `0016-fixed-point-color-range` deles |
| 0017–0023 | Receptores de sombra (`shadowMapOffsetScales`) reconstroem a posição de mundo pela matriz da cascata 0 e traçam `RayQuery` ao sol; substituem as cascatas, alcance total, penumbra com vários raios |
| 0024–0029, 0032 | Reflexos: a amostra `env_map` dos receptores vira o que um raio acerta (cor, tint, paleta dos prédios, tom da cidade, personagens, direção de espelho) |
| 0030 | Vetores de movimento para DLSS (ainda sem consumidor: `g_SrMotionValid` = 0, b3 = constantes atuais) |
| 0031 | Reflexo RT na água original (`gWaterSpec`) |
| 0033 | Folhas/grades com alpha test deixam o raio passar |
| 0034 | Vulkan não declara o array empilhado (o layout Vulkan não tem esse binding) |

Receptores e água compilam como `ps_6_5` (`build_catalog.py` e o tradutor em
runtime). No Vulkan o RT fica desligado no shader (`#if !defined(SR_VULKAN_BUFFERS)`).

No renderer (`native_rt.inc`, `rt_shadows.cpp`):

- Root signature: parâmetro 8 = tabela com space6 (arrays), space9 (buffers
  brutos) e space8 (TLAS); 9 = CBV b3 (patch 0030); 10/11 = owner/reactivity
  do motion. Os últimos 6 descritores do heap são fixos (`kRt*Srv`).
- Cada draw indexado de receptor vira um BLAS (chave: VB, IB, faixa) e uma
  instância por matriz de mundo; draws instanciados (`rt_instanced_regs.inc`)
  idem, com o índice da malha no stream `POSITION1`. Personagens com skinning e
  a capa do Superman passam por stream output e entram num BLAS por frame.
- O primeiro receptor de cada frame sobe os dados do frame (`kRtFrameSrv`,
  256 bytes: matriz inversa da luz, sol, chaves, tom, câmera) e constrói o TLAS.
- Tabelas geradas a partir do catálogo de shaders: `shadow_regs.inc`,
  `rt_*_regs.inc` (`tools/shaders/gen_shadow_regs.py`).

## Opções

Menu Graphics Options (pausa) e launcher:

- **RT Shadows**: sombras do sol pelo RT em vez das cascatas.
- **Shadow Softness**: Low = disco real do sol (≤ 8 raios), High = disco 2× (≥ 16 raios).
- **Character Shadows**: personagens e capa na cena RT (sombra e reflexo).
- **RT Reflections**: janelas/vidros refletem a cena RT; o céu fica onde o raio erra.

Exigem Direct3D 12 e GPU com DXR 1.1 / SM 6.5. Cvars de ajuste e diagnóstico:
`sr_rt_shadow_debug` (1 grade de posição, 2 visibilidade, 4 distância, 6–8
reflexos), `sr_rt_shadow_bias`, `sr_rt_builds_per_frame`, `sr_rt_full_scene`,
`sr_rt_reflection_bump`, `sr_rt_instance_range`, `sr_rt_dump_obj_swap`.

## Pendente

- DLSS deles (fase 3): o `dlss.cpp` usa MRT extra para o movimento por objeto e
  constantes VS do frame anterior por draw (patch 0030). O DLSS atual deste
  projeto continua ativo até essa troca.
- Co-op em tela dividida: o frame RT usa a primeira matriz de luz do frame; a
  segunda câmera ainda não foi validada com RT ligado.
