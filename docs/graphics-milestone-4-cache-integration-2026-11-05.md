# Integração e evidências M4 — 05/11/2026

Estado: implementação local; nenhuma aceitação de gameplay/GPU é alegada aqui.
O agente principal coordena build, testes e medições. Este agente não executou
build, testes, benchmark, jogo ou GPU.

## Pontos exatos de integração no renderer (arquivo compartilhado)

`native_renderer.cpp` já inclui shader_translator.h/pipeline_cache.h. Próximo de
PackShaderLookup, acrescente a declaração antecipada e implementação abaixo.
Os bytes continuam em memória com vida de processo; o callback roda somente em
workers do cache e pode aguardar o prewarm.

```cpp
bool PrewarmedVariantLookup(uint64_t hash, uint32_t variant,
                           const void** data, size_t* size) {
  static std::mutex mutex;
  static std::unordered_map<uint64_t, std::vector<uint8_t>> rt, coverage;
  if (variant != 1 && variant != 2) return false;
  std::lock_guard<std::mutex> lock(mutex);
  auto& bytes = (variant == 1 ? rt : coverage)[hash];
  if (bytes.empty() &&
      !ShaderTranslator::Get().LoadPrewarmedVariant(hash, variant, bytes)) return false;
  *data = bytes.data();
  *size = bytes.size();
  return true;
}
```

Na inicialização, ANTES de `pipeline_cache_.Start`, use os flags ativos das
features; `true,true` é permitido para preparar todas as especializações
suportadas do corpus. Isso pode compilar no carregamento e deve preceder o
início do percurso instrumentado.

```cpp
if (const auto* library = PreShaderLibrary()) {
  const auto result = ShaderTranslator::Get().PrewarmVariants(*library, true, true);
  // O método já registra offered/ready/failed/pending; não ocultar failures.
}
pipeline_cache_.Start(device_, root_signature_.Get(), &PackShaderLookup,
    &UsageSemantic, msaa, foliage_a2c, &PrewarmedVariantLookup);
```

Em `fill_record` (GetPipeline) acrescente:

```cpp
record.pixel_variant = coverage_variant ? 2u : ray_variant ? 1u : 0u;
```

Em GetPipeline, remova `!special_variant &&` SOMENTE destas três condições:
`pipeline_cache_.Take(key, precompiled)`, `elements.size() <=
PsoRecord::kMaxElements` no bloco de TakeByDesc, e a condição final de Record.
Mantenha o gate `!special_variant` de CompileAsync: não pule draws das novas
variantes. Record ainda recebe somente descrição completamente preenchida;
record.pixel_variant evita reutilizar DXIL original em um PSO especializado.

Para contar todos os compiles de PSO em runtime, acrescente:

```cpp
PipelineCache::RecordRuntimeCreate();
```

imediatamente antes de CADA `device_->CreateGraphicsPipelineState` e
`device_->CreateComputePipelineState` nos caminhos do renderer, passes DLSS,
movimento e outros utilitários. NÃO contar a chamada Worker do pipeline_cache:
ela é precompile. Incluir tentativas que falham. Uma chamada que permanecer
sem instrumentar invalida a afirmação zero runtime creates.

## Delimitadores reais de rota

O root/harness deve iniciar somente depois de prewarm e aquecimento. A própria
thread do renderer registra snapshots monotônicos antes/depois do percurso:

```cpp
const auto shaders_begin = ShaderTranslator::AuditSnapshot();
const auto pso_begin = PipelineCache::RuntimeCreates();
// Capturar aqui draws_begin e frame_begin de contadores cumulativos reais.
REXLOG_INFO("native cache audit: route_begin name=title_quality");
// ... executar a rota e coletar captura de verdade ...
const auto shaders_end = ShaderTranslator::AuditSnapshot();
REXLOG_INFO("native cache audit: route_end name=title_quality frames={} draws={} "
            "pso_runtime_creates={} shader_runtime_compiles={} shader_misses={}",
    frames_end - frames_begin, draws_end - draws_begin,
    PipelineCache::RuntimeCreates() - pso_begin,
    shaders_end.compile_started - shaders_begin.compile_started,
    shaders_end.fetch_not_ready - shaders_begin.fetch_not_ready);
```

frames/draws não podem ser constantes nem estimados pelo log. Stats::draws
precisa ser convertido em contador cumulativo caso seja zerado a cada relatório.
Cópias de stats ao iniciar/encerrar não bastam se houver reset no meio da rota.
Counters contam todas as instâncias base/RT/coverage do ShaderTranslator. Fetch
pending/failed/unknown fica visível em shader_misses; hit de disco especializado
é contado separadamente de compile_started. O antigo texto `N translated`
incluía hits de disco: não constitui prova de novo compile DXC no baseline.

