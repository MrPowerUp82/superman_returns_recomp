# Correção da seleção do SDK no build.cmd

## Integração dos pré-shaders Vulkan (design)

A build normal já gera SRSL/DXIL, mas não dependia das bibliotecas SRVK que eram
produzidas por um script separado. Adicionar outputs CMake explícitos para os
perfis portable/global, usando o compilador de produção e os containers locais.
O executável dependerá desses outputs e os instalará ao lado do binário. Manter
o gate `SR_NATIVE_EMBED_SHADERS`: builds de distribuição OFF não devem incluir
containers derivados do jogo. Falhas de tradução devem reprovar a build.

O build.cmd usava implicitamente `.tools/rexglue-write-tracking-source`, enquanto
a build dos milestones usa `.tools/rexglue-object-motion-source`. O checkout
antigo contém alterações de codegen não reconhecidas pelo verificador canônico;
o erro reproduzido foi `SDK has unknown local changes: src/codegen/builders/system.cpp`.

O build agora seleciona explicitamente o checkout dos milestones e instala/verifica
os overlays de pacing e teclado antes da configuração CMake. O manifesto combinado
é atualizado pelo setup oficial após os overlays. Nenhuma verificação é desativada,
e o checkout antigo não é sobrescrito. A opção `SR_MEMORY_TRACKING_SDK=OFF` mantém
o caminho anterior sem tracking.

Para escolher outra cópia isolada dentro de `.tools`, em cmd.exe:

```bat
set "SR_MEMORY_SDK_CHECKOUT=C:\caminho\superman_returns_recomp\.tools\meu-sdk"
build.cmd
```

A cópia continua sujeita aos mesmos checks de revisão, conteúdo e patches. Não
selecionar um checkout com experimentos não versionados para contornar um erro.

Logs locais da reprodução e validação: `logs/build_fix_20261107/before.log` e
`after.log`. As alterações de otimização do HUD co-op são trabalho anterior e
foram preservadas durante esta correção.

## Validação executada

- `build.cmd` completo: jogo compilado/linkado e launcher WPF publicado.
- Segunda execução via `cmd /d /c "call build.cmd"`: exit 0, launcher publicado
  novamente. O Ninja reduziu o trabalho pendente de 161 para 16 etapas após
  verificar a geração, sem recompilar os 147 módulos PPC inteiros.
- Override explícito para o checkout antigo: exit 1, com o mesmo erro de conteúdo
  desconhecido; SHA256 do arquivo recusado permaneceu igual. A proteção contra
  sobrescrever experimentos locais permanece ativa.
- `git diff --check`: passou. Não foram alterados hooks ou preferências gráficas.

Evidências adicionais: `incremental.log`, `incremental_result.json`,
`unknown_sdk_call.log` e `unknown_sdk_result.json`, no mesmo diretório de logs.

## Validação dos pré-shaders

A build completa passou com geração dos dois perfis Vulkan e publicação do
launcher. Os targets `sr_preshaders` e `sr_vulkan_preshaders` foram executados
novamente com as dependências finais (containers, fontes/patches do emissor,
contratos Python e DXC/DLLs); passaram. A execução seguinte retornou
`ninja: no work to do.`

Na pasta `port/out/build/win-amd64-release`:

| Biblioteca | Bytes | Cobertura do corpus local |
|---|---:|---:|
| superman_returns_shaders.srsl | 5.023.116 | 241/241 |
| superman_returns_vulkan.srvk | 10.525.048 | 241/241 |
| superman_returns_vulkan_global.srvk | 14.777.328 | 241/241 |

A inspeção validou ABI, checksum e containers, sem entradas ausentes ou chaves
divergentes. Evidência em `logs/preshader_build_fix_20261107/libraries.json`,
`build.log`, `targets.log`, `targets_incremental.log` e `validation.log`.
Isso certifica as bibliotecas frente ao corpus conhecido, não zero PSOs runtime
em todo o jogo. Os arquivos derivados do jogo continuam ignorados pelo Git.
