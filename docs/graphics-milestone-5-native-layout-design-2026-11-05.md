# M5 — ajuste de layout após captura real

Antes do código: a captura Vulkan revelou colisão entre CHARACTER SHADOWS (N/A)
e seu valor. Mostrar indisponibilidade na coluna de valor como N/A, conservando
o nome original e a preferência interna. A lista nativa mantém os mesmos índices;
requests indisponíveis continuam rejeitados pelo backend.

O disassembly format-aware do asset TOGGLE (logs/native_toggle_inspection.json)
mostra que SetWidth(nWidth) define mArrowWidth e posiciona a seta esquerda em
rightArrow.x - nWidth - leftArrow.width. Logo790 era separação de setas, não
largura da linha. Conservar200original: seta esquerda perto do valor, ambas dentro
da moldura, sem alterar TOGGLE compartilhado. Regressão verifica os seis calls;
captura posterior deve confirmar o alinhamento com a referência do usuário.
