# M1, M3 e M4 — implementação e validação de 06/11/2026

Escopo final autorizado: M1, M3 e M4. M2 ficou fora desta etapa. Este relatório complementa a auditoria histórica em `IMPLEMENTATION_AUDIT.md`; não substitui os resultados anteriores nem reatribui medições a builds novos.

## Estado verificável

| Milestone | Implementação e evidência | Limite do aceite |
|---|---|---|
| M1 — movimento por objeto | **A, funcional nos caminhos observados**: câmera, skin/capa, NPC e duas famílias reais de veículos; NGX consome RG16F com validade/rejeição por pixel | Homologação visual ampla de ghosting em voo livre rápido, todos os LODs, transparências e fases continua **C/E**. Screenshots isolados não demonstram eliminação universal de rastros |
| M3 — pacing | **A**: presenter separado do worker, mailbox limitado, prazos racionais, VSync/intervalo1/FIFO nos caminhos configurados; comparativo de 18 execuções | O hardware não produziu 60/120 imagens novas/s nessa cena; scanout físico estável a 60 Hz permanece **E** neste painel |
| M4 — shaders/PSOs | **A, escopo observado**: 241/241 originais nas três bibliotecas, seed D3D 1024+4 estados de raster, Vulkan 303; seis rotas frias finais sem misses/compiles/PSOs | Cobertura de outras fases/configurações/GPUs não percorridas permanece **E**; não há certificação mundial do jogo |

Não há FSR3 integrado nesta entrega. O contrato de movimento/validade pode servir a outros consumidores; o primeiro backend de M1 continua D3D12. O fallback Vulkan foi preservado.

## Builds e condições

- Executável final SHA256: `082c3f4b09d96201d9e324fd34ae618505134971baaba1e39b48ae542e84436a`; cópia em `logs/graphics_m134_final_20261106/executable_d3d1024_vulkan303.exe`.
- Matriz de comparação inicial M1 e M3: `3d3ff31265cf76d0b556bc8c81d572640533f84ed132e8f6b97652c8677b391a`. Os dados continuam vinculados a esse hash. Alterações posteriores foram de seeds/prewarm, documentadas no design.
- Build intermediário Vulkan 303: `03ef15c97b77f1b8bb59c4326b3cce9abc815392ec97581c757e3ff07af0df72`; antes do último prewarm D3D, a seção `.text` era idêntica à matriz anterior, conforme `vulkan303_code_identity.json`. O prewarm final alterou o código de inicialização; não se afirma identidade binária com o executável final.
- Hardware: Intel Core i7-10750H, aproximadamente 16 GB RAM; RTX 2060 de 6144 MiB; driver NVIDIA 617.42. `hardware.json` contém a coleta. Painel nominal 144 Hz; WMI reportou 143 Hz pelo adaptador Intel que dirige a tela. Não foi disponibilizado um modo físico 60 Hz para homologação.
- Benchmark embutido, chamado diretamente pelo executável que `run.cmd` executa, com `--sr_benchmark=true`. Argumentos completos em cada `result.json`; a seleção gráfica foi explícita e os INIs de teste isolados dos do launcher.
- Comparações M1: janela/saída solicitada 1920×1080, escala 1, MSAA 1, FXAA Off, RT Off, limite 0/VSync Off; warmup de 5 s + medição de 30 s. O dump final confirma saída 1920×1080 e MV 1280×720 no DLSS Quality, apesar de o nome do arquivo conter o tamanho guest 1280×720.
- Comparação M3: 1280×720, DLSS/RT/MV Off, VSync On, 5 s+30 s; três rodadas por limite/mecanismo, ordem invertida na segunda. `SR_PRESENTATION_LEGACY=1` seleciona o mecanismo anterior.
- Aceite final M4: três rodadas de 60 s por backend, com 5 s de warmup, no mesmo executável final. D3D Quality+objetos; Vulkan Off/sem MVs. CNPS e journal temporal ausentes em cada rodada D3D; driver cache Vulkan e descriptions ausentes em cada rodada Vulkan. Arquivos originais preservados e restaurados em `finally`, com hashes conferidos. O cache interno implícito do driver NVIDIA não foi apagado.
- Capturas e leituras GPU ocorreram em execuções separadas. Não houve build, fixture GPU ou automação de UI durante as janelas de performance. Os três workers Gemini já tinham terminado antes delas.

## Alterações e evidência de M1

