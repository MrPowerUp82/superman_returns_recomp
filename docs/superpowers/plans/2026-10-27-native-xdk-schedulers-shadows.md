# Native XDK, schedulers e sombras

Objetivo autorizado: substituir o processamento gráfico XDK, investigar os quatro schedulers e verificar sombras/fachadas.

Decisão técnica: substituir por contratos observados, com recusa antes de qualquer escrita nos casos ainda não portados. Um retorno antecipado nos hooks existentes perderia os uploads de constantes/literais e as alterações de estado que o mirror PM4 consome.

## Entregas verificáveis

- [ ] Submissão nativa de draws cujo estado não exige preparação de shaders/RT/fetch pelo XDK: montar constantes float e comando de draw sem executar as rotinas originais, preservando o caminho original nos demais casos. Começar experimental, contar cobertura e comparar com execução gerada.
- [ ] Testes de equivalência de estado PM4 com as rotinas geradas; recusa atômica em overflow, ring cheio e estados não portados. Gameplay A/B antes de selecionar padrão.
- [ ] Mapa dos quatro schedulers e callbacks; alterar somente gates demonstrados de renderização, preservando os peers de passo fixo.
- [ ] Exportação de todos os slices dos volumes de shadow resolve, com testes de limites/padding. Capturar e comparar os slices reais e identificar o caminho de emissão das janelas independentemente do shadow map.
- [ ] Build Release, CTest e registro dos resultados/limitações. Nenhuma alegação de bypass completo, quatro limitadores removidos ou fidelidade Xbox sem evidência correspondente.

O primeiro item é uma etapa da substituição completa: setters, shader preparation, inline vertices, clear, resolve e tiling continuam exigindo contratos próprios. Áudio e alterações de qualidade fora dessas áreas ficam fora desta etapa.
