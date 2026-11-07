# Auditoria técnica da implementação — 06/11/2026

## A. Resumo executivo

Auditoria somente leitura da implementação: nenhuma correção ou otimização foi aplicada.
Builds, testes, benchmarks, logs e este relatório foram autorizados pelo pedido. O relatório
anterior está preservado em `logs/full_implementation_audit_20261106_01/IMPLEMENTATION_AUDIT_previous.md`.
O inventário de 783 arquivos e seus hashes está em `source_before.json` nesse diretório.
As numerosas alterações locais anteriores fazem parte do objeto auditado.
Conferência final em `source_verification.json`: os 783 arquivos, os seeds, HEAD e
o executável permaneceram idênticos; nenhum novo arquivo de fonte no escopo inventariado.
As únicas entregas desta etapa são relatório e artefatos de auditoria. Caches/journals
normais do runtime foram escritos pelos benchmarks; não são mudanças de seed embutido.

HEAD: `2b07a6803cbcd1323bfec41d2ed3ca025952ff94`. Executável examinado, SHA256:
`994b1b640574eb7e2dc5b63663b313c0b51988cb3e01895ccaa605a46136035c`.
O hash, e não o timestamp antigo impresso pelo aplicativo, identifica as medições.

Há implementações reais: motion buffers chegam a NGX; DXR constrói BLAS/TLAS e executa
ray queries; apresentação é independente do worker; há prewarm/cache e ponte entre menu
EA APT, renderer e launcher. Entretanto, isso não certifica cobertura integral, materiais
completos, ausência universal de ghosting, scanout físico a 60 Hz ou todo o jogo.

Limites relevantes: cobertura temporal parcial; reflexos com radiância projetada e material
proxy; alocações/BVH reconstruídos por quadro RT; qualidade de reflexos acoplada a sombras;
período guest ajustado apenas na construção; certificação prolongada de áudio pendente.
Uma nova rodada Vulkan encontrou cinco PSOs/descrições fora do seed 308; as rodadas seguintes
aqueceram o cache. Portanto M4 não recebe aceite zero-runtime para a nova rota (AUD-12).
Não foi reproduzido crash, corrupção ou deadlock P0 nesta auditoria.

O relatório anterior chamava M1/M3 de “fechados”, apesar de registrar lacunas de cobertura
visual e de homologação física. Essa certificação não é repetida. Também estava desatualizado
sobre os seeds e o contrato NGX: o código atual declara movimento **parcial com máscara de
rejeição**, não cobertura integral. O diagnóstico abaixo distingue implementação de aceite.

Legenda: **A** = execução confirmada no cenário indicado; **B** = caminho ativo confirmado
no código; **C** = parcial; **D** = não localizado na fronteira examinada; **E** = indeterminado.
A não significa cobertura de todo o jogo. Prioridades são recomendações, não correções executadas.

## B. Matriz de implementação

| Funcionalidade | Estado | Evidência | Problema | Prioridade |
|---|---|---|---|---|
| M1 — motion vectors por objeto | C; produtor/consumo A | `port/src/native_renderer/native_motion_capture.inc:503–612,1217–1241`; `port/src/native_renderer/native_renderer.cpp:8150–8170`; fixture GPU e probes NGX | Máscara parcial; ausência universal de rastros não homologada | P1 |
| M2 — RT Shadows | C; DXR A | `port/src/native_renderer/native_visible_rt.inc:91–231`; `port/src/graphics/d3d12/visible_ray_scene.cpp:174–328`; `port/src/graphics/d3d12/ray_tracing_scene.cpp:283–335` | Geometria submetida/opaca; alpha e receivers restritos | P2 |
| M2 — RT Reflections | C; ray queries A | `port/src/graphics/d3d12/shaders/visible_ray_scene.hlsl:91–118,169–190`; fixture GPU e probes DXR | Cor projetada; F0 fixo; materiais/roughness incompletos | P1 |
| M3 — frame pacing | C; mecanismo B, testes A | `port/src/native_renderer/native_graphics_system.cpp:195–259,452–522`; presenter SDK; `port/src/graphics/presentation_pacing.h:99–123` | Retorno de API não é scanout; limite guest não acompanha toda troca ao vivo | P1 |
| M4 — pré-shaders | A no corpus; C global | SRSL/SRVK; `cold_evidence_recheck.json`; 241/241 originais | Fases não percorridas; fallback desconhecido não exercitado aqui | P2 |
| M4 — PSO cache | C; falha nova A, sucessos históricos A | `port/src/native_renderer/pipeline_cache.cpp:346–523`; `vulkan_seed_gap.json`; seeds 1056/308 | Cinco PSOs Vulkan novos nesta auditoria; catálogo runtime por descrição incompleto | P1 |
| M5 — menu gráfico | C na lista completa; núcleo B, testes/histórico A | `port/src/graphics/native_apt_menu.cpp:116–164`; `port/src/native_renderer/native_graphics_options.inc`; testes nativos/launcher | Falta qualidade independente de reflexos; FPS host não confirma todos os limitadores | P2 |
| Upload/readback | B; fixtures A | `port/src/native_renderer/native_renderer.cpp:1800–1920,2084–2150` e readbacks RT | UPLOAD mapeado não é download GPU; READBACK tem outros recursos/fences | P2 |
| Cache de texturas | B | `port/src/native_renderer/native_renderer.cpp:3775–3919` | Hash residual existe, mas há backoff/provas/reutilização | P1: medir |
| VirtualQuery | B | `port/src/native_renderer/pm4_mirror.cpp:63–77,274–306`; reader capturado/scratch | Ramo legado/live existe; não executa em todos os loads de constantes | P2 |
| Render targets | B | `port/src/native_renderer/native_renderer.cpp:2323–2425`; `port/src/graphics/d3d12/visible_ray_scene.cpp:165–334` | Guest surfaces cacheadas; temporários RT recriados por quadro | P1 no RT |
| Pipeline nativo | B; execução A nos dois backends | XDK hooks → WorkBatch → Execute → D3D12/Vulkan | Ainda interpreta PM4, formatos, estados e passes Xenos | P2 |
| Fidelity/shadows/windows | C/E | capturas atuais e shaders/patches | Sem comparação Xbox 360 alinhada para todos os materiais/efeitos | P2 |
| XMA/audio | B; políticas A; equivalência auditiva E | `port/src/xma_fixes.cpp:196–405`; SDK XMA/SDL; testes | EOS/tail/loop tratados; voo, diálogos e transições não homologados | P1: validar |
| FSR3 | D na integração pesquisada | CMake, opções e consumidores em `port/src/graphics` | MVs extensíveis e LSFG opcional não são integração FidelityFX FSR3 | P2 se requisito |

### Fluxo efetivamente ativo

1. PPC recompilado executa lógica do jogo e chamadas XDK. Os interceptadores estão em
   `port/src/native_renderer/native_hooks.cpp:630–699,876–890` e `port/src/frame_stats.cpp:48`. `TryNativeDrawSubmission:342`
   substitui parte da submissão; estados recusados seguem `__imp__...` original recompilado.
2. `Renderer::BeginCmd/EndCmd` (`port/src/native_renderer/native_renderer.cpp:9180–9277`) captura ring, constantes,
   streams, recursos e proveniência. WorkBatch congela dados; `WorkerMain:9542–9718`
   consome a fila. O frontend espera o lote anterior no swap (`10340–10360`).
