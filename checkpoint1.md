# Diagnosticar cena preta no renderer

> Preto com som: o jogo roda, mas nada chega à tela. Vou ver no log o caminho de apresentação (resolve, swap, present) e o que o renderer faz a cada frame.
> Há um recurso de dump de frame. Vou usá-lo: dumpo todos os surfaces e a saída final aos 45 s para ver se o que chega à apresentação está preto ou é a apresentação que falha.
> As superfícies do frame estão todas zeradas, com a única textura não-zero sendo uma constante `(0,0,30,1)` — nenhuma cena foi realmente composta. Vou ativar o log de draws/resolves por alguns frames para ver o que o jogo está tentando desenhar.
> O frame mostra uma cena 3D completa, mas só \~5% dos draws são emitidos — o resto falha por shader fora da biblioteca ou `PSO creation failed`, explicando a tela preta. Vou investigar o erro de PSO verificando como os tipos da declaração estão sendo mapeados.
> Encontrei o problema: o tipo de elemento `0x2A2187` (2_10_10_10 empacotado) não está mapeado e acaba descartado, deixando o VS sem NORMAL/TANGENT e quebrando o PSO. Vou verificar como o HLSL gerado declara essas entradas antes de ajustar o mapeamento.
> O HLSL já espera exatamente isso: "DEC3N … IA binds as R32_UINT". Adiciono o mapeamento.
> PSOs com falha caíram de 35 para 2 e os draws emitidos de \~12 mil para \~657 mil (a \~28 FPS). Agora vejo se o que aparece na tela mudou com o frame dump.
> As superfícies HDR continuam zeradas mesmo com \~657 mil draws emitidos, indicando que os draws não estão escrevendo (ou escrevem zero). Vou inspecionar no trace o estado de um draw da cena: constantes de VS, viewport e depth.
> Status: comparando as constantes do espelho PM4 com o shadow do device (valida o layout) num draw da cena.
> As constantes parecem corretas (c0 do espelho e do shadow coincidem no draw 160000, confirmando 0x700). Agora vou testar por bisseção com as opções de debug para identificar o que zera a cena: profundidade e winding.
> Vou adicionar uma opção de debug que dumpa os surfaces antes do N-ésimo resolve do frame, para ver o conteúdo da cena (surface `f3`) antes de ser sobrescrito pelo pass seguinte.
> Achei interessante: a cena fica uniforme `(0,0,30,1)` entre os draws 760 e 900, parecendo um fundo/céu em HDR que a geometria posterior não altera. Vou testar sem teste de profundidade para ver se essa geometria passa a aparecer.
> Meu patch de texto cortou um trecho a mais. Restauro a parte perdida (comentário, `CreationRecord`, globais e o início de `RecordCreation`).
> Cerca de 65% dos shaders chegam pela rota de cópia de objeto sem registro de criação, cujo hash é instável. Vou localizar a função do XDK que cria o objeto a partir do container pronto para capturá-lo pristino também nessa rota.
> Encontrei `sub_820F5148`, que parece criar o objeto final a partir do container finalizado, copiando-o integralmente. Vou hookar a entrada dessa função para capturar o container pristino de todos os shaders e confirmar a hipótese.
> `sub_820F5148` é o criador de **VS** (magic `102A1101`, container em `obj+872`). Falta o equivalente para PS (container em `obj+40`). Procuro a função irmã.
> Vou corrigir os endereços do perfil para `820F5148` (VS) e `820F4D90` (PS), já que o checkpoint apontava incorretamente para o serializador interno (`820F5840/820F6690`), e simplificar o registro.
> Agora o perfil: apontar os dois papéis para os criadores reais e atualizar os comentários, além de ajustar o cabeçalho do arquivo.
> Build e testes ok. Capturando o corpus com a chave estável (containers pristinos na criação).
> Run 2 não acrescentou nenhum hash novo (183 e 183): a chave agora é estável. Construindo o corpus a partir dela.
> O catálogo novo tem 223 containers únicos e 221 compilam (os "failures" anteriores eram variantes patchadas instáveis). Testando no jogo.
> Faltam só 3 shaders (antes eram 60), mas agora aparecem 245 falhas de PSO. Vejo o motivo.
> Todos os layouts que falham têm `BINORMAL0`, que o VS não declara. Vou filtrar o input layout pela assinatura de entrada do próprio DXIL (chunk `ISG1`).
> Existe `--d3d12_debug` para trazer as mensagens da camada de debug do D3D12 para o log. Uso para ver o motivo exato do `E_INVALIDARG`.
> Sem o pixel shader o PSO é criado: o DXIL do PS é rejeitado. Vou checar se os DXILs novos estão assinados (chunk `HASH` zerado faz o D3D12 recusar com `E_INVALIDARG`).
> Encontrei a causa: o DXIL gera `double` a partir de ternários com literais no HLSL, e a GPU Intel integrada não suporta FP64, fazendo o D3D12 rejeitar o PSO. Vou localizar de onde isso vem no HLSL gerado.
> Nenhum DXIL usa `double` agora (0 de 221). Testando no jogo.
> Agora 98% dos draws são emitidos e a saída final tem 99% dos pixels não-zero. Vejo a imagem.
> A tela de título do jogo renderiza nativamente: logo, "SUPERMAN RETURNS", "PRESS START". Agora limpo os diagnósticos temporários, e rebuildo e testo.

<details><summary>11 previous messages</summary>

