# Reuso de armazenamento entre samplers — 29/10/2026

Implementar a segunda candidata da pesquisa Canary, autorizada por "próximo".
Compartilhar backing e validação entre fetches que diferem apenas em clamp
U/V/W, filtros mag/min/mip/aniso, filtros de volume ou bias de LOD.

Identidade conservadora: preservar todos os demais bits, incluindo mip min/max,
swizzle, signals/gamma, exp_adjust, formato/endian, pitch/tiling, dimensões,
base/mip address, border e bits desconhecidos. A chave de backing é comparada
por todos os seis dwords normalizados; hash nunca basta como identidade.

D3D12: manter fetches exatos no cache de slots/samplers, agrupar apenas a entrada
de textura por identidade normalizada. As provas pertencem ao mesmo recurso e
às suas faixas, sem enfraquecer cobertura/versionamento ou fallback por hash.

Vulkan: agrupar o cache de snapshots do front-end pela mesma chave; o fetch
original de cada binding permanece no pacote. O executor só compartilha imagem
e view quando chave exata, versão e identidade do snapshot coincidem. Apenas
um serial igual em capturas independentes não prova igualdade. Versão nova cria
outra imagem, preservando draws antigos, inclusive com uploads separados.
Resolves GPU continuam no caminho próprio. Usar referências fracas no índice
de backing e nunca promover um alias de resolve a upload de CPU.

`SR_NATIVE_TEXTURE_STORAGE_REUSE=0` restaura o caminho anterior nas duas APIs,
lido uma vez por processo. Padrão final: On D3D12, Off Vulkan, pois a regressão
Vulkan repetiu em dois pares de ordem alternada. `=1` força ambas. Não incluir ainda
compartilhamento entre swizzles/formats nem atualização in-place de conteúdo.

Validar bits excluídos/preservados, ranges e decoder reais, mudança de conteúdo,
ordem de versões, samplers distintos com mesma imagem e lifetime até fence.
Rodar suítes nativas/Vulkan/runtime, build Release e ensaios On/Off sem prometer
ganho de FPS. Preservar todas as alterações anteriores sem commit/push automático.
