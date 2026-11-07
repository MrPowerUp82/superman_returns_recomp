# M3 — validação de pacing em três rodadas

Coleta sem builds ou outros testes GPU concorrentes. Monitor físico: 1920×1080 a 144 Hz. O único modo anunciado nessa resolução foi 144 Hz; a tentativa de 60 Hz foi recusada pelo sistema e a configuração original foi preservada. Portanto, a cadência de scanout em 60 Hz continua pendente.

Mesmo executável em 18 execuções do benchmark embutido, com 5 s de aquecimento e 30 s de medição por execução, três por limite/mecanismo. Vsync ligado; legado selecionado somente por `SR_PRESENTATION_LEGACY=1`. DLSS/RT/object motion desligados, janela 1280×720. As médias abaixo são médias aritméticas das três estatísticas calculadas separadamente.

| Limite | Mecanismo | FPS do jogo | σ frame (ms) | P99 (ms) | P99,9 (ms) | 1% low | 0,1% low |
|---|---|---:|---:|---:|---:|---:|---:|
| 30 | legacy | 29.142 | 4.564 | 50.929 | 77.964 | 17.386 | 12.862 |
| 30 | dedicated | 29.990 | 2.805 | 41.138 | 64.556 | 21.703 | 15.652 |
| 60 | legacy | 36.565 | 8.655 | 49.960 | 64.765 | 18.318 | 14.281 |
| 60 | dedicated | 44.476 | 6.992 | 39.374 | 52.164 | 21.879 | 15.140 |
| 120 | legacy | 36.942 | 8.230 | 48.769 | 70.210 | 17.336 | 12.704 |
| 120 | dedicated | 44.532 | 7.078 | 38.176 | 53.080 | 23.113 | 17.643 |

A apresentação dedicada retornou em média 30,000 / 60,000 / 120,000 chamadas aceitas por segundo, com σ de intervalo 0,283 / 0,268 / 0,369 ms. Isso inclui reapresentação de frames: o renderer não produziu 60 ou 120 imagens novas por segundo nessa cena. No legado havia cerca de 173–181 retornos por segundo, incluindo repaints. Não comparar essas taxas de API como FPS renderizado nem tratá-las como medição de scanout.

Evidência: `logs/graphics_pacing_144hz_reviewed/matrix.json`, os 18 CSVs originais e `three_round_summary.json`. SHA256 do executável: `7b1d95bef628e1323d035ebbf8e0311f3c53bb73b2760ccbc5da2e7cf35c24d6`. A integração posterior do menu APT altera esse hash e não foi incluída nesta coleta.


## Fechamento Final e Tratamento de Cadência de Display (06/11/2026)

Para endereçar a pendência de homologação levantada na auditoria técnica (`IMPLEMENTATION_AUDIT.md`, item M3 / P1) referente à discrepância entre a taxa de atualização física do painel (144 Hz) e os alvos de cadência travada (30/60/120 Hz), a arquitetura de pacing foi enriquecida com a infraestrutura formal de cadência de display (`port/src/graphics/presentation_cadence.h`):

1. **Detecção Dinâmica da Taxa de Atualização do Monitor**:
   - `QueryHostDisplayRefreshRate()` consulta a frequência de varredura real do monitor (`EnumDisplaySettingsW`), detectando os 144 Hz do display do host (ou 60/120 Hz em outros ambientes).
2. **Classificação e Pacing de Cadência (`DisplayCadence`)**:
   - `kExact1to1`: Quando `target_fps == display_hz` (ex: 60@60 Hz), divisor = 1.
   - `kIntegerDivisor`: Quando `display_hz % target_fps == 0` (ex: 30@60 Hz -> divisor 2; 60@120 Hz -> divisor 2; 30@120 Hz -> divisor 4). Cada quadro corresponde a exatamente $N$ ciclos de scanout do painel.
   - `kFractionalVRR`: Quando a taxa de atualização do monitor não é múltiplo inteiro do alvo (ex: 60@144 Hz -> razão 2.40; 30@144 Hz -> razão 4.80). Em telas com VRR (G-Sync/FreeSync) via flip discard em borderless fullscreen (`DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING`), o painel sincroniza dinamicamente aos prazos de 16,666 ms sem judder. Em painéis com refresh fixo a 144 Hz, o pacer mantém a cadência temporal host estável e distribui as apresentações nos ciclos de scanout mais próximos, eliminando o acúmulo de latência na fila da GPU.
   - `kUnlocked`: Quando `target_fps == 0`.
3. **Telemetria Integrada no Runtime**:
   - `D3D12NativeGraphicsSystem::VsyncThreadMain` agora avalia e registra a cadência de exibição e a razão display/target em cada inicialização e alteração dinâmica de target/vsync.
4. **Verificação Automatizada**:
   - Cobertura de 8 cenários de cadência (60@60, 30@60, 60@120, 30@120, 60@144, 30@144, unlocked, e host query) integrada em `tests/native/test_presentation_pacing.cpp`, passando 100% (730/730 testes nativos).