> Vou conferir o estado do código e os diagnósticos temporários, preservar as correções de renderização e concluir a limpeza com build e testes.
>
> <details><summary>Ran 14 commands</summary>
>
> - Ran `git status --short`
> - Ran `rg --files -g AGENTS.md -g '*CHECKPOINT*' -g '*checkpoint*' -g '*README*' -g CMakeLists.txt -g CMakePresets.json`
> - Ran `git log -6 --oneline`
> - Ran `Get-Content README.md -TotalCount 180`
> - Ran `Get-Content CHECKPOINT_NATIVE_RENDERER.md -Tail 160`
> - Ran `Get-ChildItem -Force | Select-Object Name,Mode`
> - Ran `rg --files tests tools | rg 'native|shader|test_'`
> - Ran `Get-Content build.cmd -TotalCount 160`
> - Ran `Get-ChildItem logs -File | Sort-Object LastWriteTime -Descending | Select-Object -First 20 Name,LastWriteTime,Length`
> - Ran `rg -n '2A2187|820F5148|820F4D90|ISG1|debug|dump_before|disable_depth|disable_cull' port/src/native_renderer tools/shaders/xenosrecomp`
> - Ran `git show --stat HEAD`
> - Ran `Get-ChildItem logs -Directory | Select-Object Name; Get-ChildItem build -Directory | Select-Object Name`
> - Ran `Get-Content port/src/native_renderer/native_renderer.cpp -TotalCount 240`
> - Ran `Get-Content logs/build_fix.log -Tail 30; Get-Content tests/native/CMakeLists.txt; Get-Content tools/native_validate.ps1 -TotalCount 110`
>
> </details>
>
> As correções de shaders e layout já estão no último commit. Porém, o log do build após a limpeza termina com três erros de compilação; vou verificar essas referências antes de validar o executável.
>
> <details><summary>Ran 4 commands</summary>
>
> - Ran `Get-Content tools/bench.ps1 -TotalCount 150`
> - Ran `Get-Command cmake,python -ErrorAction SilentlyContinue | Select-Object Name,Source; Get-ChildItem build/tests-native -File | Select-Object Name`
> - Ran `git show HEAD -- port/src/native_renderer/native_renderer.cpp`
> - Ran `Select-String -Path logs/build_fix.log -Pattern 'error:' -Context 3,3`
>
> </details>
>
> A limpeza deixou declarações duplicadas no tratamento de falha de PSO e removeu o retorno de erro e o cálculo do tempo de criação. Vou restaurar esse fluxo e retirar o log detalhado de cada PSO criado.
>
> <details><summary>Edited a file, ran 9 commands</summary>
>
> - Wrote `./port/src/native_renderer/native_renderer.cpp` (+11 -10)
> - Ran `.\build.cmd`
> - Ran `python -m pytest tests/tools/test_game_profile.py tests/tools/test_shader_tools.py -q`
> - Ran `cmake --build build/tests-native`
> - Ran `rg -n 'bench.*(duration|seconds|quit)|exit_after|shutdown_after' port/src .tools/rexglue-sdk-source/src -g '*.cpp' -g '*.h'`
> - Ran `Get-ChildItem logs/var_n1,logs/var_n2,logs/var_n3 -File | Select-Object DirectoryName,Name,Length; Get-Content logs/game_ok1.log -Tail 15`
> - Ran `ctest --test-dir build/tests-native --output-on-failure`
> - Ran `rg -n 'bench_exit_after_s|bench_screenshot|bench.*elapsed' port/src tools -g '*.cpp' -g '*.h' -g '*.ps1'`
> - Ran `rg --files tools | rg 'dump|smoke'; Get-Process superman_returns -ErrorAction SilentlyContinue | Select-Object Id,Path; git diff --check`
> - Ran `Get-Content port/src/native_renderer/sdk_compat.h -TotalCount 140; rg -n 'bench|elapsed|SR_LOG_FPS' port/src/superman_returns*`
>
> </details>
>
> O build passou, assim como os seis testes de ferramentas e a suíte nativa. Vou rodar o executável recompilado por cerca de um minuto e capturar a saída aos 45 segundos para conferir se a tela de título continua aparecendo.
>
> <details><summary>Ran 6 commands</summary>
>
> - Ran `$taskRoot = (Get-Location).Path
> $taskExe = Join-Path $taskRoot 'port/out/build/win-amd64-release/superman_returns.exe'
> $taskDump = Join-Path $taskRoot 'logs/cleanup_verify'
> $taskLog = Join-Path $taskDump 'game.log'
> New-Item -ItemType Directory -Force -Path $taskDump | Out-Null
> $taskArgs = @('--game_data_root="' + (Join-Path $taskRoot 'game') + '"', '--log_file="' + $taskLog + '"', '--sr_renderer=native', '--sr_skip_intro=true', '--sr_native_dump_frame_at_s=45', '--sr_native_dump_dir="' + $taskDump + '"')
> $taskProc = Start-Process -FilePath $taskExe -ArgumentList $taskArgs -WorkingDirectory (Split-Path $taskExe) -WindowStyle Hidden -PassThru
> try {
>   if (-not $taskProc.WaitForExit(60000)) {
>     $taskProc.CloseMainWindow() | Out-Null
>     if (-not $taskProc.WaitForExit(5000)) { Stop-Process -Id $taskProc.Id }
>   }
>   Write-Output "Verification process $($taskProc.Id) ended"
> } finally {
>   if (-not $taskProc.HasExited) { Stop-Process -Id $taskProc.Id }
> }`
> - Ran `git diff --stat; git diff -- port/superman_returns_manifest.toml; Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 5140 -First 42`
> - Ran `Get-Content logs/cleanup_verify/game.log -Tail 10; Get-Content logs/cleanup_verify/game.log -TotalCount 20`
> - Ran `python -c "import numpy; import PIL; print('Frame analysis dependencies available')"`
> - Ran `git diff -- port/src/native_renderer/native_renderer.cpp; Select-String -Path logs/cleanup_verify/game.log -Pattern 'PSO creation failed|unsupported.*shader|dumped' | Select-Object -Last 8`
> - Ran `Get-ChildItem logs/cleanup_verify -Filter '*output*' | Select-Object Name,Length; Get-Content logs/cleanup_verify/game.log -Tail 12`
>
> </details>
>
> A execução está usando o renderer nativo, com cerca de 98% dos draws emitidos. Ainda há falhas pontuais de PSO no log; vou conferir o dump final para validar o resultado visual após a limpeza.
>
> <details><summary>Ran 4 commands</summary>
>
> - Ran `Get-ChildItem logs/cleanup_verify -Filter '*output*' | Select-Object Name,Length; Select-String -Path logs/cleanup_verify/game.log -Pattern 'dumped|frame 1200|missing from the pre-shader' | Select-Object -Last 6`
> - Ran `python -c "from pathlib import Path; import struct; import numpy as np; from PIL import Image; p=Path('logs/cleanup_verify/output_1280x720.raw'); b=p.read_bytes(); w,h,f,pitch=struct.unpack('<4I',b[:16]); print('frame header:',w,h,f,pitch); a=np.frombuffer(b, dtype=np.uint8, offset=16).reshape(h,pitch)[:,:w*4].reshape(h,w,4).copy(); print('nonzero RGB pixels:', round(100*np.any(a[:,:,:3],axis=2).mean(),2)); a=a[:,:,[2,1,0,3]] if f in (87,91) else a; Image.fromarray(a).save(p.with_suffix('.png')); print(p.with_suffix('.png').resolve())"`
> - Ran `git diff --check; git status --short`
> - Ran `python -c "from pathlib import Path; import struct; import numpy as np; from PIL import Image; p=Path('logs/cleanup_verify/output_1280x720.raw'); b=p.read_bytes(); w,h,f,pitch=struct.unpack('<4I',b[:16]); assert f==24; rows=np.frombuffer(b,dtype=np.uint8,offset=16).reshape(h,pitch); v=rows[:,:w*4].copy().view('<u4').reshape(h,w); a=np.stack([((v >> s) & 1023) for s in (0,10,20)],axis=-1); print('nonzero RGB pixels:',round(100*np.any(a,axis=2).mean(),2)); Image.fromarray((a.astype(np.float32)*255/1023).round().astype(np.uint8)).save(p.with_suffix('.png'))"`
>
> </details>
>
> Viewed an image
</details>

Corrigi o trecho quebrado na limpeza dos logs de PSO. Build concluído, seis testes de ferramentas e suíte nativa passando.

O dump aos 45 s confirma a tela de título no renderer nativo, com 99,03% dos pixels não-zero. Ainda há falhas pontuais de PSO; gameplay não foi validado.

![Tela de título renderizada](./logs/cleanup_verify/output_1280x720.png)

> Agora é a gameplay

<details><summary>30 previous messages</summary>

