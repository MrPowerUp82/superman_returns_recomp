Objetivo: implementar mapeamento EXATO de dimensões guest->host por eixo para cenas DLSS, sem usar escala escalar/8.8 aproximada, e testes C++20 autocontidos.

Diretório de trabalho ISOLADO: C:/Users/Gusta/.codex/worktrees/gemini-scene-mapping/superman_returns_recomp. Edite SOMENTE aqui. Não edite o checkout principal C:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp.

Arquivos permitidos: criar port/src/graphics/scene_resolution_mapping.h e tests/native/test_scene_resolution_mapping.cpp. Pode criar scratch/test binaries/logs somente em .gemini-tasks/ dentro do worktree. Não altere CMakeLists, renderer ou quaisquer outros arquivos; o orquestrador fará integração. Sem commits/push.

Contexto: o renderer D3D12 usa scala float para alocação/viewport/scissor/cópias. NGX Quality1280x720 consulta853x480, logo proporções X/Y diferem. Precisamos matemática verificável em helper puro sem Windows ou SDK, sem declarar semântica/lifecycle/readiness.

Interfaces EXATAS em namespace superman_returns::graphics:
struct ScenePixelExtent {uint32_t width=0,height=0; bool operator==(const ScenePixelExtent&)const=default;};
struct SceneSurfaceMapping {ScenePixelExtent guest{},host{}; bool operator==(const SceneSurfaceMapping&)const=default;};
struct ScenePixelRect {int32_t left=0,top=0,right=0,bottom=0; bool operator==(const ScenePixelRect&)const=default;};
struct ScenePixelViewport {float x=0,y=0,width=0,height=0,min_depth=0,max_depth=1;};
enum class SceneEdgeRounding {Nearest,Floor,Ceil};
bool ValidSceneSurfaceMapping(const SceneSurfaceMapping&);
std::optional<int32_t> MapScenePixelEdge(int32_t edge,uint32_t guest_extent,uint32_t host_extent,SceneEdgeRounding rounding=SceneEdgeRounding::Nearest);
std::optional<ScenePixelRect> MapScenePixelRect(const SceneSurfaceMapping&,ScenePixelRect,SceneEdgeRounding rounding=SceneEdgeRounding::Nearest);
std::optional<ScenePixelViewport> MapScenePixelViewport(const SceneSurfaceMapping&,ScenePixelViewport);
bool SameScenePixelScale(const SceneSurfaceMapping&,const SceneSurfaceMapping&); // exact rational X/Y equality, no epsilon
struct ScenePixelCopy {ScenePixelRect source;int32_t dest_x=0,dest_y=0;};
std::optional<ScenePixelCopy> PlanScenePixelCopy(const SceneSurfaceMapping& source,const SceneSurfaceMapping& destination,ScenePixelRect guest_source,int32_t guest_dest_x,int32_t guest_dest_y);

Requisitos: bounds de extents1..16384 por eixo; sem clamp silencioso. MapEdge usa aritmética int64 exata, nearest com empate afastando de zero (compatível lround) e floor/ceil assinados corretos. Edges assinadas podem ficar fora do target; rejeite overflowint32. MapRect rejeita inversão e regiões vazias; não recorta pois consumidores farão clipping. Viewport exige floats finitos e width/height>0 e depth0<=min<=max<=1; calcula cada eixo usando host/guest e retorna tamanho COMPLETO exato para viewport0..guest. SameScale compara produtos inteiros por eixo; sem semântica dedomínio/recurso. PlanCopy rejeita src/destfora doslimites, escalas diferentes, retângulos vazios/invertidos e tamanhos físicos diferentes depois de arredondamento; NÃO inventa upscale. Retorna sourcehostrect e desthostoffset. Resolver subrects pode produzir footprintdiferente mesmo com escalasiguais: rejeite ao invés de corromper.

TDD obrigatório: escreva testes primeiro com test_main.h; observe RED; implemente; GREEN. Casos meaningful:1280x720->853x480 viewport/edges/rect; signednearest/floor/ceil;1079x607 irregular; iguais vs ratios diferentes;copysubrect arredondamento;overflow INT32_MAX/INT32_MIN;NaN/Inf/zero/reversed viewport;empty/outofbounds;copy refusing853x480->1280x720. Não adicione dependências.

Verificação: compile tests/native/main.cpp + novo test.cpp com compilador principal C:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/.tools/clang+llvm-23.1.2-x86_64-pc-windows-msvc/bin/clang++.exe -std=c++20, inicializando VC via C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/Tools/VsDevCmd.bat -arch=x64 -host_arch=x64 se necessário. Binaries somente scratch .gemini-tasks/. Rode o teste e corrija até passar. Se o ambiente impedir comando, ainda implemente e relate exatamente a limitação, sem inventar sucesso.

Resposta final: arquivos alterados, testeRED eGREEN/código de saída, invariantes e lacunas. Não escreva plano só; EDITE OS DOIS ARQUIVOS no worktree.
