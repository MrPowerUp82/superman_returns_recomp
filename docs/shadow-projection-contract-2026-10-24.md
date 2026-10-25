# Projeção afim da sombra original

Fonte auditada: HLSL traduzido do corpus local. Este contrato identifica a
direção dos raios da projeção original; não identifica intensidade, tamanho
angular, caster lógico, materiais ou cobertura completa da cena.

VS receptor `2C45BFFE76D77398`: constantes `shadowMapU/V/Z` em c9/c10/c11.
Nas linhas 1003–1011, TEXCOORD6 recebe o produto das três linhas com a mesma
posição world XYZ exportada em TEXCOORD2. A permutação `.zxyw` do coeficiente
e `.zyxw` do registro mantém as componentes canônicas XYZ.

VS caster `57A00D4EB3BF4C31` e `C7C0E381A3C9B79E`: U/V/Z em c4/c5/c6,
projeção afim com oPos.w=1. O depth exportado recebe a transformação de Z
e o viés/escala do atlas. No receptor `559F422BEF5E5714`, linhas 1464–1484,
depth do receptor menos o armazenado gera a atenuação da sombra.

Uma direção de raio para a luz precisa preservar U e V e diminuir Z:
`d = normalize(cross(U.xyz,V.xyz))`, escolhendo o sinal com `dot(Z.xyz,d)<0`.
O cálculo rejeita projeções sem posto, dados não finitos e Z sem variação
nessa direção. Não usa uma luz procedural chamada gLight como substituta.

A captura aceita esse candidato somente se as três linhas do caster e do
receptor coincidirem no mesmo quadro e não variarem durante a captura.
`tests/native/test_shadow_projection.cpp` cobre escala, shear, mudança de
sinal, posto insuficiente, NaN e projeções diferentes.

Em `logs/bench_rt_visibility_2410.log`, a captura encontrou a mesma projeção
no caster e receptor e derivou direção (-0,694407; 0,686629; 0,215266).
A GPU produziu 914.092 receptores válidos, com 312.412 ocluídos.
O raw e a imagem de inspeção ficam em `logs/rt_visibility_2410/`.
Essa é uma textura de visibilidade, sem composição de iluminação.

O experimento de textura usa cone zero, viés 0,05 e alcance 10.000 como
parâmetros explícitos de diagnóstico. Não são apresentados como valores
recuperados do jogo. A composição posterior na família auditada é descrita
abaixo; cobertura completa de materiais e alpha test continuam pendentes.

## Casters do passe original

Além dos varyings de mundo dos receptores, a captura suporta os dois shaders
originais de caster. Ambos exportam clip=(2U−1, 1−2V, 0,5, 1) e
TEXCOORD0.x=32×(Z+bias), depois de zero a três transformações de atlas.
`shadow_caster_geometry.h` e a variante GS `WORLD_TEXCOORD=0` desfazem atlas,
viés de depth e deslocamento de pixel e invertem a projeção afim U/V/Z.
Não usam o clip Z constante para reconstruir mundo. A posição conserva
a deformação e o deslocamento Thickness do caster original; não é uma
geometria sem viés apropriada automaticamente para reflexos.

O teste GPU da variante GS de produção recuperou triângulos conhecidos nos
quatro níveis de atlas, inclusive um vértice fora do frustum. Atlas singular
produziu zeros degenerados, sem NaN. Os dois VS originais passaram criação
de PSO com a variante. `logs/shadow_caster_gpu_2410.log` contém os resultados.

No gameplay `logs/bench_rt_casters_2410.log`, swap 1828: 2.002 draws,
1.469.295 vértices emitidos, nenhum descarte ou contador SO malformado.
57A00 contribuiu com 4 draws/30.726 vértices; C7C0 com 1.080/715.638.
O BLAS recebeu 596.685 triângulos reservados, incluindo padding degenerado.
A nova textura teve 915.761 receptores válidos e 460.216 ocluídos.
As cenas diferem entre runs; esses números não são um A/B de qualidade.
Os casters ampliam a residência além da câmera somente até o conjunto
submetido pelo passe original. Não garantem cobertura de toda a cidade.

## Composição parcial executada no gameplay

Uma variante versionada do PS 8A6D, exclusivamente com VS 2C45, agora
substitui o fator de oclusão do atlas dentro de seu bloco p0 auditado.
Mantém fade, materiais e mistura de cor gShadCol originais; famílias não
mapeadas usam raster. A fonte é traduzida a partir do container local do
jogo e compilada como ps_6_5, sem modificar o shader original.
TLAS usa t0/space6, constantes de 48 bytes b3/space4 e contador u0/space6.
O contador registra invocações reais da função de raios por wave; o host só
publica On após ler resultado positivo com o fence concluído.

Build e testes CPU passaram, e a fonte do corpus e do emissor runtime
produziram DXIL idêntico. `logs/bench_rt_inline_menu_2410.log` confirma
execução no jogo: High frame1885/rev2, Low frame1905/rev3 e High
frame1919/rev4 tiveram 2.241.641 invocações; Off frame1941/rev5 teve zero.
Todos os estados aplicados foram confirmados após fence; o harness de menu
passou On/Low/High/Off. Low usa 4 amostras/cone 0,003 e High 16/cone 0,01.
Esses parâmetros são escolhas explícitas do renderer. A evidência não
aprova fidelidade geral, alpha test ou desempenho.

Character Shadows continua indisponível: skinning/hash e proximidade do
jogador não comprovam propriedade do ator ou da capa. Os casters originais
incluem personagens, mas ainda não há controle independente desse conjunto.