Manifesto JSON schema 1, paths relativos ao próprio manifesto:

```json
{
  "schema": 1,
  "routes": [{
    "name": "title_quality", "api": "d3d12", "profile": "d3d12",
    "log": "run/game.log", "complete": true,
    "executable": "../../port/out/build/win-amd64-release/superman_returns.exe",
    "executable_sha256": "SHA256_REAL_DO_EXE_MEDIDO",
    "library_sha256": "SHA256_REAL_DA_BIBLIOTECA_USADA",
    "settings": {"dlss": "quality", "render": "1280x720", "output": "1920x1080", "msaa": 1},
    "captures": ["run/capture/presented.png"]
  }]
}
```

Cada nome exige exatamente um begin e end ordenados. API/profile/hash/settings,
EXE existente e captura não vazia são necessários. Bibliotecas e logs entram por
hash SHA256. Zero resumo contradito por qualquer compile/miss logado reprova.
Eventos anteriores ao begin são preservados no relatório como session_events.
O escopo aprovado é apenas a lista explícita de rotas; não todo o jogo.

## Comandos para o root

```powershell
python -m unittest discover -s tools/shaders -p 'test_*.py'
python tools/shaders/cache_audit.py --corpus artifacts/shaders/raw --srsl artifacts/shaders/superman_returns_shaders.srsl --srvk artifacts/shaders/superman_returns_vulkan.srvk --srvk-global artifacts/shaders/superman_returns_vulkan_global.srvk --pso-cache port/out/build/win-amd64-release/superman_returns_pipelines.bin --out artifacts/shaders/cache-audit-m4.json
python tools/shaders/pack_pipeline_seed.py port/out/build/win-amd64-release/superman_returns_pipelines.bin artifacts/shaders/pipeline-seed-m4.bin --merge port/assets/native_pipeline_seed.bin --merge artifacts/warworld_diagnostic/superman_returns_pipelines.bin --upgrade-v2
```

O auditor retorna exit 1 sem rotas instrumentadas mesmo se libraries.*.complete
for true. Acrescente `--routes <manifesto_real.json>` após coletar os percursos.
O correto override CMake existente é
`-DSR_NATIVE_PIPELINE_SEED=<absoluto>/artifacts/shaders/pipeline-seed-m4.bin`,
e não `SR_PIPELINE_SEED`. O seed default de 376 registros é local em
port/assets/native_pipeline_seed.bin; o seed expandido também deve ficar fora
do Git. O packer não gera bytecode e preserva SHA256/proveniência de cada fonte.

## Compatibilidade e riscos

CNPS v2 acrescenta pixel_variant/pipeline_kind (8 bytes), 704 bytes por PsoRecord;
v1 usa 696. Loader v2 aceita v1 embedded/local, preenche variante base e migra o
arquivo local por .v2.pending + MoveFileEx com backup .v1.bak. Falha desabilita
escritas sem apagar o cache anterior. Executável antigo rejeita v2; mantenha o
backup para rollback. Não há mudança de bytes em bibliotecas SRSL/SRVK.

A fingerprint de tradução v2 usa conteúdo dos quatro binários de ferramenta,
header e contrato da linha de comando. A anterior usava majoritariamente os
tamanhos dos binários e podia reutilizar DXIL após troca de mesmo tamanho.
Isso cria novo domínio local e exige prewarm; caches antigos ficam preservados.

Objetos PSO prontos permanecem vivos para aliases; há aumento esperado de memória
para evitar recompilar o mesmo objeto. Take espera registros ainda em fila;
uma primeira inicialização pode aguardar a compilação no carregamento. Falhas
terminais despertam waiters e são logadas. Chaves com descrições ambíguas usam
TakeByDesc em vez de escolher uma descrição arbitrária. Synchronous fallback
continua disponível quando corpus/seed é realmente incompleto.

Não foram escritos bytes de shaders proprietários no Git nem alteradas fontes
SDK. Faltam validação nativa da ABI/migração e concorrência, rotas reais depois
das mudanças, integração dos contadores e captura visual. Seed base não contém
PSOs RT/coverage observados porque o renderer anterior não os persistia. Uma
primeira execução v2 deve capturá-los e a seguinte demonstrar precompile/hits.

## Evidências locais desta implementação