3. PM4, fetches, declarações e hashes viram descrições e comandos host. D3D12 usa
   `GetPipeline:3282` e DXIL. Vulkan instala packet sink (`port/src/graphics/vulkan/platform/native_provider.cpp:428`),
   decodifica RenderPacket e usa `port/src/graphics/vulkan/game_pipeline.cpp`, `port/src/graphics/vulkan/game_renderer.cpp`, `port/src/graphics/vulkan/game_frame.cpp`.
4. Replay do VS original e histórico produzem clips/movimento. `FinishMotionScene:560`
   sela cena e depth. `port/src/native_renderer/native_renderer.cpp:8016–8023` chama RT e depois DLSS antes do HUD.
5. `RecordDlssFrame:8049` compõe inputs. O adapter fornece `pInMotionVectors`, depth e
   `pInBiasCurrentColorMask` a NGX (`port/src/graphics/d3d12/dlss_super_resolution.cpp:306–334`).
6. `SetupPresentation:195` força o presenter UI existente. No build examinado **SR_LSFG=OFF**:
   o D3D12 presenter ligado é `.tools/rexglue-object-motion-source/src/ui/d3d12/d3d12_presenter.cpp`,
   não a alternativa em `port/third_party/rexui-lsfg`.
7. Esse presenter aguarda antes do mailbox (`560–599`) e usa Present(1) com VSync
   (`1165–1169`). Vulkan tem provider próprio (`port/src/graphics/vulkan/platform/native_provider.cpp:264–304`) e
   `port/src/graphics/vulkan/frame_loop.cpp:95–188`. Pacing espera fora do GPU mutex; operações de queue ainda
   exigem sincronização, inclusive QueuePresent quando a queue é compartilhada.

CMakeCache confirmado: Release, SR_NATIVE=RENDERER, SR_DLSS=ON, SR_LSFG=OFF,
SR_VULKAN_GAME=ON, SDK/fontes GPU em `.tools/rexglue-object-motion-source`.
Outros backends/presenters continuam na árvore; presença não prova que estejam ligados.
O caminho final é nativo no host, mas não remove toda interpretação/semântica Xbox 360.

### Contratos de movimento, RT, cache e UI

**M1:** `TemporalGeometryHistory` (`port/src/graphics/temporal_geometry_history.h:24–110`) exige identidade/prova,
geração, malha/domínio, contagem/topologia, quadros consecutivos e tamanho compatível.
Não é associação por ordinal. `port/src/graphics/observed_instance_lifetimes.h:69–104` invalida destruição/reuso;
`port/src/graphics/observed_vehicle_batch.h` acompanha membros reais dos lotes. Skin/capa/rigid têm contratos
próprios. Clips anteriores preservam deformação do VS, não somente câmera.

O PS temporal calcula `(previousUV-currentUV)*renderSize`, remove jitter atual/anterior,
grava RG16F + validade R8_UINT e rejeita mistura/valores inválidos (`port/src/graphics/d3d12/shaders/temporal_motion.hlsl:54–78`).
Há resets por gap/resize/modo/conflito; cuts usam heurística projetada
(`port/src/graphics/object_motion_frame.h:102–117`; `port/src/native_renderer/native_motion_capture.inc:569–573`). Não foi localizado
evento universal de teleporte por ator; objeto novo/LOD não correspondente rejeita histórico.

`ObjectMotionInputsReady:1217–1241` admite cobertura parcial. O adapter marca
`motionIncludesCameraObjectsAndDeformation=false` e `motionPartialWithRejectionMask=true`
(`port/src/native_renderer/native_renderer.cpp:8162–8163`). `port/src/graphics/d3d12/shaders/dlss_inputs.hlsl` escolhe MV válido; outros pixels com
depth recebem zero + rejeição, não evidência falsa de cenário estático. BiasCurrentColorMask
não é reset duro por pixel nem garantia de ausência de ghosting. NGX usa MVLowRes, input
externo, escala1 em pixels internos e MVJittered desligado para vetores sem jitter. Não se soma
câmera novamente. Vulkan não tem producer GPU equivalente; os contratos comuns são portáveis.

**M2:** BLAS/TLAS são reais, com PREFER_FAST_BUILD e sem refit/ALLOW_UPDATE nesse caminho
(`port/src/graphics/d3d12/ray_tracing_scene.cpp:283–335`). Clips pós-VS são recuperados para mundo, incluindo
geometria deformada; AS usa spans opacos da câmera, agrupados em world/character.
Alpha variável não recebe avaliação material/any-hit completa. Luz vem da projeção solar.
Predication começa com fallback original habilitado; Trace+Composite bem-sucedidos suprimem
os mapas/receivers auditados (`port/src/native_renderer/native_visible_rt.inc:5–80,164–231`). Luz ambígua, receiver
não tratado ou submissão parcial conserva fallback; isso não elimina todos os passes do jogo.

Reflexos fazem TraceRayInline, mas colhem RGB de `lit_scene` após projeção/depth/viewport
(`port/src/graphics/d3d12/shaders/visible_ray_scene.hlsl:91–118`). Portanto há RT geométrico real com radiância híbrida,
não apenas SSR nem shading completo. Miss/hit sem cor validada preserva ambiente. Normal
vem do depth e F0=.04 é proxy. Denoiser é bilateral + temporal com MVs, reatividade,
clamp e reset; `HistorySettings` rejeita mudanças do estimador. Hardware sem DXR tem gates
para preservar raster (B), mas não houve GPU física sem DXR no teste. RT funciona também
com DLSS Off nos probes novos; o producer temporal pode ser solicitado por RT sozinho.

**M3:** há frontend, worker, scheduler guest, vblank e presenter UI; Vulkan tem estágio de
submissão próprio. Mailbox/fences/backpressure existem. O pacer usa prazos racionais e
histórico de oversleep, enquanto vblank guest é outra camada:30/60/120 selecionam60/120/240
vblanks/s; ilimitado sem VSync marca progresso e espera nominal1 ms.
`port/src/native_renderer/guest_render_scheduler.cpp:12–39` também ajusta período guest no construtor82166178,
com validação de classe/modo/valor. Isso preserva os campos de timestep, mas não certifica
toda física/áudio. Sleep(33) conhecido em824822C4 é condicional do caminho de vídeo, não
uma trava global comprovada. Houve título acima de30FPS nesta auditoria.
`port/src/graphics/presentation_cadence.h` classifica Hz/ratio; não detecta VRR real nem mede scanout.
O modo SR_PRESENTATION_LEGACY muda pacer/intervalo; não restaura toda a arquitetura antiga.

**M4:** SRSL/SRVK verificam originais/checksum/ABI. Tradução D3D12 fingerprinta emissor,
DXC/DLLs/header/contrato e patches RT/coverage (`port/src/native_renderer/shader_translator.cpp:125–174`). Vulkan
usa ferramentas/contrato/digest de shader; cache opaco verifica vendor/device/driverVersion/UUID
(`port/src/graphics/vulkan/game_pipeline.cpp:64–66,275–280`). CNPS é descrição portátil, não PSO opaco de driver.
PSOs conhecidos são reutilizados por chave/descrição; misses ainda podem compilar ou esperar
precompile (`port/src/native_renderer/pipeline_cache.cpp:346–523`). Zero observado não elimina o fallback.