O caminho contínuo usa clips pós-VS atuais/anteriores, preservando deformação real, câmera e constantes do draw. `native_motion_capture.inc::FinishMotionScene`(:560) sela a saída; `ObjectMotionInputsReady`(:1204) valida câmera/linhagem/recursos. O histórico exige identidade e geração, geometria/fetch compatíveis e quadros consecutivos. Mudança ambígua de lote/LOD/geração rejeita a correspondência. Ordem de submissão isolada não qualifica um objeto.

Veículos reais: `observed_vehicle_batch.h::ObservedVehicleBatchRegistry` registra membros do enqueue/append/flush, confirma as seis linhas float4 efetivamente copiadas por instância e verifica os tokens vivos. `instance_probe.cpp` congela essa seleção para o worker. Os hooks estão no manifesto e descritos em `graphics-milestone-1-queued-vehicle-motion-design-2026-11-06.md`. Não foram adicionados hooks ad hoc.

As famílias comprovadas são corpoVS `9A62E7C65051447A`/PS `04CC5C7EC9FDFD7E` e rodasVS `315E6843ECE68A00`/PS `C66813A81FDBD7C7`. O primeiro caminho DF1, anteriormente chamado de veículo, produziu postes e não é usado como prova de carro. O diagnóstico de IDs/proprietários originais estabeleceu essa diferença.

NGX: `dlss_super_resolution.cpp:306` configura `MVLowRes`; :321 passa efetivamente `pInMotionVectors=e.motion`; :324 passa a escala. A convenção é current-to-previous em pixels internos, sem jitter nos vetores; os offsets de jitter e o reset de histórico têm tratamento separado. A máscara de rejeição protege pixels não certificados/misturados. Não se fornece somente a câmera quando o caminho de objetos está aceito.

Prova runtime adicional: `logs/graphics_m134_gameplay_20261106/capture`, quadro ` s4211` (build362b0a21...). O analisador passou a atribuir o residual da câmera congelada aos proprietários originais, com prova2 e validade de produção, sem depender da cobertura do diagnóstico privado.

| Família | Pixels válidos selecionados | Residual >0,25px | Residual >1px | Máximo |
|---|---:|---:|---:|---:|
| Carroceria |888|83|43|1,734px|
| Rodas |100|11|9|2,110px|

Isso comprova componente de objeto independente da câmera. `s2235` não tinha pixels de carros visíveis e não recebe aprovação veicular; teve15688 pixels válidos de Superman e 68449 de capa. NPCs possuem a prova separada de proprietário/identidade em `graphics_owner_cold_gameplay_20261106/capture/owner_inspection.json`, anterior a esta etapa; não se inferiu classe de ator apenas pelo hash de shader.

No executável final, `graphics_m134_capture_final1024_20261106` confirmou avaliação NGX, composição e conclusão GPU. A captura `inspection.json` mediu99,67% de cobertura de movimento válido entre pixels com depth não vazio. É uma cena específica da animação do título, não cobertura de todas as fases.

![Comparação de saídas reais](../logs/graphics_m134_final_20261106/rendered_comparison.png)

As capturas Off/câmera/objetos são execuções separadas; tempo de simulação/câmera não está alinhado pixel a pixel. A figura ilustra saídas reais e não certifica ausência de ghosting em objetos não testados. A captura de gameplay com corpo/capa/carro/pedestre está em `logs/graphics_m134_gameplay_20261106/capture/s004212_output_1280x720.png`.

## Performance observada de M1

Médias aritméticas das três estatísticas individuais, build da matriz inicial,30 s por rodada. As duas últimas rodadas de objetos tiveram um PSO novo cada; essa evidência de falha foi preservada e levou à ampliação final do cache. Os números não isolam custo CPU/GPU de MVs.

| Configuração | FPS | Frame médio ms | σ ms | P50 ms | P95 ms | P99 ms | P99,9 ms | 1% low | 0,1% low | Stutters >100 ms/rodada |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
|D3D12 Off|20.25|49.75|21.82|45.66|77.91|106.85|346.92|6.66|3.73|11.00|
|D3D12 Quality/câmera|20.59|49.37|36.30|47.12|89.39|129.88|569.77|4.48|1.85|27.67|
|D3D12 Quality/objetos|15.07|68.98|52.32|60.37|118.04|234.67|654.91|2.38|1.69|43.67|
|Vulkan 302 Off — antes do fechamento|15.72|70.35|63.34|57.12|144.08|306.22|983.05|2.25|1.24|65.67|

## M3 — correção e resultados

