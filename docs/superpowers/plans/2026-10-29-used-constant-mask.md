# Plano — constantes usadas pelo shader

- [x] Analisar fonte Canary/SDK e dependências adicionadas pelo emissor/GS.
- [x] Parser limitado de microcode, máscaras por estágio e fallback dinâmico.
- [x] Metadados imutáveis nas três rotas de captura de shaders.
- [x] Cache D3D12 por versão/máscara e baseline real em CPU; reset com upload ring.
- [x] Integração no cache Vulkan opcional, mantendo chunk/submissão e baseline.
- [x] Testes de campos ALU, escalar especial, máscaras/versões, shader switches,
      defaults desconhecidos, comparações bit a bit e reset.
- [x] Referência independente do SDK: 241/241 containers conhecidos; sem
      dependências omitidas. Decoder SDK compilado sem alterações no target de
      testes quando o código-fonte do SDK está disponível.
- [x] GPU: sete resultados congelados com máscara On/Off e cache Off; regressões
      de textura/máscara e storage merged.
- [x] Build Release e suítes: native 615/CTest 5/5; Vulkan 109/CTest 1/1;
      tracking 37 testes principais/CTest 4/4.
- [x] Medições no jogo: dois pares D3D12, um invertido, e um par no cache Vulkan
      opcional. Menos uploads, mas ambos D3D12 negativos; Vulkan favorável em um
      par. Padrão final experimental Off, flag explícito `=1`.
- [x] Rebuild e verificação dos padrões finais; GPU default/On/Off e CTest.

Relatório: `docs/used-constant-mask-2026-10-29.md`. Build final SHA256
`B5FE2070107D0779D90D4606E062CD6B5107580433E474074BACA8105683768D`.

Restrição: preservar bancos completos, ABI, bool/loop/shared e recursos usados
por submissões anteriores. Não ler memória de upload para comparar. Motion,
post-VS e shaders/tools explicitamente substituídos usam a regra anterior.
