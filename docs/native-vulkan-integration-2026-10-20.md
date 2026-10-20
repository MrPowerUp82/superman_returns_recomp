# Integração das otimizações Vulkan — 20/10/2026

Integração local de `codex/vulkan-global-contract` na `main`, partindo de
`92c26e0`. Solicitada pelo usuário para preservar as reduções comprovadas
de custo de CPU/GPU, mesmo quando não produziram ganho no FPS total.

## Trabalho preservado

- Packets de replay compactos: bancos float imutáveis compartilhados e
  remoção de metadados duplicados, preservando o upload portátil de 12288
  bytes e a cópia profunda de bancos editáveis.
- Caminho global Vulkan opcional, com contrato de shaders versionado,
  negociação de recursos, validação, compilação e empacotamento consistentes.
- Preparação global em páginas de 128 draws, caches por submissão e
  substituição de slots sem a varredura quadrática do prefixo pendente.
- Reaproveitamento da preparação de texturas quando a descrição completa
  e a identidade do recurso atual continuam iguais. Cerca de 89% de reusos
  na coleta; nenhuma versão substituída é reutilizada indevidamente.
- Diagnósticos de fases e instrumentos de comparação com os resultados
  positivos, negativos e suas limitações registrados nos documentos.

O replay serial, lotes de 32 comandos e pool de packets desligado continuam
como padrões. O caminho global continua opcional por
`SR_VULKAN_GLOBAL_RESOURCES=1`; nele, páginas e cache de texturas estão
ativos. `SR_VULKAN_TEXTURE_BINDING_CACHE=0` permite comparar sem reuso.
Replay paralelo e lotes maiores continuam experimentais e opt-in: os testes
no jogo não comprovaram ganho que justificasse promovê-los ao padrão.

O merge não declara 60 FPS atingidos nem ganho de GPU comprovado. A última
comparação limpa com cache de texturas ficou próxima de 12,5–13 FPS reais.
As reduções isoladas de CPU não devem ser apresentadas como ganho de FPS.

## Verificação antes da integração

- Build Release do jogo concluído; SHA-256 preservado:
  `D3DBDACAE8EA97FD45BA4DC5F29C45CCA97A316D38E05C2E30DBF8080EDFD9DF`.
- 195 testes nativos e teste de orçamento de captura: CTest 2/2.
- 108 testes Vulkan: CTest 1/1.
- Suíte Python completa: 111 testes aprovados.
- Contratos GPU global e portátil: pixels aprovados, zero erros de validação.
- Gravação com passes mesclados, recorder assíncrono, tempo de vida de
  recursos, amostragem de resolve, composição e interface imediata aprovados.
- Revisão independente da integração e da correção do fixture sem achados.

A checagem ampliada encontrou uma falha anterior em `CheckHoldLifetime`:
o teste simulava a aposentadoria dos recursos sem descartar as referências
do command buffer nunca submetido. A camada de validação detectou seis
erros. O fixture agora reseta o pool antes da aposentadoria simulada e inicia
uma gravação vazia. As verificações de retenção e liberação permaneceram;
o teste corrigido passou com validação ativa. Nenhum código de produção
precisou mudar para corrigir essa simulação.

Logs locais: `logs/native60_merge_*_20261020.log`. Depois do merge, repetir
as suítes nativa, Vulkan, Python e contrato GPU global sobre a árvore integrada.
Resultados históricos e executáveis de comparação permanecem nos snapshots
em `artifacts/native-renderer-performance/`.
