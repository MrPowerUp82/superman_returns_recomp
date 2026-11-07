# M2: sombras e reflexos DXR da geometria visível

Desenho escrito antes do código. Backend inicial D3D12/DXR 1.1, Vulkan conserva
raster. Não adicionar fatal ou hooks fora do manifesto existente.

Reusar clips pós-VS do M1 para recuperar posições mundiais com o inverso da
câmera final (incluindo fixups/jitter) e construir BLAS/TLAS de triângulos
submetidos. Isso inclui deformação já executada pelo VS e evita assumir um
layout universal de vértice/skin. Não converter passes de outra câmera.
Excluir transparência sem cobertura opaca do BLAS. Reuso de recursos por slot
obedece fences; qualquer falha deixa o pipeline raster efetivo.

Executar traçado próprio antes de DLSS/HUD. Sombras usam direção de sol obtida
do contrato de projeção de sombra auditado. Eliminar draws de geração de mapas
originais somente quando o novo caminho estiver ativo; conservar fallback
durante inicialização/falhas. Não remover sombras de projeção desconhecida.

Reflexos traçam contra TLAS real; um hit é projetado para a imagem iluminada
retida, com verificação depth/viewport antes de aproveitar a cor. Misses/hits
sem cor retornam o ambiente original. Esta versão usa a geometria visível,
conforme pedido; não promete reflexão de objetos nunca submetidos/offscreen.
Não chamar de reflexão RT a antiga multiplicação do ambiente por 0,25.

Filtro temporal usa movimento/validity do M1, rejeição por depth, clamping e
reset; filtro espacial bilateral cobre regiões sem histórico. Qualidade expõe
distância máxima do reflexo, número de raios e escala de trace. Registrar tempo
GPU por timestamp, confirmação por fence e estado aplicado após execução.

Aceitação: sombras sem dependência temporal do atlas antigo nos passes
substituídos, reflexos com cor de hit observável, controles On/Off/qualidade,
benchmarks de três rodadas e screenshots. Cobertura restante deve aparecer no
relatório; não transformar capacidade DXR em evidência de imagem executada.
## Fallback atômico do quadro atual

Sucesso do quadro anterior não autoriza remover sombras do quadro atual. Gravar
os draws originais com predication D3D12 e buffer upload de 64 bits por slot,
inicialmente 1 (executar fallback), operação EQUAL_ZERO (zero suprime). Somente
após registrar Trace+Composite atuais com sucesso e luz válida escrever zero,
antes de submeter a lista. Constantes standalone de cada receiver ficam zero
até esse mesmo commit CPU. Em falha, mapas e receivers originais executam.
Consultas antigas de ray tracing ficam desativadas no caminho dedicado.

Submissões parciais de diagnóstico congelam o fallback; nesse quadro não ativar
a sombra dedicada nem alterar constantes já submetidas. Slot e upload só são
reutilizados após seus fences. Testar explícito sucesso/falha alternados.
Referência: [contrato Microsoft de SetPredication](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setpredication).

O filtro temporal soma previousJitter-currentJitter aos MVs sem jitter e rejeita
histórico onde o UAV de reatividade indicar VFX sem correspondência.

## Receivers com coverage e sombra na mesma PS

A seleção efetiva do PSO deve compor coverage e neutralização da sombra antiga.
Adicionar variante CNPS v2 de domínio 3 (RT + coverage), pré-aquecida dos mesmos
originais auditados. Aplicar primeiro coverage, depois o patch RT, preservando
o MRT e alpha/discard. Identidade inclui ambos os patches; não reutilizar o
PS coverage simples para atestar receiver RT. Calcular unpatched somente após
os fallbacks finais de PSO. Testar composição nas interseções reais do corpus.