O presenter usa sua thread existente, separada do render worker, aguardando antes de adquirir o mailbox e sem manter o lockGPU durante o pacing. `presentation_pacing.h` aplica prazos racionais e histórico de 32 sobre-esperas; os caminhos capped/VSync utilizam intervalo1 no D3D12 e FIFO no Vulkan. O caminho Vulkan efetivo é `platform/native_provider.cpp:270/302`, não o presenter Vulkan original do SDK.

Foi corrigida uma subcontagem: em 60 Hz, depois deComplete(0), Complete(33333333) avançava corretamente a 50000000ns, mas não contava um slot expirado. O ramo :43 agora incrementa `skipped_`; os prazos e a lógica temporal do jogo não foram alterados. TesteRED:713 testes/1 falha; GREEN:713/0. HeaderSDK, patch0003, manifesto e cópia de build foram sincronizados e verificados.

O resumo passou a reportar fontes únicas aceitas separadamente de repaints. Fontes aceitas também não são scanout nem todos os frames produzidos, pois o mailbox pode substituir saída antiga. Não foram alterados física, áudio, timebase ou delta de simulação.

| Limite | Mecanismo | FPS do jogo | σ ms | P99 ms | P99,9 ms | 1% low | 0,1% low |
|---|---|---:|---:|---:|---:|---:|---:|
|30|legacy|18.86|25.54|111.74|382.42|6.16|4.05|
|30|dedicated|28.58|5.18|51.21|81.05|17.16|12.64|
|60|legacy|18.22|22.60|107.81|367.94|5.81|3.19|
|60|dedicated|28.77|20.12|81.14|336.21|10.14|7.84|
|120|legacy|16.39|60.02|238.13|898.27|2.24|1.16|
|120|dedicated|25.53|13.42|76.84|118.88|11.72|8.85|

Nos três limites, desvio padrão, P99/P99,9 e lows melhoraram na média das três rodadas. A60, P99 passou de 107,81para 81,14 ms e 1% low de 5,81para 10,14 FPS. Isso não fez a cena produzir60 imagens novas/s: o jogo ficou em 28,77 FPS nessa configuração. A homologação física em 60 Hz continua pendente por indisponibilidade do modo no painel; os retornos deAPI não suprem essa prova.

![Pacing, média e rodadas individuais](../logs/graphics_m134_final_20261106/pacing_comparison.png)

## M4 — fechamento de shaders e PSOs

CNPS evoluiu de 967para 1002 descrições; a segunda rodada fria encontrouDF1/ADAF e levou a 1003. A verificação de TODAS as rodadas descobriu aindaDF1/F2BB e outras descrições nos journals, levando à união final de 1024. O prewarm frio final criou1019 PSOs efetivos a partir delas, com deduplicação semântica. O seed C++ de raster temporal passou de 3para4 estados e não depende mais do quarto registro local para pré-aquecer essa combinação observada.

Vulkan 302 apresentou um description miss na terceira rodada, apesar de não traduzir shader. O journal 303foi validado integralmente por magic/ABI/FNV64, confirmou inclusão dos 302 anteriores, passou no decoder CPU de todos os estados e foi embutido. O novo trio frio foi repetido no hash final.

| Recurso | Registros | Bytes | SHA256 |
|---|---:|---:|---|
| native_pipeline_seed.bin |1024|720912|`d9071ba0a26d60d9de54e8649e4dae2646f3e2fc664ccdd30b75f12f8e42953d`|
| vulkan_pipeline_seed.bin |303|77040|`204307a4786cb7d660bfed61b99b7570ece662866cb03bce797edcb908530909`|
| motion_pipeline_records.h |4 estados numéricos|—|versionado em fonte, ABI de journal preservada|

`cache_audit_final.json`: acceptance=true, sem erros,241/241 originais reconhecidos em SRSL e nas SRVK portable/global; seis rotas finais aprovadas com logs, configurações, capturas e hash do executável. Isso demonstra100%do corpus local e ausência de misses nas rotas selecionadas. O próprio relatório mantém `all_game_coverage=not demonstrated`.

| Backend/configuração final,60 s | FPS médio | Frame médio ms | σ ms | P99 ms | P99,9 ms | 1% low | 0,1% low | Stutters >100 ms/rodada |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
|D3D12 Quality/objetos, CNPS+journal ausentes|23.57|42.51|17.57|79.72|217.91|8.16|3.66|6.00|
|Vulkan Off, driver cache+descriptions ausentes|24.57|40.77|21.00|120.69|252.00|5.71|3.32|19.33|

