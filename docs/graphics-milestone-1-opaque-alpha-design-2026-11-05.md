# M1 — alpha opaco certificado pelo PS original

Design antes do código. Após a correção IA, graphics_static_ia_gameplay registrou
C5E8 150/150 matches, C209145/150, C6878/8 e capa1/1 em uma captura real. Apesar
da correspondência, a etapa final invalida todo draw com blend RGB diferente de
ONE/ZERO. C209/C687 usam 010B0706: SRC_ALPHA(6), ADD(0), INV_SRC_ALPHA(7). Um
pixel com alpha exatamente1 substitui todo RGB; pixels misturados não podem
reutilizar um único vetor de objeto. Não presumir alpha1 pela classe ou textura.

Usar o PS original já especializado para ownership, depois de toda operação de
alpha/clip/output scale. Reservar bit31 do constant ID para pedir certificado de
alpha opaco; IDs reais permanecem1..4096. O PS grava o ID baixo30bits e, quando
o certificado foi pedido e oC0.w !=1.0, marca bit30 de mistura. NaN/Inf também
falham na igualdade. RGBA do jogo, discard e depth não mudam. VFX conserva seu
UAV reativo e não participa deste protocolo.

Pedir o certificado somente no blend RGB SRC_ALPHA/INV_SRC_ALPHA com ADD e
escrita de todos os canais RGB. ONE/ZERO conserva a política existente; blends
aditivos, parciais ou desconhecidos continuam invalidados. O shader de motion
compara o ID baixo30bits: owner diferente descarta, mesmo owner com bit30 escreve
MV0/validade0, mesmo owner sem mistura permite a reprojeção. Isso elimina a
possibilidade de deixar o vetor válido do fundo sob um pixel semitransparente.
Manter EQUAL, owner original, scissor/cull, geração/mesh/ordem e todos os gates IA.

Preparar um helper independente de API para a decisão RGB e o ID do constant,
testes CPU para alpha-over conhecido/fullRGB, ausência de owner, máscara parcial,
operações/fatores diferentes e ONE/ZERO. Atualizar a identidade do cache da
especialização PS, mantendo formato CNPS e demais variantes. A mesma saída MV/
validade segue para NGX e denoising RT; não ampliar geometria alpha-test de BLAS.

Testes GPU: ownership opaco permite MV, owner misturado apaga validade anterior,
owner diferente não altera geometria da frente, NaN/W inválido continua zero.
Conservar regressões de transparência ocluída e câmera. Confirmar máscara real
no Superman/NPC e repetição do benchmark após esta mudança. Screenshot isolada
não certifica ausência de ghosting; capturas sequenciais devem registrar movimento.