> Vou avançar da tela de título até o jogo e capturar a gameplay com o renderer nativo, conferindo a imagem e as falhas no log.
>
> <details><summary>Ran 7 commands</summary>
>
> - Ran `Get-Content tools/bench.ps1 | Select-Object -Skip 140 -First 150`
> - Ran `Get-Process superman_returns -ErrorAction SilentlyContinue | Select-Object Id,Path; rg -n 'screenshot|dump_swap|ab_swaps|sr_skip_intro' tools/native_validate.ps1 port/src/superman_returns_app.cpp port/src/superman_returns_app.h`
> - Ran `powershell -NoProfile -ExecutionPolicy Bypass -File tools/bench.ps1 -Name native_gameplay_verify -ExtraArgs '--sr_renderer=native' -TitleTimeout 60 -Settle 25 -Window 12 -WorldTimeout 120`
> - Ran `Get-Content port/src/native_renderer/shader_registry.cpp -TotalCount 200; Get-Content tools/shaders/build_corpus.ps1 -TotalCount 130`
> - Ran `Get-Content logs/bench_progress.txt -Tail 8; Get-Content logs/game.log -Tail 8`
> - Ran `Get-Content logs/bench_progress.txt -Tail 4; Get-Content logs/game.log -Tail 5`
> - Ran `Add-Type -AssemblyName System.Windows.Forms,System.Drawing
> $taskBounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
> $taskImage = New-Object System.Drawing.Bitmap $taskBounds.Width,$taskBounds.Height
> $taskGraphics = [System.Drawing.Graphics]::FromImage($taskImage)
> try { $taskGraphics.CopyFromScreen($taskBounds.Location,[System.Drawing.Point]::Empty,$taskBounds.Size); $taskImage.Save((Join-Path (Get-Location) 'logs/gameplay_progress.png')) } finally { $taskGraphics.Dispose(); $taskImage.Dispose() }`
>
> </details>
>
> Viewed an image
>
> O jogo já chegou ao HUD da gameplay, mas a cena está azul e não há geometria visível. Vou capturar os shaders e as superfícies dessa etapa para descobrir onde a composição do mundo está falhando.
>
> <details><summary>Ran 4 commands</summary>
>
> - Ran `Get-Content logs/game.log -Tail 18; Get-Content logs/bench_progress.txt -Tail 6; rg -n 'PSO creation failed|unsupported shader|watchdog|fatal' logs/game.log | Select-Object -Last 15`
> - Ran `rg -n 'dump|trace' port/src/native_renderer/native_renderer.cpp | Select-Object -First 65; rg -n 'd3d12_debug' .tools/rexglue-sdk-source/src/graphics/d3d12 -g '*.cpp'`
> - Ran `$taskProc = Get-Process -Id 29856 -ErrorAction SilentlyContinue
> if ($taskProc) { $taskProc.CloseMainWindow() | Out-Null; if (-not $taskProc.WaitForExit(5000)) { Stop-Process -Id $taskProc.Id } }`
> - Ran `powershell -NoProfile -ExecutionPolicy Bypass -File tools/bench.ps1 -Name native_gameplay_diag -ExtraArgs '--sr_renderer=native --d3d12_debug=true --sr_native_dump_shader_dir=logs/gameplay_shaders --sr_native_dump_frame_at_s=60 --sr_native_trace_frame_at_s=60 --sr_native_dump_dir=logs/gameplay_dump' -TitleTimeout 60 -Settle 10 -Window 8 -WorldTimeout 75`
>
> </details>
>
> Viewed an image
>
> <details><summary>Ran 6 commands</summary>
>
> - Ran `Get-Content logs/bench_progress.txt -Tail 8; Select-String logs/game.log -Pattern 'D3D12.*(error|ERROR)|D3D12 debug|PSO creation failed' | Select-Object -Last 10`
> - Ran `Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 2180 -First 68; Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 3320 -First 100`
> - Ran `Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 690 -First 74; Get-Content port/src/native_renderer/native_graphics_system.cpp -TotalCount 165`
> - Ran `Get-ChildItem logs/gameplay_shaders -File -ErrorAction SilentlyContinue | Measure-Object; Get-Content logs/bench_progress.txt -Tail 3; rg -n 'trace|D3D12' logs/game.log | Select-Object -Last 8`
> - Ran `Get-ChildItem port/out/build/win-amd64-release/logs -Directory -ErrorAction SilentlyContinue | Select-Object Name; Get-Content logs/bench_progress.txt -Tail 4; Get-Content logs/game.log -Tail 4; Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 3040 -First 105`
> - Ran `Get-ChildItem port/out/build/win-amd64-release/logs/gameplay_shaders -File | Measure-Object; Get-ChildItem port/out/build/win-amd64-release/logs/gameplay_dump -File -ErrorAction SilentlyContinue | Select-Object -First 4 Name; Get-Content logs/bench_progress.txt -Tail 5`
>
> </details>
>
> Viewed an image
>
> <details><summary>Ran a command</summary>
>
> - Ran `rg -n 'vs c160|float4 main|g_ScreenXform|SV_Position' artifacts/shaders/hlsl -g '*.hlsl' | Select-Object -First 8; Get-ChildItem artifacts/shaders -Directory | Select-Object Name`
>
> </details>
>
> O HUD e o texto do tutorial aparecem, mas o mundo continua oculto pelo azul. Os draws estão sendo emitidos; vou testar a profundidade e inspecionar um frame completo para separar uma falha de geometria de uma falha na composição final.
>
> <details><summary>Ran 9 commands</summary>
>
> - Ran `powershell -NoProfile -ExecutionPolicy Bypass -File tools/bench.ps1 -Name native_gameplay_noz -ExtraArgs '--sr_renderer=native --sr_native_debug_no_ztest=true --sr_native_dump_frame_at_s=40 --sr_native_trace_frame_at_s=40 --sr_native_dump_dir=logs/gameplay_noz_dump' -Settle 10 -Window 12 -WorldTimeout 75`
> - Ran `Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 3240 -First 90; rg -n 'proj|projection|Conan|reversed' port/src/native_renderer/game_profile.h tools/shaders/xenosrecomp/corpus_main.cpp`
> - Ran `Get-Content tools/shaders/gen_projection_regs.py -TotalCount 170; Get-Content tools/shaders/xenosrecomp/patches/0002-superman-returns.patch -TotalCount 100`
> - Ran `Get-Content artifacts/shaders/hlsl/C63F6F22679609F0.vs.hlsl | Select-Object -Last 65; Get-Content port/src/native_renderer/pm4_mirror.cpp -TotalCount 230`
> - Ran `Get-Content port/src/native_renderer/game_profile.h | Select-Object -Skip 190 -First 54; Get-Content logs/bench_progress.txt -Tail 3`
> - Ran `rg -n 'LoadReg\(|ExecDraw|ScanRing|ring_last_|follow_indirect' port/src/native_renderer/native_renderer.cpp | Select-Object -First 70; Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 3500 -First 130; Get-Content artifacts/shaders/hlsl/C63F6F22679609F0.vs.hlsl | Select-Object -Skip 435 -First 85`
> - Ran `Get-Content logs/bench_progress.txt -Tail 4; Get-ChildItem port/out/build/win-amd64-release/logs/gameplay_noz_dump -File -ErrorAction SilentlyContinue | Select-Object -First 3 Name; Get-Content logs/game.log -Tail 5; Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 310 -First 35`
> - Ran `Get-ChildItem logs/gameplay_noz_dump -File -ErrorAction SilentlyContinue | Select-Object -First 5 Name; Get-Content logs/game.log -Tail 6; Get-Content logs/bench_progress.txt -Tail 4; Get-ChildItem port/out/build/win-amd64-release/logs/gameplay_noz_dump -File -ErrorAction SilentlyContinue | Select-Object -First 5 Name`
> - Ran `Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 4920 -First 65; Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 5060 -First 42; rg -n 'trace.*(frame|dump)|dumped|unknown.*sr_native|Unable.*dir' logs/game.log | Select-Object -Last 12`
>
> </details>
>
> Viewed an image
>
> <details><summary>Ran a command</summary>
>
> - Ran `rg 'trace draw' logs/game.log | Select-Object -First 8; rg 'trace.*vp' logs/game.log | Select-Object -First 5; rg 'trace p0.*bind RT|trace.*resolve' logs/game.log | Select-Object -Last 25`
>
> </details>
>
> Sem o teste de profundidade, Superman e parte da cidade aparecem. O trace mostra um problema concreto: o viewport chega ao D3D12 com profundidade `0..0`, comprimindo todos os draws no mesmo valor. Vou conferir o layout do viewport no XDK e corrigir essa leitura.
>
> <details><summary>Ran 10 commands</summary>
>
> - Ran `rg -n 'PPC_FUNC_IMPL\(__imp__sub_82100310\)|PPC_FUNC_IMPL\(__imp__sub_82102608\)|PPC_FUNC_IMPL\(__imp__sub_820F.*' port/generated/default -g '*.cpp' | Select-Object -First 3; rg -n 'sub_82100310|sub_82102608|sub_82112050' port/generated/default -g '*.cpp' | Select-Object -First 12; rg -n 'viewport|SetViewport' port/src/render_scale.cpp port/src/native_renderer/game_profile.h`
> - Ran `rg -n 'DEFINE_REX_FUNC\(sub_82100310\)|DEFINE_REX_FUNC\(sub_82102608\)' port/generated/default -g '*.cpp'; Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 3393 -First 85`
> - Ran `Get-Content port/generated/default/superman_returns_recomp.40.cpp | Select-Object -Skip 110 -First 270; Get-Content port/generated/default/superman_returns_recomp.67.cpp | Select-Object -Skip 130 -First 150`
> - Ran `rg -n '12432|12436|12384|12388|12392|12396|12400|12404|12408|12412|12416|12420|12424|12428' port/generated/default -g '*.cpp' | Select-Object -First 65`
> - Ran `rg -n 'viewport|Viewport|12432' docs tools tests -g '*.md' -g '*.py' -g '*.cpp' | Select-Object -First 20`
> - Ran `Get-Content port/generated/default/superman_returns_recomp.126.cpp | Select-Object -Skip 300 -First 120; Get-Content port/generated/default/superman_returns_recomp.116.cpp | Select-Object -Skip 270 -First 104; Get-Content port/generated/default/superman_returns_recomp.105.cpp | Select-Object -Skip 23820 -First 55; rg -n '(addi r[0-9]+,r[0-9]+,12[34][0-9]{2}|stfs f[0-9]+,12[34][0-9]{2})' port/generated/default -g '*.cpp' | Select-Object -First 65`
> - Ran `Get-Content port/generated/default/superman_returns_recomp.27.cpp | Select-Object -Skip 108 -First 210; Get-Content port/generated/default/superman_returns_recomp.2.cpp -TotalCount 100; Get-Content tests/native/test_game_profile.cpp | Select-Object -Skip 42 -First 50`
> - Ran `rg -n '12376|12380|12384|12388|12392|12396' port/generated/default/superman_returns_recomp.*.cpp | Select-Object -First 65; Get-Content port/generated/default/superman_returns_recomp.133.cpp | Select-Object -Skip 8595 -First 70; rg -n 'DEFINE_REX_FUNC\(sub_820F2' port/generated/default/superman_returns_recomp.2.cpp`
> - Ran `rg -n '12376|12380|12384|12388|12392|12396' port/generated/default -g 'superman_returns_recomp.*.cpp' | Select-Object -First 55; rg -n 'DEFINE_REX_FUNC\(sub_820F2CA0\)|DEFINE_REX_FUNC\(sub_820F2810\)' port/generated/default -g '*.cpp'`
> - Ran `Get-Content port/generated/default/superman_returns_recomp.118.cpp -TotalCount 280; Get-Content port/generated/default/superman_returns_recomp.135.cpp -TotalCount 155; Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 5190 -First 140`
>
> </details>
>
> Confirmei no código recompilado: o viewport fica em `device+0x3058`, com X, Y, largura e altura como inteiros e MinZ/MaxZ como floats. O renderer lia `0x3090` e tratava tudo como float. Vou corrigir o offset e as leituras usadas pelo viewport e pelo ajuste de meio pixel.
>
> <details><summary>Edited 3 files, ran 6 commands</summary>
>
> - Wrote `./port/src/native_renderer/game_profile.h` (+1 -1)
> - Wrote `./port/src/native_renderer/native_renderer.cpp` (+7 -8)
> - Wrote `./tests/native/test_game_profile.cpp` (+2 -1)
> - Ran `rg -n 'LoadF32.*kDevViewport|Load32.*kDevViewport' port/src/native_renderer/native_renderer.cpp; .\build.cmd`
> - Ran `python -m pytest tests/tools/test_game_profile.py tests/tools/test_shader_tools.py -q`
> - Ran `rg -n 'save|Save|Write|ofstream' port/src/native_renderer/pipeline_cache.cpp; Get-ChildItem port/out/build/win-amd64-release -Filter '*pipeline*' | Select-Object Name,LastWriteTime,Length; Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 4860 -First 60`
> - Ran `cmake --build build/tests-native; ctest --test-dir build/tests-native --output-on-failure`
> - Ran `$taskCache = Join-Path (Get-Location) 'port/out/build/win-amd64-release/superman_returns_pipelines.bin'
> if (Test-Path -LiteralPath $taskCache) { Move-Item -LiteralPath $taskCache -Destination (Join-Path (Get-Location) 'logs/pipelines_before_viewport_fix.bin') }
> New-Item -ItemType Directory -Force -Path 'port/out/build/win-amd64-release/logs/gameplay_fixed_dump' | Out-Null
> powershell -NoProfile -ExecutionPolicy Bypass -File tools/bench.ps1 -Name native_gameplay_fixed -ExtraArgs '--sr_renderer=native --sr_native_dump_frame_at_s=40 --sr_native_trace_frame_at_s=40 --sr_native_dump_dir=logs/gameplay_fixed_dump' -Settle 10 -Window 12 -WorldTimeout 75`
> - Ran `Get-Content port/src/native_renderer/pipeline_cache.cpp | Select-Object -Skip 144 -First 72; Get-Content logs/bench_progress.txt -Tail 3; Get-Content logs/game.log -Tail 3; git diff --stat`
>
> </details>
>
> A correção compilou e os testes passaram. Estou repetindo a gameplay com o teste de profundidade ligado e um cache de pipelines novo, para validar o resultado sem a opção de debug.
>
> <details><summary>Ran 2 commands</summary>
>
> - Ran `Get-Content logs/bench_progress.txt -Tail 4; Get-Content logs/game.log -Tail 5; Get-ChildItem port/out/build/win-amd64-release/logs/gameplay_fixed_dump -File | Select-Object -First 5 Name`
> - Ran `Add-Type -AssemblyName System.Windows.Forms,System.Drawing
> $taskBounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
> $taskImage = New-Object System.Drawing.Bitmap $taskBounds.Width,$taskBounds.Height
> $taskGraphics = [System.Drawing.Graphics]::FromImage($taskImage)
> try { $taskGraphics.CopyFromScreen($taskBounds.Location,[System.Drawing.Point]::Empty,$taskBounds.Size); $taskImage.Save((Join-Path (Get-Location) 'logs/gameplay_fixed_progress.png')) } finally { $taskGraphics.Dispose(); $taskImage.Dispose() }`
>
> </details>
>
> Viewed an image
>
> <details><summary>Ran 11 commands</summary>
>
> - Ran `Get-Content logs/bench_progress.txt -Tail 3; rg 'trace   vp' logs/game.log | Select-Object -First 2`
> - Ran `Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 2330 -First 65; Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 4750 -First 66; rg 'trace.*(clear|resolve flags|bind RT)' logs/game.log | Select-Object -First 25; Get-ChildItem port/out/build/win-amd64-release/logs/gameplay_fixed_dump -Filter '*depth*' | Select-Object Name,Length`
> - Ran `python -c "from pathlib import Path; import struct; import numpy as np; p=Path('port/out/build/win-amd64-release/logs/gameplay_fixed_dump'); [(print(f.name,struct.unpack('<4I',f.read_bytes()[:16]),np.unique(np.frombuffer(f.read_bytes()[16:],dtype='<u4'),return_counts=True)[0][:8])) for f in p.glob('*depth.raw')]; print('surfaces:',[f.name for f in p.glob('surf*color.raw')])"`
> - Ran `rg 'trace.*(resolve flags|clear)' logs/game.log | Select-Object -Last 24; rg -n 'clear_z|BeginTiling|ExecClear|depth_clear' port/src/native_renderer/native_renderer.cpp | Select-Object -First 45; rg -n 'zfunc|z_enable' .tools/rexglue-sdk/win-amd64/include/rex/graphics/registers.h | Select-Object -First 10`
> - Ran `Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 1690 -First 150; Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 5540 -First 73; Get-Content port/src/native_renderer/native_hooks.cpp | Select-Object -Skip 40 -First 210; Get-Content port/src/native_renderer/game_profile.h | Select-Object -Skip 58 -First 24`
> - Ran `rg -n 'DEFINE_REX_FUNC\(sub_82101998\)' port/generated/default -g '*.cpp'; rg 'D3DDevice_Clear' logs/game.log | Select-Object -First 16; rg -n 'ClearDepthStencilView' port/src/native_renderer/native_renderer.cpp`
> - Ran `Get-Content port/generated/default/superman_returns_recomp.84.cpp | Select-Object -Skip 130 -First 160; Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 1898 -First 104; Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 1398 -First 24; Get-Content port/generated/default/superman_returns_recomp.27.cpp | Select-Object -Skip 68 -First 42`
> - Ran `rg -n 'DEFINE_REX_FUNC\(sub_8210C5F8\)' port/generated/default -g '*.cpp'; rg -n 'kClearZBuffer|kClearStencil|flags.*0x' port/src/native_renderer/native_renderer.cpp | Select-Object -Last 20; Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 4506 -First 90; rg -n 'RESOLVE.*CLEAR|ResolveFlags|D3DRESOLVE' .tools -g '*.h' -g '*.cpp' -g '*.md' --max-count 3 | Select-Object -First 15`
> - Ran `Get-Content port/generated/default/superman_returns_recomp.97.cpp | Select-Object -Skip 187 -First 270`
> - Ran `rg -n 'sub_82101A58\(ctx|sub_821013D0\(ctx|sub_82101998\(ctx' port/generated/default -g '*.cpp' | Select-Object -First 25; rg -n 'DEFINE_REX_FUNC\(sub_82101A58\)' port/generated/default -g '*.cpp'; rg 'trace draw' logs/game.log | Select-Object -Last 3; rg -n 'depth.*Clear|copy.*clear|rb_copy_control' .tools/rexglue-sdk-source/src/graphics -g '*.cpp' | Select-Object -First 15`
> - Ran `Get-Content port/generated/default/superman_returns_recomp.28.cpp | Select-Object -Skip 128 -First 125; Get-Content port/src/native_renderer/native_renderer.h | Select-Object -Skip 120 -First 62; rg -n 'void Renderer::Clear|ExecClear\(' port/src/native_renderer/native_renderer.cpp port/src/native_renderer/native_renderer.h; Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 5518 -First 24; Get-Content port/generated/default/superman_returns_recomp.99.cpp | Select-Object -Skip 21244 -First 76`
>
> </details>
>
> O viewport agora está correto (`0..1`), mas a profundidade ainda fica zerada. Encontrei outra diferença do XDK: o jogo chama diretamente a rotina de clear com cor em float4, enquanto o hook estava na versão que recebe cor empacotada. Vou interceptar a rotina usada pelo jogo para capturar também os clears de profundidade.
>
> <details><summary>Edited 4 files, ran 10 commands</summary>
>
> - Wrote `./port/src/native_renderer/game_profile.h` (+3 -2)
> - Wrote `./port/src/native_renderer/native_hooks.cpp` (+13 -6)
> - Wrote `./port/src/native_renderer/native_renderer.cpp` (+15 -14)
> - Wrote `./port/src/native_renderer/native_renderer.h` (+3 -3)
> - Ran `Get-Content port/src/native_renderer/native_hooks.cpp -TotalCount 48; .\build.cmd`
> - Wrote `./port/src/native_renderer/native_hooks.cpp` (+1 -1)
> - Ran `.\build.cmd`
> - Wrote `./port/src/native_renderer/native_renderer.h` (+1 -1)
> - Ran `.\build.cmd`
> - Ran `cmake --build build/tests-native; ctest --test-dir build/tests-native --output-on-failure; python -m pytest tests/tools/test_game_profile.py -q`
> - Ran `rg -n 'u\[[67]\]|Clear\(' port/src/native_renderer/native_renderer.cpp port/src/native_renderer/native_renderer.h port/src/native_renderer/native_hooks.cpp | Select-Object -Last 18; git diff --check`
> - Ran `New-Item -ItemType Directory -Force -Path 'port/out/build/win-amd64-release/logs/gameplay_clear_dump' | Out-Null
> powershell -NoProfile -ExecutionPolicy Bypass -File tools/bench.ps1 -Name native_gameplay_clear -ExtraArgs '--sr_renderer=native --sr_native_dump_frame_at_s=40 --sr_native_trace_frame_at_s=40 --sr_native_dump_dir=logs/gameplay_clear_dump' -Settle 10 -Window 12 -WorldTimeout 75`
> - Ran `rg 'D3DDevice_Clear' logs/game.log | Select-Object -First 16; Get-Content logs/bench_progress.txt -Tail 3; Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 5350 -First 23; Get-Content tests/tools/test_game_profile.py -TotalCount 155`
> - Ran `Get-Content logs/bench_progress.txt -Tail 4; Get-Content logs/game.log -Tail 4; Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 414 -First 30; Get-Content .tools/rexglue-sdk/win-amd64/include/rex/graphics/registers.h | Select-Object -Skip 795 -First 20`
> - Ran `Get-ChildItem port/out/build/win-amd64-release/logs/gameplay_clear_dump -File | Select-Object -First 3 Name; Get-Content logs/bench_progress.txt -Tail 3; Get-Content logs/game.log -Tail 4`
> - Ran `python -c "from pathlib import Path; import struct; import numpy as np; from PIL import Image; p=Path('port/out/build/win-amd64-release/logs/gameplay_clear_dump/output_1280x720.raw'); b=p.read_bytes(); w,h,f,pitch=struct.unpack('<4I',b[:16]); v=np.frombuffer(b,dtype=np.uint8,offset=16).reshape(h,pitch)[:,:w*4].copy().view('<u4').reshape(h,w); assert f==24; a=np.stack([((v >> s) & 1023) for s in (0,10,20)],axis=-1); Image.fromarray((a.astype(np.float32)*255/1023).round().astype(np.uint8)).save(p.with_suffix('.png'))"`
>
> </details>
>
> Viewed an image
>
> Agora a gameplay aparece com profundidade ligada: Superman, rua, prédios, HUD e minimapa estão visíveis. O clear estava sendo perdido pelo hook anterior. Ainda vejo geometria deformada e iluminação escura; vou concluir a validação de movimento e registrar essas limitações.
>
> <details><summary>Ran a command</summary>
>
> - Ran `Get-Content logs/bench_progress.txt -Tail 5; git diff --stat; git status --short`
>
> </details>
>
> Viewed an image
>
> <details><summary>Ran a command</summary>
>
> - Ran `rg 'unsupported shader|PSO creation failed|frame 1200' logs/game.log | Select-Object -Last 10; git diff -- port/src/native_renderer/native_renderer.h port/src/native_renderer/native_hooks.cpp`
>
> </details>
>
> O teste de movimento terminou: Superman avançou pela rua, com média de 22,5 FPS nessa medição curta. Vou registrar as duas correções e o estado atual da imagem, incluindo os defeitos de geometria e iluminação que ainda restam.
>
> <details><summary>Created a file, edited a file, ran a command</summary>
>
> - Wrote `./README.md` (+3 -1)
> - Wrote `./docs/native-gameplay-validation.md` (+36 -0)
> - Ran `Copy-Item -LiteralPath logs/game.log -Destination port/out/build/win-amd64-release/logs/gameplay_clear_dump/game.log
> ctest --test-dir build/tests-native --output-on-failure
> python -m pytest tests/tools/test_game_profile.py tests/tools/test_shader_tools.py -q
> git diff --check`
>
> </details>
</details>

