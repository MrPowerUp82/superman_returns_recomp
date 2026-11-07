# M2 — invalidação temporal ao aplicar opções em tempo real

Design complementar, escrito antes da alteração. Os designs de M1 a M5 já
existentes continuam sendo a referência de arquitetura; o checkout contém
implementações locais que devem ser preservadas. Este incremento trata uma
lacuna concreta entre o filtro DXR de M2 e as opções aplicáveis sem reiniciar
de M5.

## Problema

`VisibleRayScene::Trace` permite histórico quando há quadros consecutivos,
projeção anterior, dimensões iguais e MVs/validade. Não compara a configuração
do estimador anterior com a atual. Trocar distância/qualidade/resolução do trace,
inclusão de personagens ou direção de sol pode misturar respostas diferentes
durante vários quadros, mesmo que a geometria e seus MVs estejam corretos.

## Contrato

Guardar no slot anterior uma chave tipada com os parâmetros que mudam o
significado dos resultados: efeitos habilitados, máscaras de personagens,
quantidade/cones/distância dos raios, escala de trace, bias, convenção de clear
depth, thresholds do filtro e direção normalizada do sol. Igualdade explícita
de campos, sem hash de padding nem comparação binária de struct. Os valores
já são validados antes de construir a chave. Câmera, jitter, serial e posição
dos objetos não pertencem à chave: têm seus contratos de reprojeção próprios.

Só consumir histórico quando as chaves forem iguais. No primeiro quadro de
uma alteração, publicar o filtro espacial e armazenar a nova chave; no próximo
quadro consecutivo com configuração estável, permitir o temporal novamente.
Manter os demais gates de reset/validade e os fences atuais. Nenhum readback,
PSO, fatal, hook ou dependência de Vulkan adicional é necessário.

## Validação e limites

Adicionar regressão GPU que estabiliza histórico, muda somente a distância
de reflexo e exige rejeição, em seguida exige recuperação no quadro seguinte.
Exercitar também mudança de escala e máscaras, inclusive configurações que
mantêm as mesmas dimensões finais. Comparar saída após rejeição com controle
espacial no mesmo quadro quando possível.

Medir três rodadas antes/depois via benchmark embutido, com configuração
idêntica e hashes explícitos; fazer capturas em processos separados. Repetir
smoke GPU e build Release antes de medir o resultado. O benchmark de título
mede custo e execução DXR/NGX, não demonstra eliminação de swimming/ghosting
em todas as fases. Reflexos continuam híbridos com radiância projetada; esta
correção não implementa recuperação completa de materiais fora da tela.
