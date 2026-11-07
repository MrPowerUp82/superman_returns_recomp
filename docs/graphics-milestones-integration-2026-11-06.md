# Milestones gráficos: implementação e evidências

Este relatório consolida o trabalho de 05–06/11/2026. Os designs individuais em
docs/ precedem as respectivas mudanças. A direção posterior do usuário exige
UI original EA APT; ela substitui o overlay do primeiro design de M5.

## Implementação integrada

| Milestone | Comportamento implementado | Limite da evidência |
|---|---|---|
| M1 | Histórico pós-VS por objeto/parte, incluindo skin/capa; RG16F + validade + ownership do PS original; movimento de câmera e objeto no input NGX externo, low-res, sem jitter | Pixels de corpo, capa e NPCs atribuídos em gameplay; identificação individual de veículos e comparação temporal exaustiva ainda não demonstradas |
| M2 | BLAS/TLAS de geometria visível, DXR inline de sol/reflexos, filtro espacial/temporal usando M1, supressão dos mapas originais somente no quadro com substituição válida | Reflexo usa radiância retida do hit projetado e material proxy; geometrias fora da cena e alpha variável não recebem opacidade de triângulo |
| M3 | Apresentação dedicada, interval1/FIFO no modo travado, deadlines racionais30/60/120 e margem adaptativa de jitter | Três rodadas por modo em monitor144Hz; scanout físico60Hz indisponível |
| M4 | CNPSv2, variantes de PS/SO e utilitários pré-aquecidos; seed portátil Vulkan; auditoria com fingerprints e rotas delimitadas | Zero misses refere-se às rotas observadas; não a um playthrough integral |
| M5 | GRAPHICS OPTIONS na pausa original, moldura/fonte/Toggle/foco/Back originais, 11 opções em 3 páginas, teclado/controle, preferências compartilhadas com WPF | API exige reinício; capacidades indisponíveis aparecem como N/A |

MVs e DXR estão habilitados no D3D12. Contratos de identidade, cobertura,
configuração, auditoria e pacing preservam a separação do backend Vulkan.
Não há integração nova do SDK FSR3 nesta versão. Nenhum REX_FATAL novo foi
necessário. Hooks adicionais constam no manifesto/midasm documentado.

## Gameplay e cobertura de motion vectors

`graphics_owner_cold_gameplay_20261106/capture/owner_inspection.json` cruza
ownership original por pixel com validade/velocidade e o manifesto de replay.
No quadro de draw 2539/present 2540, corpo C687 teve 39.094 pixels válidos, capa A523
24.830 e NPCs C209 tiveram 39. Outros 41 pixels C209 eram mistos e rejeitados.
No draw 3801 foram 117 pixels C209 válidos/móveis. Correspondência de draw não
é promovida automaticamente a cobertura visível. O mesmo buffer alimenta NGX
e o filtro temporal de RT; regiões sem prova rejeitam histórico.

Correções verificadas por RED/GREEN: envelope de índices na produção, fetch
inativo de IA excluído da assinatura e alpha final1 no ownership. A fixture
GPU mostrou que simplesmente descartar um pixel misto conservava o MV válido
do fundo; agora esse pixel escreve validade 0. A captura final usa o ticket de
quadro, pois ExecOnSwap atualiza o número depois dos draws. Logs registram o
vínculo exato dos dois números, sem usar captura de quadro vizinho.

![Gameplay com tráfego durante decolagem](../logs/graphics_live_frame_gameplay_20261106/flight_window.png)

## Shaders e PSOs

Seed D3D12 final: 967 descrições CNPSv2, 680.784 bytes, SHA256
`42db71103eb23ac342f12e58c43a902025d55a0a7a6e319bb09892d7b0d6458e`.
No cold start sem cache local,962 descrições efetivas para MSAA 1 foram
pré-compiladas em 0,8 s. As demais são variantes normalizadas/deduplicadas.
O trecho de solo/movimento/decolagem de 89,4 s registrou 1.551 quadros,
4.139.606 draws e zero PSOs criados, zero compilações e zero misses em runtime.
Evidência: `logs/graphics_owner_cold_final_20261106/game.log`. O cache anterior
foi preservado e restaurado com SHA verificado.

A tentativa anterior encontrou 1 PSO de blend8E5232/A828 em 0,27 ms. Essa tentativa
permanece reprovada para zero-runtime-PSO; sua descrição foi incorporada antes
da repetição. O seed Vulkan contém 268 descrições portáteis ABI1; cold start
real importou 268/268 com zero falhas, independente do driver cache do dono.

O corpus observado tem 241 originais e está completo nas bibliotecas SRSL,
SRVK portátil e SRVK global. O auditor aceitou duas rotas de gameplay do build
anterior em `logs/graphics_live_frame_routes_audit_20261106.json`, com exe
arquivado, screenshots e fingerprints. Não demonstra todas as fases do jogo.