Cada uma das seis rotas registrou pso_runtime_creates=0, shader_runtime_compiles=0, shader_misses=0 e valid=1. Os arquivos `d3d1024_cold_report.json` e `vulkan303_final_report.json` confirmam restauração por bytes/hash dos quatro caches. As rodadas anteriores que falharam continuam nos logs; não foram apagadas nem tratadas como aprovadas.

Estas configurações têm recursos diferentes e não demonstram vantagem isolada de desempenho do Vulkan. Os tempos das coletas de 30 s e60s também não devem ser comparados como ganho causal de uma otimização. Permanecem stutters sem compilação de PSO; a causa dominante deCPU/GPU não foi isolada por PIX nesta etapa.

Captura Vulkan do executável final: `logs/graphics_m134_final_vulkan_visual_20261106/presented.png`; configuração e hash em `metadata.json`. Essa inspeção confirmou a cena do título, não todas as fases ou equivalência pixel a pixel entre APIs.

## Testes, limites e reprodução

- Release final compilou; logs `final_release_build.log` e `final_gpu_build.log` preservados. Sem novo REX_FATAL nesta etapa.
- 713 testes nativos passaram após o último estado de prewarm;120testes Vulkan passaram com decoder303;17testes Python de shaders passaram;3 testes de atribuição por proprietário e 2 de resumo de apresentação passaram. CTest:6/6. A fixture GPU de motion/composição passou, incluindo sinal, jitter, EQUALdepth, rejeição de alpha misto e invalidação conservadora.
- Os três workers Gemini fizeram revisão somente leitura em cópia isolada. A primeira tentativa retornou respostas vazias por comandos bloqueados; a segunda completou3/3. O gate aceitou o bug do contador e rejeitou alegações falsas de ausência do pacingVulkan e propostas de ocultar criações de prewarm. Ver `graphics-m134-gemini-review-2026-11-06.md`; respostas em `logs/graphics_m134_final_20261106/gemini/`. As duas pastas de trabalho novas foram removidas; pastas anteriores do usuário preservadas.
- Não houve commit/push ou reset do checkout. Os demais trabalhos não commitados foram preservados.

Comandos efetivamente usados, a partir da raiz do repositório; escolher nomes/pastas novos ao repetir, pois os harnesses preservam as evidências existentes:

```powershell
cmd /c .tools\build_reference_release_incremental.cmd
cmd /c .tools\build_reference_tests.cmd
.\build\tests-native\sr_native_tests.exe
.\build\tests-vulkan\sr_vulkan_tests.exe
cmd /c .tools\build_object_motion_gpu.cmd
.\.tools\object-motion-gpu-tests\temporal_motion_smoke.exe
python tools/graphics/test_rigid_camera_motion.py
python tools/bench/test_summarize_presentation.py
python -m unittest discover -s tools/shaders -p "test_*.py"
python tools/bench/bench_graphics_suite.py --prefix NOVO_NOME --groups camera objects vulkan --captures
python tools/bench/bench_presentation.py --name NOVA_MATRIZ --compare-legacy
python tools/graphics/inspect_rigid_camera_motion.py logs/graphics_m134_gameplay_20261106/capture --depth-format d24 --owner-vs 9A62E7C65051447A --owner-vs 315E6843ECE68A00
python tools/shaders/cache_audit.py --corpus artifacts/shaders/raw --srsl port/out/build/win-amd64-release/superman_returns_shaders.srsl --srvk port/out/build/win-amd64-release/superman_returns_vulkan.srvk --srvk-global port/out/build/win-amd64-release/superman_returns_vulkan_global.srvk --pso-cache port/assets/native_pipeline_seed.bin --routes logs/graphics_m134_final_20261106/routes_final.json --out logs/graphics_m134_final_20261106/cache_audit_recheck.json
```

Os scripts locais de cold-cache `.tools/run_m134_d3d1024_cold.py` e `.tools/run_m134_vulkan303_final.py` registram os procedimentos exatos de backup/ausência/restauração. Para repeti-los, usar nomes de saída/backup novos; não sobrescrever os originais. Os argv de cada teste permanecem nos `result.json` e o resumo reproduzível nos CSVs e `final_summary.json`.

Pendências de homologação: sobrevoo livre rápido e sequências temporais alinhadas para quantificar ghosting residual; fases e configurações gráficas adicionais para expandir cobertura; equipamento/modo físico 60 Hz para scanout; profiling do caminho crítico para explicar os stutters restantes. Esses itens não foram simulados nem declarados aprovados.
