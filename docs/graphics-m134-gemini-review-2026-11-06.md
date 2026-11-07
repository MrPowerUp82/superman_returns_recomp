# Gate de revisão Gemini — M1/M3/M4

Skill usada: C:/Users/Gusta/.codex/skills/gemini-orchestrate/SKILL.md. Três pesquisas independentes, sem escrita no checkout. A primeira tentativa em mode plan retornou três respostas vazias por comandos bloqueados; não foi aceita. A segunda tentativa usou cópia isolada de fontes em .gemini-tasks, mode plan com permissões automáticas apenas nessa cópia. Os três workers retornaram respostas. Nenhum código Gemini foi integrado automaticamente.

## M1
Conferidos: observed_vehicle_batch.h:95 exige token vivo; instance_probe.cpp:328–349 delimita flush, :419 congela seleção na invocação; captura pós-VS e integração NGX permanecem ativas. A ausência de bug encontrado pelo worker não prova correção global. Aceito o protocolo de atribuição por proprietário original e subtração da câmera. A opção sugerida --owner-sources não existe: o comando real implementado é --owner-vs repetível. Dados runtime devem ser conferidos diretamente; o snapshot Gemini não continha os dumps grandes.

## M3
Aceito e reproduzido: subcontagem de slots no ramo racional deadline<=now. Teste antes da correção: 713 testes, uma falha (0 versus1); depois: 713 testes, zero falhas. Correção só incrementa skipped_, sem mudar o deadline. Header e patch SDK sincronizados por manifesto, com rebuild.

Aceita a recomendação de expor taxa das fontes únicas aceitas separadamente da taxa de retornos API: dois testes sintéticos verificam repaints, falhas e serial vazio. Essa taxa não representa scanout nem todos os frames produzidos.

Rejeitada a alegação de Vulkan ausente: o caminho efetivo é port/src/graphics/vulkan/platform/native_provider.cpp, PaintAndPresentImpl, que chama timing_.Wait antes de consumir mailbox e timing_.Record depois de apresentar; policy.cpp seleciona FIFO com vsync. O patch SDK D3D12 sozinho não descreve esse backend.

Rejeitado tratar repaints documentados como FPS falsificado: as métricas antigas são explicitamente API, acompanhadas de FPS do jogo. Mantida essa distinção e ampliada a telemetria. Não alterar reset de calibração, yield ou flush CSV por hipótese de custo sem medição. Flush opt-in é necessário porque o benchmark termina com _Exit; sua remoção simples perderia dados. O display disponível permanece144Hz, não certificado60Hz.

## M4
Conferidos: shader_translator.cpp:125–167 inclui conteúdo das ferramentas/contrato na chave; pipeline_cache.cpp:141–188 calcula descrição por campos, sem padding; migração v1 preserva backup. Recomendada atualização do seed observado, executada separadamente com proveniência SHA e deduplicação.

Rejeitada a proposta de não contar PSOs de prewarm: temporal_motion.cpp registra criações reais; deltas route_begin/end excluem startup naturalmente. Instanciar diagnóstico privado no meio de uma rota pode criar PSOs de fato, portanto deve ser separado da medição, não escondido. Os comandos de exclusão/backup genéricos do worker não foram executados: a validação usa arquivo exato, backup de bytes, restauração em finally e SHA.

## Limites
As respostas dos workers são apoio à revisão estática, não nívelA. Testes/benchmarks e seus hashes constam do relatório de validação final. M2 não foi ampliado. Não há autorização de commit/push.