**M5:** menu ativo usa assets Pause/Toggle originais EA APT, recipe e VFS local.
`SrNativeGraphicsAptCall:116–164` encaminha requests para SharedGraphicsOptions e exige pausa;
não confundir GraphicsOptionsDialog antigo com UI ativa. Backend aplica revisões, espera
fences antes de reconstruir recursos e publica efeito concluído. API exige reinício;
DLSS preserva intenção AA mas impõe compatibilidade efetiva. Vulkan mostra N/A onde falta suporte.
Launcher/jogo compartilham INI com merge por campo/lock/replace atômico
(`port/src/graphics/graphics_settings.cpp:81–173`; `LauncherSettings.cs:92–130,199–246`). Migração antiga só
preenche campos ausentes. Defaults RT/DLSS/FG são Off; suporte/prewarm não habilita efeitos.
Não houve navegação manual nova de menu com teclado/controle nesta auditoria. Evidência
manual anterior está referenciada no relatório preservado; o aceite novo do núcleo é
estático e por testes, não uma certificação de todas as transições de foco/input.

## C. Problemas encontrados

### AUD-01 — P1 — Cobertura temporal parcial, não aceite integral de M1

**Local:** `port/src/native_renderer/native_renderer.cpp:8159–8170`; `port/src/native_renderer/native_motion_capture.inc:1217–1241`.
**Causa confirmada:** máscara parcial e contratos de identidade para famílias observadas.
**Consequência:** feature real, mas não certifica rastros em todos os atores/LODs/teleportes.
O relatório anterior promovia isso a “fechado”. **Evidência:** flags do consumidor, masks,
fixture GPU e probes; screenshots isoladas não demonstram sequência temporal limpa.
**Reprodução:** voo alinhado Off/câmera/objetos, atores móveis identificados, troca de LOD e
cuts/teleportes; medir ownership, validade e sequência final. **Recomendação:** ampliar apenas
produtores provados e corrigir lacunas reproduzidas. **Risco:** alto ao relaxar identidade ou
tratar rejeição como cobertura universal.

### AUD-02 — P1 — Reflexos e iluminação com material proxy

**Local:** `port/src/graphics/d3d12/shaders/visible_ray_scene.hlsl:91–118,169–190`.
**Causa confirmada:** radiância projetada, normal de depth, F0 fixo, sem BRDF/roughness material
completos. **Consequência:** hit fora da imagem/depth validado não fornece cor; especular e
atenuação global não reproduzem todos os materiais/luzes. **Evidência:** queries são reais,
mas o shader amostra lit_scene e multiplica a cor inteira por `.35+.65*visibility`.
**Reprodução:** refletor junto à borda; objeto saindo da tela; vidro/concreto/personagem com
mesma orientação. **Recomendação:** atributos/G-buffer/material auditados e fallback explícito
antes de anunciar shading completo. **Risco:** alto em alpha, UVs e mudança de iluminação.

### AUD-03 — P1 — Alocação por quadro e rebuild completo no RT

**Local:** `port/src/graphics/d3d12/visible_ray_scene.cpp:165–169,191,255–288,319–334`;
`port/src/graphics/d3d12/ray_tracing_scene.cpp:283–335`.
**Causa confirmada no código:** BeginFrame limpa o slot; Build aloca world; Trace aloca cinco
texturas e buffers/heaps; Composite aloca outra textura; BLAS/TLAS e scratch são reconstruídos,
sem refit. **Consequência:** churn CPU/VRAM e trabalho GPU por quadro RT válido.
**Evidência:** chamadas incondicionais nesse fluxo; custo CPU individual de CreateCommittedResource
não isolado por PIX. É diferente de afirmar que o mesmo RT original é recriado duas vezes.
**Reprodução:** RT Off/On na mesma rota, contar alocações e separar CPU de BuildAS/Trace GPU.
**Recomendação:** pool por slot/formato/tamanho; reuse de scratch; refit só com topologia
compatível e medição de ganho. **Risco:** alto: histórico deve sobreviver ao fence do consumidor.

### AUD-04 — P1 — FPS ao vivo não atualiza todos os limitadores

**Local:** `port/src/native_renderer/guest_render_scheduler.cpp:16–39`; `port/src/native_renderer/guest_render_scheduler_policy.h:9–26`;
`port/src/native_renderer/native_graphics_system.cpp:482–497`; `port/src/native_renderer/native_graphics_options.inc`.
**Causa confirmada:** período guest é ajustado somente no construtor; vblank/pacer leem FPS
atualizado ao vivo. Não foi localizada reaplicação do período guest pelo menu.
**Consequência provável:** iniciar em 30 e selecionar 60/120/0 pode manter o limite guest inicial.
O presenter confirmar teto de 60 não prova que todos os limitadores mudaram. **Evidência B:**
writers/call sites e logs de construção; a troca ao vivo não foi reproduzida nesta auditoria.
**Reprodução:** cena leve com headroom, cold 30→hot 120/0 versus cold 120/0; medir fontes únicas.
**Recomendação:** indicar reinício nessa condição ou localizar setter guest seguro com
ABI/lifetime/locks e validação temporal. **Risco:** alto; não escrever object+72 concorrentemente
nem substituir constantes de simulação apenas para obter FPS.

### AUD-05 — P2 — Qualidade de reflexos acoplada às sombras

**Local:** `port/src/graphics/graphics_options.h:15–18`; `port/src/graphics/graphics_options.cpp:66–73`;
`port/src/native_renderer/native_visible_rt.inc:174–181`.
**Causa confirmada:** reflection_rays depende de ShadowSoftness, cujo request é recusado sem
sombras aplicadas. Não há linha independente de qualidade/distância/trace-scale do reflexo;
distância/escala existem como cvars. **Consequência:** sombras Off/reflexos On não oferecem o
controle independente solicitado. Não torna o toggle de reflexo fictício.
**Reprodução:** ligar apenas reflexos e tentar variar qualidade pelo menu.
**Recomendação:** separar enum/preferência/launcher/ponte APT/backend e verificar reset temporal.
**Risco:** médio de divergência entre persistência, UI e recurso efetivo.

### AUD-06 — P2 — PSOs runtime novos não são publicados no catálogo por descrição

**Local:** `port/src/native_renderer/native_renderer.cpp:3590–3600,3632–3675`; `port/src/native_renderer/pipeline_cache.cpp:491–523`.
**Causa confirmada:** criação síncrona entra em pipelines_[key]; Record persiste a descrição,
mas não publica o novo ComPtr em by_desc/ready. Outra chave equivalente pode criar outro PSO
nessa sessão. Prewarm/reuso de descrições já conhecidas existe e funciona.
**Evidência histórica:** `milestones_resume_seed_final_d3d12_r2`, hash accfa8: sete criações,
seis descrições novas; aliases de desc 7F0535F3F53969A2. Não é um novo miss observado no 994b.
**Reprodução:** duas chaves, mesma descrição ainda fora do seed, cache isolado; contar calls.
**Recomendação:** publicação canônica do PSO pronto com sincronização de aliases/precompile.
**Risco:** médio de corrida e mistura de variantes.

### AUD-07 — P2 — Depth temporal de objetos é aproximado

