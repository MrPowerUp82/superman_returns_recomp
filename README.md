# Superman Returns Recomp

Projeto experimental de recompilação estática da versão Xbox 360 de **Superman Returns** para PC com [ReXGlue v0.10.0](https://github.com/rexglue/rexglue-sdk). O código-fonte C++ é gerado localmente a partir do `default.xex` fornecido pelo usuário. O projeto ainda não é uma versão jogável.

## Versão analisada

- Xbox 360 Title ID: `454107ED`; Media ID: `64A4002A`; versão do XEX: `0.0.0.1`.
- SHA-256 de `default.xex`: `C8F243ACD99DE9A91F5AE4F409721C0E954E3D5EB96861419D3DA07B8106DB2B`.
- A ISO contém `default.xex` e 12 arquivos `.AST` em `DATA/`. Os arquivos do jogo ficam em `game/`, ignorados pelo Git.

## Estado atual

O ReXGlue valida a análise e gera cerca de 145 arquivos C++ a partir deste XEX. As dicas de limites de função estão em [`port/superman_returns_manifest.toml`](port/superman_returns_manifest.toml). Há um salto condicional em `0x82627E74 → 0x82627E14` que ainda vira `REX_FATAL` no código gerado. A compilação e o teste de inicialização são marcos separados; a geração de C++ por si só não significa que o jogo funciona.

## Estrutura

- `port/`: projeto CMake e manifesto ReXGlue.
- `game/`: arquivos extraídos da ISO, mantidos apenas localmente.
- `.tools/`: ferramentas e SDK baixados localmente.
- `build.cmd`: configura e compila a versão Windows x64.

## Preparação no Windows

1. Instale Visual Studio Build Tools com a carga de trabalho C++ e o Windows SDK.
2. Extraia o [ReXGlue SDK v0.10.0 para Windows](https://github.com/rexglue/rexglue-sdk/releases/tag/v0.10.0) em `.tools/rexglue-sdk/`, deixando `rexglue.exe` em `.tools/rexglue-sdk/win-amd64/bin/`.
3. Extraia o pacote portátil [Clang/LLVM 23.1.2 x86_64 Windows](https://github.com/llvm/llvm-project/releases/tag/llvmorg-23.1.2) em `.tools/`, deixando `clang++.exe` em `.tools/clang+llvm-23.1.2-x86_64-pc-windows-msvc/bin/`.
4. Extraia sua própria ISO com [xdvdfs](https://github.com/antangelo/xdvdfs): copie `default.xex` para `game/default.xex` e `DATA/` para `game/DATA/`.
5. Execute `build.cmd` na raiz do projeto. O executável, quando compilado, fica em `port/out/build/win-amd64-release/`.

O `build.cmd` usa o manifesto para regenerar o C++ automaticamente. O diretório `port/generated/default/` é descartável e não deve ser editado diretamente. Ajustes para o jogo devem ficar no manifesto, em `port/src/` ou em uma etapa de patch reproduzível.

## Próximos marcos técnicos

1. Resolver o salto condicional restante de forma reproduzível.
2. Compilar e tratar imports ou erros de link específicos deste jogo.
3. Testar a inicialização, caminhos de arquivos `.AST`, gráficos, áudio e entrada.
4. Só considerar o jogo jogável após passar por menus e gameplay real.

Os arquivos originais do jogo não são incluídos no projeto.