A união realmente executada acrescentou também os caches reference/variant de
benchmark-cpu-stutter às três fontes do comando abreviado acima. Resultado em
artifacts/shaders/pipeline-seed-m4.bin.json: 7623 registros recebidos, 813
descrições raw únicas, v2/704 bytes, 572368 bytes de saída, SHA256
`da51a380c809d7630bb5d5649d5f502f08b8c909211f401a5a8a2a85d72a5748`.
O cache release tinha 6259 registros neste momento. A união amplia o seed
embedded de 376 para 813, mas não demonstra novas descrições além das 813 já
presentes no cache local. Há 0 registros RT/coverage até a captura v2.

O auditor foi executado sobre os BYTES locais das três bibliotecas, não somente
os reports antigos: SRSL e SRVK portable/global contêm exatamente 241/241
originais do raw/, com bounds/checksum/stage/identidade consistentes; Vulkan
passou também pela reflexão independente existente. Nenhuma dependência de
shader original faltou nas 813 descrições do seed. O resultado em
artifacts/shaders/cache-audit-m4.json contém hashes individuais e saiu com
exit 1/acceptance=false, pois não existem rotas novas instrumentadas no report.
Isso mede 100% deste corpus fechado, sem inferir cobertura de gameplay.

O SDK canônico foi inspecionado em
.tools/rexglue-object-motion-source/src/graphics/d3d12/pipeline_cache.cpp:
InitializeShaderStorage cria `.xsh` e `.rtv.d3d12.xpso` sob shaders/shareable,
com seus próprios cabeçalhos/versões. Exemplares locais em artifacts/coop-test
medem 163664 e 10092 bytes respectivamente. Esses arquivos pertencem ao renderer
Xenos do SDK. O log baseline declara sr_renderer=native/no Xenos emulation;
aquecê-los não precompila os CNPS do renderer nativo. Nenhum desses arquivos nem
fontes do SDK foi alterado.

Vulkan nativo mantém ainda um cache de driver próprio em game_pipeline.cpp,
validado por vendor/device/pipelineCacheUUID e ABI. SRVK completo fornece
SPIR-V/reflexão; não demonstra que vkCreateGraphicsPipelines deixou de ocorrer
no draw. A instrumentação de rota Vulkan deve contar o serviço de shaders e
GamePipeline separadamente; os counters D3D12 descritos acima não certificam
uma rota Vulkan. O auditor aceita ambos os profiles somente com evidência de
rota emitida pelo backend realmente executado.

## Extensão utilitária implementada

O contador puro em graphics/pipeline_creation_audit.h é compartilhado entre o
renderer e bibliotecas D3D12; PipelineCache::RecordRuntimeCreate/RuntimeCreates
redirecionam a ele. TemporalMotion e as três chamadas compute de RayTracingScene
já incrementam o contador antes da tentativa. Não os incremente uma segunda vez
no chamador. GUI/SDK e NGX internos ainda exigem consideração separada de escopo;
NGX opaco não é certificado por contadores de chamadas pertencentes ao port.

TemporalMotion::Initialize agora aceita `const std::filesystem::path&` opcional.
Passe na instância principal, no native_motion_capture.inc:

```cpp
motion->Initialize(device_, rex::filesystem::GetExecutableFolder() /
    "superman_returns_motion_raster.bin")
```

Initialize cria os 12 estados zero-bias (NONE/FRONT/BACK × winding × D32/D24),
mais as combinações finitas já observadas no journal. Novos estados continuam
contados como runtime creates; seus seis campos numéricos são publicados em
journal separado, versionado, por worker assíncrono. Não há I/O síncrono no draw.
O formato é SRTMPSO, versão 1, count <=128, seis u32 por registro; bits dos
floats são preservados e NaN/Inf/combinações inválidas recusados. Um arquivo
inválido fica preservado. O journal não contém shaders nem ponteiros. A primeira
rota que descobrir um bias novo permanece miss; a seguinte deve demonstrar hit.
A escrita é assíncrona: o processo morto antes de publicar o último lote pode
perdê-lo e a rodada seguinte registra a falta real.

A extensão final CNPSv2 usa o último u32 como `pipeline_kind` (0 raster guest,
1 post-VS clip streamout). O loader v1 preenche ambos os campos novos com zero.
kind1 exige PS=0/variante=0 e reconstrói GS/SO de PostVertexClipCapture próprio.
O callback de shaders original fornece apenas o VS; nenhum GS proprietário é
serializado. A seguinte integração no bloco novo do inc substitui a criação
incondicional de streamout pelo lookup de descrição + fallback contado:

