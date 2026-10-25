# Posições de mundo pós-VS para DXR

Data: 24/10/2026. Base `adddef2`, branch `codex/rt-dlss-menu-20261024`.
Fonte: corpus local `artifacts/shaders/hlsl`, confrontado com a assinatura de
saída OSG1 do DXIL efetivamente carregado. A existência de TEXCOORD por si só
não determina sua semântica: apenas os hashes auditados abaixo são aceitos.

| VS (container hash) | Varying XYZ canônico | Evidência HLSL (linhas) |
|---|---|---|
| 2C45BFFE76D77398 | TEXCOORD2 | 934–946: projeção row-dot do mundo `(r1.y,r1.x,r1.z)`; 1034: exporta o mesmo mundo |
| 4F17209D364D5B51 | TEXCOORD1 | 944–959: gWorldXform; 996: exportação |
| 18B4F69400B7F0AF | TEXCOORD1 | 917–932: gWorldXform; 969: exportação |
| 9A62E7C65051447A | TEXCOORD1 | 925–937: projeção; 1114: mundo r5 |
| 008A68F78E2E3502 | TEXCOORD1 | 993–1005: projeção após instância; 1138: mundo r4 |
| 32EAA2870BB32915 | TEXCOORD1 | paleta c15; 1346–1361 → 1407 |
| 406390546D9691FA | TEXCOORD1 | paleta c16; 1499–1518 → 1581 |
| 770469DDF5AB09DC | TEXCOORD1 | paleta c22; 1344–1364 → 1442 |
| 9B2F97272E1F510B | TEXCOORD1 | paleta c18; 1309–1324 → 1365 |
| C687EB65D635C5D7 | TEXCOORD1 | paleta c18; 1302–1317 → 1358 |
| C20921D0DBBD2F1F | TEXCOORD1 | paleta c22; 1339–1359 → 1442 |
| E57FB2C0DC954BAA | TEXCOORD1 | paleta c16; 1343, 1447–1452, 1483 → 1518 |
| EC4D2CD998957026 | TEXCOORD1 | paleta c18; 1339–1359 → 1442 |

Os oito últimos possuem skinning. Isso não identifica semanticamente Superman,
capa ou qualquer instância como personagem. As permutações já ocorreram no VS;
o consumidor recebe XYZ e não aplica uma segunda permutação.

O shader E57FB2C0DC954BAA modifica Z projetado depois do WVP (1338–1362), então
inverter o clip final não é uma alternativa geral. Os casters
57A00D4EB3BF4C31 e C7C0E381A3C9B79E têm viés geométrico antes da pele e não
foram incluídos. Não tratar o subconjunto atual como cobertura fora de câmera.

## Implementação

`WorldPositionTexcoord` seleciona o contrato; `VertexShaderHasFloat3Output`
confere nome, índice, stream, tipo float e máscara XYZ na assinatura OSG1.
`CapturePostVertexDraw` reexecuta o VS original com os mesmos buffers, constantes,
booleanos e metadados de fetch. Um GS copia o mundo para stream output float3,
expandindo primitivas triangulares segundo a IA original, inclusive cuts.
Uma arena distinta por frame em voo é reutilizada apenas após seu fence.
Espaços não emitidos ficam zerados/degenerados; a capacidade é limitada e
descartes são contados. A captura é opt-in, uma vez, por
`--sr_native_rt_capture_swap=<swap>`.

No final da captura, o caminho de validação pode construir um BLAS/TLAS real
com os triângulos capturados e lançar um lote de raios. Ele não atesta materiais,
alpha cutouts, identidade de personagem, residência ou cobertura completa.

## Primeira execução comprovada

`logs/bench_rt_postvs_first_2410.log`, HUD confirmado às 08:24:54, captura no
guest swap 1650 às 08:25:14: **616 draws, 338.856 vértices, zero invalid/zero
dropped**. Esse EXE ainda usava a primeira variante de extração por inversa VP,
somente para 2C45BFFE76D77398. Exemplos de triângulos em mundo aparecem no log
após o fence; a captura de início está em `logs/bench_rt_postvs_first_2410_start.png`.
Isso NÃO valida retroativamente os 13 contratos diretos adicionados depois.

## Captura direta e execução DXR no gameplay

`logs/bench_rt_menu_isolated_2410.log`: HUD confirmado às 13:40:42; swap 1733:
992 draws, 626.625 vértices emitidos, nenhum contador SO inválido ou draw
descartado. A arena reservou 723.519 vértices, incluindo espaços degenerados;
o BLAS recebeu 241.173 triângulos. Nove raios tiveram interseções reais,
com distâncias de 18,9808 a 506,672, lidas após o fence do slot.
Essa execução usa os varyings diretos auditados. Não significa que todos os
13 shaders apareceram nessa cena, nem que as posições de todos os vértices
foram inspecionadas individualmente na CPU.

A assinatura completa VS→GS é necessária: SV_Position, TEXCOORD0..15,
COLOR0..1 e NORMAL0..1 na ordem original. Um GS com somente TEXCOORD1/2
falhou com E_INVALIDARG; o teste independente com os shaders reais 2C45,
4F172 e EC4D passou após a correção. A geração dos headers GS por DXC está
reproduzível no CMake de `tests/gpu`.

`logs/bench_rt_depth_prefs_2410.log`: HUD confirmado às 13:54:02; snapshot
D24S8 1280×720 antes da troca de superfície. Foram emitidos 650.472 vértices
em 892 draws; a arena reservou 731.148 vértices para 243.716 triângulos.
O inverso de projeção inclui agora o mesmo deslocamento de pixel do raster.
Em uma grade de 576 amostras, houve 321 interseções e 302 diferenças de
distância até 1%; erro relativo mediano de 0,0000112759 e p95 de 0,0234015
**entre as amostras com interseção**, não entre todas as amostras.
Isso deixa 274 amostras sem concordância nesse limiar. A câmera/depth não
estão aprovados como contrato temporal completo.

O gatilho `--sr_native_rt_capture_trigger_path=<arquivo>` permite capturar
depois da confirmação do HUD, sem depender de um swap fixo de inicialização.
O caminho continua opt-in e não aplica RT/DLSS ao resultado apresentado.

RT visual, materiais, caster fora de tela, correspondência temporal e composição
da iluminação continuam como critérios separados. O teste GPU independente
`tests/gpu/rt_scene_smoke.cpp` exercita o backend com uma cena conhecida; não
substitui a integração e comparação no jogo.
