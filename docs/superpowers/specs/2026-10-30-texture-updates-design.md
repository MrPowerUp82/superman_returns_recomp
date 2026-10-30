# Atualizações seletivas de texturas — 30/10/2026

Concluir o quinto candidato Canary nas APIs nativas. Reutilizar recursos apenas
quando formato, layout e view forem compatíveis e o lifetime provar ausência
de uso pendente; caso contrário manter versões imutáveis. Transferir somente
subresources alterados quando existir baseline completo e comparação exata.
Snapshots de CPU devem ter orçamento limitado; falta de baseline exige fallback.
Não confundir upload seletivo com decoder seletivo. Packed mips, volumes e
layouts não suportados devem manter upload completo correto.

Controle inicialmente experimental: SR_NATIVE_TEXTURE_UPDATES=1. Default final
depende da validação. Preservar tracking, aliases, resolves GPU, samplers,
submissões com uploads separados e todas as etapas anteriores. Validar por
readbacks antes/depois de fence, mudanças de conteúdo/layout e holders externos.
Não prometer ganhos de FPS, áudio ouvido ou equivalência visual geral.
