# Milestones gráficos — continuação e validação do checkout atual

Esta continuação corrigiu a invalidação do denoiser de M2 e completou novas
descrições observadas em M4. O executável final passou em três rotas frias por
backend com zero compilações/misses runtime. M1/M3/M5 existentes foram preservados.
O aceite integral dos cinco milestones continua limitado pelas pendências ao
fim deste documento; os reflexos permanecem híbridos.

## Escopo e proveniência

O pedido atual abrange M1 a M5. A inspeção inicial encontrou implementações,
designs e resultados locais dos cinco milestones, com numerosas alterações
não commitadas. Elas foram preservadas. Este relatório distingue essa base
existente das alterações e execuções desta continuação; os números antigos
não são atribuídos ao novo executável.

| Milestone | Design anterior ao código e implementação encontrada | Evidência existente e limite |
|---|---|---|
| M1 | `graphics-milestone-1-object-motion-design-2026-11-05.md`; histórico pós-VS/identidade por objeto, RG16F de câmera + objeto + deformação, consumidor NGX low-res | `graphics-m134-final-validation-2026-11-06.md`: Superman, capa, NPCs e famílias veiculares observadas; ausência universal de ghosting em todos os LODs/fases não demonstrada |
| M2 | `graphics-milestone-2-dxr-design-2026-11-05.md`; BLAS/TLAS de draws visíveis, raios de sol/reflexos, fallback predicado, denoiser temporal/espacial | `graphics-milestones-integration-2026-11-06.md`: DXR real, reflexos híbridos por radiância projetada e material proxy; não recupera materiais/cena completos fora da tela |
| M3 | `graphics-milestone-3-pacing-design-2026-11-05.md`; presenter separado do worker, deadlines racionais, histórico de oversleep, interval1/FIFO | Matriz de 18 execuções em `graphics-m134-final-validation-2026-11-06.md`; melhora medida de caudas, sem homologação de scanout físico a 60 Hz no painel disponível |
| M4 | `graphics-milestone-4-cache-design-2026-11-05.md` e desenhos Vulkan; bibliotecas pré-compiladas, sementes e prewarm auditável | Corpus observado 241/241; sementes D3D 1024/Vulkan 303 e seis execuções frias documentadas no relatório M134; não equivalem a cobertura de todas as fases |
| M5 | `graphics-milestone-5-native-apt-design-2026-11-05.md`; GRAPHICS OPTIONS na pausa EA APT original, 11 opções/3 páginas, preferências compartilhadas com WPF | `graphics-milestone-5-native-apt-validation-2026-11-05.md`: controle/teclado, aplicação ao vivo e persistência; API requer reinício e recursos ausentes aparecem N/A |

FSR3 não está instalado: o contrato de MVs pode alimentar outro consumidor,
mas não se declara integração de FSR3. MVs/DXR continuam D3D12 nesta versão.

## Correção implementada nesta continuação — M2/M5

Design prévio: `graphics-milestone-2-live-settings-history-design-2026-11-06.md`.
O filtro DXR aceitava histórico após mudar parâmetros do trace ao vivo. Agora
cada slot retém uma chave tipada dos parâmetros do estimador. O temporal exige
compatibilidade de efeitos, máscaras, raios, cones, distâncias, escala, bias,
clear depth, thresholds/peso e luz normalizada, além dos gates existentes de
frame, projeção, recursos e MVs. A comparação é campo a campo, sem padding.

Uma mudança publica somente o resultado espacial; o quadro consecutivo com
parâmetros estáveis volta a permitir temporal. Câmera, jitter e seed aleatória
continuam fora da chave. Não foram alterados shaders, PSOs, bibliotecas,
manifesto, hooks, SDK ou código Vulkan. Nenhum fatal novo foi introduzido.

Regressão GPU adicionada em `tests/gpu/visible_ray_scene_smoke.cpp`: dez quadros
alternam mudança e estabilidade, exercitando distância de reflexo, máscara de
personagens, escala e sol. Ela falhou contra a implementação anterior e passou
com a correção. Os demais testes de sombra real, cor refletida, reprojeção,
reatividade, reset e composição continuaram passando por fence/readback.
O debug layer D3D12 não está disponível: não se declara GPU validation ativo.

