# Instalador e builds pré-compilados

O port pode ser distribuído **sem nenhum dado do jogo e sem nenhum shader traduzido**. O usuário monta o pacote em uma página (GitHub Pages) que roda inteira no navegador, a partir da própria cópia do jogo. A página pode gerar pré-shaders Vulkan e D3D12 com WebAssembly antes de salvar o pacote; shaders ausentes continuam sendo traduzidos no PC durante o jogo. Veja [pré-shaders Vulkan](vulkan-preshaders.md) para cobertura, geração local e ferramentas. O fluxo foi inspirado pelo [`nfsmw-nx`](https://github.com/StevensND/nfsmw-nx).

Marque as APIs desejadas no passo 2. D3D12 gera `superman_returns_shaders.srsl`
(containers originais + DXIL); Vulkan gera `superman_returns_vulkan.srvk`
(containers originais + SPIR-V). O pacote inclui as bibliotecas selecionadas e
um relatório para cada API. **Salvar somente os pré-shaders selecionados** salva
a biblioteca diretamente se houver uma API, ou um ZIP com as duas bibliotecas
e relatórios se ambas estiverem marcadas. Extraia esse ZIP ao lado do executável.

A geração D3D12 usa os mesmos perfis `vs_6_0`/`ps_6_0`, constantes em runtime,
chaves XXH3-64 e formato SRSHLIB v1 do gerador nativo. O checksum do container
DXIL reproduz `ComputeHashRetail` do DXC; os testes comparam a assinatura com
o validador Microsoft `dxv.exe`. Só os containers não comprimidos nos `.AST`
são cobertos; shaders do XEX e criados dinamicamente continuam no runtime.

## Peças

| Peça | Onde | O que faz |
| --- | --- | --- |
| Página do instalador | `docs/index.html`, `docs/js/*` | Lê o ISO (XDVDFS) ou a pasta do jogo no navegador, confere o SHA-256 do `default.xex`, baixa o build e grava tudo em uma pasta ou em um `.zip`. Nada sai do computador do usuário. |
| Tradução de shaders em runtime | `port/src/native_renderer/shader_translator.*` | Cada container de shader que o jogo cria vai para o `sr_xenosrecomp.exe` (emissor HLSL) e o `dxc.exe`, em processos filhos e threads de fundo; o DXIL fica em `shader_cache/`. |
| Pacote | `tools/package_release.ps1`, `tools/release/` | Executável sem pack nem biblioteca de shaders embutidos, launcher GUI portátil com .NET, `shader_tools/`, runtimes do Visual C++, scripts `run*.cmd`, licenças. |
| Publicação | `tools/publish_release.ps1` | Branch órfão `builds` (a página baixa de lá, pois `raw.githubusercontent.com` tem CORS e os assets de release não) e uma release com o mesmo zip. |

## Gerar e publicar um build

```powershell
powershell -File tools\package_release.ps1          # compila em port\out\build\win-amd64-dist (SR_EMBED_SHADERS=OFF) e empacota
powershell -File tools\publish_release.ps1 -Confirm  # branch builds + release (exige o repositório sem alterações pendentes)
```

O pacote sai em `artifacts/release/superman_returns_win64.zip` com `version.json` e o SHA-256. A compilação do launcher requer .NET 8 SDK e grava o executável na mesma pasta do build de distribuição. Para só empacotar builds existentes, gere o launcher com `tools/build_launcher.ps1 -OutDir <pasta-do-build>` e use `-NoBuild -BuildDir <pasta-do-build>`.

O empacotamento e a publicação validam o ZIP com `tools/release/verify_package.py`: launcher, avisos do .NET, runtimes e ferramentas de shaders Vulkan precisam estar presentes e não vazios. O ZIP público não inclui as bibliotecas locais `.srsl`, `.pak` ou `.srvk`; gere os pré-shaders com `tools/shaders/build_corpus.ps1` para uso local ou pelo instalador no navegador.

Para ativar a página: Settings → Pages → Deploy from a branch → `main` / `docs`.

## Tradução de shaders na máquina do usuário

- Ferramentas em `shader_tools/` ao lado do executável (`sr_xenosrecomp.exe`, `dxc.exe`, `dxcompiler.dll`, `dxil.dll`, `shader_common.h`); em desenvolvimento ele usa as de `.tools/`.
- O cache fica em `shader_cache/<impressão>/`, onde a impressão resume as ferramentas: trocar o emissor, o cabeçalho ou o DXC invalida tudo.
- Cvars: `sr_native_runtime_shaders` (liga/desliga), `sr_native_runtime_shader_threads` (padrão 2), `sr_native_runtime_shader_wait_ms` (quanto um draw espera pela tradução, padrão 4000), `sr_native_shader_tools_dir`, `sr_native_shader_cache_dir`.
- Para testar numa build de desenvolvimento (que embute o pack): `--sr_native_preshaders=false --sr_native_ignore_pack=true`.
- Medido nesta máquina: ~190–270 ms por shader, 188 shaders traduzidos no primeiro uso com 2 threads, 1 falha (o VS `978B0FF62C3693C2`, com `iPosition0` duplicado, o mesmo que o corpus já não traduz); com o cache quente nada é traduzido.

## Testes

```powershell
node --test tests\web\installer.test.mjs
```

Cobrem o leitor XDVDFS (imagem sintética e, se `game/` existir, o disco real), a validação do `default.xex`, uma pasta com prefixo e maiúsculas/minúsculas diferentes, e o pacote gravado em pasta e em `.zip` conferido byte a byte e por um leitor independente (`zipfile` do Python).

## Limites conhecidos

- O navegador precisa da File System Access API (Chrome ou Edge) para gravar arquivos grandes; sem ela a página avisa.
- O `.zip` não usa ZIP64: o pacote precisa ter menos de 4 GB (hoje ~2,3 GB).
- Só o `default.xex` com o SHA-256 da página é aceito: o executável foi gerado a partir dele.
- A página baixa o build de `raw.githubusercontent.com` (branch `builds`); se ainda não houver build publicado, ela oferece escolher o zip baixado da página de releases.
