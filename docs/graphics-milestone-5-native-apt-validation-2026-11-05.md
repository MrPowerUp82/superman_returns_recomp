# M5 — validação da UI EA APT original

Implementação guiada pelas duas imagens fornecidas pelo usuário. O design
foi escrito antes do código em graphics-milestone-5-native-apt-design-2026-11-05.md.
O build nativo deixou de instalar/compilar GraphicsOptionsDialog. Os controles,
fonte, moldura, foco, setas, hints e navegação pertencem ao EA APT do jogo.
A tela aparece como GRAPHICS OPTIONS entre AUDIO OPTIONS e QUIT GAME.

## Pacote e ponte

`tools/graphics/native_graphics_apt.py` deriva a extensão do ui.AST local do dono,
verifica SHA256 do original e emite uma receita COPY/INSERT no diretório de build.
Nenhum asset EA é adicionado ao Git. O cache privado usa LocalAppData, por digest
da receita. O VFS encaminha a árvore DATA original e substitui somente o child
ui.AST; o original em game/DATA permanece byte a byte intacto.

A classe/export ScrGraphicsOpt deriva do template de Toggles existente. Cinco
opções por página e um Toggle PAGE expõem11opções em3páginas. A ponte usa somente
chamadas PauseGame com3argumentos e sentinel próprio, por midasm822A0F38 no
manifesto. Chamadas normais seguem a função original; não há fatal novo.

## Teste D3D12 real

`graphics_native_apt_full` terminou com exit0 e PERSISTENCE PASS. Execução separada
de performance, na RTX2060, janela1280×720. Evidência local:

- logs/bench_graphics_native_apt_full.log
- logs/graphics_native_apt_full_progress.log
- logs/graphics_native_apt_full_launcher.ini
- logs/bench_graphics_native_apt_full_native_entry.png
- logs/bench_graphics_native_apt_full_native_graphics.png
- logs/bench_graphics_native_apt_full_native_rt.png
- logs/bench_graphics_native_apt_full_native_dlss_1.png até _4.png
- capturas native_api_restart, native_resolution, native_fps_60, native_fps_120,
  native_msaa, native_scale e native_final_quality da mesma rodada.

A abriu a entrada; B retornou à mesma pausa preservando a seleção; Start retomou.
Pedidos foram verificados no callback real por row/value/accepted. Sombras Low/
High, personagens e reflexos executaram com contadores positivos e fences. Os
quatro modos NGX Quality/Balanced/Performance/DLAA publicaram avaliação, conclusão
e composição, sem rejeições na sequência observada. Resolução1600×900 e retorno
1280×720, FPS30→60→120→60, FXAA, MSAA4x e escala125%→100% responderam. O pedido
Vulkan ficou marcado para reinício e foi restaurado para D3D12 sem recriar API.

Após fechar normalmente, o mesmo INI do launcher guardou DLSSQuality, D3D12,
1280×720, FPS60, FXAA, MSAA4 e escala100, preservando comentário/chave desconhecida.
MSAA efetivo permanece1 com DLSS, mas a preferência4 é retida para DLSSOff.
O teste ocorreu antes da adição visual de `(N/A)` para capacidades indisponíveis;
essa alteração posterior não muda o backend.

## Vulkan e teclado

Vulkan abriu a tela nativa e voltou à pausa. Também foi aberto o menu Gameplay
original e reaberta a tela gráfica no mesmo processo, exercitando exports/classes
independentes. logs/bench_graphics_native_apt_vulkan_keyboard.log e capturas
native_original_gameplay/native_keyboard documentam esse percurso parcial.
Não declarar a rodada inteira aprovada: a etapa final de teclado não terminou.
RT/NGX/FXAA/MSAA/escala de cena são marcados N/A no Vulkan quando não implementados;
API requer reinício, e resolução final/FPS usam o caminho próprio disponível.

A automação de teclado por Computer Use revelou pulsos Return com down/up no
mesmo milissegundo. O MnK recebia os eventos, mas nenhum botão era publicado
entre polls. O design complementar graphics-milestone-5-keyboard-edge-design-
2026-11-05.md especifica retenção de botões resolvidos por um poll, sem overlay
nem modificação da duração da VM. A validação final por teclado depende desse
patch e não pode ser substituída por um shim de atraso Frida.

### Verificações adicionais de teclado

O overlaySDK0004 passou8regressõesCPU e reconstrução canônica sobre0001/0002.
Execuções graphics_native_keyboard_edges e edges2 confirmaram Return/Start,
Space/A e pausa real state4 por toques curtos, sem alterar sua duração.
O helper Computer Use envia Shift+Down como VK0x62(NumPad2), observado por REA
ev_08b9055979996da077efa96b2966c656e75236f9a39980695488c0880668e94a;
essa ação não comprova o binding padrão Shift+Down. O teste subsequente usa
bindings explícitos J/U/K/H somente em sua linha de comando isolada.

Ao fechar edges2 foi observado deadlock entre QueueMouseCaptureUpdate e
OnLostFocus durante notificação SDL. Evidência de stacks:
ev_9ca1ed4357b33784b28b49ca4f1f8cf6d8e6484e323f3678cd20573a6ecdc470.
A correção libera state_mutex antes de notificar SDL. O teste extraído do método
real reproduziu retenção do mutex no baseline e ausência dela após correção:
logs/graphics_keyboard_queue_regression.log. Patch0004/hashes regenerados e
setup_memory_tracking_sdk -SkipDependencies confirmou a cadeia inteira.
O processo pendurado próprio foi encerrado; essa rodada não prova fechamento.

A rodada graphics_native_keyboard_final passou o percurso completo via Computer
Use: Return→Space→pausa state4→J até Graphics→Space→PAGE2→FPS30→60 via H→Backspace→
pausa com seleção preservada→Return→gameplay com HUD/minimap. Log confirmou
row7/value2/accepted1 e screen open/close com overlay=false. O INI terminou
fps_limit=60, graphics_api=vulkan, preservando comentário e chave desconhecida.
Alt+F4 terminou normalmente às22:04:39, com shutdown/hard-exit em201ms, sem
deadlock. Hash executável c7900ff807e040b67943b199af8276ce497b1b712c7c108f4be61d600e893ee2.
Essa rodada antecede somente o ajuste visual de setas/N/A descrito em
graphics-milestone-5-native-layout-design-2026-11-05.md. A nova regressão de assets
falhou com790 e passou com200; seis testes de transformação passaram.

O layout corrigido também foi observado no D3D12 durante gameplay real, com
DLSSQuality: `logs/graphics_final_gameplay_indexed/native_pause_entry.png` e
`native_graphics_layout.png`. Ambas as setas ficaram junto da coluna de valor
dentro do painel; o nome Character Shadows não encostou no valor. Execução
7a2e08015a2585d6b49c422435a2688a5e3684fddbbb737b78ef0152b29ca243,
fechamento normal em201ms, INI manteve Quality/D3D12 e as chaves de teste.

## Limites de evidência

Screenshots da janela do jogo são obrigatórios; capturas de desktop ocluído
que mostrem outro aplicativo são descartadas. Uma rodada Vulkan de benchmark
foi encerrada por fechamento de janela antes de produzir JSON e está inválida;
exit0 de encerramento normal não representa resultado de performance válido.
A matriz final deve ter três resultados completos por configuração e um único
hash por grupo. Pacing físico60Hz continua pendente no monitor144Hz disponível.
