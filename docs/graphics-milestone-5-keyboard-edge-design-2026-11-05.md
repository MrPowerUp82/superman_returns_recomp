# M5 — preservar toques rápidos do teclado entre consultas

Design anterior ao código. REA/Frida observou OnKeyDown e OnKeyUp de Return
no mesmo milissegundo; GetDeviceState não publicou botão nãozero. A VM continua
na demonstração da tela de título. O caminho MnK recebe os eventos, mas seu
snapshot de teclas atuais perde um toque completo entre dois polls do jogo.
O teste automatizado por teclado deve funcionar sem modificar a UI APT.

## Política

No driver MnK do SDK isolado v0.10.0, reutilizar a resolução de bindings para
botões XInput. Em OnKeyDown habilitado/ativo, guardar os bits de botão resolvidos
naquele instante em pending_buttons. Em GetDeviceState ativo, OR com o estado
atual e consumir uma vez. Teclas continuam pressionadas seguem o estado normal.
Resolver no evento, em vez de guardar teclas soltas individuais, evita inventar
combinações de modificadores que nunca estiveram simultaneamente pressionados.
Não estender eixos analógicos ou usar sleeps, nem alterar o intervalo da VM.
Limpar pendências quando perde foco/inativo/desabilitado/fecha. Guardar e consumir
sob state_mutex, já usado pelo driver. Sem novo hook guest e sem fatal novo.

A resolução de botões deve ser a mesma função para evento e polling. Preservar
bare modifier bindings, listas de teclas, Dpad/Start/A/B e botões atuais. O pulso
pendente dura um poll, não até outro pressionamento; não reaparece ao recuperar
foco nem durante overlays de UI que desativam o input do jogo.

## Entrega e validação

Preparar patch versionado e manifesto de hashes sobre o checkout isolado, integrado
às verificações canônicas do SDK; não alterar outros checkouts do usuário. Design
não autoriza mascarar rejeições do verificador. Regressões CPU devem cobrir toque
completo antes do poll, consumo único, tecla mantida, modificadores verdadeiros/
não simultâneos, foco/inatividade e modo desabilitado. Validar depois com Computer
Use na janela do jogo: Start, seleção, Graphics Options, mudança, Back e retomada.
Sem shim Frida que altere duração do evento para declarar o teste aprovado.

## Correção de lock durante fechamento

Antes do código adicional, REA ev_9ca1ed4357b33784b28b49ca4f1f8cf6d8e6484e323f3678cd20573a6ecdc470
observou UI25796 esperando state_mutex em OnLostFocus/ProcessQuitRequest, enquanto
guest14308 esperava SDL em NotifyUILoopOfPendingFunctions, chamado por
QueueMouseCaptureUpdate ainda com state_mutex. O deadlock já pertence ao caminho
de notificação diferida; o teste real reproduziu janela sem resposta ao fechar.

Congelar ponteiro do app context sob state_mutex, soltar o mutex e somente então
chamar CallInUIThreadDeferred. O context vive durante o runtime e não é a janela
que pode ser destacada. O callback existente mantém verificação de attached_window.
Falha ao enfileirar deve limpar a flag de pendência. Regenerar patch0004 e hashes
canônicos; validar a cadeia exata e novo fechamento real, sem mascarar o verificador.
