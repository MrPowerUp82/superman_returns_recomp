# Máscara de texturas usadas — 29/10/2026

Implementar a primeira candidata recomendada em `docs/xenia-canary-opportunities-2026-10-29.md`, autorizada pelo usuário com "pode começar". O objetivo é evitar captura, lookup e preparação de texturas que o par de shaders não pode consultar.

## Abordagem

Analisar conservadoramente o microcódigo do container original uma vez ao registrar o shader. Publicar `{mask, known}` no snapshot imutável `ShaderCapture`. Usar a união VS/PS por draw. Essa rota cobre bibliotecas antigas e tradução runtime sem mudar formatos de shader/cache. Uma tabela por hash seria limitada ao corpus conhecido; metadata HLSL exigiria migração de todos os caminhos de cache. A leitura de microcódigo preserva o fallback quando não puder estabelecer cobertura completa.

Container truncado, instrução/fluxo não suportado ou falta de shader necessário produzem máscara desconhecida e processamento completo. Um shader conhecido sem TFETCH tem máscara zero, diferente de desconhecido. Capturar todos os ramos referenciados; não inferir quais branches executam. A análise deve ser limitada e nunca executar código guest.

Integrar no caminho D3D12 de slots e na captura/replay neutros para Vulkan. Preservar descriptors nulos, mudanças de shader, resolves, snapshots pendentes e revalidação quando um slot voltar a ser usado. Variantes de shaders host substituídos, motion capture e diagnóstico que precisem do estado completo conservam todos os slots. Um controle de reinício permite desligar a otimização para comparação. Nenhuma outra candidata da pesquisa será implementada nesta etapa.

## Validação

Testes puros do decoder e política (zero/desconhecido, união VS/PS, slot31, fluxos condicionais, bytes malformados), comparação da máscara com os 241 shaders/HLSLs locais, testes de captura/replay e regressões nativas/Vulkan. Build Release, revisão independente e benchmark On/Off no mesmo executável. Comparação de imagem deve usar a mesma cena congelada quando disponível; não atribuir causalidade a diferenças de FPS de uma única rodada.
