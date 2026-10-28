# Troca de texturas e traje Reino do Amanhã — plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Dump e troca de texturas por PNG nos dois backends nativos, opção para vestir qualquer traje, e o Golden Age repintado como Reino do Amanhã por uma receita no código.

**Architecture:** Um módulo puro, `port/src/graphics/texture_mods/`, recebe cada `guest::LinearTexture` logo depois de `DecodeTextureLayout`. Ele calcula a chave pelo conteúdo decodificado, grava o dump, aplica o PNG do usuário ou a receita do Reino do Amanhã, e devolve a textura como RGBA8 com mips. Os três pontos de decodificação o chamam: dois em `native_renderer/texture_decode.cpp` (D3D12) e o decodificador Vulkan em `native_provider.cpp`. Um hook no jogo força o traje.

**Tech Stack:** C++20 (clang), ReXGlue SDK v0.10.0, `stb_image.h` v2.29 (copiado de `.tools/rexglue-write-tracking-source/thirdparty/stb/`), testes `tests/native` (SR_TEST), launcher WPF .NET 8.

**Spec:** `docs/superpowers/specs/2026-10-28-texture-replacement-kingdom-come-design.md`

## Global Constraints

- Chave: hash XXH3 de `LinearTexture::data` mais largura, altura, profundidade, mips, `dimension` e `format`; nome de arquivo `<16 hex>.png`.
- `--sr_texture_dump_dir=<pasta>` (vazio = desligado); `--sr_texture_replace_dir=<pasta>` (padrão `textures` ao lado do exe); `--sr_texture_replace=true|false` (padrão true); `--sr_costume=default|0|1|2` (padrão default); `--sr_costume_skin=none|kingdom_come` (padrão none).
- A troca exige as mesmas dimensões; dimensões diferentes → ignora e avisa uma vez por chave.
- O PNG do usuário tem prioridade sobre a receita.
- Custo zero quando não há dump, nem pasta com PNG, nem skin: não calcular a chave.
- Nada do jogo no Git: dumps em `logs/`; a receita contém só hashes, cores e formas próprias.
- Launcher: campo "Traje" com "Padrão do jogo", "Superman Returns", "Pod Suit" e "Reino do Amanhã"; "Reino do Amanhã" envia `--sr_costume=2 --sr_costume_skin=kingdom_come`.
- Textos visíveis ao usuário em pt-BR.

## Review Focus

- PNG do usuário com outras dimensões, corrompido ou com só 3 canais: o jogo segue com a textura original, sem crash (Task 2 testa).
- Textura com mais de um slice (array, cubo, volume): dump e troca ficam só no 2D simples; os outros passam intactos (Task 2 testa).
- Versão diferente do jogo, em que os hashes não batem: a skin não pinta nada e avisa uma vez (Task 5 testa).
- `--sr_costume` com valor inválido: tratado como `default` (Task 3 testa).
- Pasta `textures` inexistente: sem custo e sem aviso (Task 2 testa).

---

### Task 1: Conversões e PNG

**Files:**
- Create: `port/third_party/stb/stb_image.h` (cópia de `.tools/rexglue-write-tracking-source/thirdparty/stb/stb_image.h`, v2.29, domínio público), `port/src/graphics/texture_mods/texture_image.h`, `port/src/graphics/texture_mods/texture_image.cpp`
- Test: `tests/native/test_texture_image.cpp` (+ `tests/native/CMakeLists.txt`: fonte e o `.cpp` acima)

**Interfaces:**
- Produces (namespace `superman_returns::graphics::texture_mods`):
  - `struct Image { uint32_t width = 0, height = 0; std::vector<uint8_t> rgba; };` (RGBA8, linhas contíguas)
  - `uint64_t TextureKey(const guest::LinearTexture&);`
  - `bool BaseLevelToImage(const guest::LinearTexture&, Image& out);` (nível 0, slice 0; BC1/2/3/5, RGBA8, BGR565, BGR5A1, BGRA4, R8, RG8; outros → false). Os canais saem na ordem lógica dos dados decodificados, **antes** do swizzle do fetch, para que o swizzle continue valendo na troca.
  - `void ImageToTexture(const Image&, guest::LinearTexture& inout);` (troca o conteúdo por RGBA8 `kRGBA8Unorm`, gera a cadeia de mips por média 2×2 até o número de mips original, mantém `swizzle`, `guest_format`, `base_address`, `base_size`)
  - `bool WritePng(const std::filesystem::path&, const Image&);` (PNG sem compressão: blocos deflate "stored", CRC e Adler próprios)
  - `bool ReadPng(const std::filesystem::path&, Image& out);` (stb_image, força 4 canais)

