### Arquivos Criados / Alterados

No diretório de trabalho isolado `C:/Users/Gusta/.codex/worktrees/gemini-scene-mapping/superman_returns_recomp`:
1. [port/src/graphics/scene_resolution_mapping.h](file:///C:/Users/Gusta/.codex/worktrees/gemini-scene-mapping/superman_returns_recomp/port/src/graphics/scene_resolution_mapping.h): Implementação pura C++20 (header-only, sem Windows SDK ou runtime dependencies) com as estruturas e funções do namespace `superman_returns::graphics`:
   - `ScenePixelExtent`
   - `SceneSurfaceMapping`
   - `ScenePixelRect`
   - `ScenePixelViewport`
   - `SceneEdgeRounding` (`Nearest`, `Floor`, `Ceil`)
   - `ValidSceneSurfaceMapping`
   - `MapScenePixelEdge`
   - `MapScenePixelRect`
   - `MapScenePixelViewport`
   - `SameScenePixelScale`
   - `ScenePixelCopy`
   - `PlanScenePixelCopy`
2. [tests/native/test_scene_resolution_mapping.cpp](file:///C:/Users/Gusta/.codex/worktrees/gemini-scene-mapping/superman_returns_recomp/tests/native/test_scene_resolution_mapping.cpp): Bateria completa de testes unitários C++20 autocontidos integrados via [test_main.h](file:///C:/Users/Gusta/.codex/worktrees/gemini-scene-mapping/superman_returns_recomp/tests/native/test_main.h).
3. `.gemini-tasks/test_scene_resolution_mapping.exe`: Binário scratch gerado para validação.

Nenhum arquivo fora da lista permitida foi alterado (sem alterações em CMakeLists, renderer ou no checkout principal).

---

### Execução dos Testes (Ciclo TDD)

#### 1. Fase RED (Testes criados + Stubs)
Compilação executada com `clang++.exe -std=c++20 tests/native/main.cpp tests/native/test_scene_resolution_mapping.cpp -o .gemini-tasks/test_scene_resolution_mapping.exe`:
- **Código de saída:** `1`
- **Resultado:** `13 tests, 82 failed checks` (falhas em `valid_scene_surface_mapping_bounds`, `map_scene_pixel_edge_nearest_floor_ceil`, `map_scene_pixel_edge_signed_tie_breaking`, `map_scene_pixel_edge_overflow_rejection`, `map_scene_pixel_rect_valid_and_empty_inverted_rejection`, `map_scene_pixel_viewport_dlss_quality_exact_full_and_sub`, `map_scene_pixel_viewport_irregular_1079x607`, `same_scene_pixel_scale_exact_rational_comparison`, `plan_scene_pixel_copy_success_and_matching_footprints`, `plan_scene_pixel_copy_rejects_subrect_footprint_mismatch`).

#### 2. Fase GREEN (Implementação completa)
Recompilação e execução com `-std=c++20 -Wall -Wextra -Werror -Wpedantic`:
- **Código de saída:** `0`
- **Resultado:**
```text
PASS valid_scene_surface_mapping_bounds
PASS map_scene_pixel_edge_nearest_floor_ceil
PASS map_scene_pixel_edge_signed_tie_breaking
PASS map_scene_pixel_edge_overflow_rejection
PASS map_scene_pixel_rect_valid_and_empty_inverted_rejection
PASS map_scene_pixel_viewport_dlss_quality_exact_full_and_sub
PASS map_scene_pixel_viewport_nan_inf_zero_reversed_rejection
PASS map_scene_pixel_viewport_irregular_1079x607
PASS same_scene_pixel_scale_exact_rational_comparison
PASS plan_scene_pixel_copy_success_and_matching_footprints
PASS plan_scene_pixel_copy_rejects_subrect_footprint_mismatch
PASS plan_scene_pixel_copy_rejects_differing_scales_853x480_to_1280x720
PASS plan_scene_pixel_copy_rejects_out_of_bounds_and_empty
13 tests, 0 failed checks
```

---

### Invariantes Garantidas

1. **Extents Bounds (1..16384):** Nenhuma dimensão guest ou host aceita valores fora de `[1, 16384]`; operações retornam `std::nullopt` ou `false` sem clamping silencioso.
2. **Aritmética Int64 Exata e Arredondamento Assinado:**
   - Multiplicação `edge * host_extent` e divisão por `guest_extent` em `int64_t`, prevenindo overflows intermediários.
   - `Nearest`: desempate de frações `.5` afastando de zero tanto positivo quanto negativo (compatível com `std::lround`).
   - `Floor` e `Ceil`: respeitam a semântica matemática estrita para dividendos negativos (ex.: `-5 / 2 = -2.5` resulta em floor `-3` e ceil `-2`).
   - Coordenadas assinadas podem estar fora dos limites do alvo, mas overflow fora do intervalo `[INT32_MIN, INT32_MAX]` retorna `std::nullopt`.
3. **Retângulos sem Recorte Silencioso:** `MapScenePixelRect` rejeita retângulos invertidos (`left >= right` ou `top >= bottom`) ou vazios pós-arredondamento, mas preserva coordenadas fora do alvo sem clamp (deixando o clipping para os consumidores).
4. **Viewport Exata 0..guest:** Rejeita `NaN`, `Inf`, dimensões `<= 0`, e profundidades inválidas/invertidas. Para o caso `0..guest`, a largura e altura no host mapeiam com precisão exata para `host.width` e `host.height` (eliminando desvios de precisão float como `853.333` em resoluções como 1280x720 -> 853x480).
5. **Comparações Racionais sem Epsilon:** `SameScenePixelScale` valida produtos inteiros cruzados por eixo `(host_a * guest_b == host_b * guest_a)` em `uint64_t`.
6. **Footprint Mismatch Rejection em Cópias:** `PlanScenePixelCopy` rejeita cópias entre superfícies com escalas diferentes (não inventa upscale) e verifica se os footprints físicos resultantes no host para `source` e `destination` são idênticos em largura e altura. Quando um sub-retângulo com deslocamento gera footprints discretos divergentes devido ao arredondamento dos cantos, a operação é rejeitada com `std::nullopt` ao invés de corromper a cópia no D3D12.

---

### Lacunas e Próximos Passos (Integração)

- **Integração de Build:** O arquivo `CMakeLists.txt` e o renderer não foram modificados nesta etapa, conforme solicitado. O orquestrador precisará incluir `scene_resolution_mapping.h` nos headers do renderer e adicionar `tests/native/test_scene_resolution_mapping.cpp` ao target de testes nativos do CMake.
- **Isolamento Completo:** Nenhuma dependência externa ou header de plataforma (Windows/D3D12/DirectXMath) foi introduzido.