O auditor também aprovou a rota fria final em
`logs/graphics_cold_final_routes_audit_20261106.json`. Na matriz Vulkan foram
observadas34/0/3 criações de PSO; portanto o seed inicial de268 não recebeu
aceitação zero-runtime. A coleta auxiliar de65s não criou pipelines e o
journal validado contém302 estados ABI1,76.880bytes, SHA256
`fa0f052b2b4190d022922fb562b4e07aae6cb51e00ac30da1d982e7894d66fa9`.
Esse é o novo recurso embutido, com todos302 estados decodificados pelo teste.
A repetição fria em três rodadas tem o prefixo graphics_final_body_vk302_20261106.
As três passaram com zero PSOs em runtime, zero compilações e zero misses. A
primeira iniciou com local=0, merged=302 e302/302 prontos; as outras iniciaram
com302 descrições locais. Os caches anteriores foram preservados e restaurados
com hash verificado. Isso cobre essas rotas, sem certificar todas as fases.

## Corpo no DXR

O export literal alpha1 de C687/3EF6 agora tem certificado do draw inteiro,
condicionado aos valores reais de spec/normalização e ao PS original auditado.
DEC3N/R11 usam somente os bits de normal0/6; fades/testes/coverage e bits
desconhecidos rejeitam o certificado. No CSV s2143 do gameplay final, cinco
draws3EF6 somam19.446 vértices admitidos; trêsE83A continuam rejeitados.
O teste GPU confirmou que o toggle de sombra dos personagens não os remove dos
reflexos. O trecho delimitado teve732 quadros e2.059.625 draws, sem criar PSO,
compilar shaders ou encontrar misses. Dados em
`logs/graphics_literal_alpha_body_final_20261106/`, build244ea9da75af48a0...
Os reflexos permanecem uma primeira aproximação com F0 fixo e radiância de hit
visível; não recuperam roughness/BRDF do jogo e podem refletir superfícies que
um material mais completo deveria tornar foscas.

## Menu original do jogo

![Entrada na pausa original](../logs/graphics_final_gameplay_indexed/native_pause_entry.png)

![Opções gráficas com Toggle original](../logs/graphics_final_gameplay_indexed/native_graphics_layout.png)

O teste real exercitou as11opções, todos os modos NGX e seus fences, resize,
FPS30/60/120, FXAA/MSAA/escala e API pendente de reinício. O teclado fez
entrada→Graphics→página2→FPS30/60→volta→resume. O INI preservou comentários e
chaves desconhecidas. A correção de setas usa SetWidth 200 no Toggle original;
790 controla a separação das setas e não a largura da linha. Não há renderer
de overlay instalado. `game/DATA/ui.AST` permanece original; somente a cópia
privada derivada é montada no VFS.

O patch SDK0004 retém taps digitais até o poll e libera state_mutex antes de
notificar a UI. Evidência REA do ciclo de espera:
`ev_9ca1ed4357b33784b28b49ca4f1f8cf6d8e6484e323f3678cd20573a6ecdc470`.
Após a correção, os fechamentos normais observados levaram201–202ms.
Investigação e limites do REA estão no design nativo de M5 e nos artefatos
`artifacts/graphics-menu-apt-research/`; não houve suporte inventado a XEX/PPC.

## Validação de desempenho

A matriz `graphics_final_owner_20261106` concluiu 12 medições válidas e 3 capturas:
três rodadas por câmera, objeto, DXR e Vulkan, com um único SHA do executável
`cae7efb81ef69999ea1bd974dffb9108599e9e701bfc0da0564bd1f15b70b5c8`.
As capturas têm execuções próprias e não entram na média. O harness invoca
diretamente o mesmo executável de run.cmd, com --sr_benchmark=true, 5 s de
aquecimento e 30 s de medição. Não há builds, testes GPU ou instrumentação
concorrentes. Resultados incompletos não são promovidos a médias de 3.

Médias aritméticas das três métricas por execução; percentis nearest-rank e
desvio populacional dos CSVs. Não são percentis de uma população agrupada.

| Configuração | FPS | Média ms | σ ms | P99 ms | P99,9 ms | 1% low FPS | 0,1% low FPS |
|---|---:|---:|---:|---:|---:|---:|---:|
| D3D12 DLSS Quality, câmera |17,044|59,472|39,753|181,849|638,283|3,226|1,648|
| D3D12 DLSS Quality, objetos |15,510|65,447|55,442|215,858|785,364|2,345|1,320|
| D3D12 objetos + DXR |11,931|84,635|89,436|313,927|1.130,144|1,492|0,905|
| Vulkan, DLSS/RT Off |16,021|62,712|84,257|430,567|891,860|1,926|1,329|

Há custo de desempenho e picos importantes, não uma melhora geral de FPS.
CSV, JSON, hash e contadores estão em
`logs/graphics_final_owner_20261106_summary.json`. A extensão posterior de M2
para o corpo com export alpha 1 terá sua própria medição no novo executável;
esta tabela preserva exatamente o build medido.

