Objetivo: identificar as lacunas de motion relevantes para DLSS depois da capa, a partir da captura REAL mais recente, e propor somente contratos de identidade/dependência comprováveis.

Contexto: recomp C++ D3D12 Superman Returns. Capa A523/71CB já tem slots persistentes source-qualified, corpo C687 e C209 skinned têm tokens/partes, e shaders estáticos auditados têm world-inputproof. DLSS permanece Off. RaizR=C:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp.

Leia R/logs/bench_motion_cloth_correspondence_2610d.log (registros native motion family: phase=2); R/logs/motion_cloth_correspondence_2610d/motion_geometry_s1449.csv e inspection.json; R/port/src/graphics/static_world_shader_dependencies.h; R/port/src/graphics/temporal_geometry_history.h; R/port/src/native_renderer/native_motion_capture.inc. Abra APENAS os3–4VS mais relevantes em R/artifacts/shaders/hlsl para classificar suas dependências de posição. Priorize famílias com draws/geometria elegível sem match; separe transparência, sky e semdepthwrite de geomopaca dinâmica.

Faça: liste famílias ainda rejeitadas e motivo exato; para até3candidatos estáticos, dê rows/booleans/loops/fetches que determinam world e clip, separando câmera vs posição local. Para dinâmicos, diga que lifecycle/vertexcorrespondence falta, sem promover hash/draworder em identidade. Métricas de draws NÃO são métricas de pixels; você não executará replay nem inventará cobertura porfamília.

Não faça: editar/criar/apagar arquivos, executar comandos/scripts/builds/jogo, rede ou delegação. Somente leitura de arquivos.

Saída: resumo até5linhas; tabela prioridade/VS/draws/matches/motivo/sourceproofpossível; âncoras de shader por candidato; desconhecidos. Pronto quando tivermos os próximos contratos precisos a implementar, não apenas um inventário geral.
