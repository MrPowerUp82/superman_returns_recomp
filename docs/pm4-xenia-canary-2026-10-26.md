# Espelho de comandos gráficos PM4 — referência Xenia Canary (26/10/2026)

Referência consultada: `xenia-canary/xenia-canary`, branch `canary_experimental`,
HEAD observado `291f0c9be398a3e0a0e29f378cbd564fa28c55a9`.
Arquivos de interesse: `src/xenia/gpu/pm4_command_processor_implement.h`,
`src/xenia/gpu/command_processor.h` e `src/xenia/gpu/xenos.h`.
Comparação de semântica do código-fonte; não é evidência de equivalência de
execução com o emulador.

## Correções no `Pm4Mirror`

- `REG_RMW` (`0x21`) agora combina operadores imediatos e operadores obtidos
  de registradores, respeitando as máscaras de índice usadas pelo Xenia.
- Os comandos `SET_BIN_MASK`/`SELECT` (`0x50`/`0x51`) e variantes de metade
  alta/baixa (`0x60` a `0x63`) mantêm o estado de predicação. O bit 0 dos
  headers tipo 3 impede que um pacote condicional escreva registradores ou
  percorra buffers indiretos quando `(bin_mask & bin_select) == 0`. O estado
  inicial das duas máscaras é `~0ull`, assim como na referência.
- Marcadores PM4 `0` e `0x0BADF00D` são tratados como padding/packet inválido,
  consumindo só o header; limites do parser são conferidos por subtração para
  impedir overflow de offset perto de `UINT32_MAX`.
- No caminho Windows de leitura ao vivo, `INDIRECT_BUFFER` passa por uma
  cópia protegida de todo o buffer antes da interpretação; leituras parciais
  são rejeitadas sem executar seus comandos. Os slots de cópia sobrevivem a
  chamadas indiretas aninhadas. O caminho `MemoryReader` de captura/replay
  mantém sua semântica e suas próprias cópias. Buffers de tamanho zero não
  são percorridos.

## Verificações e limites

Novos testes verificam `REG_RMW`, predicação de escritas e de buffers
indiretos, atualização das metades das máscaras, padding, overflow de leitura,
buffers indiretos aninhados e páginas de memória parcialmente inacessíveis.
Compilação e testes unitários no alvo Windows Release passaram; CTest 3/3,
suíte nativa 483 testes sem falhas (confirmados por execução direta).
O microbenchmark de escrita PM4 atingiu razão 1,04 frente à rotina de
referência (limite 2,0); ele não inclui o custo
de cópias de buffers indiretos. A compilação integral do jogo também passou
com `build.cmd` no perfil `port/out/build/win-amd64-write-tracking` (MSVC/Clang,
`SR_NATIVE=RENDERER`, `SR_VULKAN=ON`, `SR_MEMORY_TRACKING_SDK=ON`,
`SR_DLSS=OFF`, `SR_LSFG=OFF`), incluindo o vínculo do executável
`superman_returns.exe`. A compilação emitiu avisos de depreciação e macros
redefinidas em arquivos existentes; não houve erro de build.

Não há nesta etapa medição controlada de FPS ou certificação de equivalência
visual no jogo. A cópia segura de buffers indiretos pode ter custo adicional
em cenas com muitos comandos desse tipo; medir numa cena reproduzível antes de
atribuir ganho ou regressão de desempenho. As opções experimentais de leitura
ALU existentes continuam inalteradas.