**Local:** `port/src/graphics/d3d12/shaders/visible_ray_scene.hlsl:140–151`.
**Causa confirmada:** prevê depth anterior projetando mundo atual na VP anterior; MV2D muda
UV, mas não fornece depth/posição anterior deformada. **Consequência provável:** movimento em
profundidade rejeita histórico legítimo, expondo ruído/flicker. Não é ghosting reproduzido nem
prova de que a rejeição conservadora esteja insegura. **Evidência B:** equação no shader.
**Reprodução:** superfície identificada movendo-se em Z, MV válido, cor controlada; comparar
aceitação com reset espacial. **Recomendação:** depth anterior/identidade de superfície
comprovados. **Risco:** alto se apenas aumentar threshold e aceitar disocclusions erradas.

### AUD-08 — P2 — Quarentena não é recuperação completa do Vulkan

**Local:** `port/src/native_renderer/packet_sink_policy.h:44–75`; `port/src/native_renderer/native_renderer.cpp:9722,9756–9768`;
`port/src/graphics/vulkan/platform/native_provider.cpp:428`; `port/src/graphics/vulkan/game_frame.cpp::Cancel`.
**Causa confirmada:** modo não estrito pode pular draw; falha de sink cancela/quarentena e
futuros Execute retornam. Não há recriação automática ou migração para D3D12 nesse ramo.
**Consequência provável:** draw ausente ou imagem parada. Há logs; não é exceção simplesmente
silenciada. Falha de sink não foi injetada no jogo nesta auditoria (B, não A).
**Reprodução:** fault injection isolado em decode/draw/swap/sink; observar imagem/input/erro.
**Recomendação:** estado de erro visível e encerramento/recriação segura quando possível.
**Risco:** alto ao continuar com estado GPU inválido ou remover sincronização/fatal cegamente.

### AUD-09 — P1 de investigação — Caminho crítico e trabalho CPU residual

**Local:** `port/src/native_renderer/native_renderer.cpp:3775–3919,9180–9277,9542–9718,10340–10360`;
`port/src/native_renderer/pm4_mirror.cpp:63–77,274–306`.
**Causa confirmada:** hashing quando prova falha/expira, captura/cópias e waits existem.
Há também backoff, versões, snapshots, intervalos publicados e reutilização; não é re-hash
integral a cada draw. Reader capturado evita VirtualQuery; fallback live pode consultá-lo.
**Consequência:** trabalho residual pode limitar throughput. O perfil novo separado (D)
mediu worker 19,29 ms / GPU 5,59 ms no caso Off e hash residual em dois lados da captura;
os 20/10 ms de relatos externos não foram usados como medição local. **Reprodução:** SR_NATIVE_CPU_PROFILE e SR_FRAME_TIMELINE,
alinhando fronteiras sem somar etapas sobrepostas. **Recomendação:** otimizar o componente
medido e suas causas de perda de prova. **Risco:** alto em cache só por endereço/remapeamentos.

### AUD-10 — P1 de validação — Áudio sem aceite prolongado

**Local:** `port/src/xma_fixes.cpp:196–405`; SDK `audio/xma_context.cpp:96–196,535–589`;
`audio/sdl/sdl_audio_driver.cpp:58–104,161–216`.
**Causa confirmada:** há fix de progresso, output_valid, carry final e proteção de loop. Flush
toma mutex do contexto; não se presume race apenas por acessar estado privado. Decoder usa
FFmpeg e host cria SDL_AudioStream. Engine guest exata permanece E; não se provou RenderWare.
**Consequência:** políticas testadas não certificam música após decolagem, diálogos, cutscenes,
loops e A/V. Log “released” não é sozinho deadlock reproduzido; nenhum novo deadlock de áudio
foi reproduzido aqui. **Reprodução:** PCM + EOS/loop/feeder/mixer/timestamps, voo e transições.
**Recomendação:** homologar antes de substituir output por XAudio2. Não se localizou integração
própria XAudio2 no caminho host ligado; SDL não prova qual API final foi selecionada.
**Risco:** alto em flush amplo: silêncio, duplicação ou truncamento de loops legítimos.

### AUD-11 — P2 — Cadência física e fidelidade não certificadas

**Local:** `port/src/graphics/presentation_cadence.h:46–86`; SDK presenter:560–599,1165–1169;
`port/src/graphics/vulkan/frame_loop.cpp:95–188`; capturas/matrizes.
**Causa confirmada:** classificação de Hz não mede VRR/scanout. GPU e display podem estar em
adaptadores distintos. QueuePresent serializa a queue; waits/acquire foram movidos para fora
do mutex, mas acesso à mesma queue continua exigindo exclusão.
**Consequência:** API FPS/repaint não é FPS de imagem nova ou cadência física a 60 Hz. Falta também
referência Xbox360 alinhada para janelas, sombras, partículas e transparência.
**Reprodução:** PresentMon/ETW/captura física em 60/120/144 Hz e referência visual alinhada.
**Recomendação:** medir entrega física antes de certificar; manter real/repeat/occluded separados.
**Risco:** alto ao remover mutex necessário da queue para “desbloquear” performance.

### AUD-12 — P1 — Seed Vulkan não cobre cinco descrições observadas nesta auditoria

**Local:** `port/src/graphics/vulkan/game_pipeline.cpp:283–426`; `port/assets/vulkan_pipeline_seed.bin`;
`logs/full_audit_20261106_01_vulkan_r1/game.log:626–722`.
**Causa confirmada em execução:** a sessão começou com 306 descrições locais, mesclou o seed 308
e encontrou mais cinco durante a janela de 30 s. Counters: runtime_attempts=5,
description_misses=5, shader_misses real=0, compiler_invocations=0. O campo agregado
shader_misses da rota inclui description misses e por isso mostra5; não são shaders desconhecidos.
**Consequência:** “zero criação runtime” não foi mantido nessa nova rota, mesmo com caches locais.
As duas rodadas seguintes ficaram em zero por reaproveitar o journal/driver cache aquecido.
O benchmark foi válido como medição, mas esse resultado reprova o aceite zero-runtime de M4.
**Evidência adicional:** `vulkan_seed_gap.json` verifica ABI/FNV64 e contém 308 registros no seed
versus 313 no journal; cinco novos, nenhum antigo perdido. O journal foi copiado para logs,
sem substituir o asset do projeto.
**Reprodução:** mesma configuração da rodada r1, começando com o snapshot de descrições anterior
à descoberta; preservar cache antes de qualquer ensaio frio. Não apagar o cache do usuário
nem considerar somente as rodadas aquecidas. **Recomendação:** conferir os cinco estados no
decoder de produção, unir somente observações válidas e repetir três rotas frias no novo hash.
**Risco:** baixo/médio em mudança apenas de seed, mas preservar ABI/shader fingerprints e
não esconder custo mudando os contadores. Nenhum seed foi ampliado nesta auditoria.

## D. Performance

Hardware: Core i7-10750H (6C/12T), 16 GB RAM, RTX 2060 (6144 MiB), driver NVIDIA
617.42. WMI informa painel Intel ativo em 1920×1080, 143 Hz (nominal 144); não certifica
60 Hz físicos. Dados completos em `logs/full_implementation_audit_20261106_01/hardware.json`.
Build Release, mesmo hash em todas as rodadas. SDK efetivo em `.tools/rexglue-object-motion-source`;
DLSS ON, LSFG OFF, Vulkan foundation/game ON. Cache local e driver não foram apagados.