O teste foi ampliado com mudança real da cor iluminada e um segundo pipeline
independente, com reset espacial explícito. A saída RGBA do primeiro quadro
após mudar o estimador coincidiu por readback com esse controle. Como controle
negativo, uma cópia isolada do código com **somente o gate da chave removido**
falhou por mistura de RGB antigo (`milestones_resume_m2_gpu_pixel_red.log`).
A implementação real passou (`milestones_resume_m2_gpu_final.log`). Essa cópia
de controle não foi compilada no jogo nem usada em benchmarks.

## Execuções novas

Build Release: `logs/milestones_resume_release_build.log`.
Testes: 730 nativos, 120 Vulkan, 17 Python de shaders, CTest 6/6 e fixture DXR
passaram. Os logs estão em `logs/milestones_resume_*tests.log`,
`milestones_resume_ctest.log` e `milestones_resume_m2_gpu_{red,green}.log`.

Baseline M2: três rodadas válidas em `milestones_resume_m2_before_r1`,
`milestones_resume_m2_before_r2`, `milestones_resume_m2_before_round3`, todas
em `logs/`. Saída solicitada 1920×1080, DLSS Quality, objetos On, RT On,
MSAA1/FXAAOff, escala1, limite0/VSyncOff, warmup5s/medição30s. Os argumentos
completos e hashes estão nos respectivos `result.json`.

O harness chama o mesmo executável de `run.cmd`, com `--sr_benchmark=true`,
e isola o INI de teste. Nenhuma compilação ou fixture GPU ocorreu nas janelas
de performance. Capturas têm processos próprios e não entram nas médias.

SHA256 antes: `9865beae64d3106c960b9afc7c9094ac366851d7d1a932c4f6dd8dd9cd39cd25`.
SHA256 depois: `565cd1d26e13a1ba36f3fd77b2eecadfa3a7e1afa3ca967a6ea044940e2fe641`.
A matriz final tem três rodadas por configuração, com ordem invertida na segunda.

| Configuração | FPS | Média ms | σ ms | P50 ms | P95 ms | P99 ms | P99,9 ms | 1% low | 0,1% low |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Antes: Quality + objetos + DXR |15,92|63,00|34,19|57,01|99,19|182,84|468,02|3,54|2,26|
| Depois: Quality + objetos + DXR |17,43|57,63|22,44|55,20|86,40|108,24|384,09|5,74|2,64|
| Depois: Quality + objetos, RT Off |24,74|40,58|17,81|37,41|63,35|83,49|296,53|7,32|3,38|
| Depois: Vulkan, DLSS/RT/MVs Off |20,85|48,48|29,22|42,59|93,71|183,78|321,20|4,34|3,29|

Fonte: `logs/milestones_resume_summary.json`; médias aritméticas das três
estatísticas individuais. σ populacional, percentis nearest-rank, lows calculados
por 1000/média da pior fração de tempos, e não 1000/P99. A variação observada
entre as coletas não isola causalmente ganho de desempenho desta correção de
validade. As configurações Vulkan e D3D12 têm recursos distintos e não permitem
concluir superioridade geral de API. Nenhum resultado foi reatribuído a outro hash.

As nove janelas finais passaram no probe e registraram zero PSOs runtime,
compilações de shader e misses. Isso ainda é uma condição com caches locais;
o ensaio frio é uma evidência separada. Os dumps pós-build de objetos e DXR
também passaram na checagem de correspondência apresentação/NGX, com 100% dos
pixels dentro de 2 unidades de 8 bits.

Uma primeira captura solicitou swap420, mas o ensaio terminou antes disso.
Seu probe marcou `valid=false`; foi preservado e excluído da comparação. A
repetição em swap120 passou: `milestones_resume_m2_capture_before120`, com
100% dos pixels apresentados a até 2 unidades de 8 bits da saída NGX.

As capturas são da animação de título, com relógios de processos independentes.
Elas mostram a saída efetiva, mas não isolam ghosting de um objeto nem provam
continuidade visual em sobrevoo livre. Para o bug corrigido, o gate de histórico
da fixture GPU é a prova controlada; não se atribui à imagem estática um efeito
que só aparece durante uma transição de configuração.

## M4 — falha fria, correção dos seeds e proveniência