- [ ] **Step 1: Testes que falham**

  - `texture_key_ignores_guest_address`: duas `LinearTexture` iguais com `base_address` diferentes têm a mesma chave; mudar 1 byte de `data` ou `width` muda a chave.
  - `bc1_block_decodes_to_reference_colors`: um bloco BC1 conhecido (`c0=0xF800` vermelho, `c1=0x001F` azul, índices `0b00011011`...) gera os 4 tons esperados na ordem dos índices.
  - `png_round_trip_keeps_pixels`: `WritePng` de uma imagem 7×5 com alfa variado, depois `ReadPng`, devolve bytes idênticos.
  - `image_to_texture_builds_mip_chain`: imagem 8×8 em `LinearTexture` com 4 mips vira `kRGBA8Unorm` com níveis 8, 4, 2, 1; o nível 1 é a média 2×2; o `swizzle` original é mantido.
  - `read_png_rejects_garbage`: um arquivo com bytes aleatórios devolve false.

- [ ] **Step 2: Rodar e ver falhar** — `cmake --build %TEMP%\sr-tests-native --target sr_native_tests` (após `tools\dev_env.ps1`). Esperado: header ausente.
- [ ] **Step 3: Implementar `texture_image.h/.cpp`** com as assinaturas acima. O `STB_IMAGE_IMPLEMENTATION` fica só em `texture_image.cpp`, com `STBI_ONLY_PNG`.
- [ ] **Step 4: Rodar** — `%TEMP%\sr-tests-native\sr_native_tests.exe`. Esperado: `0 failed checks`.
- [ ] **Step 5: Commit** — `feat(textures): add texture key, RGBA conversion and PNG I/O`

### Task 2: Pipeline de mods e ligação nos dois backends

**Files:**
- Create: `port/src/graphics/texture_mods/texture_mods.h`, `.cpp`
- Modify: `port/src/native_renderer/texture_decode.cpp` (após cada `DecodeTextureLayout`/`DecodeTextureLayoutUsing`, antes de `ConvertLinear`), `port/src/graphics/vulkan/platform/native_provider.cpp` (decodificador `TextureDecoder`), CMake do `port` (novos `.cpp`), cvars em `texture_mods.cpp`
- Test: `tests/native/test_texture_mods.cpp`

**Interfaces:**
- Consumes: Task 1.
- Produces:
  - `struct ModConfig { std::filesystem::path dump_dir, replace_dir; bool replace = true; bool kingdom_come = false; };`
  - `class TextureMods { public: explicit TextureMods(ModConfig); bool Apply(guest::LinearTexture&); /* true se trocou */ void SetRecipe(std::function<bool(uint64_t key, Image&)>); };` (thread-safe: os backends decodificam em threads diferentes)
  - `TextureMods& GlobalTextureMods();` (configurada pelas cvars na primeira chamada)
  - `void ApplyTextureMods(guest::LinearTexture&);` (chamada pelos backends; não faz nada quando nada está ativo)

- [ ] **Step 1: Testes que falham**
  - `mods_inactive_never_hash`: com config vazia e pasta inexistente, `Apply` devolve false e não lê nem cria nada (contar chamadas por um contador interno exposto para teste, ou verificar que o diretório não foi criado).
  - `mods_dump_writes_each_texture_once`: dump num diretório temporário; a mesma textura aplicada duas vezes gera um único `<chave>.png` e uma linha no `index.csv` com `chave,largura,altura,formato`.
  - `mods_replace_uses_png_with_same_size`: com `<chave>.png` 8×8 de outra cor, `Apply` devolve true e a textura vira `kRGBA8Unorm` com os pixels do PNG.
  - `mods_replace_ignores_other_size_and_garbage`: um PNG 16×16 para uma textura 8×8, ou um arquivo inválido, deixam a textura original intacta (bytes iguais) e devolvem false.
  - `mods_png_overrides_recipe`: com receita e PNG para a mesma chave, vence o PNG.
  - `mods_skip_non_2d_textures`: textura com `depth = 6` (cubo) ou `dimension != 1` não é tocada.
