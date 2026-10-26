### Resumo (até 5 linhas)
A separação estrutural entre solicitado, capaz, implementado e aplicado existe em [`GraphicsOptions`](file:///port/src/graphics/graphics_options.h#L22-L42) e no [menu](file:///port/src/graphics/graphics_options_dialog.cpp#L137-L141), mas há brechas de estado obsoleto (*stale*) e ausência de sincronização em [`RayShadowExecution`](file:///port/src/graphics/ray_shadow_execution.h#L18-L34). O runtime permite que a conclusão tardia de um frame anterior à mudança de revisão anuncie RT Shadows como aplicado antes do fence da revisão atual. DLSS e RT Reflexos não possuem trava rígida em [`PublishBackend`](file:///port/src/graphics/graphics_options.cpp#L91-L114) impedindo a aceitação de relatórios indevidos caso o backend marque suporte. Não foi executado nenhum teste nem comando em ambiente live.

---

### Achados (arquivo:linha, cenário e correção mínima)

1. **Ressurreição de RT On por frame obsoleto em transição de revisão**
   * **Referência:** [`port/src/graphics/ray_shadow_execution.h:23-28`](file:///port/src/graphics/ray_shadow_execution.h#L23-L28) e [`port/src/graphics/graphics_options.cpp:93-105`](file:///port/src/graphics/graphics_options.cpp#L93-L105)
   * **Cenário:** O usuário solicita RT Off (incrementando `request_revision` de 1 para 2). Enquanto o novo frame 11 está na fila GPU, o frame 10 (revisão 1, RT On) conclui seu fence. Como `pending.frame (10) > latest_.frame (0)`, [`RayShadowExecution::Complete`](file:///port/src/graphics/ray_shadow_execution.h#L18-L29) grava `latest_.applied[0] = 1`. Em seguida, [`PublishBackend`](file:///port/src/graphics/graphics_options.cpp#L91-L114) aceita o relatório do frame 10 com `request_revision = 1` pois `1 < state_.completed_revision (0)` é falso e `1 > state_.request_revision (2)` é falso. O menu passa a anunciar RT Shadows "On" aplicado durante a vigência de uma solicitação de Off.
   * **Correção mínima:** Em [`RayShadowExecution`](file:///port/src/graphics/ray_shadow_execution.h#L18-L28), vincular a aceitação à revisão ativa esperada (`pending.revision == current_revision`) e zerar `latest_.applied[0]` imediatamente na solicitação de Off/invalidação; em [`PublishBackend`](file:///port/src/graphics/graphics_options.cpp#L101-L105), se `report.request_revision < state_.request_revision` e `state_.requested[row] == 0`, rejeitar a elevação de `state_.applied[row]` para 1.

2. **Ausência de invalidador de época/backend em `RayShadowExecution`**
   * **Referência:** [`port/src/graphics/ray_shadow_execution.h:4-35`](file:///port/src/graphics/ray_shadow_execution.h#L4-L35) e [`port/src/graphics/graphics_options.cpp:79-89`](file:///port/src/graphics/graphics_options.cpp#L79-L89)
   * **Cenário:** Em reinicialização de dispositivo ou troca de API ([`GraphicsOptions::BeginBackend`](file:///port/src/graphics/graphics_options.cpp#L79-L89)), o `backend_epoch` incrementa e `state_.applied` é zerado. Porém, [`RayShadowExecution`](file:///port/src/graphics/ray_shadow_execution.h#L4-L35) não armazena época nem possui método de descarte. Tickets remanescentes ou `latest_` preservado de uma sessão anterior podem alimentar o novo backend com comprovações de contadores/fences caducados da época anterior.
   * **Correção mínima:** Adicionar campo `epoch` em `Ticket` e método `Reset(uint64_t epoch)` em [`RayShadowExecution`](file:///port/src/graphics/ray_shadow_execution.h#L4-L35), chamado obrigatoriamente durante `BeginBackend`.

3. **Data race em `RayShadowExecution` (falta de sincronização multithread)**
   * **Referência:** [`port/src/graphics/ray_shadow_execution.h:12-34`](file:///port/src/graphics/ray_shadow_execution.h#L12-L34)
   * **Cenário:** `Submit` é invocado na thread de renderização, `Complete` no encerramento/polling de fence, e `Latest()` é lido pela thread de opções/UI. Os membros `slots_` e `latest_` são acessados e modificados concorrentemente sem exclusão mútua (`std::mutex`) nem operações atômicas, causando leituras parciais/inconsistentes de `Result`.
   * **Correção mínima:** Adicionar um `std::mutex mutex_` interno em [`RayShadowExecution`](file:///port/src/graphics/ray_shadow_execution.h#L4-L35) cobrindo `Submit`, `Complete` e `Latest`.

4. **Falta de gateway forçado contra suporte falso para DLSS e RT Reflexos**
   * **Referência:** [`port/src/graphics/graphics_options.cpp:96-100`](file:///port/src/graphics/graphics_options.cpp#L96-L100)
   * **Cenário:** DLSS e RT reflexos estão incompletos na base. Em [`PublishBackend`](file:///port/src/graphics/graphics_options.cpp#L96-L100), a validação aceita `applied[row] = 1` contanto que o relatório afirme `implemented && capable`. Se um backend provisório ou teste emitir `Ready()` com todas as opções ativas, `PublishBackend` acata DLSS e Reflexos como aplicados sem validação de contrato real.
   * **Correção mínima:** Em [`PublishBackend`](file:///port/src/graphics/graphics_options.cpp#L96-L100), impor restrição explícita (`row == kDlss || row == kRtReflections ? report.applied[row] == 0 : true`) até que a implementação completa desses subsistemas seja integrada.

5. **Lacuna no teste unitário de ressurreição**
   * **Referência:** [`tests/native/test_ray_shadow_execution.cpp:17-28`](file:///tests/native/test_ray_shadow_execution.cpp#L17-L28)
   * **Cenário:** O teste `ray_shadow_newer_off_cannot_be_resurrected_by_late_old_completion` apenas testa a conclusão do frame 3 (Off) *antes* da conclusão do frame 2 (On). Não cobre o caso em que o frame antigo conclui antes do novo frame finalizar seu fence, cenário no qual o código atual ressuscita `applied[0] = 1`.
   * **Correção mínima:** Adicionar caso de teste onde `Complete(old_slot)` ocorre enquanto a revisão 2 está pendente, verificando que `Latest()` não reporta On da revisão caduca.

---

### Limitações Explícitas
* **Modo de operação:** Análise puramente estática e de leitura de código-fonte. Nenhuma compilação, teste automatizado, comando PowerShell ou processo foi executado neste diagnóstico.
* **Cobertura de renderização:** O renderer nativo integral (`native_renderer.cpp`, >9000 linhas) e pipelines GPU D3D12/HLSL não foram varridos por completo, limitando-se aos gateways de interface, sincronização de opções e controle de execução de sombras traçadas/DLSS especificados.
* **Fidelidade visual:** Nenhuma avaliação da qualidade visual das sombras ou reconstrução temporal pôde ser realizada por ausência de capturas/imagens e conformidade estrita com o modo de revisão textual.