### Baseline nova: seis configurações, três rodadas cada

Benchmark integrado `--sr_benchmark=true`, warmup 5 s + medição 30 s por execução;
1920×1080, escala 1, MSAA 1, FXAA off, FPS ilimitado, VSync off, headless. O benchmark
é o **flythrough da tela de título**, não gameplay controlado nem voo com atores
selecionados (`port/src/title_benchmark.h:2–3`; `port/src/title_benchmark.cpp:224–260`). A medição começa
após scene readiness/warmup. O nome da cache route rotula contadores; não seleciona
uma rota de gameplay. Não há alinhamento exato de câmera entre as execuções. Os scripts
chamam diretamente o mesmo executável usado por `run.cmd`. Não são medições de scanout.
Primeiras cinco configurações tiveram ordem invertida na segunda rodada; RT sem DLSS
foi medido depois, em três rodadas separadas. Nenhum build, teste GPU ou captura foi
executado durante essas janelas. Não houve controle laboratorial de temperatura,
potência ou todos os processos de fundo. Buscas leves de código ocorreram em paralelo.

Cada célula abaixo é a média aritmética das três estatísticas por rodada, sem concatenar
CSVs. Percentis nearest-rank; sigma populacional; lows = 1000 / média dos piores ceil(1%
ou 0,1%) tempos. Assim 1% low não é simplesmente 1000/P99. Stutter descritivo = frame
acima de 100 ms; não é exclusivamente compilação. Dados e caminhos em `performance.json`.

| Backend/configuração | FPS médio | 1% low | 0,1% low | P99 ms | Stutters/rodada |
|---|---:|---:|---:|---:|---:|
| D3D12, DLSS/RT/MV off | 31,15 | 9,51 | 6,49 | 97,44 | 6,67 |
| D3D12, DLSS Quality, câmera | 32,99 | 6,78 | 2,67 | 78,77 | 6,67 |
| D3D12, Quality + MV objeto | 21,61 | 5,33 | 2,17 | 138,35 | 12,33 |
| D3D12, Quality + MV + RT | 13,76 | 3,25 | 1,71 | 207,87 | 62,67 |
| Vulkan, DLSS/RT/MV off | 19,66 | 3,81 | 2,60 | 221,81 | 27,67 |
| D3D12, RT on / DLSS off | 17,42 | 5,70 | 2,72 | 111,12 | 8,33 |

| Configuração | Média ms | Sigma ms | P50 ms | P95 ms | P99,9 ms | FPS r1 / r2 / r3 |
|---|---:|---:|---:|---:|---:|---|
| Off | 34,95 | 23,45 | 31,97 | 56,44 | 338,37 | 19,79 / 37,61 / 36,04 |
| Quality câmera | 32,64 | 25,93 | 29,10 | 51,66 | 437,80 | 21,75 / 36,60 / 40,62 |
| Quality objetos | 47,20 | 30,11 | 42,74 | 75,91 | 543,24 | 19,76 / 19,00 / 26,06 |
| Quality objetos RT | 73,14 | 50,95 | 67,91 | 125,46 | 734,82 | 13,40 / 12,61 / 15,27 |
| Vulkan off | 51,39 | 38,09 | 41,38 | 99,32 | 460,04 | 17,65 / 18,90 / 22,43 |
| RT sem DLSS | 57,42 | 22,11 | 53,87 | 81,04 | 368,27 | 17,03 / 17,44 / 17,80 |

A variabilidade entre rodadas é grande. Não atribuir o delta inteiro ao algoritmo, nem
subtrair médias como tempo isolado de DLSS/MV/RT. Não há mudança de código antes/depois
nesta auditoria; são configurações da mesma build. O melhor confronto de backend é
Off contra Off: Vulkan não mostrou vantagem nesta rota. RT/DLSS combinados tiveram o
pior resultado observado, mas isso não identifica sozinho uma função como causa raiz.

Os 18 probes foram válidos. RT-only confirmou pixels de sombras/reflexos e fences sem
NGX; não depende de ativar DLSS para executar. Em 17 rodadas houve zero PSO runtime;
a primeira Vulkan teve cinco criações, zero falhas e zero compilação de shaders.
As seis rotas frias históricas de 60 s do mesmo hash foram rechecadas pelo auditor de
cache, não reexecutadas nesta etapa: `cold_evidence_recheck.json`. Elas passam para
seus percursos; não anulam o novo contraexemplo. As bibliotecas cobrem 241/241 shaders
do corpus conhecido, não 100% de todas as fases possíveis do jogo.

### Evidência visual e seus limites

Capturas já existentes do mesmo hash: `logs/milestones_resume_capture_final/capture/presented.png`
(D3D12, com buffers e inspeção de MV no diretório) e
`logs/milestones_resume_visual_vulkan/presented.jpg` (Vulkan, metadados no diretório).
`logs/milestones_resume_seed_v2/rendered_comparison.png` mostra frames de título
independentes, sem alinhamento temporal. Essas imagens comprovam saída renderizada;
não comprovam desaparecimento de rastros, materiais corretos ou equivalência Xbox 360.
Evidências anteriores de gameplay com Superman/NPC/veículo pertencem a outras builds
quando o hash difere e são apenas históricas. As medições baseline novas não tiveram
captura de tela, para evitar perturbar seus tempos. Não existe comparação visual nova
alinhada com/sem MV de todos os atores nesta auditoria; essa parte do aceite continua E.

**Capturas novas, fora das médias:** a primeira tentativa no swap 120 capturou tela
uniforme de inicialização. Off passou o probe; Quality+MV+RT teve processo exit 0 e
benchmark válido, mas o probe retornou 1 por faltar `*_dlss_upscaled.raw` nesse instante.
Logs preservados em `captures_console.log` e `full_audit_20261106_01_visual_mv_rt/result.json`.
Isso é falha do ensaio visual por captura prematura, não crash ou prova de falha NGX:
os logs posteriores da mesma sessão registram avaliação real. A falha não foi descartada.

Repetição no swap 480, limite 30, warmup 5 s + 30 s: os dois probes passaram, sem trocar
binário, fonte, seed ou preferências do usuário. Foram alterados os parâmetros de coleta
(instante, duração e limite explícito) para capturar após a inicialização; isso não é
repetição equivalente de desempenho nem correção do renderer. `captures_late.json` registra caminhos.
Imagens novas inspecionadas: `logs/full_audit_20261106_01_visual_late_off/capture/s000480_output_1280x720.png`
e `logs/full_audit_20261106_01_visual_late_mv_rt/capture/s000480_output_1280x720.png`;
o segundo diretório contém também o upscaled NGX e buffers RT/MV.
As câmeras diferem mesmo com o mesmo índice guest; o flythrough depende da progressão
temporal e as execuções não foram sincronizadas. A montagem abaixo é inventário visual,
sem estimar ganho de qualidade ou ausência de ghosting. Benchmarks com captura ficaram
fora das médias e perfis; a conferência de hashes ocorreu durante esse diagnóstico.

![Capturas independentes, sem alinhamento de câmera](C:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/logs/full_implementation_audit_20261106_01/visual_comparison.png)

