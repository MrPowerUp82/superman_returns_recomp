# Drenagem de subframes XMA — 30/10/2026

Adaptar apenas o loop Work do decoder síncrono ReXGlue, usando o Canary fixado
como referência. Entregar blocos PCM já decodificados enquanto houver capacidade,
mesmo sem entrada válida ou com erro 4. Preservar o primeiro Decode do kick,
carry final, truncamento/skip de loop, padding e StoreContextMerged.

Checar ausência real de progresso incluindo offset e buffer de entrada, valid
flags, loop count, pending/carry/limit/skip e orçamento/offset de saída. Offset
igual sozinho não basta. Não adicionar esperas, yields nem limite arbitrário
às iterações produtivas. A drenagem consume-only terá a mesma admissão por
capacidade, sem alterar a semântica output-valid existente. Saída de tamanho
zero deve retornar antes do módulo do ring buffer.

Distribuir como patch canônico 0002 separado do 0001 de tracking. Setup deve
migrar checkouts antigos e verificar ambas as hashes, rejeitando edições
desconhecidas antes de modificar qualquer entrada. Testes executam Work real
contra PCM marcado em memória guest, cobrindo mono/stereo, SDC, fim de entrada,
erro 4, padding, wrap, limites e stalls. Isso não comprova audição nem corrige
por si só o carry final: o hook de flush existente permanece necessário.