A primeira rodada fria Vulkan do hash565cd1 reprovou M4: duas criações de PSO
e duas description misses. O benchmark terminou válido como medição, mas o
auditor retornou `zero_runtime_misses_demonstrated=false`; não foi contado como
aceite. Não houve compilação nem miss real de shader. Os quatro caches originais
foram restaurados por bytes/hash; o relatório da falha está preservado em
`logs/milestones_resume_cold/report.json`.

Design antes da mudança: `graphics-milestone-4-observed-seed-union-design-2026-11-06.md`.
A união validada de303 entradas embutidas,306 anteriores locais e305 produzidas
no teste frio resultou em308 entradas Vulkan. O decoder de produção reconstruiu
todas308 e os120 testes Vulkan recompilados passaram. A união D3D12 de1024
entradas embutidas com6495 registros locais resultou em1050 descrições distintas.
Os números de entrada não equivalem a PSOs únicos após deduplicação semântica.

| Asset intermediário | Registros | Bytes | SHA256 |
|---|---:|---:|---|
| native_pipeline_seed.bin |1050|739216|`5197b857c14a8051cd944a732058206e0a459942d0470ddbd9863bffda8aa7a0`|
| vulkan_pipeline_seed.bin |308|78304|`9dfa17b1ae560fdf4c3b93ba7df53f4cb542e98042f77ebe17fd3d5ea819b8e5`|

Fontes e seeds anteriores estão arquivados em `logs/milestones_resume_seed_union/`,
com bounds/ABI/FNV64 e hashes verificados antes da publicação. Nenhum registro
foi sintetizado. Nenhum blob de driver foi embutido.

O executável intermediário após esses recursos tem SHA256
`accfa8e11bec100be776a8882a987bb074e5faef9cb437bb5be76caf74c5ef18`.
`code_identity.json` confirma `.text` idêntica ao hash565cd1:
`488d038b0e00cd74d5b53384e34f862a324763c482a1440f0bf4028935aae92a`.
As métricas anteriores continuam atribuídas ao565cd1; não foram renomeadas como
medições do executável accfa8. O build novo está em
`logs/milestones_resume_seed_release_build.log`.

Essa primeira ampliação ainda não fechou M4: a segunda rodada D3D12 no accfa8
encontrou seis descrições novas e sete criações em runtime (aliases distintos
compartilham uma descrição). O gate reprovou a rota e restaurou os caches.
Essa falha está em `logs/milestones_resume_seed_final/report.json`.

O complemento foi documentado no design antes da nova publicação. A semente
D3D12 passou a1056 descrições,743440 bytes, SHA256
`6a79253e6b38e2b278e86ef22961b226b354a4f0e175abf9ebc89319ac4a9ad6`.
Vulkan permaneceu308. As fontes e a união estão arquivadas no mesmo diretório
de evidências. O executável final tem SHA256
`994b1b640574eb7e2dc5b63663b313c0b51988cb3e01895ccaa605a46136035c`,
com `.text` ainda idêntica, conforme `code_identity_v2.json`.
Build: `logs/milestones_resume_seed_v2_release_build.log`.

O trio D3D12 e o trio Vulkan no hash994b1 passaram: seis janelas de60s com
`pso_runtime_creates=0`, `shader_runtime_compiles=0`, `shader_misses=0` e
`valid=1`. Todos os quatro caches foram restaurados por bytes/hash. O relatório
`logs/milestones_resume_seed_v2/report.json` registra `acceptance=true` e os
aceites individuais; os ensaios reprovados anteriores permanecem separados.
Cada rodada começou sem cache/journal local; o cache implícito NVIDIA não foi
apagado. Os60s começam após5s de warmup. Eventos de sessão também são retidos
pelo auditor para distinguir inicialização de trabalho dentro da janela.

| Configuração fria final, média de3 | FPS | Média ms | σ ms | P99 ms | P99,9 ms | 1% low | 0,1% low |
|---|---:|---:|---:|---:|---:|---:|---:|
| D3D12 Quality + objetos + DXR |17,24|58,08|19,23|105,24|240,69|6,88|3,35|
| Vulkan, DLSS/RT/MVs Off |22,90|43,93|26,09|136,76|359,98|4,56|2,45|