### Pacing novo: 18 execuções, três por alvo/mecanismo

`logs/full_audit_20261106_01_pacing/matrix.json` registra as 18 execuções válidas,
5 s + 30 s, janela real 1280×720, efeitos extras off, mesmo hash. Ordem invertida na
segunda rodada. `SR_PRESENTATION_LEGACY=1` desativa deadlines e usa Present(0);
dedicado usa pacing e Present(1). Ambos mantêm a arquitetura atual, portanto esse
comparador não restaura toda a versão anterior do renderer. Painel continuou nominalmente
144 Hz. Médias de três, derivadas em `pacing_statistics.json` no diretório da auditoria.

| Alvo / mecanismo | Retornos API/s | Fontes únicas/s | Sigma intervalo API ms | P99 API ms | P99,9 API ms | 1% API low | 0,1% API low |
|---|---:|---:|---:|---:|---:|---:|---:|
| 30 legado | 173,46 | 29,49 | 2,394 | 8,594 | 9,538 | 110,65 | 100,34 |
| 30 dedicado | 30,00 | 28,32 | 0,285 | 34,037 | 34,549 | 29,23 | 28,95 |
| 60 legado | 180,39 | 36,42 | 2,530 | 8,865 | 9,948 | 106,63 | 91,66 |
| 60 dedicado | 60,00 | 47,61 | 0,234 | 17,365 | 17,711 | 57,07 | 55,98 |
| 120 legado | 181,26 | 37,27 | 2,556 | 8,744 | 9,825 | 108,28 | 94,67 |
| 120 dedicado | 120,00 | 45,53 | 0,301 | 9,115 | 10,048 | 105,95 | 96,21 |

Os valores altos de API no legado incluem repetições; não são FPS do jogo. P99 API não
deve ser minimizado sem considerar o intervalo alvo: 33,33 ms a 30 e 16,67 ms a 60.
O desvio dos retornos caiu nos três alvos, mas retorno CPU da API não mede scanout.

| Alvo / mecanismo | FPS jogo | Sigma frame ms | P99 frame ms | P99,9 frame ms | 1% low | 0,1% low |
|---|---:|---:|---:|---:|---:|---:|
| 30 legado | 29,47 | 4,119 | 47,173 | 76,301 | 18,34 | 13,11 |
| 30 dedicado | 30,00 | 2,941 | 41,030 | 63,667 | 21,92 | 16,04 |
| 60 legado | 36,39 | 8,248 | 51,115 | 68,686 | 16,77 | 13,16 |
| 60 dedicado | 48,68 | 5,008 | 33,707 | 41,757 | 26,98 | 20,86 |
| 120 legado | 37,25 | 6,825 | 43,871 | 54,241 | 20,70 | 18,00 |
| 120 dedicado | 45,51 | 6,758 | 37,138 | 47,959 | 23,73 | 18,98 |

O CSV do benchmark mostrou melhora de sigma/caudas/lows nessa comparação; a melhora de
sigma em 120 é pequena. A 60, por exemplo, sigma caiu de 8,248 para 5,008 ms e P99 de
51,115 para 33,707 ms. Ainda não houve 60 frames novos/s sustentados nesse teste.
Isso sustenta mecanismo funcional e melhora no cenário, sem certificar painel físico
60 Hz, todos os limitadores ao vivo ou equivalência da física. Stutters, máximos,
contagens de repetições e seriais estão nos CSVs e JSONs, não foram removidos.

### Perfis diagnósticos novos, fora das médias de três rodadas

Três execuções adicionais, uma por configuração, warmup 5 s + 30 s. Ativados somente
nesses ensaios: `SR_NATIVE_CPU_PROFILE=1`, `SR_FRAME_TIMELINE=<CSV absoluto>` e
`SR_FRAME_TIMELINE_DETAIL=1`. Sem screenshots e sem outro ensaio GPU concorrente.
`run_profiles.py`, `profiles.json` e `profile_*.csv` preservam análise e dados.
Selecionaram-se os últimos 20 s disponíveis de `game.end_ns`, e os mesmos IDs guest
nos outros estágios: 748/470/327 frames, respectivamente. Há margem de flush; essa
janela não é uma extração exata da janela do benchmark, nem replica a baseline sem overhead.

| D3D12 / configuração | Game wall ms | Frontend ms | Worker ms | GPU ticks ms | GPU gap ms | Front wait ms |
|---|---:|---:|---:|---:|---:|---:|
| Off | 26,59 | 7,06 | 19,29 | 5,59 | 21,15 | 0,06 |
| Quality + MV objeto | 35,83 | 10,90 | 39,21 | 24,92 | 17,80 | 6,64 |
| RT sem DLSS | 25,32 | 7,40 | 60,86 | 37,55 | 23,71 | 35,74 |

**Semântica indispensável:** `game` cobre o intervalo entre swaps e inclui frontend;
não isola lógica PPC. `worker` acumula wall de execução dos lotes, descontando só
esperas explicitamente marcadas, sem queue idle. Desagendamento e chamadas bloqueantes
não instrumentadas ainda podem aparecer como busy; não são ciclos CPU puros.
`front_wait` é wall de espera, apesar da coluna busy do CSV, e não deve ser tratado como
CPU útil. `frontend` e seus detalhes são aninhados. GPU é duração de timestamps,
sem calibração para o relógio CPU; `gpu_real` coincidiu com `gpu` nos frames selecionados.
O gap é intervalo entre timestamps de frames, não tempo de execução de um shader.
Essas colunas **não se somam** para obter frame time.

No caso Off há GPU gap considerável e worker wall maior que o trecho GPU: evidência
de trabalho CPU/submissão relevante, sem provar uma única causa universal. Com RT,
o frontend passa a esperar muito pelo worker. O profiling não identifica sozinho quanto
desse worker é alocação, driver ou desagendamento. GPU também cresce substancialmente.

Exemplo concreto, último bloco completo de 120 swaps do perfil Off (`game.log`,
23:19:06): execute 25,01 ms; prepare 15,23; constants 10,01; streams 3,39;
texture_lookup 5,30 e texture_hash 2,52 ms, **aninhados**, com 81 checks/~8748 KiB
hasheados por frame. Não são médias da tabela. O detalhe de frontend `fb_hash` teve
média 0,98 ms na janela Off; é outro escopo, não somar cegamente ao hash do worker.
Isso confirma hash residual mensurável, embora não aconteça a cada draw.
No último bloco RT (23:21:03), execute 61,95; prepare 22,67; targets 10,728;
constants 8,17 ms. Atribuir esses 10,728 ms somente às alocações RT seria inferência
sem PIX/stack sampling; o timer de targets abrange mais trabalho.

Não houve amostragem ETW/PIX nem correlação calibrada completa entre threads e GPU.
Assim foi localizado custo relevante, mas a maior função causal do caminho crítico
continua em aberto. Não foi aplicado ajuste para melhorar os números.

## E. Plano de correção, sem execução nesta auditoria

**P0:** nenhum defeito crítico foi reproduzido. Ausência de reprodução não garante ausência
de crash/device loss/corrupção. Não promover hipóteses a P0 sem trace ou repro.

**P1, na ordem de dependências:**

