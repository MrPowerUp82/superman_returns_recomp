# Preparação das ligações globais em blocos

Meta persistente: 60 FPS reais em Vulkan nativo, escala 1, sem geração de quadros.
Esta etapa ataca bindings (~46 ms), antes da emissão (~4 ms) observada no modo
global. Execução nesta sessão, sobre o checkout atual que contém a integração
global ainda não commitada; preservar todas as demais alterações.

## Decisão

`GlobalDrawBindings::Prepare` mantém a API e a ABI dos shaders. Um bloco reúne
128 draws: constantes de 8704 bytes, streams de 528 bytes e tabela de registros
de 16 bytes, com cabeçalho de 16 bytes. As regiões respeitam alinhamento de
16 bytes; as slices completas e as tabelas continuam respeitando os limites
e alinhamentos de storage buffers da GPU. O bloco é adquirido uma vez, e
`Prepare` apenas preenche a próxima posição. Cabeçalhos referenciam chunks
de constantes, declaração e streams, conservando offsets absolutos da ABI.

Um cache por submissão mapeia identidade de BufferResource/TextureResource/
SamplerResource para slots adquiridos no heap. Só entradas adquiridas com
sucesso são publicadas. O heap mantém os owners vivos até o fence. Um upload
de nova versão produz novo owner e portanto novo slot, mesmo com o mesmo ID
guest. Limpar caches no início de cada submissão; nunca reaproveitar números
após retirada/evicção sem adquirir novamente no heap.

## Tarefas e verificação

1. Medir fixture global/portátil antes da alteração. Acrescentar cobertura GPU
   de versões alternadas de textura e buffer sob o mesmo ID no mesmo quadro.
2. Teste RED: estatísticas de produção devem observar 24 páginas para 3000
   draws com uma declaração; aquisições de recursos devem crescer com owners
   distintos, não com draws. Não usar tempo de execução como assert de teste.
3. Implementar blocos e cache no global_draw_bindings.{h,cpp}. Contar páginas,
   draws e aquisições reais para diagnosticar a redução de trabalho.
4. Rodar suite Vulkan, prova GPU global/portátil (inclui IA, índices, offsets,
   todos os pixels e duas versões pendentes), e build do jogo.
5. Medir fixture novamente incluindo preparação e gravação. Conferir gameplay
   e medir FPS real em janela isolada. Registrar limites de comparação de cena.

## Revisão focada

Offsets de página perto do fim de chunk; tabela com alinhamento de descriptor;
vida útil após limpar cache de CPU; mudanças do mesmo ResourceId; dados em
memória não coerente; publicação somente após flush; imagens 2D/3D/cube;
slots compartilhados entre duas submissões pendentes. Manter caminho global
opcional até demonstração de ganho e correção no jogo.
