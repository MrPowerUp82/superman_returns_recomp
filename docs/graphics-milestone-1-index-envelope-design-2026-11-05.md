# M1 — envelope indexado em produção

Correção projetada antes do código. O frontend já resolve o intervalo de índices
normalizados enquanto o draw possui seus inputs, mas só o copia ao comando quando
um arquivo de captura diagnóstica está configurado. O consumidor contínuo exige
esse intervalo para certificar dependências de posição; sem ele rejeita draws
indexados mesmo quando DLSS ou DXR estão ativos.

Capturar o mesmo envelope também quando object motion com DLSS ativo, sombras RT ou reflexos RT
forem solicitados no renderer D3D12. Preservar o caminho diagnóstico explícito e
evitar trabalho no packet sink Vulkan. Não ler índices do guest no worker.
Com DLSS e RT desligados, manter a resolução preguiçosa original dos buffers.
Reutilizar Resolve (endian, restart e base vertex), verificar a soma em 64 bits e
só publicar intervalos finitos, não vazios, abaixo do limite existente. Nenhuma
identidade ou cobertura passa a ser presumida por causa desse intervalo.

Validar build, regressões existentes de índices e motion, GPU e contadores reais
de correspondência em captura posterior. Um aumento de cobertura não prova por
si só ausência de ghosting em todas as classes de objetos.

## Resultado inicial e próxima observação

A captura de produção `graphics_final_indexed_20261105_capture_objects` avaliou
NGX e compôs saída com fences, sem trigger diagnóstico. No swap120 houve
correspondências de geometria, mas os contadores rígidos permaneceram0; portanto a
correção remove um bloqueio necessário e não certifica veículos. Na cena do
título também houve matches0 nas famílias skin/capa presentes nesse instante.

Antes de qualquer ampliação de identidade/cobertura: habilitar os relatórios
de proveniência já existentes também no único quadro escolhido por capture-swap
ou trigger contínuo. Guardar de modo limitado o banco1024float/4bool real e a
invocação congelada de skin/cloth/rigid no diretório de dump explícito. Esse
diagnóstico não pode autorizar correspondência, mudar flags ou rodar em janelas
de performance. Ele deve distinguir falta de token/parte, bancos não finitos,
blend, versão de mesh, ambiguidade e ausência de câmera comum antes de correções.