1. AUD-12: conferir as cinco descrições Vulkan novas, validar decoder/fingerprint,
   atualizar seed somente numa etapa de implementação autorizada e repetir três rotas
   frias preservando backups. Aceite zero-runtime exige todos os contadores, não só valid=true.
2. AUD-04: unificar a alteração ao vivo do limite host e período guest, ou explicitar
   necessidade de reinício. Testar 30→60→120→off e retorno sem alterar física/cutscenes.
3. AUD-09/AUD-03: medir estágios CPU/GPU; somente então reutilizar temporários RT e
   avaliar BLAS persistentes/refit quando topologia/proveniência permitirem. Não refitar
   malhas deformadas sem contrato de atualização nem liberar recursos antes do fence.
4. AUD-01: ampliar provas de correspondência por classe; validar desoclusão, skin/cloth,
   mudança de mesh, nascimento/destruição/teleporte e cortes de câmera. Manter rejeição
   conservadora em vez de declarar geometria ambígua como confiável.
5. AUD-02: definir contrato de material/radiância para RT e qualidade mínima aceitável;
   desacoplar cor refletida da projeção na imagem e validar fora da tela. Custo e memória
   devem ser medidos antes de elevar resolução/rays por padrão.
6. AUD-10: fechar reprodução auditiva e A/V antes de mexer no decoder/feeder novamente.
   Preservar loop, EOS, ownership e locking; um teste de política não valida uma música.

**P2:** AUD-05 qualidade independente de reflexos; AUD-06 publicação canônica do PSO
runtime sob sincronização; AUD-07 histórico de depth adequado ao objeto; AUD-08 recuperação
ou indicação clara da quarentena Vulkan; AUD-11 scanout físico/fidelidade. Alterações de
fila/queue e lifetimes têm risco maior que mudanças de UI/seed e exigem testes específicos.

**P3:** atualizar documentação de limitações/telemetria, separar contadores de shader e
description misses, guardar rotas/corpus com proveniência. Evitar novos toggles cujo estado
aplicado não possa ser consultado. Nenhuma opção extra deve ser ativada implicitamente.

### Quais otimizações externas se aplicam

Aplicam-se como alvos verificados: pooling dos temporários RT e revisão da reconstrução
integral de AS por quadro (AUD-03); publicação/reuso canônico de PSO que surgiu em runtime
(AUD-06); preencher somente lacunas observadas de seed (AUD-12). Ainda é necessário medir
benefício e regressões. Hash residual/VirtualQuery existem, mas suas frequências precisam
de perfil; já há write tracking, provas e backoff, portanto não cabe copiar uma otimização
como se cada draw relesse e rehasheasse toda a textura.

Não se aplicam como diagnóstico comprovado: “todo UPLOAD mapeado é readback”; “toda
surface original é recriada duas vezes por frame”; “Vulkan é sempre mais rápido”; “fila
nativa removeu toda tradução Xenos”; “todo XAudio* implica engine PC XAudio2”; “RenderWare
é o engine confirmado”; “desbloquear QueuePresent sem sincronização é seguro”; “worker
20 ms/GPU 10 ms” de outro projeto. Esses enunciados não descrevem evidência medida daqui.

## F. Validação executada e testes recomendados

Builds executados e aprovados nesta auditoria: Release do jogo, testes native e Vulkan.
Logs `release_build.log`, `native_build.log`, `vulkan_build.log` no diretório da auditoria.
A build Release informou geração de código com zero arquivos novos e target atualizado;
não foi rebuild limpo independente. Nenhuma falha de build foi ocultada.

| Verificação executada | Resultado | Evidência |
|---|---|---|
| sr_native_tests.exe | 730, zero falhas | native_tests.log |
| sr_vulkan_tests.exe | 120, zero falhas | vulkan_tests.log |
| temporal_motion_smoke.exe GPU | aprovado, sintético | motion_gpu.log |
| sr_visible_ray_scene_smoke.exe GPU | aprovado, BLAS/queries/temporal e mudança de estimador | rt_gpu.log |
| LauncherChecks | zero falhas | launcher_tests.log |
| Python shader tests | 17 aprovados | shader_tests.log |
| CTest native | 6/6 aprovados | ctest.log |
| Benchmarks baseline | 18/18 válidos, três por configuração | matrix.json, run_rt_only.json, performance.json |
| Pacing dedicado/legado | 18/18 válidos, três por alvo/mecanismo | full_audit_20261106_01_pacing/matrix.json |
| Perfis diagnósticos | 3/3 válidos; não usados nas médias baseline | profiles_console.log, profiles.json |
| Capturas | primeira tentativa MV/RT reprovada por dump prematuro; repetição tardia 2/2 passou | captures_console.log, captures_late_console.log, captures_late.json |
| Rechecagem cache/corpus histórico | passou nos percursos registrados | cold_evidence_recheck.json |
| REA no XEX | formato não suportado; sem decompilação | rea_audio.json |

A camada de debug D3D12 estava indisponível nos fixtures; não se certificam GPU validation,
ASan, TSan, fault injection abrangente, sessões longas ou áudio ouvido. O XEX foi enviado
somente à inspeção estática REA, que recusou o formato; nenhum provedor foi instalado e
nenhum binário foi modificado. Essa tentativa não identifica o engine guest.

Estabilidade estática: o manifesto contém 12 blocos midasm e 311 funções declaradas;
também há overrides REX_FUNC/REX_HOOK_RAW com endereços/ABI em fonte e designs existentes.
Nenhum hook foi adicionado. A ocorrência fatal na região de packet decode do diff local
é movimentação/condicionamento de um fatal que já existia no HEAD, não um novo fatal
introduzido pela auditoria. Política padrão não estrita cancela/quarentena o sink Vulkan;
não é recuperação automática de device loss. O fatal de device loss já existia também.
Não houve fault injection para demonstrar recuperação ou ausência de deadlock sob falha.

Comandos de reprodução (PowerShell, raiz do projeto; usar nomes de benchmark inéditos):

```powershell
cmake --build port/out/build/win-amd64-release --config Release
cmake --build build/tests-native --config Release
cmake --build build/tests-vulkan --config Release
& build/tests-native/sr_native_tests.exe
& build/tests-vulkan/sr_vulkan_tests.exe
& .tools/object-motion-gpu-tests/temporal_motion_smoke.exe
& build/tests-native/sr_visible_ray_scene_smoke.exe
& .tools/dotnet-launcher/dotnet.exe run --project tests/launcher/LauncherChecks.csproj
python -m unittest discover -s tools/shaders -p "test_*.py"
& 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe' --test-dir build/tests-native --output-on-failure
$env:SR_VULKAN_PROFILE='0'
python tools/bench/bench_dlss.py --name audit_repro_off_r1 --mode off --warmup 5 --measure 30
python tools/bench/bench_dlss.py --name audit_repro_mv_r1 --mode quality --object-motion --warmup 5 --measure 30
python tools/bench/bench_dlss.py --name audit_repro_rt_r1 --mode quality --object-motion --with-rt --warmup 5 --measure 30
python tools/bench/bench_dlss.py --name audit_repro_rt_only_r1 --mode off --with-rt --warmup 5 --measure 30
python tools/bench/bench_dlss.py --name audit_repro_vk_r1 --api vulkan --mode off --warmup 5 --measure 30
python tools/bench/bench_presentation.py --name audit_repro_pacing --compare-legacy
python tools/shaders/cache_audit.py --corpus artifacts/shaders/raw --srsl port/out/build/win-amd64-release/superman_returns_shaders.srsl --srvk port/out/build/win-amd64-release/superman_returns_vulkan.srvk --srvk-global port/out/build/win-amd64-release/superman_returns_vulkan_global.srvk --pso-cache port/assets/native_pipeline_seed.bin --routes logs/milestones_resume_seed_v2/routes.json --out logs/audit_repro_cache.json
$env:SR_NATIVE_CPU_PROFILE='1'
$env:SR_FRAME_TIMELINE_DETAIL='1'
$env:SR_FRAME_TIMELINE=Join-Path (Get-Location) 'logs/audit_repro_profile.csv'
python tools/bench/bench_dlss.py --name audit_repro_profile_off --mode off --warmup 5 --measure 30
Remove-Item Env:SR_NATIVE_CPU_PROFILE,Env:SR_FRAME_TIMELINE_DETAIL,Env:SR_FRAME_TIMELINE
```

