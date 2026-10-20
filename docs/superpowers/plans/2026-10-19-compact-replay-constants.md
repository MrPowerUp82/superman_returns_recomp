# Constantes compactas no replay

Objetivo: reduzir arena_setup (~13 ms) e decode (~32 ms), sem alterar a ABI
Vulkan, a semântica dos draws, nem consultar memória guest após a captura.

## Interface e invariantes

Manter `ConstantSnapshot` de 12288 bytes como layout de upload GPU e para os
fixtures. `DrawPacket::constants` passa a `PacketConstants`: dois bancos com
ownership compartilhada e cópia ao modificar, mais 512 bytes de dados shared
específicos do draw. Os 3584 bytes finais do antigo shared duplicavam
`vertex_fetch` e são reconstruídos apenas pelo backend portátil no upload.

`Pm4Mirror::ShareFloatConstants(bool)` publica um snapshot normalizado imutável
por versão de banco. Escritas posteriores e reset/destruição do mirror não
alteram packets já publicados. PacketConstants mantém acesso por índice/data
e atribuição a partir de ConstantSnapshot para fixtures e draws construídos
manualmente. Modificar um banco compartilhado cria uma cópia privada.

O decoder referencia os bancos compartilhados. Os backends leem seus dados
explicitamente: nenhum memcpy pode presumir que PacketConstants contém os
8192 bytes inline. Buffers da GPU permanecem no formato e tamanho anteriores.

## Execução nesta sessão

1. RED: limite estrutural de tamanho do packet e testes de snapshot/COW,
   reset do mirror, normalização e ausência de publicação após falha.
2. Implementar bancos, PacketConstants, snapshots versionados do mirror e
   integração no decoder. Remover cópia da cauda redundante de metadados.
3. Atualizar uploads e caches Vulkan, preservando offsets/ABI; testes de
   upload com bancos compartilhados devem conferir todos os bytes na GPU.
4. Medir replay completo em Release incluindo descarte e sobreposição, com
   versões alteradas e repetidas; fixture vazio não representa o jogo.
5. Rodar suites nativa/Vulkan e provas GPU com validação, revisar a mudança,
   build do jogo e medir gameplay sem builds concorrentes. Não declarar 60 FPS
   a partir de benchmark isolado.

Revisão: compartilhamento após reset/cópia do mirror; edição de packet normal;
const-correctness; sizeof/offsetof usados como upload; padding shared; aliases
de arena e últimos owners; custo de alocar novos snapshots quando VS/PS muda.
