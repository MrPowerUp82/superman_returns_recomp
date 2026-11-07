# M1 — domínio rígido comum por igualdade de clip

Extensão escrita antes do código. A seleção rígida original por partes continua
preferida; esta extensão trata somente draws síncronos de dois dispatches já
observados: 8230F8E0/vtable820211C0 e823103C8/vtable82021930. Sem hook novo.

## Prova necessária

Owner/generation/mesh e duplicatas por frame não bastam: partes A/B de uma mesma
mesh podem alternar visibilidade entre frames com transformações diferentes.
O frontend deve congelar os64bytes de owner+128 somente com token vivo/dispatch
verificado, junto do comando. Não usar endereço de pacote como identidade.

A matriz do owner usa row vectors; sua última coluna é0,0,0,1 e a translação
fica no quarto float4. A forma para dot rows do shader é sua transposta O.
O fallback só é permitido nos cinco VS rígidos já auditados, usando os16valores
ACTUAIS de clip_rows e uma câmera world-to-clip da cena já auditada no mesmo
frame/domínio, sem conflito. Exigir igualdade estrita, normalizando somente ±0:

    CBobjectClip == C_atual * transpose(ownerRaw_congelado)

Isso prova que a posição local da mesh canônica usa o mesmo domínio do owner.
Não comparar somente world de iluminação e não introduzir tolerância aberta.
Rejeitar não finitos, owner não afim, falta de câmera, geração/dispatch inválidos,
CB/bindings não verificados e qualquer desigualdade. Igualdade pode rejeitar draws
legítimos por arredondamento; essa perda de cobertura é preferível a falsa história.

A chave usa owner, generation e domínio rigid-common distinto das partes originais;
a mesh real completa continua sendo parte da chave do consumidor. Mudanças de
mundo/câmera ficam nos inputs. Manter todos os testes de buffers/versões/fetch/
ordem, câmera, consecutividade, cobertura, duplicatas e budgets já existentes.

## Evidência e validação

Auditoria local: sub82612658(.75.cpp:21800) transpõe owner mas guarda3float4;
sub82634A80(.26.cpp:23401) sobe count3. Isso não prova world44. sub8260C800
(.43.cpp:22649) calcula composição affine parcial; sub8260C920(.32.cpp:23050)
usa câmera raw de global não ligado à câmera2C45 nesta análise. O gate usa os
bancos reais e a câmera auditada, em vez de assumir equivalência desses helpers.

Regressões CPU: transposição/translação, matriz não afim, WVP divergente, owner/
geração/vtable inválidos, câmera ausente/conflitante, não finitos, igualdade ±0,
domínios distintos e correspondência quando mundo muda mas igualdade se conserva.
Medir contadores reais de fallback qualificado/correspondente em gameplay antes
de atribuir cobertura a veículos. Zero cobertura permanece um resultado possível.
