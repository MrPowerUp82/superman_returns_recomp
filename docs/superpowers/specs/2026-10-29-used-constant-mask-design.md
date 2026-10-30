# Constantes usadas pelo shader — 29/10/2026

Terceira candidata da pesquisa Canary. Capturar uma máscara conservadora dos
256 float4 por estágio a partir do microcode original. CF deve ser validado;
incluir todos os caminhos/operandos ALU, inclusive operandos escalares especiais.
Endereçamento dinâmico exige o banco inteiro. Shaders desconhecidos, host
overrides e passes auxiliares não podem autorizar exclusões.

Manter buffers de 4096 bytes e ABI atuais. Comparar valores sanitizados apenas
em memória normal de CPU contra os bytes efetivamente enviados; não atualizar
o baseline por mudanças em registradores omitidos. Uma troca de shader pode
passar a ler esses registradores e deve provocar upload quando necessário.
No D3D12, manter o fast path por versão de banco e memoizar provas por versão
e máscara; resetar a validade com o ring de uploads. Debug override força upload.
No Vulkan, aplicar ao cache opcional existente por banco, com validade limitada
à mesma submissão/chunk. Heap global e layout dos offsets permanecem atuais.

Controle de comparação: `SR_NATIVE_USED_CONSTANT_MASK=0`, lido no início do
processo; `=1` ativa. Padrão final Off após dois pares D3D12 negativos, mesmo
com menos uploads. Preservar comportamento e todas as alterações anteriores.
Validar parser, máscaras e dynamic fallback, mudanças usadas/omitidas, troca de
shader, reset, CPU baseline vs upload, GPU pixels On/Off e medições sem prometer FPS.