Repetir cada configuração baseline três vezes, incluindo Quality sem --object-motion,
invertendo ordem na segunda rodada; não executar testes GPU/capturas durante os ensaios.
Scripts exatos e listas de comandos usados estão em `run_matrix.py`, `run_rt_only.py`
e nos `result.json`. Não sobrescrever evidências anteriores nem apagar caches sem backup.

| Correção/aceite | Demonstração necessária |
|---|---|
| AUD-01 | Voo alinhado com Superman, NPC e veículo; buffers RG16F/ownership/rejeição; câmera e objeto isolados/combinados; cortes e teleporte; comparar sequência temporal, não só frame parado |
| AUD-02/03/07 | Receptores/classe/alpha, oclusor fora da tela, objeto em Z, água/vidro; RTX com/sem; PIX timestamps/allocs/AS memory, três rodadas; validação de fence/lifetime |
| AUD-04 | Mudar FPS ao vivo 30/60/120/off; registrar host e campo guest; verificar velocidade, áudio e cutscene, teclado e controle |
| AUD-05 | Reflexos on e sombras off: alterar qualidade efetiva sem habilitar sombras, persistir/reabrir menu/launcher |
| AUD-06/12 | Cache frio com backup, três percursos de pelo menos 60 s/fases principais; aliases de mesmo descriptor; zero PSO runtime, zero unknown shaders, fingerprint/driver inválido reconstroem com segurança |
| AUD-08 | Packet inválido e erro de dispositivo controlados; confirmar cancelamento sem UAF/deadlock e feedback/recuperação; estrito e não estrito |
| AUD-09 | Perfil por estágio, amostras ETW de CPU, timestamps GPU calibrados; counts/hash bytes/VirtualQuery; isolar mudança e repetir três vezes |
| AUD-10 | Música contínua, diálogo, troca de nível, pause/resume, loop e EOS por 15–30 min; capturar PCM/underrun e offset A/V; comparar referência |
| AUD-11/M3 | PresentMon/ETW e painel físico 60 Hz, também 120/144/VRR; separar real/repeat/drop/occluded; sigma/P99/1%/0,1% e física |
| Defaults | INI ausente começa DLSS/RT/FG off; preferência opt-in explícita persiste; nunca sobrescrever escolha já salva |

## G. Conclusões explícitas

1. **Milestones completos?** Nenhum dos cinco pode receber aceite integral de todos os
   requisitos com esta evidência. Há partes funcionais e verificadas; pré-shaders cobrem
   o corpus conhecido e o núcleo do menu existe, mas escopo global/qualidade ainda têm lacunas.
2. **Parciais?** M1 por cobertura temporal; M2 por geometria/material/radiância e custo;
   M3 por certificação física e troca guest ao vivo; M4 pelo novo contraexemplo de cinco
   PSOs Vulkan; M5 por qualidade independente e aplicação efetiva de FPS.
3. **Ausentes?** Não se encontrou milestone inteiro puramente ausente. FSR3 não foi
   localizado como integração de SDK, nem RT/MV equivalentes no Vulkan atual. Essas
   ausências não anulam a arquitetura compartilhada ou o suporte D3D12 real.
4. **DLSS usa MV real por objeto?** Sim, nos produtores com correspondência provada,
   incluindo famílias de deformação suportadas. NGX recebe RG16F low-res com câmera e
   objeto compostos; cobertura parcial é acompanhada por máscara de rejeição. Não se
   demonstrou ausência de rastros em todos os atores, LODs e situações.
5. **RT funciona?** Sim: BLAS/TLAS, TraceRayInline, sombras/reflexos, composição e fences
   foram exercitados em GPU. É implementação híbrida e parcial, com limitações de
   materiais e cor fora da imagem; não é apenas código preparatório.
6. **Vulkan mais rápido?** Não nesta comparação equivalente: Off apresentou 19,66 FPS
   contra 31,15 no D3D12, com grande variação entre rodadas. Isso não prova inferioridade
   universal. Comparar Vulkan Off a D3D12 RT+DLSS seria inválido.
7. **Gargalo CPU no worker?** Há trabalho CPU relevante medido no perfil separado da
   seção D; no caso Off, worker excede o intervalo GPU instrumentado. Isso sustenta
   investigar CPU, mas não prova que uma única função é o caminho crítico de todo frame.
8. **Re-hash/RT redundante?** Hash residual existe e é mensurável, com tracking/backoff
   já implantados. Surfaces guest têm cache; não foi confirmado recriar o mesmo RT
   original duas vezes por quadro. Temporários RT e AS são recriados no fluxo RT válido.
9. **Pacing correto?** O mecanismo dedicado funciona e melhorou sigma/caudas nos CSVs
   medidos, com retornos próximos dos alvos. Não há aceite físico 60 Hz, 60 fontes novas/s
   sustentadas nessa rota ou confirmação de todas as trocas ao vivo. Estado parcial.
10. **Problema de áudio semelhante?** Há correções reais XMA/feeder/EOS/loop e saída host
    SDL/FFmpeg; não foi reproduzido novo deadlock nem realizado aceite auditivo prolongado.
    Não é possível certificar ou negar os sintomas de música/diálogo/A-V só com esses logs.
    Engine guest é indeterminada; REA recusou o XEX. Não se afirma RenderWare.
11. **Maior gargalo comprovado?** A configuração RT+Quality+MV teve o pior throughput
    observado (13,76 FPS) e caudas. No perfil Off há custo CPU significativo no worker e
    frontend, com componentes de constantes/lookup/hash. A maior causa individual de
    todas as configurações ainda não foi isolada por trace calibrado/experimento controlado.
12. **Primeira correção?** Para fechar um critério binário já reprovado, verificar as cinco
    descrições Vulkan do AUD-12 e certificar o seed em novas rotas frias. Para comportamento
    exposto ao usuário, reproduzir/corrigir o FPS ao vivo do AUD-04. Para desempenho,
    priorizar o componente medido no perfil, antes de mexer em sincronização ou lifetimes.

DLSS, RT e frame generation devem continuar **desativados por padrão**. A implementação
atual tem defaults Off; prewarm de PSO, capability detection ou presença de DLLs não
significam que os efeitos estejam ativos. Preferências explicitamente salvas pelo usuário
podem ativá-los e não devem ser sobrescritas pela auditoria.