- [ ] **Step 2: Ver falhar** — esperado: header ausente.
- [ ] **Step 3: Implementar** `TextureMods` e as cvars (`sr_texture_dump_dir`, `sr_texture_replace_dir`, `sr_texture_replace`). Ao iniciar, escanear `replace_dir` uma vez e guardar o conjunto de chaves presentes, para não procurar arquivo por textura. Ligar `ApplyTextureMods` nos três pontos de decodificação.
- [ ] **Step 4: Rodar testes e `build.cmd`** — esperado: `0 failed checks`, build exit 0.
- [ ] **Step 5: Verificar no jogo** — D3D12 com `--sr_texture_dump_dir=logs\tex_dump`: a pasta recebe PNGs legíveis e um `index.csv`. Copiar um PNG do céu ou de um prédio para `textures/`, pintado de magenta, e confirmar por print que ele aparece em D3D12 e Vulkan. Benchmark headless com e sem a pasta: sem diferença além da variação conhecida.
- [ ] **Step 6: Commit** — `feat(textures): dump and PNG replacement in D3D12 and Vulkan`

### Task 3: Forçar o traje (`--sr_costume`)

**Files:**
- Create: `port/src/costume_override.cpp`, `port/src/costume_override_policy.h`
- Modify: CMake do `port`; `docs/` (nota com endereços e evidências, nome livre com data)
- Test: `tests/native/test_costume_override_policy.cpp`

**Interfaces:**
- Produces: `std::optional<int> ParseCostume(std::string_view)` ("default" e inválidos → `nullopt`; "0", "1", "2" → valor); cvars `sr_costume` (string, "default") e `sr_costume_skin` (string, "none"); `bool KingdomComeSkinActive()` (`sr_costume=2` e `sr_costume_skin=kingdom_come`).

- [ ] **Step 1: Teste da política** — `costume_parse_accepts_only_known_values`: "0", "1", "2" → valores; "default", "", "3", "-1", "abc" → `nullopt`. Ver falhar e implementar.
- [ ] **Step 2: Achar o traje no jogo** — procurar as strings `SetActiveCostume`, `SetCostumeNum`, `GetCostumeData` em `logs/image.bin` e os pontos que as carregam em `port/generated/default`, como foi feito para `SetToggleAttractMode` (tabela de nomes nativos do Apt). Confirmar em execução com um hook de log temporário (endereço chamado e argumento) ao vestir trajes pelo menu num save com trajes, ou pelo caminho de desbloqueio. **Critério de parada:** se não houver um setter claro, chamável com `(traje)`, que o próprio menu use, parar e falar com o usuário.
- [ ] **Step 3: Implementar o hook** — ao entrar no gameplay (primeira chamada do setter ou do ponto equivalente achado no Step 2), aplicar o valor de `ParseCostume(sr_costume)` pelo mesmo caminho do menu, sem chamar as funções de desbloqueio. "default" não instala nada.
- [ ] **Step 4: Verificar no jogo** — `--sr_costume=0`, `1` e `2`, com prints do Superman no início do gameplay, e "default". Registrar se o save mudou (comparar arquivos de save antes e depois).
- [ ] **Step 5: Commit** — `feat: force a costume with --sr_costume`

### Task 4: Identificar as texturas do Golden Age

**Files:**
- Create: `docs/kingdom-come-texture-map-2026-10-28.md` (chaves, dimensões, formato e o que cada textura é: corpo, capa, emblema, rosto, cabelo, normal/especular), `tools/texture_mods/find_costume_textures.py` (diferença entre dois `index.csv`)

- [ ] **Step 1** — dump com `--sr_costume=2` e com `--sr_costume=0`, no mesmo ponto do jogo. O script lista as chaves que só aparecem com o Golden Age.
- [ ] **Step 2** — abrir os PNGs dessas chaves, classificar cada uma, e anotar as regiões de UV (retângulos em pixels) do emblema e do cabelo, que a Task 5 usa. Nenhum PNG do jogo entra no commit.
- [ ] **Step 3: Commit** — `docs: map Golden Age costume textures`

### Task 5: Receita do Reino do Amanhã

