# Handoff para o Codex — Superman Returns Recomp (renderer nativo D3D12)

## Estado atual (02/10/2026)
- Repo: `MrPowerUp82/superman_returns_recomp`, branch de trabalho `main-lenk9a` (limpo, 1 commit à frente: `e5cd4b6` — subsistema do renderer nativo com espelhamento PM4 e registro de shaders).
- PR #4 já foi **mergeado**. Não há PR aberto para acompanhar. Novo trabalho = novo branch/PR.
- Padrão já trocado: `--sr_renderer=native` (`port/src/sr_settings.cpp:17`) e `build.cmd` com `SR_NATIVE=RENDERER` (linha 28; `OFF`/`CAPTURE` via env). Fallback automático para `xenos` se o nativo não puder ser usado (linha de log `sr_renderer=native: ...; using the xenos backend`).
- Scripts de medição (`post_effects_check.ps1`, passo `bench` do `native_validate.ps1`) pedem `--sr_renderer=xenos` explicitamente.

## Contexto técnico
- XDK `2.0.3529.0` (LTCG). Bloco D3D em `0x820F0000`–`0x82114000`.
- 21 hooks D3D e o layout do `D3DDevice` estão confirmados em `port/src/native_renderer/game_profile.h` (`kDeviceLayoutConfirmed = true`).
- Último problema conhecido (build RENDERER): todos os draws pulados (`p0:0/Nshader`), porque os hashes calculados no draw não batem com o catálogo (D3D do XDK altera as cópias dos containers; campos de tamanho do MemStream zerados dentro de `sub_820F5840`/`sub_820F6690`).
- Correção implementada, **ainda não testada no jogo**: biblioteca de pré-shaders `superman_returns_shaders.srsl` (`port/src/native_renderer/shader_library.h`, gerada por `tools/shaders/make_preshaders.py`). Identificação na criação do shader (match `exact`/`body`/`microcode`), fallback único no draw com log `shader object XXXXXXXX (vs|ps) not recognised`. Opções: `sr_native_preshaders`, `sr_native_preshaders_path`, `sr_native_shader_dir`, `sr_native_dump_shader_dir`.

## Limitações do ambiente
- Linux/cloud: não compila nem roda o jogo (precisa de Windows + `game/` com `default.xex`). Só dá para rodar testes sem o jogo:
  - `cmake -S tests/native -B build/tests-native && cmake --build build/tests-native && build/tests-native/sr_native_tests` (44 OK esperados)
  - `python -m pytest tests/tools` (38 OK esperados)

## Próximos passos (no Windows, pelo usuário)
```powershell
powershell -File tools\shaders\build_corpus.ps1
python tools\shaders\make_preshaders.py --verify artifacts\shaders\superman_returns_shaders.srsl
powershell -File tools\native_validate.ps1 -Step build -Native RENDERER
.\port\out\build\win-amd64-release\superman_returns.exe --game_data_root=game --sr_renderer=native --sr_skip_intro=true --sr_native_dump_shader_dir=logs\native_shaders --log_file=logs\game.log
Select-String logs\game.log -Pattern "pre-shaders|exact|body match|microcode|not recognised"
```
Verificar no `logs/game.log`:
- `pre-shaders: N shaders from ...` (biblioteca carregou)
- `sr_renderer=native: native graphics system, no Xenos emulation` (nativo ativo)
- contadores `exact`/`body` subindo e draws diferentes de zero.
- Se houver `not recognised`: `python tools\shaders\make_preshaders.py --diagnose logs\native_shaders\unmatched` e ajustar `ResolveContainer`/`ResolveInline` em `shader_registry.cpp`.
- Tela preta/errada: `run.cmd --sr_renderer=xenos`. Sem nativo no build: `set SR_NATIVE=OFF`.

## Depois disso (roteiro do CHECKPOINT_NATIVE_RENDERER.md)
1. Validação A/B: `tools\native_validate.ps1 -Step ab` (PSNR médio ≥ 40 dB; saídas em `logs/native_ab/`).
2. Benchmark: `-Step bench` (meta: 30 FPS estáveis, frametime constante).
3. Se o usuário trouxer logs com draws ainda em zero, investigar a identificação de shaders seguindo o método de causa raiz (sem correções antes de entender).

Documentação completa: `CHECKPOINT_NATIVE_RENDERER.md`.
