# Superman Returns Recomp — XMA Final Block Flush & Takeoff Audio Fix

**Data:** 27 de Outubro de 2026  
**Status:** Implementado e validado em build Release, testes nativos (518/518 PASS) e oracle diferencial XDK (2.048/2.048 PASS)  
**Arquivos modificados:**
- [`port/src/xma_fixes.cpp`](file:///C:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/xma_fixes.cpp)
- [`port/src/xma_flush_policy.h`](file:///C:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/xma_flush_policy.h)
- [`tests/native/test_xma_flush_policy.cpp`](file:///C:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/test_xma_flush_policy.cpp)

---

## 1. Contexto e Problema Observado

### Relato do Usuário
> *"Antes da decolagem tocava música e o Jor-El falava, depois cortou tudo."*  
> *"Voltou no início, mas depois cortou tudo novamente."*  
> *"Acabei de testar e piorou o som"* (após a primeira tentativa de espera/spin no mixer).

Na versão anterior, o áudio funcionava durante a introdução/Fortaleza da Solidão, mas no instante da decolagem rumo a Metrópolis todo o áudio (música de voo, vento, vozes) emudecia por completo.

Na primeira tentativa de correção, a adição de `yield()` com espera ativa na thread do mixer causou estolamento severo:
- A thread de áudio travou em loop no mixer com 17,5 milhões de recusas registradas no log em ~88 segundos.
- O I/O de disco e a CPU foram saturados por logs excessivos, causando engasgos massivos (*audio crackle/stutter*).

---

## 2. Engenharia Reversa (REA) e Diagnóstico das Causas Raiz

A análise aprofundada do código recomputado (`superman_returns_recomp.35.cpp`, `superman_returns_recomp.52.cpp`, `superman_returns_recomp.118.cpp`) e do runtime ReXGlue (`xma_context.cpp`) revelou a cadeia de eventos exata:

### A. A Inversão Arquitetural de Alimentação de Pacotes (`sub_826586A0`)
No código recomputado (`superman_returns_recomp.35.cpp` linha 23076, `loc_82659658`), a rotina responsável por alimentar novos pacotes de entrada nos buffers dos fluxos XMA (`sub_826586A0`) é chamada **após** o loop de mixagem de fluxos (`loc_826593C8`).
- A alimentação de pacotes roda **na mesma thread** do mixer, sequencialmente.
- Se `XmaKickStarvedContext` mantiver `r26 != 0` tentando esperar com `yield()`, a execução nunca sai do loop de mixagem e **nunca alcança `sub_826586A0`**.
- Cria-se um *deadlock*: o mixer espera por pacotes que só seriam fornecidos após o próprio mixer sair do loop.
- **Regra fundamental:** `XmaKickStarvedContext` **nunca deve fazer spin ou yield**. Se um kick síncrono não produzir novas amostras, `r26` deve ser zerado imediatamente para prosseguir a `sub_826586A0`.

### B. Inversão Semântica de `output_buffer_valid` no ReXGlue
No ReXGlue (`xma_context.cpp` linha 146):
```cpp
if (output_rb.empty()) {
  data.output_buffer_valid = 0; // Zera quando vazio!
}
```
E no início de `XmaContext::Work()`:
```cpp
if (!data.output_buffer_valid) {
  return true; // Aborta sem decodificar!
}
```
No hardware real do Xbox 360:
- `output_buffer_valid == 1` significa que o buffer é válido/pronto para o hardware escrever.
- O hardware só zera `output_buffer_valid = 0` quando o buffer circular fica **completamente cheio**, para sinalizar à CPU que pare de submeter decodificação.
- Quando `read_offset == write_offset`, o jogo testa `XMAIsOutputBufferValid`:
  - Se retornar `1`: o buffer está **vazio** (`r30 = 0`).
  - Se retornar `0`: o buffer está **cheio** (`r30 += 6144`).

O ReXGlue inverteu essa semântica:
1. Quando o jogo lia as últimas amostras, o buffer esvaziava e o ReXGlue zerava `output_buffer_valid = 0`.
2. Quando `sub_826586A0` colocava novos pacotes no buffer de entrada e chamava `XMAEnableContext`, o ReXGlue via `!data.output_buffer_valid` e **recusava-se a decodificar os pacotes**.
3. Quando o mixer inspecionava o buffer vazio (`read == write`), via `output_buffer_valid == 0` e incorretamente adicionava 6144 amostras fantasmas.
4. O stream entrava em estolamento permanente, o contador de amostras do som era drenado e o som era destruído.

---

## 3. Correção Implementada

1. **`EnsureContextsValid` em `sub_826586A0` e `sub_826592F0`:**
   Garante que todos os contextos XMA de cada som ativo possuam `output_buffer_valid = 1` antes e depois das chamadas do alimentador de pacotes (`sub_826586A0`) e do mixer (`sub_826592F0`).
   - O ReXGlue agora decodifica imediatamente todos os pacotes entregues por `sub_826586A0`.
   - O mixer não confunde buffers vazios com cheios.

2. **Remoção de Spin Loops e Yields:**
   Em `XmaKickStarvedContext`:
   - Se o kick decodificar amostras (`after.dword0 != before.dword0`), permite no máximo 2 tentativas para misturar as novas amostras.
   - Se não houver progresso, zera `r26 = 0` imediatamente, permitindo que o mixer conclua o passe e execute `sub_826586A0`.
   - Remoção de qualquer alteração artificial em `sound + 36`.

3. **Otimização e Throttling de `TryFlushFinalBlock`:**
   - Retorno imediato (`false`) quando não for a requisição final do som (`!IsFinalXmaRequest`), com custo zero para sons normais durante o jogo.
   - Limite estrito de no máximo 10 mensagens de recusa em todo o tempo de execução do jogo, eliminando saturação de log.

---

## 4. Verificação e Testes

1. **Testes Unitários Nativos (`build/tests-native/sr_native_tests.exe`):**
   - **518/518 testes passaram com 0 falhas**.
2. **Oracle Diferencial XDK (`tests/xdk/out/build/sr_xdk_draw_tests.exe`):**
   - **2.048/2.048 streams gerados validados byte a byte**.
3. **Compilação Release (`superman_returns.exe`):**
   - Compilado e linkado com sucesso em modo Release sem erros.

---

## 5. Diagnóstico e Resolução do Crash no Início (`0xc0000005`)

### Sintoma
Ao iniciar o jogo após a primeira versão de `EnsureContextsValid`, o jogo fechava sozinho logo na inicialização.
O log do Windows e o `game.log` registraram:
```text
[error] [sys] [t27280] Unhandled guest access violation: read of guest 0x00000019 (host 0x0000000100000019) on thread 0xF80000E4
Fault offset: 0x0000000002de2e95 (superman_returns.exe)
Exception code: 0xc0000005
```

### Causa Raiz
1. Em `REX_HOOK_RAW(sub_826586A0)` e `REX_HOOK_RAW(sub_826592F0)`, a função nativa `__imp__` era executada e modificava `ctx.r3` (registrador PowerPC de valor de retorno), deixando `ctx.r3.u32 = 1`.
2. A segunda chamada de `EnsureContextsValid(base, ctx.r3.u32)` recebia `sound = 1`.
3. Ao calcular o deslocamento do array de fluxos (`sound + 24`), o endereço lido tornou-se `1 + 24 = 25` (`0x00000019`), acessando a página zero não mapeada e disparando a Violação de Acesso.
4. `EnsureContextsValid` também não validava se os ponteiros de memória guest estavam na faixa mapeada (`IsValidGuestPtr`).

### Correção Aplicada
1. **Preservação de `sound`:** Os hooks agora armazenam `const uint32_t sound = ctx.r3.u32;` antes da chamada `__imp__` e passam `sound` para ambas as chamadas.
2. **Validação Estrita de Limites (`IsValidGuestPtr`):** Todo acesso guest agora valida `ptr >= 0x00010000 && ptr <= 0xFFFF0000 - 4`. `Load32` e `ReadState` retornam 0 de forma segura caso um ponteiro inválido seja fornecido, eliminando qualquer risco de falha de proteção de memória.
