# Fundação Vulkan M1

O branch `codex/vulkan-backend` contém uma biblioteca Vulkan independente do ReXGlue
e um smoke test Windows que renderiza um triângulo. **O jogo ainda usa D3D12.**
Este marco não integra os shaders, texturas ou comandos de Superman Returns.

## Compilar e executar

```powershell
powershell -File tools/setup_vulkan.ps1
powershell -File tools/build_vulkan.ps1
build/tests-vulkan/sr_vulkan_tests.exe
build/tests-vulkan/vulkan/sr_vulkan_smoke.exe --list-gpus
build/tests-vulkan/vulkan/sr_vulkan_smoke.exe --validation
powershell -File tests/vulkan/smoke_integration.ps1 -GpuUuid 787589225ad18fb3ea2345350716d21b
```

O UUID do exemplo pertence à RTX 2060 da máquina de teste; use o UUID listado no seu
PC. Sem seleção explícita, a política prefere uma GPU discreta compatível. O smoke
aceita `--frames=120`, `--self-test` e `--log-file=<caminho>`.

Requer o toolchain VS/Clang local e DXC em `.tools/dxc/bin/x64`, já usado pelo projeto.
Headers: Vulkan-Headers v1.3.290, revisão `b379292b2ab6df5771ba9870d53cf8b2c9295daf`.
DXC local: 1.9.0.5402, revisão `0d3ee6b55-dirty` reportada pela ferramenta.
O DXC compila HLSL escrito para este teste com target SPIR-V/Vulkan 1.1.
Não há shaders extraídos do jogo.

O loader é carregado em runtime; não se exige Vulkan SDK no PC do jogador. A GPU e
seu driver precisam oferecer Vulkan 1.1, swapchain e apresentação na surface.
`shaders/triangle.vert.spv` e `triangle.frag.spv` precisam acompanhar o executável.

## Estrutura e ownership

- `policy.*`: seleção de GPU/filas, swapchain e alinhamento de memória.
- `loader.*`, `functions.inc`, `context.*`: loader, dispatch, instance/GPU/device e diagnósticos.
- `swapchain.*`, `frame_loop.*`: imagens/views/framebuffers/render pass e quadros.
- `triangle.*`: upload host-visible, módulos SPIR-V e pipeline do teste.
- `platform/win32_surface.*`: janela e surface Win32 separadas do núcleo.
- `smoke_main.cpp`: CLI e teste real de resize/minimização/encerramento.

O núcleo não expõe HWND/COM/Direct3D e não linka import library Vulkan. O loader
contém adaptadores Windows/POSIX/Android; isso não significa que o runtime do jogo
ou uma aplicação Android estejam portados.

Context adota a surface; a janela vive mais que o Context. FrameLoop é destruído
antes de Triangle/Swapchain; o Context vive mais que todos. Ao reconstruir, retirar
quadros pendentes, destruir pipeline, reconstruir swapchain e recriar o pipeline.
`UploadBuffer::Destroy` e `Triangle::Destroy` pressupõem submissões já retiradas.

O CMake principal oferece `SR_VULKAN_FOUNDATION=ON` como opt-in, mantendo OFF por
padrão. A opção acrescenta o smoke; não troca a API do jogo. A build standalone
em `tests/vulkan` não inclui ReXGlue nem precisa dos arquivos do jogo.

## Evidências locais — 03/10/2026

- 30 testes Vulkan passaram: políticas, cleanup, falha de vkCreateDevice, upload
  não coerente, SPIR-V inválido e fault injection do protocolo de quadros.
- 50 testes nativos e 13 checks do launcher passaram.
- A build D3D12 padrão e a build separada com `SR_VULKAN_FOUNDATION=ON` compilaram.
  O jogo iniciou em D3D12 na RTX 2060 e apresentou quadros, sem carregar o slot 1.
- O smoke apresentou 120 quadros: 1280×720 → 960×540 → 640×480, extent zero,
  restauração e encerramento. Graphics/present: família 0; três imagens,
  BGRA8_UNORM, FIFO. GPU Vulkan 1.4, driver `2588114944`; piso usado Vulkan 1.1,
  nenhuma feature opcional de device habilitada.
- O triângulo com cores interpoladas foi conferido visualmente: imagem local
  `logs/vulkan/m1-triangle.jpg`; log `logs/vulkan/m1-integration.log`.
- Layers/debug-utils não disponíveis. A ausência foi registrada;
  **não foi realizada validação pelas layers**. Com layers instaladas,
  `--validation` habilita o callback e erros provocam exit não zero, inclusive no teardown.
- GPU UUID inexistente e shaders ausentes retornaram erros explícitos.

Logs/imagens são artefatos locais ignorados pelo Git. Não foi medido ganho de FPS
em War World. Este teste ainda não representa a renderização do jogo em Vulkan.

## Sincronização, revisão e limites

Dois slots de comandos com acquire semaphore/fence por slot; render-finished
semaphore por imagem. A fence só é resetada imediatamente antes da submissão.
OUT_OF_DATE no acquire não reseta fence; imagem adquirida SUBOPTIMAL é consumida
antes de recriar. Falha fatal no present não é mascarada por acquire SUBOPTIMAL.

Resize/shutdown usam espera conservadora de device idle, nunca por quadro normal.
M1 não implementa manutenção da swapchain/present fences; teardown WSI deve ser
validado novamente pelas layers/drivers na integração. Não presumir que uma submit
fence prova conclusão de apresentação; o reuso normal segue a imagem readquirida.
Referências: [reuso de semaphores](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)
e [recriação/fences](https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/04_Swap_chain_recreation.html).

Revisão independente: nenhum finding crítico. Dois findings materiais reproduzidos
por testes e corrigidos: surface com extent zero agora suspende e permite nova
tentativa sem resize; exceções do logger ficam contidas no callback, preservando
contagem de erros. RED: 30 testes, duas falhas; GREEN: 30 testes, zero falhas.

A verificação de headers rejeita mudanças em arquivos rastreados sem alterá-las.
Um fixture demonstrou que o setup anterior aceitava mudanças e o novo as rejeita.
Este ajuste atende ao contrato de dependência fixada/preservação do checkout.
A cobertura pode crescer para falhas em cada etapa da alocação da swapchain e
alterações da quantidade de imagens. Layers e plataformas adicionais permanecem
pendentes antes da integração do jogo.

## Próximo marco

Compartilhar o frontend de comandos guest e traduzir shaders do jogo para SPIR-V,
com bindings e caches separados de DXIL. A opção Vulkan do launcher só será oferecida
após existir renderização integrada e validada. APK/runtime ARM64 são posteriores;
esta fundação não declara suporte Android completo.