A repetição após o certificado do corpo e o seed Vulkan302 concluiu seis
medições válidas (três por API/configuração) no build
`244ea9da75af48a0112a909dfebb9b5ad7973d812e16cddb15c3fe17428a9945`:

| Configuração final | FPS | Média ms | σ ms | P99 ms | P99,9 ms | 1% low FPS | 0,1% low FPS |
|---|---:|---:|---:|---:|---:|---:|---:|
| D3D12 DLSS Quality + objetos + DXR com corpo certificado |10,687|94,879|66,867|348,305|750,162|1,822|1,344|
| Vulkan, seed302, DLSS/RT Off |13,193|76,968|71,737|387,934|682,860|2,046|1,532|

As seis tiveram zero PSOs em runtime, compilações ou misses nas janelas de
medição. O seed resolve essas criações observadas; os picos restantes e o baixo
FPS não desapareceram. Não atribuir causalmente a variação entre matrizes ao
seed ou ao pequeno conjunto de triângulos adicionado. Evidência:
`logs/graphics_final_body_vk302_20261106_summary.json`. A captura DXR separada
também passou a conferência NGX; sua taxa não compõe a média de desempenho.
Nas amostras GPU periódicas das três rodadas (4/3/3 amostras), as médias por
rodada variaram de1,494 a7,576ms para conversão/build e de1,703 a3,363ms para
trace/denoise/composite. São timestamps nativos de estágios D3D12, não medição
via PIX nem orçamento de GPU do quadro inteiro.

Capturas separadas, gamma desligado para conferir pixels NGX (poses da animação
de título variam; não são imagens pareadas para diferença pixel a pixel):

![DLSS com movimento de câmera](../logs/graphics_final_owner_20261106_capture_camera/capture/presented.png)

![DLSS com movimento por objeto](../logs/graphics_final_owner_20261106_capture_objects/capture/presented.png)

![DLSS com objetos e DXR](../logs/graphics_final_owner_20261106_capture_dxr/capture/presented.png)

O baseline histórico camera-only teve40,293FPS e σ17,163ms, mas outro exe e
janela headless0x0; a matriz atual fixa1920×1080. Ele não permite atribuir
causalmente uma diferença a M1, M4 ou M5. O custo de M1/M2 é comparado também
com câmera no mesmo build e configuração, retendo a variação da cena de título.

M3 tem sua própria matriz de 18 execuções no build7b1d95..., descrita em
[validação de pacing](graphics-milestone-3-pacing-validation-2026-11-05.md).
A 30 FPS, σ caiu de 4,564 para 2,805 ms e P99 de 50,929 para 41,138 ms; a 60,
σ caiu de 8,655 para 6,992 ms e P99 de 49,960 para 39,374 ms. Não confundir retornos
da API/repaints com frames novos nem com scanout físico60Hz.

![Tempos de quadro antes/depois do pacing dedicado, três rodadas por modo](../logs/graphics_pacing_144hz_reviewed/comparison.png)

## Verificação técnica

CTest6/6 passou, com 703 casos no sr_native_tests e 8 regressões de teclas no
target separado. Testes Python: 6 de APT, 14 de auditoria e 3 de seed. Vulkan:
120 casos e zero falhas. Fixtures GPU reais na RTX2060 passaram para movimento,
composição DLSS, alpha misto, três slots/resize, sombras/reflexos e rejeição
temporal reativa. Logs finais: `graphics_final_owner_motion_gpu.log`,
`graphics_final_owner_rt_gpu.log`, `graphics_final_owner_vulkan_cpu.log`.
O debug layer D3D12 não estava instalado; essa cobertura não é alegada.

O auditor do gameplay final com RT também passou em
`logs/graphics_literal_body_final_routes_audit_20261106.json`: corpus241/241 nas
três bibliotecas, seed D3D12967, executável/screenshot/raws identificados e
rota de732 quadros com contadores zerados. O SHA256 do ui.AST original continua
`9893245576351240f1362b7d9e8e8121a1a899dd16e605d9decb8096e08c177f`.
git diff --check não encontrou erros de whitespace. Nenhum processo de teste
do jogo permaneceu aberto após o encerramento das coletas.

## Critérios ainda sem certificação completa

As implementações acima não equivalem a aceitação irrestrita dos cinco
milestones: não há playthrough das fases principais que certifique todos os
shaders/PSOs do jogo, classificação de todos os veículos móveis ou sequência
pareada que demonstre ausência de ghosting em todas as animações. O monitor
disponível não forneceu scanout60Hz para esse teste físico. RT usa somente a
geometria submetida e o material proxy descrito acima; alpha variável ainda
não recebe opacidade de triângulo. Esses limites permanecem explícitos e não
são promovidos a resultados positivos por testes sintéticos ou counters zero.