As janelas de30s e60s têm escopos distintos e não são um A/B causal do seed.
P50/P95 e os dados individuais permanecem em `milestones_resume_summary.json`.
Ainda há stutters sem compilação de PSO. Nenhum profiling PIX foi realizado;
os custos DXR estão nos timestamps/fences do próprio renderer e Vulkan foi
medido com `SR_VULKAN_PROFILE=1`.

Os testes finais adicionais passaram: rebuild e fixture GPU de M1
(`milestones_resume_m1_gpu_{build,tests}.log`) e preferências/textos do launcher
(`milestones_resume_launcher_final_tests.log`, zero falhas). O `dotnet` do PATH
não tinha SDK; foi usado o SDK local existente `.tools/dotnet-launcher/dotnet.exe`,
sem instalação ou alteração do ambiente global.

## Auditoria final e capturas

`logs/milestones_resume_seed_v2/cache_audit.json` terminou com `acceptance=true`,
sem erros:241/241 originais nas bibliotecas SRSL, SRVK portable e SRVK global;
seis rotas frias aprovadas, com fingerprints do executável/bibliotecas,
configuração e capturas reais. A semente final D3D12 tem1056 descrições e a
Vulkan308. `all_game_coverage` permanece `not demonstrated`.

A execução diagnóstica separada com `--sr_native_dump_shader_dir` produziu183
containers. Todos183 estão nas três bibliotecas; as bibliotecas têm58 entradas
adicionais do corpus mais amplo. O primeiro verificador auxiliar exigiu igualdade
exata entre o dump parcial e a biblioteca completa, e reprovou por essas entradas
extras. A checagem foi corrigida para cobertura do subconjunto observado,
preservando os campos de igualdade exata, sem modificar o auditor principal.
`fresh_shader_dump_audit.json` conserva `complete=false` para igualdade exata e
`observed_subset_complete=true` para183/183 em cada perfil. Nenhum shader foi
fabricado ou incluído para contornar esse diagnóstico.

Capturas finais:

- D3D12: `logs/milestones_resume_capture_final/capture/presented.png`, com RG16F,
  validade e dados de RT no mesmo diretório. Probe, inspeção temporal e
  comparação apresentação/NGX passaram (100% dentro de2 unidades de8bits).
- Vulkan: `logs/milestones_resume_visual_vulkan/presented.jpg`, captura original
  da janela por Computer Use, com `metadata.json`/`window.json`. Foi observada
  a cena renderizada; o estado cinza de inicialização foi descartado. Encerramento
  normal por Alt+F4, sem processo restante.
- Os caches produzidos pelos diagnósticos foram arquivados antes de restaurar
  novamente os quatro originais. `milestones_resume_post_capture_caches/restored.json`
  confirma todos por hash. Preferências dos testes usaram INIs isolados.

![Antes/depois: saídas reais independentes, sem alinhamento temporal](../logs/milestones_resume_seed_v2/rendered_comparison.png)

O painel não demonstra eliminação universal de ghosting; o bug de M2 foi
reproduzido e corrigido por RGB/alpha GPU contra um controle independente.

## Reprodução

Os argv completos estão nos `result.json`; CSVs e snapshots dos executáveis
permanecem junto das rotas. Scripts de execução específicos desta continuação
ficam em `.tools/`: `run_milestones_resume_seed_v2.py`,
`summarize_milestones_resume.py` e `finalize_milestones_resume_audit.py`.
Eles preservam evidências existentes e recusam sobrescrevê-las. Para repetir
uma matriz fria, usar uma cópia com prefixo novo; manter backup/restauração e
o gate de aceite. Não executar builds, fixtures GPU ou capturas em paralelo
com as medições. Para uma nova matriz comum:

```powershell
python tools/bench/bench_graphics_suite.py --prefix NOVO_PREFIXO --groups objects dxr vulkan --captures
```

## Pendências de aceite integral

Continuam necessárias sequências temporais alinhadas de voo com veículos/NPCs,
homologação física a 60 Hz e rotas de fases adicionais para ampliar o corpus
de shaders/PSOs. Reflexos usam material proxy e radiância projetada. Os testes
de título não certificam cobertura completa de materiais ou todo o jogo.
