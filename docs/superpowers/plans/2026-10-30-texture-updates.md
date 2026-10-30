# Texture update implementation plan

- [x] Definir lifetime conservador, fallback e orçamento de snapshots.
- [x] Implementar e validar D3D12 sem sobrescrever recursos pendentes.
- [x] Implementar e validar Vulkan, incluindo aliases e uploads separados.
- [x] Integrar testes e executar Release, CTest e readbacks GPU.
- [x] Medir contadores/execução no jogo; decidir padrões e registrar limites.

Resultado: 618 casos nativos, CTest 5/5; D3D12 readback RGBA8/BC1 passou;
Vulkan CPU 109/CTest 1/1 e matriz GPU 2 caminhos × 2 flags passou, incluindo
admissão de snapshots e fallback >8 MiB. Runtime/tracking/XMA CTest 6/6.
Revisão estática independente sem P1/P2. Após corrigir admissão que preenchia
o orçamento com texturas estáticas, o build final é
5786F7BC0EF65A222C54BCE1FA9A303C349599B93617E6C469CC2217384F55E9.
Pares de jogo não exercitaram reusos; D3D12 perdeu FPS e Vulkan teve resultado
isolado favorável. Manter experimental Off e registrar a ausência de ganho causal.

Spec: docs/superpowers/specs/2026-10-30-texture-updates-design.md
