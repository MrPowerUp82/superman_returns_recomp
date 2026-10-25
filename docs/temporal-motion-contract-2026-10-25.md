# Captura temporal D3D12 — experimento parcial

Não é uma declaração de DLSS funcional no gameplay. O adaptador NGX já
executa com dados sintéticos; faltam entradas completas do jogo.

## Dados e correspondência

`PostVertexClipCapture` reexecuta o VS original com os mesmos inputs e
constantes. Um GS grava o `SV_Position` final como float4, incluindo ajuste
de pixel legado e a deformação realizada pelo VS. Não reconstrói clips a
partir de uma câmera aproximada. Strips são expandidos em triângulos; o
padding é zerado antes da captura. São três slots de 64 MiB, 4096 draws por
slot. O consumidor aceita até 4.194.303 vértices cumulativos, arredondados
para triângulos completos. GPU e produtor compartilham esse limite por
assert de compilação.

`TemporalGeometryHistory` exige quadros consecutivos, dimensões iguais e
histórico selado antes de resolver correspondências. Ordem de draws ou
hash de malha não identifica um objeto móvel. Só VS2C45 possui prova parcial
de posição estática: buffers/versões e dependências world iguais.
O fallback amplo considera c4/c98..255; a captura usa o intervalo normalizado
de vértices, congelado no frontend, para limitar as constantes alcançáveis
c100+2×instância/c101+2×instância. O índice da instância vem de
`trunc((vertexId+0,5)/c4.x)`. c4.y também participa do remapeamento dos inputs.
O envelope de precisão foi auditado para o `fdiv` preciso presente no DXIL
do corpus e cache runtime; não é uma garantia para um `Rcp` aproximado.
O banco de câmera c0..3 pode mudar entre quadros. Conflito de câmera dentro
do quadro invalida o experimento. Instâncias móveis/deformadas precisam de
identidade e geração comprovadas; não foram identificadas neste estágio.

Descritores SRV são transitórios. A assinatura semântica substitui cada SRV
pelo recurso, geração host e versão de upload resolvidos nos streams reais;
conserva semantic/offset/stride/tipo. Binding desconhecido ou conflitante é
rejeitado atomicamente. Isso não transforma conteúdo em identidade de ator.

## Passe de movimento

`TemporalMotion` usa os clips atuais e anteriores e o depth congelado em
recurso privado antes de o guest reutilizá-lo. A saída é `R16G16_FLOAT`,
current→previous em pixels de renderização; `R8_UINT` separa validade de
velocidade zero. Interpolação anterior é perspective-correct. O passe usa
depth EQUAL sem escrita e preserva cull, winding, depth bias e scissor.
Inputs desconhecidos escrevem validade zero. Alpha test, translucência e
cobertura customizada não são emulados; a saída é explicitamente parcial.

O módulo retém recursos/descritores por slot e clobbera o estado gráfico.
O executor restabelece heaps, root signature, PSO, IA, viewport/scissor e
targets antes dos próximos draws. Reuso/destruição e readback exigem fence;
uma submissão em outra fila exige sincronização adicional do chamador.

## Evidência e reprodução

O teste isolado na RTX 2060 aprovou D24/D32, movimento de câmera/objeto,
deformação e perspectiva, jitter real das fixtures, três slots em voo,
cull/winding/bias/scissor e limites. O teste de limite executa 4.194.303
vértices, incluindo um triângulo em movimento no final da arena. Isso
valida o backend com fixtures, não a semântica completa do gameplay.

`tools/bench/motion_capture_check.ps1` inicia uma rodada com nome novo,
recusa outra instância aberta e solicita dois quadros após reconhecer o
HUD. Não mede desempenho nem liga DLSS. Dumps são interpretados por:

```powershell
python tools/graphics/inspect_temporal_motion.py logs/<nome-da-rodada>
```

O PNG de validade mostra branco para movimento verificado e preto para
regiões não verificadas. O PNG de velocidade colore direção/magnitude;
magenta indica ausência de correspondência válida. A cor capturada tem
nome `candidate_pre_hud_color`: precisa de inspeção e evidência da ordem de
composição antes de ser aprovada como entrada anterior ao HUD.

Primeira rodada (`motion_capture_safe_2410`) rejeitou o limite antigo de
3M e encontrou zero matches entre 646 candidatos estáticos. Não gerou uma
saída de movimento aceita. Limite e canonização foram corrigidos; 331
testes nativos passaram. A rodada `motion_validation_2510` executou o passe
com 3.629.637 vértices e readbacks após fence, mas teve zero pixels válidos.
Buffers/metadata foram estáveis; constantes world mudaram. A imagem de cor
capturada antes do primeiro font já continha a barra do HUD e foi rejeitada.

`motion_vertex_range_2510` também rejeitou todos os matches: 689 candidatos
estáticos no primeiro quadro, zero correspondências no seguinte. O passe
continuou produzindo validade zero. Foram adicionados snapshots limitados
dos valores reais para esclarecer a mudança antes de alterar a regra.
Checkpoints do mesmo quadro mostraram a cena completa antes de VS6952/PS4074;
antes de VS7320/PSF047 já havia mira e contorno da barra. A classificação do
primeiro passe de HUD ainda exige a auditoria correspondente.

`logs/static_world_address_gpu_2510.log` registra teste independente na RTX
2060: 320.911 vértices, 423 strides e 331.866 leituras de constantes reais
cobertas pelo envelope CPU; 41 entradas inválidas rejeitadas. Esse teste
valida o intervalo de acesso, sem afirmar que os valores persistem na cidade.
