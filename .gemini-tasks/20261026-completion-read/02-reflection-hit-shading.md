Objetivo: decompor a implementação concreta de um callable hit evaluator para PS8A6DD1D52752FAD6 que reutilize o shading original, sem devolver só albedo ou duplicar ambiente.

Contexto: recomp C++ D3D12 Superman Returns; RTreflections pendente. O orquestrador integra o renderer/resolução em paralelo. Raiz R=C:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp.

Leia R/docs/rt-reflection-input-contract-2026-10-25.md; R/artifacts/shaders/hlsl/8A6DD1D52752FAD6.ps.hlsl e2C45BFFE76D77398.vs.hlsl; R/port/src/graphics/rt_shadow_shader.cpp; R/port/src/graphics/d3d12/post_vertex_capture.h/.cpp; R/port/src/native_renderer/shader_translator.cpp; R/port/src/native_renderer/native_renderer.cpp UploadConstants,texturebindings,post-VScapture. Descubra nomes reais com leitura de diretório se algum arquivo divergir.

Faça: identifique macros/declaracões exatas que precisam consumir PS/shared snapshots por material, sampler/texture descriptors bindless e originalPS callable main. Liste transformações mínimas para executar o PS original com atributos interpolados do hit, view voltada à origem do raio secundário e LOD explícito por cone/footprint, preservando light/AO/emissivos/gamma e fallback cubemap no hit sem recursãoRT. Analise as funções Sample/derivatives que impedem usá-lo dentro de pixel inlinequery e uma política concreta de footprint. Forneça interfaces/payload layouts mínimos e pontos de inserção exatos, não uma reescrita integral. Confirme nearest unsupported blocker/confidence0 e substituição do único sampleenv do receiver.

Não faça: editar/criar/apagar arquivos, comandos, builds, jogo, internet ou delegação. Somente leitura. Não infira roughness/PBR ou actorclass por hash. Se não existir prova numérica/LOD, marque a lacuna.

Saída: resumo até5linhas; plano implementável de3–6partes; arquivo/âncora por afirmação; contratos de buffers/constants; lacunas verificáveis. Pronto quando o orquestrador tiver mapa preciso das alterações de shader/resources, sem falsa declaração de efeito pronto.