```cpp
PsoRecord observed{};
observed.key=pipeline_key;observed.vs_hash=current_vs_hash_;observed.pipeline_kind=1;
observed.blend=desc.BlendState;observed.raster=desc.RasterizerState;
observed.depth_stencil=desc.DepthStencilState;
for(uint32_t i=0;i<8;++i)observed.rtv_formats[i]=desc.RTVFormats[i];
observed.dsv_format=desc.DSVFormat;observed.num_render_targets=desc.NumRenderTargets;
observed.sample_count=desc.SampleDesc.Count;
observed.topology_type=uint32_t(desc.PrimitiveTopologyType);
observed.strip_cut=uint32_t(desc.IBStripCutValue);
bool serializable=elements.size()<=PsoRecord::kMaxElements;
if(serializable) {
  observed.element_count=uint32_t(elements.size());
  for(size_t i=0;i<elements.size();++i) {
    const auto& e=elements[i];uint32_t usage=0;
    while(usage<14 && std::strcmp(e.SemanticName,UsageSemantic(usage)))++usage;
    if(usage==14) {serializable=false;break;}
    observed.elements[i]={uint8_t(usage),uint8_t(e.SemanticIndex),uint8_t(e.InputSlot),0,
                          uint32_t(e.Format),e.AlignedByteOffset};
  }
}
ComPtr<ID3D12PipelineState> pipeline;
HRESULT hr=S_OK;
if(!serializable || !pipeline_cache_.TakeByDesc(observed,pipeline)) {
  PipelineCache::RecordRuntimeCreate();
  hr=device_->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&pipeline));
  if(SUCCEEDED(hr) && serializable)pipeline_cache_.Record(observed);
}
// Conservar o log FAILED e emplace originais do inc.
```

Não invente record quando não for serializable. O fallback continua contado.
GetPipeline normal preenche kind=0 por padrão, então descrições nunca se misturam.
`pipeline_cache_.WaitForStartup()` oferece a barreira explícita antes de iniciar
um percurso que exige precompile finalizado. Retorno false indica timeout/falha,
não autoriza declarar hit.

## Benchmark instrumentado sem lifetime de Renderer

title_benchmark.{h,cpp} agora emite begin/end automaticamente na transição
Measuring e Finish, antes do ExitNow. Nome padrão title, customizável por
`--sr_benchmark_cache_route=title_quality`. O provider raiz deve ser registrado
antes da medição:

```cpp
#include "../graphics/pipeline_creation_audit.h"
benchmark::CacheAuditCounts NativeCacheAuditSnapshot() {
  const auto shader=ShaderTranslator::AuditSnapshot();
  return {graphics::g_cache_audit_frames.load(),graphics::g_cache_audit_draws.load(),
      graphics::RuntimePipelineCreates(),shader.compile_started,shader.fetch_not_ready};
}
// Inicialização native D3D12:
benchmark::SetCacheAuditProvider(&NativeCacheAuditSnapshot);
// Exatamente um incremento por frame/draw real:
graphics::RecordCacheAuditFrame(); // EndFrame
graphics::RecordCacheAuditDraw();  // CompleteGuestDraw após submissão original
```

O provider lê somente atomics de vida do processo, sem ponteiro para Renderer.
Sem provider, begin/end não são emitidos. Counters decrescentes geram route_invalid.
Interrupções de cena geram valid=0; o auditor as reprova mesmo com counters zero.
Vulkan precisa de seu próprio provider ligado aos respectivos serviços e eventos.

Verificação CPU liberada na coordenação e executada: 16 testes Python passaram
em 0,003s, incluindo promoção/merge v1/v2, leitura de bibliotecas, corrupção,
miss ocultado por resumo zero, fronteiras ausentes e interrupção de rota.
Os dois testes nativos em test_motion_pipeline_records.cpp aguardam integração
CMake/build pelo agente principal; este agente não os declarou aprovados.

## Fechamento posterior de 06/11

O recurso D3D12 final contém 1024 descrições CNPS v2 (720912 bytes), SHA256 d9071ba0a26d60d9de54e8649e4dae2646f3e2fc664ccdd30b75f12f8e42953d. O seed numérico de raster temporal contém quatro estados observados. No executável 082c3f4b..., três rodadas frias de 60 s, sem CNPS ou journal temporal local, terminaram com zero PSOs/compilações/misses. Os dois arquivos originais foram restaurados por hash. Tentativas anteriores com misses permanecem documentadas; ver graphics-m134-final-validation-2026-11-06.md.
