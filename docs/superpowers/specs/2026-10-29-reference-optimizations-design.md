# Otimizações das referências — 29/10/2026

O pedido é aplicar as otimizações dos relatos anexados. O checkout já contém o
renderer nativo D3D12/Vulkan, staging CPU, hashes condicionais, leituras PM4
protegidas por SEH, cache de superfícies/resolves, pacing, cache de pipelines,
resolves empilhados D3D12 e correções de áudio XMA. Os números do fork privado
não são uma medição deste projeto.

## Abordagem

Preservar as implementações existentes e fechar uma lacuna concreta: o caminho
de texturas ainda chama Sample com locks por textura/quadro. Reutilizar a view
de épocas de páginas que já atende os buffers, com fallback para Sample nos
runtimes antigos ou quando a prova rápida não estiver disponível. Reescrever
o renderer ou remover esperas de simulação sem evidência ampliaria o risco sem
resolver esse custo identificado.

Uma prova rápida deve ter cobertura completa e sem escritores, identificar o
mesmo recurso enviado à GPU, incluir todas as faixas (mips/camadas) e pertencer
ao mesmo domínio de tracking desde a captura. Escrita, liberação, troca de
domínio ou prova incompleta impedem reutilização. Compare continua auditando
por hash; hashes e uploads permanecem disponíveis como fallback.

A auditoria do áudio também identificou duas correções necessárias no flush
existente: cada chamada ao export XMA deve reconstruir o argumento de contexto,
pois o retorno sobrescreve r3; a capacidade de entrada deve usar 2.048 bytes
(16.384 bits) por pacote, sem confundir o deslocamento em bits com bytes.
Testar os dois buffers e o limite imediatamente anterior ao esgotamento.

## Validação

Testar essas condições com o estado real de tracking e seu espelho de épocas,
compilar testes e jogo Release, revisar a integração e comparar uma rodada do
benchmark embutido antes/depois. Uma rodada não comprova ganho estatístico nem
fidelidade ao console; não prometer 60 FPS sem medição.