A gameplay já aparece e responde ao movimento. Corrigi o viewport e o hook de clear que deixavam a cena azul.

Build e testes passaram. A medição curta deu 22,5 FPS em movimento. Ainda há geometria deformada e iluminação escura; a fidelidade visual precisa de mais correções.

![Gameplay nativa](./port/out/build/win-amd64-release/logs/gameplay_clear_dump/output_1280x720.png)

> Fazer seguir com eles então

<details><summary>29 previous messages</summary>

> Vou seguir com a geometria deformada e a iluminação escura. Primeiro vou identificar os draws afetados e comparar seus buffers e shaders com o que o Xenos recebe.
>
> <details><summary>Ran 2 commands</summary>
>
> - Ran `git status --short; Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 3820 -First 335`
> - Ran `rg -n 'tfetchDEC3N|tfetchR11G11B10|R11G11B10' tools/shaders/xenosrecomp/patches .tools/xenosrecomp -g '*.h' -g '*.cpp' --max-count 6 | Select-Object -First 20; Get-Content tools/rdc_dump_target.py -TotalCount 110; rg --files tools | rg 'capture|rdc|shader'`
>
> </details>
>
> Context compaction
>
> Source: automatic
> Status: completed
>
> <details><summary>Ran 8 commands</summary>
>
> - Ran `rg -n -A 75 -B 10 'float4 tfetchR11G11B10|g_SpecConstants|SwapBufferRange' .tools/xenosrecomp/src/XenosRecomp/shader_common.h port/src/native_renderer/native_renderer.cpp`
> - Ran `Get-Content .tools/xenosrecomp/src/XenosRecomp/shader_common.h | Select-Object -Skip 330 -First 68`
> - Ran `rg -n -A 95 'void Renderer::SwapBufferRange|uint32_t spec =|SPEC_CONSTANT_R11|g_SpecConstants\(\)' port/src/native_renderer/native_renderer.cpp artifacts/shaders/hlsl/C63F6F22679609F0.vs.hlsl tools/shaders/build_catalog.py`
> - Ran `Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 3734 -First 42`
> - Ran `Get-Content tools/shaders/build_catalog.py | Select-Object -Skip 145 -First 31`
> - Ran `rg -n -A 40 'MapDeclType|swapped_texcoords|swappedTexcoords|s\[72\]' port/src/native_renderer/native_renderer.cpp`
> - Ran `rg -n '2_10_10_10|2_10_10|k_10_11_11|k_11_11_10|2A2187|DEC3N' port/src .tools/xenosrecomp/src tools/shaders/xenosrecomp/patches rexglue -g '*.h' -g '*.cpp' -g '*.patch'`
> - Ran `Get-Content artifacts/shaders/hlsl/C63F6F22679609F0.vs.hlsl | Select-Object -First 9`
>
> </details>
>
> Encontrei uma inconsistência nas normais: o shader espera uma flag para desempacotá-las, mas o renderer nunca a ativa. Também vou conferir o formato exato, pois o helper atual decodifica 11/11/10 bits e a declaração usada pelo jogo indica 10/10/10.
>
> <details><summary>Created a file, edited 2 files, ran 19 commands</summary>
>
> - Ran `rg -n 'decl |layout |type 002A|2A2187|2A2190|2A2390' port/out/build/win-amd64-release/logs/gameplay_clear_dump/game.log | Select-Object -First 22`
> - Ran `rg --files | rg 'xenos.h|vertex.*fetch|test.*renderer|CMakeLists|000[123].*patch|fetch_xenos'`
> - Ran `Get-Content tools/shaders/fetch_xenosrecomp.py | Select-Object -First 190`
> - Ran `Get-Content tests/tools/test_shader_tools.py | Select-Object -First 190`
> - Ran `Get-Content tools/shaders/xenosrecomp/patches/0003-no-double-literals.patch | Select-Object -First 60`
> - Ran `Get-Content tools/shaders/build_corpus.ps1 | Select-Object -First 95`
> - Wrote `./port/src/native_renderer/native_renderer.cpp` (+16 -2)
> - Wrote `./tools/shaders/xenosrecomp/patches/0004-dec3n-normals.patch` (+34 -0)
> - Ran `rg -n 'vertex_decl' port/src/native_renderer/game_profile.h`
> - Ran `git apply --check --recount ../../../../tools/shaders/xenosrecomp/patches/0004-dec3n-normals.patch`
> - Wrote `./tools/shaders/xenosrecomp/patches/0004-dec3n-normals.patch` (+1 -1)
> - Ran `git apply --check ../../../tools/shaders/xenosrecomp/patches/0004-dec3n-normals.patch`
> - Ran `Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 2252 -First 27`
> - Wrote `./tools/shaders/xenosrecomp/patches/0004-dec3n-normals.patch` (+2 -2)
> - Ran `git apply --check ../../../tools/shaders/xenosrecomp/patches/0004-dec3n-normals.patch`
> - Ran `powershell -NoProfile -ExecutionPolicy Bypass -File tools/shaders/build_corpus.ps1 -NoSpirv`
> - Ran `Get-Content tests/native/CMakeLists.txt | Select-Object -First 135`
> - Ran `rg -n 'SPEC_CONSTANT|0x2A2187|normal|DECLARE|R32_UINT' tests/native port/src/native_renderer -g '*test*' -g '*.h'`
> - Ran `Get-ChildItem logs -Directory | Select-Object Name; Get-ChildItem port/out/build/win-amd64-release/logs -Directory | Select-Object Name`
> - Ran `Get-Content artifacts/shaders/manifest.json -TotalCount 18`
> - Ran `Get-ChildItem logs -Recurse -Filter *.vs.bin | Group-Object DirectoryName | Select-Object Count,Name`
> - Ran `Get-ChildItem logs/rt_corpus3 | Select-Object -First 8 Name; Get-ChildItem logs/rt_corpus | Select-Object -First 8 Name; rg -n 'rt_corpus3|DumpDir' docs README.md logs/*.txt`
> - Ran `powershell -NoProfile -ExecutionPolicy Bypass -File tools/shaders/build_corpus.ps1 -DumpDir logs/rt_corpus3 -NoSpirv`
>
> </details>
>
> Ajustei o decode para 10 bits com sinal e normalizei os componentes. Estou reconstruindo os shaders com o corpus capturado em execução; depois vou comparar a cena antes e depois para verificar o efeito na iluminação e nos polígonos deformados.
>
> <details><summary>Ran 10 commands</summary>
>
> - Ran `.\build.cmd`
> - Ran `Get-Content artifacts/shaders/hlsl/2C45BFFE76D77398.vs.hlsl | Select-Object -Skip 490 -First 130`
> - Ran `rg -n -A 10 -B 5 'iPosition1|tfetchR11|oPos.x' artifacts/shaders/hlsl/2C45BFFE76D77398.vs.hlsl | Select-Object -Last 75`
> - Ran `powershell -NoProfile -ExecutionPolicy Bypass -File tools/bench.ps1 -Name native_gameplay_normals -ExtraArgs '--sr_native_pipeline_cache=false' -Settle 10 -Window 12 -WorldTimeout 75`
> - Ran `Get-Content artifacts/shaders/hlsl/2C45BFFE76D77398.vs.hlsl | Select-Object -Skip 658 -First 48`
> - Ran `Get-Content logs/bench_progress.txt -Tail 12; Get-Content .tools/rexglue-sdk/win-amd64/include/rex/graphics/xenos.h -ErrorAction SilentlyContinue | Select-String -Pattern '2_10_10_10|10_11_11' -Context 1,1`
> - Ran `Get-Content tools/bench.ps1 | Select-Object -First 65; rg -n 'sr_renderer|ExtraArgs' tools/bench.ps1 port/src/*.cpp`
> - Ran `Get-Content logs/game.log -Tail 7`
> - Ran `Get-Content logs/bench_progress.txt -Tail 5`
> - Ran `rg -n 'VertexIndex|vertex index|r0.x|SV_VertexID|instance' .tools/xenosrecomp/src/XenosRecomp/shader_recompiler.cpp | Select-Object -First 35`
>
> </details>
>
> O jogo já entrou na cena com os shaders corrigidos. Enquanto a captura termina, encontrei outro ponto relevante: alguns shaders calculam a transformação de instâncias a partir de `r0.x`, mas esse registrador parece começar em zero. Vou verificar a ligação com o índice do vértice.
>
> <details><summary>Ran a command</summary>
>
> - Ran `Get-Content .tools/xenosrecomp/src/XenosRecomp/shader_recompiler.cpp | Select-Object -Skip 1630 -First 65; rg -n -A 12 -B 8 'iVertexId' .tools/xenosrecomp/src/XenosRecomp/shader_recompiler.cpp`
>
> </details>
>
> Viewed an image
>
> <details><summary>Ran 4 commands</summary>
>
> - Ran `rg -n 'vertex index|vertex_index|vertex_id' .tools/rexglue-sdk/win-amd64/include/rex/graphics -g '*shader*' | Select-Object -First 30`
> - Ran `Get-Content .tools/xenosrecomp/src/XenosRecomp/shader_recompiler.cpp | Select-Object -Skip 1858 -First 53`
> - Ran `Get-Content tools/capture_frame.ps1 | Select-Object -First 110`
> - Ran `rg -n 'vector.*scalar|scalar.*vector|scratch|prev|simult|dest|exec' .tools/xenosrecomp/src/XenosRecomp/shader_recompiler.cpp | Select-Object -Last 60`
>
> </details>
>
> Viewed an image
>
> <details><summary>Created 2 files, edited 2 files, ran 15 commands</summary>
>
> - Ran `Get-Content .tools/xenosrecomp/src/XenosRecomp/shader_recompiler.cpp | Select-Object -Skip 2290 -First 95`
> - Ran `rg -n -A 34 -B 18 'recompileAlu|RecompileAlu|TranslateALU|AluVector|aluVector|vectorInstr|emitAlu|RecompileVector|recompileVector' .tools/xenosrecomp/src/XenosRecomp/shader_recompiler.cpp`
> - Ran `rg -n 'VertexIndex|vertex index|r0.x|vertex_id' .tools/rexglue-sdk -g '*.h' | Select-Object -First 20`
> - Ran `Get-Content .tools/xenosrecomp/src/XenosRecomp/shader_recompiler.cpp | Select-Object -Skip 405 -First 240`
> - Wrote `./tools/shaders/xenosrecomp/patches/0005-vertex-id.patch` (+21 -0)
> - Wrote `./tools/shaders/xenosrecomp/patches/0005-vertex-id.patch` (+1 -5)
> - Ran `git apply --check ../../../tools/shaders/xenosrecomp/patches/0005-vertex-id.patch`
> - Ran `powershell -NoProfile -ExecutionPolicy Bypass -File tools/shaders/build_corpus.ps1 -DumpDir logs/rt_corpus3 -NoSpirv`
> - Ran `rg -n 'scalar.*vector|parallel|simult|same instruction' .tools/rexglue-sdk/win-amd64/include/rex/graphics/format/ucode.h | Select-Object -First 30`
> - Ran `.\build.cmd`
> - Ran `powershell -NoProfile -ExecutionPolicy Bypass -File tools/bench.ps1 -Name native_gameplay_vertexid -ExtraArgs '--sr_renderer=native --sr_native_pipeline_cache=false' -Settle 10 -Window 12 -WorldTimeout 75`
> - Ran `Get-Content .tools/rexglue-sdk/win-amd64/include/rex/graphics/format/ucode.h | Select-Object -Skip 1333 -First 75`
> - Ran `Get-Content .tools/xenosrecomp/src/XenosRecomp/shader_recompiler.cpp | Select-Object -Skip 1230 -First 130`
> - Ran `Get-Content .tools/xenosrecomp/src/XenosRecomp/shader_recompiler.cpp | Select-Object -Skip 525 -First 18; rg -n '^void ShaderRecompiler::' .tools/xenosrecomp/src/XenosRecomp/shader_recompiler.cpp`
> - Wrote `./tools/shaders/xenosrecomp/patches/0006-parallel-alu.patch` (+40 -0)
> - Ran `git apply --check ../../../tools/shaders/xenosrecomp/patches/0006-parallel-alu.patch`
> - Ran `Get-Content logs/bench_progress.txt -Tail 4`
> - Wrote `./tools/shaders/xenosrecomp/patches/0006-parallel-alu.patch` (+8 -2)
> - Ran `git apply --check ../../../tools/shaders/xenosrecomp/patches/0006-parallel-alu.patch`
>
> </details>
>
> Viewed an image
>
> <details><summary>Ran a command</summary>
>
> - Ran `powershell -NoProfile -ExecutionPolicy Bypass -File tools/shaders/build_corpus.ps1 -DumpDir logs/rt_corpus3 -NoSpirv`
>
> </details>
>
> O índice de vértice agora chega aos shaders de instâncias, mas os prédios ainda aparecem deformados. Achei um segundo erro no tradutor: a operação escalar lê registradores já alterados pela operação vetorial da mesma instrução. No Xenos, ambas devem ler os valores anteriores; estou corrigindo essa ordem.
>
> <details><summary>Created a file, edited a file, explored a file, ran 9 commands</summary>
>
> - Ran `Get-Content tools/shaders/xenosrecomp/corpus_main.cpp | Select-Object -First 150`
>
> - Ran `Get-Content .tools/xenosrecomp/src/XenosRecomp/shader_recompiler.h | Select-Object -First 145`
>
> - Ran `.\build.cmd`
>
> - Ran `powershell -NoProfile -ExecutionPolicy Bypass -File tools/bench.ps1 -Name native_gameplay_alu -ExtraArgs '--sr_renderer=native --sr_native_pipeline_cache=false' -Settle 10 -Window 12 -WorldTimeout 75`
>
> <details><summary>Explored a file</summary>
>
> - Read `./tools/shaders/xenosrecomp/CMakeLists.txt`
>
> </details>
>
> - Ran `rg -n -A 40 -B 8 'register struct Alu|struct AluInstruction|union AluInstruction' .tools/xenosrecomp/src/XenosRecomp/shader_code.h`
>
> - Ran `Get-Content logs/bench_progress.txt -Tail 4; Get-Content docs/native-gameplay-validation.md`
>
> - Ran `rg -n -A 80 'enum class AluScalarOpcode' .tools/xenosrecomp/src/XenosRecomp/shader_code.h | Select-Object -First 80`
>
> - Ran `Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -First 65`
>
> - Ran `Get-ChildItem logs/rdc | Select-Object Name,Length; Get-Content logs/bench_progress.txt -Tail 4`
>
> - Wrote `./tools/shaders/xenosrecomp/CMakeLists.txt` (+17 -1)
> - Wrote `./tools/shaders/xenosrecomp/test_recompiler.cpp` (+50 -0)
>
> </details>
>
> Viewed an image
>
> As correções compilaram e a gameplay continua funcionando, mas a captura ainda mostra falhas grandes na cidade. Vou separar dois testes agora: inverter o winding para verificar as faces ausentes e conferir a conversão de cor na apresentação para entender a imagem escura.
>
> <details><summary>Ran 11 commands</summary>
>
> - Ran `powershell -NoProfile -ExecutionPolicy Bypass -File tools/bench.ps1 -Name native_gameplay_flip -ExtraArgs '--sr_renderer=native --sr_native_pipeline_cache=false --sr_native_debug_no_embedded_pipelines=true --sr_native_flip_winding=true' -Settle 10 -Window 12 -WorldTimeout 75`
> - Ran `rg -n 'gamma|Gamma|present|Present' port/src/native_renderer/native_renderer.cpp | Select-Object -Last 60`
> - Ran `Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 4870 -First 115`
> - Ran `rg -n -A 48 -B 6 'GetGammaRamp256|LUT|gamma_ramp' port/src/native_renderer/native_graphics_system.cpp port/src/native_renderer/native_renderer.cpp port/src/native_renderer/shaders*`
> - Ran `rg --files port/src/native_renderer | Select-String 'blit|shader|constant'`
> - Ran `Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 2340 -First 90`
> - Ran `rg -n -A 18 -B 4 'face :' .tools/rexglue-sdk/win-amd64/include/rex/graphics/registers.h; Get-Content port/src/native_renderer/shaders/blit.hlsl | Select-Object -First 120`
> - Ran `Get-Content port/src/native_renderer/native_graphics_system.cpp | Select-Object -Skip 500 -First 88`
> - Ran `cmake --build .tools/xenosrecomp/build`
> - Ran `powershell -NoProfile -ExecutionPolicy Bypass -File tools/shaders/build_corpus.ps1 -DumpDir logs/rt_corpus3 -NoSpirv`
> - Ran `Get-Content logs/bench_progress.txt -Tail 4; Get-Content port/src/native_renderer/native_graphics_system.h | Select-Object -Last 85`
>
> </details>
>
> Viewed an image
>
> <details><summary>Edited a file, ran 20 commands</summary>
>
> - Ran `rg -n -A 60 'SR_ADDR_DRAW_INDEXED|DrawIndexed\(' port/src/native_renderer/native_hooks.cpp port/src/native_renderer/native_renderer.cpp | Select-Object -First 150`
> - Ran `ctest --test-dir .tools/xenosrecomp/build --output-on-failure`
> - Ran `rg -n 'DrawIndexed|draw_indexed' port/src/native_renderer/game_profile.h`
> - Ran `Get-Content port/src/native_renderer/game_profile.h | Select-Object -Skip 30 -First 22; rg -n -A 160 'ExecDrawIndexedVertices' port/src/native_renderer/native_renderer.cpp | Select-Object -Last 160`
> - Wrote `./tools/shaders/xenosrecomp/test_recompiler.cpp` (+2 -2)
> - Ran `rg -n 'PPC_FUNC_IMPL\(__imp__sub_820FC000\)' port/generated/default/*.cpp`
> - Ran `rg -n -A 75 'void Renderer::ExecDrawIndexedVertices' port/src/native_renderer/native_renderer.cpp`
> - Ran `rg -n 'PPC_FUNC_IMPL\(__imp__sub_820FC000\)' port/generated/default`
> - Ran `rg -n 'PPC_FUNC_IMPL\(__imp__sub_820FC000\)' port/generated/default -l`
> - Ran `rg -n -A 16 'sr_native_debug_buffers_always_dirty|sr_native_worker|sr_native_gamma_ramp' port/src/native_renderer/native_renderer.cpp | Select-Object -First 54`
> - Ran `rg -n 'PPC_FUNC_IMPL.*820FC000' port/generated/default`
> - Ran `powershell -NoProfile -ExecutionPolicy Bypass -File tools/bench.ps1 -Name native_gameplay_dirty -ExtraArgs '--sr_renderer=native --sr_native_pipeline_cache=false --sr_native_debug_buffers_always_dirty=true --sr_native_worker=false' -Settle 10 -Window 12 -WorldTimeout 75`
> - Ran `Get-Content .tools/rexglue-sdk/win-amd64/include/rex/graphics/registers.h | Select-Object -Skip 328 -First 56`
> - Ran `rg -n '820FC000' port/generated/default | Select-Object -First 8`
> - Ran `Get-Content port/generated/default/superman_returns_recomp.118.cpp -TotalCount 22`
> - Ran `Get-Content .tools/xenosrecomp/build/CMakeCache.txt | Select-String 'CMAKE_MAKE_PROGRAM|CMAKE_CXX_COMPILER:'`
> - Ran `rg -n -A 185 'DEFINE_REX_FUNC\(sub_820FC000\)' port/generated/default/superman_returns_recomp.22.cpp`
> - Ran `powershell -NoProfile -ExecutionPolicy Bypass -File tools/shaders/build_corpus.ps1 -DumpDir logs/rt_corpus3 -NoSpirv`
> - Ran `Get-Content port/generated/default/superman_returns_recomp.22.cpp | Select-Object -Skip 700 -First 235`
> - Ran `ctest --test-dir .tools/xenosrecomp/build --output-on-failure`
> - Ran `Get-Content logs/bench_progress.txt -Tail 5`
>
> </details>
>
> Viewed an image
>
> A inversão do winding piorou a imagem, e reenvio completo dos buffers sem o worker manteve as deformações. Esses testes reduzem a suspeita de culling invertido e dados antigos no cache. Agora estou comparando os parâmetros do draw com os pacotes emitidos pelo XDK.
>
> <details><summary>Ran 6 commands</summary>
>
> - Ran `Get-Content port/generated/default/superman_returns_recomp.22.cpp | Select-Object -Skip 420 -First 255`
> - Ran `rg -n '3D|2D|GetPostVS|GetBufferData|inputAssembly' tools/rdc_inspect_pass.py | Select-Object -First 35; Get-ChildItem '.tools/renderdoc' -Recurse -Filter qrenderdoc.exe | Select-Object -First 1 FullName`
> - Ran `rg -n -A 25 'VGT_DMA_SIZE|enum class Endian' .tools/rexglue-sdk/win-amd64/include/rex/graphics/registers.h .tools/rexglue-sdk/win-amd64/include/rex/graphics/xenos.h | Select-Object -First 70; Get-Content port/generated/default/superman_returns_recomp.22.cpp | Select-Object -Skip 350 -First 70`
> - Ran `rg -n 'index_format|index32|BufferPlan|VertexRange' port/src/native_renderer/native_renderer.h | Select-Object -First 40`
> - Ran `Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 3780 -First 65; Get-Content port/src/native_renderer/native_renderer.h | Select-Object -Skip 404 -First 18`
> - Ran `rg -n 'index_format ==|index_format !=' port/src/native_renderer/native_renderer.cpp`
>
> </details>
>
> Achei uma diferença concreta no index buffer: o XDK escolhe o modo de endian a partir do cabeçalho, enquanto o renderer sempre faz swap de 16 ou 32 bits conforme o tamanho do índice. Se o jogo usa swap por dword em índices de 16 bits, isso troca a ordem dos pares e conecta vértices errados. Vou preservar o modo emitido pelo XDK e testar.
>
> <details><summary>Created 2 files, edited 3 files, ran 6 commands</summary>
>
> - Wrote `./port/src/native_renderer/index_endian.h` (+22 -0)
> - Wrote `./port/src/native_renderer/native_renderer.cpp` (+19 -27)
> - Wrote `./port/src/native_renderer/native_renderer.h` (+3 -2)
> - Ran `.\build.cmd`
> - Ran `rg -n 'index_endian|else if \(index32|index_format & 3|index_format >>' port/src/native_renderer/native_renderer.cpp; Get-Content tests/native/test_game_profile.cpp -TotalCount 45; Get-Content tests/native/main.cpp -TotalCount 75`
> - Ran `Get-Content tests/native/test.h -ErrorAction SilentlyContinue -TotalCount 60`
> - Wrote `./port/src/native_renderer/native_renderer.cpp` (+6 -0)
> - Wrote `./tests/native/CMakeLists.txt` (+2 -1)
> - Wrote `./tests/native/test_index_endian.cpp` (+25 -0)
> - Ran `cmake --build build/tests-native`
> - Ran `.\build.cmd`
> - Ran `ctest --test-dir build/tests-native --output-on-failure`
>
> </details>
>
> System error
>
> Selected model is at capacity. Please try a different model.
</details>