**Files:**
- Create: `port/src/graphics/texture_mods/kingdom_come.h`, `.cpp`
- Modify: `texture_mods.cpp` (registrar a receita quando `KingdomComeSkinActive()`)
- Test: `tests/native/test_kingdom_come.cpp`

**Interfaces:**
- Consumes: `Image`, `TextureMods::SetRecipe` (Tasks 1–2), `KingdomComeSkinActive` (Task 3), chaves e regiões (Task 4).
- Produces: `bool PaintKingdomCome(uint64_t key, Image&)` (false para chave desconhecida); operações puras usadas por ela:
  - `void RecolorHue(Image&, Rect, float hue_from, float hue_width, Rgb target)`: troca os pixels cuja cor está dentro da faixa de matiz por `target`, preservando a luminância relativa.
  - `void FillPolygon(Image&, std::span<const Vec2>, Rgba, Rect clip)`: preenchimento com antialiasing, para desenhar o emblema.
  - `void Lighten(Image&, Rect, float amount)`.

- [ ] **Step 1: Testes que falham**
  - `recolor_keeps_shading`: um degradê azul vira um degradê cinza com a mesma ordem de luminância; pixels fora da faixa de matiz ou fora do `Rect` não mudam.
  - `polygon_stays_inside_clip`: um triângulo maior que o `clip` não altera pixels fora dele.
  - `unknown_key_paints_nothing`: `PaintKingdomCome` com uma chave fora da tabela devolve false, e a imagem fica intacta.
  - `skin_warns_once_when_no_key_matches`: com a skin ativa e nenhuma chave conhecida aplicada em N texturas, sai um único aviso.
- [ ] **Step 2: Ver falhar**, implementar as operações e a tabela (chaves e regiões da Task 4; cores: traje cinza-escuro `#2B2D33` com a luminância original, emblema em vermelho `#B3141B` sobre preto `#111111`, têmporas clareadas em 35%). Os tons finais podem mudar no Step 4.
- [ ] **Step 3: Rodar testes e `build.cmd`.**
- [ ] **Step 4: Prints para o usuário** — D3D12 e Vulkan com `--sr_costume=2 --sr_costume_skin=kingdom_come`, de frente, de costas e voando. **Parar e pedir aprovação ou ajustes da arte ao usuário** antes de seguir. Os ajustes são feitos na tabela, sempre com os testes verdes.
- [ ] **Step 5: Commit** — `feat(textures): Kingdom Come skin over the Golden Age costume`

### Task 6: Campo "Traje" no launcher

**Files:**
- Modify: `launcher/LauncherSettings.cs` (propriedade `Costume`, valores `default|0|1|kingdom_come`, salvo como `costume=`), `launcher/MainWindow.xaml(.cs)`, `tests/launcher/Program.cs`

- [ ] **Step 1: Checks que falham** — `Costume = "kingdom_come"` envia exatamente `--sr_costume=2` e `--sr_costume_skin=kingdom_come`, uma vez cada; `"0"` envia `--sr_costume=0` sem skin; `"default"` não envia nenhuma das duas; preferência salva sobrevive a recarregar; valor desconhecido no arquivo vira `default`.
- [ ] **Step 2: Implementar** — `ComboBox` "Traje" com "Padrão do jogo", "Superman Returns", "Pod Suit" e "Reino do Amanhã". O benchmark respeita a escolha.
- [ ] **Step 3: Rodar** — `dotnet run --project tests/launcher/LauncherChecks.csproj`. Esperado: `0 failed checks`.
- [ ] **Step 4: Commit** — `feat(launcher): costume selector with Kingdom Come`

### Task 7: Verificação final e README

- [ ] **Step 1** — pelo launcher, "Reino do Amanhã" em D3D12 e em Vulkan: o traje novo aparece no gameplay.
- [ ] **Step 2** — "Superman Returns" e "Pod Suit" ficam iguais a antes (prints com e sem a skin), e o cenário não muda.
- [ ] **Step 3** — três rodadas do benchmark headless com e sem a skin: diferença dentro da variação conhecida.
- [ ] **Step 4** — README: seções "Trajes" e "Texturas (dump e troca)", com as opções e o aviso de que nada do jogo é distribuído. Registrar os resultados na spec.
- [ ] **Step 5: Commit** — `docs: costumes and texture replacement`
