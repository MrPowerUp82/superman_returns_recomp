# Fechamento M1, M3 e M4 — 2026-11-06

Escopo autorizado: finalizar M1/M3/M4; M2 não será ampliado. Preservar a auditoria anterior como registro histórico e distinguir validação de implementação.

## M1: atribuição por proprietário original
A captura pós-VS e NGX já estão ligados. Para comprovar movimento independente dos veículos, ampliar o analisador offline da câmera congelada: filtrar cada família solicitada pelos IDs R32_UINT escritos pelo pixel shader original, exigir draw qualificado, prova de instância e validade da saída de produção, rejeitar mistura alfa e pixels não finitos. Comparar o MV de produção com a fórmula de câmera do mesmo frame, na resolução interna. Não usar ordem de draw ou proximidade espacial como identidade. Emitir percentis e contagens por família, máscara/resíduo e relatório separado; zero pixels não constitui aprovação. Isso não comprova ausência de ghosting em toda cena.

## M3: revisão e reprodução
Revisar o pacer e sua fila com worker Gemini somente leitura e conferir os achados no código. Corrigir apenas defeitos demonstráveis, com teste específico. Repetir três rodadas por limite/mecanismo no executável final, sem captura/build/testes concorrentes. Medir FPS do jogo separadamente de retornos de Present/repaints. Capturas em execuções separadas. Monitor disponível é 144 Hz: não declarar certificação física em 60 Hz; essa exigência só pode ser concluída em equipamento que exponha o modo.

## M4: seed observado e cold-cache
Unir descrições realmente observadas do cache local com seed anterior, preservando ambos e seus hashes. Deduplicação por descrição, ABI CNPS v2, sem bytecode de driver no seed. Registrar novos números/hashes, rebuild do recurso e testar cache local ausente com backup/restauração garantida. Zero traduções/PSOs só vale para os percursos medidos; inicialização de pipelines privados de diagnóstico fica fora da janela. Manter seed Vulkan observado e testar o backend. Não alegar cobertura de fases não percorridas.

## Gate de aceitação
Resultados Gemini são sugestões até conferência de arquivos/linhas. Testes CPU e fixtures GPU existentes devem passar; benchmark embutido em três rodadas por configuração, executável imutável, screenshots separados e limitações registradas. Não adicionar REX_FATAL, hooks ad hoc ou alterar simulação/áudio. Não mascarar falhas nem promover hardware indisponível a teste aprovado.

## M3 — correção aprovada pelo gate de revisão
Defeito reproduzível: depois de Complete(0) em 60 Hz, Complete(33333333) avança para 50000000 ns, mas mantém skipped=0. O prazo de 33333333 já expirou e deve ser contabilizado como um slot pulado. Incrementar somente o contador no ramo deadline<=now, mantendo todos os prazos e a política de espera. Teste RED/GREEN em fronteira racional e teste de fronteira pontual sem atraso. Sincronizar header do projeto e overlay SDK isolado, patch versionado e manifestos SHA; rebuild obrigatório. Nenhuma mudança de simulação ou apresentação é necessária para corrigir esse contador.
Também ampliar resumo de telemetria com taxa/intervalos de fontes únicas aceitas, separadamente de repaints/API; isso não mede scanout nem frames substituídos no mailbox.

## M4 — descrição Vulkan descoberta na verificação final
A terceira rodada do build3d3ff312 registrou runtime_attempts=1 e description_misses=1; shader_misses real e compiler_invocations permaneceram0. O journal passou de302 para303 descrições. A rota não recebe aprovação zero-runtime. Antes de trocar RCDATA4, verificar magic/ABI/checksum de todo journal, exigir união que preserve o seed anterior e registrar SHA. Decodificar todos303 estados com o teste existente. Rebuild apenas do recurso e teste alterado, conferir identidade da seção .text entre builds. Repetir três rotas Vulkan sem cache local de driver/descrições e restaurar ambos em finally com hashes. Métricas D3D12/pacing já obtidas permanecem vinculadas ao build3d3ff312; não reatribuir ao novo executável. Não alterar código de renderização nem esconder a tentativa que encontrou o miss.

## M4 — consolidação integral após comparar todas as rotas
As rodadas objects_r2/r3 do build3d3ff312 registraram1PSO cada; r2 identificou DF1EB50388CC2563/F2BB7B6551646033. O journal CNPS atual unido ao seed1003 contém1024 descrições únicas. Não aprovar essas rotas como zero-runtime. Incorporar a união observada, preservando arquivo e SHA de origem.
O journal de raster de movimento tem quatro estados; o seed C++ tinha três. O quarto observado é {cull3,front1,bias0,slope1045220557,clamp0,depth720941}, formato45/comparação5. Incluí-lo no seed numérico; não alterar shaders ou política de validade. Validar com parser CPU, fixtureGPU e três rodadas D3D12 removendo tanto CNPS quanto journal temporal do caminho, com backup/restauração emfinally. Isso fecha a dependência oculta de journal local; não suprimir contadores de criação. Métricas anteriores permanecem vinculadas ao build original; a alteração é exclusivamente de prewarm/cache.