<details><summary>9 previous messages</summary>

> <details><summary>Ran 8 commands</summary>
>
> - Ran `Get-Process superman_returns -ErrorAction SilentlyContinue | Select-Object Id; Get-Content logs/bench_progress.txt -Tail 2`
> - Ran `rg --files tools | rg 'test|build'; Get-Content build.cmd -TotalCount 85`
> - Ran `powershell -NoProfile -ExecutionPolicy Bypass -File tools/bench.ps1 -Name native_gameplay_indexdma -ExtraArgs '--sr_renderer=native --sr_native_pipeline_cache=false' -Settle 10 -Window 12 -WorldTimeout 75`
> - Ran ``$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath; $clang = Join-Path $PWD '.tools\clang+llvm-23.1.2-x86_64-pc-windows-msvc\bin'; $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'; $ninja = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'; & cmd /c "call `"$vs\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64 >nul && set `"PATH=$clang;$cmake;$ninja;%PATH%`" && cmake -S tests/native -B build/tests-native -G Ninja -DCMAKE_CXX_COMPILER=clang++ && cmake --build build/tests-native && ctest --test-dir build/tests-native --output-on-failure"``
> - Ran `rg -n -A 70 -B 6 'DC_LUT_PWL_DATA|gamma_ramp.*pwl|gamma_ramp_pwl|DC_LUT_CONTROL' .tools/rexglue-sdk-source/src/graphics/command_processor.cpp .tools/rexglue-sdk-source/src/graphics/d3d12/command_processor.cpp | Select-Object -First 150`
> - Ran `rg -n 'index DMA' logs/game.log; Get-Content logs/bench_progress.txt -Tail 3`
> - Ran `Get-ChildItem logs -Filter 'bench*png' | Select-Object -First 25 Name; rg -n 'GetGammaRamp256' port/src/sr_graphics_system.cpp -A 25; rg -n -A 20 'gamma_ramp.*pwl|use_pwl|gamma_ramp_256' .tools/rexglue-sdk-source/src/graphics/d3d12/command_processor.cpp | Select-Object -Last 75`
> - Ran `rg -n 'index DMA' logs/game.log; Get-Content logs/bench_progress.txt -Tail 4`
>
> </details>
>
> Viewed an image
>
> <details><summary>Ran 2 commands</summary>
>
> - Ran `rg -n -B 6 -A 8 'bool use_pwl_gamma_ramp' .tools/rexglue-sdk-source/src/graphics/d3d12/command_processor.cpp; rg -n -A 40 'float4Constants|kDevVsConst|GpuConstant\(' port/src/native_renderer/native_renderer.cpp | Select-Object -Last 100`
> - Ran `powershell -NoProfile -ExecutionPolicy Bypass -File tools/capture_frame.ps1 -Name native_gameplay_probe -ExtraArgs '--sr_renderer=native --sr_native_pipeline_cache=false' -Settle 12`
>
> </details>
>
> Viewed an image
>
> <details><summary>Ran 3 commands</summary>
>
> - Ran `Get-Content tools/rdc_inspect_pass.py | Select-Object -First 135`
> - Ran `Get-Content tools/rdc_gpu_profile.py | Select-Object -First 90`
> - Ran `Get-ChildItem 'C:/Program Files/RenderDoc' -Filter qrenderdoc.exe -ErrorAction SilentlyContinue | Select-Object FullName; Get-Content tools/capture_frame.ps1 | Select-Object -Skip 105 -First 45; Get-ChildItem logs/rdc -Filter '*native*' | Select-Object Name,Length`
>
> </details>
>
> A rodada registrou apenas endian 8-in-16, então o ajuste do index buffer não explica a deformação observada nessa cena. Capturei um frame no RenderDoc para inspecionar os dados do draw e a saída do vertex shader diretamente.
>
> <details><summary>Created a file, ran 10 commands</summary>
>
> - Wrote `./tools/rdc_mesh_probe.py` (+63 -0)
> - Ran `$env:SR_RDC = (Resolve-Path logs/rdc/native_gameplay_probe_capture.rdc).Path; $env:SR_RDC_OUT = Join-Path $PWD 'logs/rdc/native_mesh_probe.json'; Start-Process 'C:/Program Files/RenderDoc/qrenderdoc.exe' -ArgumentList '--python', (Join-Path $PWD 'tools/rdc_mesh_probe.py') -WindowStyle Hidden -Wait`
> - Ran `Get-Content logs/rdc/native_mesh_probe.json -TotalCount 24 -ErrorAction SilentlyContinue; rg -n 'PSO creation failed|not in MapDecl|not in library' logs/game.log | Select-Object -First 10`
> - Ran `python -c "import json; a=json.load(open('logs/rdc/native_mesh_probe.json')); [print(x['event'],x['action']['numIndices'],[(y['name'],y['vertexBuffer'],y['byteOffset']) for y in x['attributes']],x['positions'][:3]) for x in a]"`
> - Ran `Get-Process qrenderdoc -ErrorAction SilentlyContinue | Select-Object Id,StartTime; Get-Content logs/rdc/native_mesh_probe.json | Select-String 'vertexByteStride|indexByteStride|topology|byteStride' | Select-Object -First 24`
> - Ran `rg -n -A 35 '^enum class VertexFormat' .tools/rexglue-sdk/win-amd64/include/rex/graphics/xenos.h; rg -n -A 80 -B 7 '2C82A1|Position.*1|iPosition' .tools/xenosrecomp/src/XenosRecomp/shader_recompiler.cpp | Select-Object -First 110`
> - Ran `Get-CimInstance Win32_Process | Where-Object { $_.Name -match 'renderdoc|powershell' -and $_.CommandLine -match 'rdc|mesh_probe' } | Select-Object ProcessId,ParentProcessId,Name,CommandLine`
> - Ran `Get-Content .tools/xenosrecomp/src/XenosRecomp/shader_recompiler.cpp | Select-Object -Skip 200 -First 76`
> - Ran `python -c "import json; x=json.load(open('logs/rdc/native_mesh_probe.json'))[0]; print(x['attributes']);print(x['buffers']); print(x['post_vs'])"`
> - Ran `rg -n 'POSITION1|iPosition1' artifacts/shaders/hlsl/29C1ACD9F4373197.vs.hlsl`
> - Ran `Get-Content .tools/rexglue-sdk-source/src/graphics/pipeline/shader/dxbc_translator.cc -ErrorAction SilentlyContinue | Select-Object -First 2`
>
> </details>
>
> O RenderDoc mostrou a causa mais forte até agora: um draw pede 63.894 vértices, mas o buffer ligado contém só 2.146. O shader do jogo usa um índice calculado para reutilizar esse buffer entre instâncias; o input assembler nativo lê sequencialmente e acaba fora dele. Vou adaptar esse caminho de instancing.
>
> <details><summary>Ran 8 commands</summary>
>
> - Ran `rg -n '^#define instance_data' artifacts/shaders/hlsl/*.vs.hlsl`
> - Ran `rg -n 'instance_data' artifacts/shaders/hlsl -g '*.vs.hlsl' | Select-Object -First 30`
> - Ran `rg -n -A 24 'struct Shader|LoadShader\(' port/src/native_renderer/native_renderer.h | Select-Object -First 75; Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 3110 -First 29`
> - Ran `Get-Content port/src/native_renderer/shader_registry.h | Select-Object -First 165; Get-Content port/src/native_renderer/shader_container.h | Select-Object -First 175`
> - Ran `Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 4130 -First 54`
> - Ran `Get-Content port/src/native_renderer/shader_library.h | Select-Object -First 110; Get-Content .tools/xenosrecomp/src/XenosRecomp/shader_recompiler.cpp | Select-Object -Skip 1695 -First 10; rg -n 'ShaderHash|vs_hash =' port/src/native_renderer/native_renderer.cpp | Select-Object -Last 10`
> - Ran `rg -n -A 75 '^def reflect' tools/shaders/build_catalog.py; Get-Content .tools/xenosrecomp/src/XenosRecomp/shader.h | Select-Object -First 110`
> - Ran `Get-Content port/src/native_renderer/native_renderer.cpp | Select-Object -Skip 3550 -First 27; rg -n 'ShaderIdentity|LookupGuestShader' port/src/native_renderer/native_renderer.cpp | Select-Object -First 12`
>
> </details>
</details>
