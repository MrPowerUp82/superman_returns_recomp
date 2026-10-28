# Submissão nativa e limites do bypass XDK

## Caminho implementado

`SR_NATIVE_XDK_DRAW_SUBMISSION=1` seleciona a substituição experimental de
`820FBBF8`/`820FC000` para draws normais/indexados de até 65.535 elementos.
O caminho monta diretamente os comandos das constantes float, registradores
escalares e fetches, sem executar os emissores recompilados `821078C0`,
`82107520` e `82107760`. O renderer e o consumidor de sincronização existentes
continuam recebendo o mesmo PM4. A configuração permanece experimental.

São preservados a ordem dos pacotes, o padding Type2, o comando de draw,
os índices de 16/32 bits, endian, aliases físicos e o marcador de invalidação
de bool/loop constants. As escritas são preparadas em CPU e só publicadas se
toda a submissão couber. O limite físico do ring e a reserva final de 160
bytes são distintos. Caminhos que exigiriam o writer lento ou troca de ring
voltam ao código original antes de alterar a memória guest.

Continuam no original: preparação de shaders/literais (`82108668`), estado
especial de RT (`821072F8`), stream-out (`821075E0`), predicação, draws divididos,
inline vertices, clear/resolve/tiling, setters e gerenciamento de recursos.
Portanto **não é bypass integral de todo o Direct3D XDK**.

## Verificação

O teste independente em `tests/xdk` extrai as rotinas geradas deste jogo como
oráculo. Compara 2.048 draws nativos/gerados, cobrindo bits individuais,
máscaras esparsas/aleatórias e máximas, alinhamentos, diferentes primitivas,
visibilidade, endian, endereços e índices. Além dos registradores do mirror e
efeitos no device, compara o **command stream inteiro, byte a byte**. Isso
inclui o payload de DRAW_INDX que o mirror não interpreta.

Os testes de recusa cobrem reserva final, truncamento, estados não portados,
overflow/aliases de ring e commits de bools. Duas falhas de borda foram
reproduzidas antes das correções: reserva final consumida e escolha incorreta
do footer de fetch perto do fim do ring. A revisão independente conferiu as
correções e não encontrou P1/P2 restantes no escopo admitido.

## Execuções intermediárias

- `native_focus_xdk_shadow_on_20261027`: o wrapper inicial recusou todos os
  rings físicos. Não valida o novo caminho nem mede ganho.
- `native_focus_xdk_on2_20261027`: versão restrita a constantes float,
  aproximadamente 0,3% de draws substituídos; gameplay aprovado.
- `native_focus_generic_on_20261027`: primeiro port de registradores/fetch,
  aproximadamente 54% substituídos; gameplay aprovado, porém anterior à
  correção final do padding. Seus 28,2/32,3 FPS não são aceitação da versão final.

Os contadores de submissão são cumulativos por thread e incluem inicialização.
Variação de câmera e estado do tutorial impede atribuir diferença entre essas
execuções a uma otimização isolada.

## Relação com schedulers e sombras

O mapa dos quatro peers está em `guest-scheduler-peer-evidence-2026-10-27.md`:
core, render, SIM e áudio. Não são quatro gates independentes de apresentação.
O desbloqueio do peer render já existente é preservado; nenhum período fixo de
simulação/áudio foi removido.

A auditoria `d3d12-stacked-readback-audit-2026-10-27.md` registra o caminho de
fachadas no sampler4, independente do shadow sampler15, e a correção da
exportação de volumes. Duas capturas reais confirmaram quatro slices distintos
e Z3 zerado; a segunda exportou 31/31 recursos sem a rejeição indevida do
formato do plano de depth 2D. Isso comprova armazenamento/exportação distintos,
sem afirmar fidelidade visual ao Xbox.

## D/F cached shader-prep sem programa (bits 49+51+52, com/sem 50)

- Origem: logs xdk_df_branchmap/cachemap mostram D e F dominando as recusas (shader_mask=D:14182, F:6533 num run), todos pelo ramo 82108520 (cache); direct08300=0.
- Divergência corrigida: o XDK real (82108668) emite dois pacotes 0xC0012700 (bloco microcode do VS no ramo bit 52 com address|1; bloco do PS em 82108CB0) e seta o bit 46, logo 0x2180 inclui o registrador 10408 (= word vs_meta+24). Nativo agora reproduz.
- Escopo: apenas shader_program (10808 bit 6) = falso; com programa, recusa atômica (testada: ring/device inalterados).
- Validação: sr_xdk_draw_tests exit 0 (2048 draws + fixtures D/F byte a byte), sr_native_tests 3/3, build Release OK.
- Pendente: A/B ON/OFF de FPS/cobertura no mesmo binário; ramo program; cache miss (82108520 -> 82107A30).

## D/F cached com programa inline (10808 bit 6)

- Ordem real de emissao (82108668): 4600, P1 (0xC0012700 VS), 6000/15555555/6100/0, 2208, C0012D01, 2180 (10400/10404; bits 47/48 consumidos, bit 46 permanece), P2 (0xC0012700 PS), 6000/12432/6100/12436, 2203.
- O ramo alternativo C0022D01 NAO ocorre (82108A4C testa 10810 bit 6, ja zerado): o oracle comprovou.
- Validacao: sr_xdk_draw_tests exit 0 (D/F com e sem programa byte a byte), sr_native_tests 3/3, Release OK.
- ABAB (logs/xdk_prog_*): FPS ON 22.6/18.3 vs OFF 18.3/24.3 -> ruido domina, sem conclusao. Cobertura ON 93.8% e 90.4% (submitted/total), sem melhora clara sobre ~93.6% anterior.
- Pendente: cache miss (82108520 -> 82107A30), medir recusas por motivo, benchmark com cena deterministica.

## Diagnostico Granular de Recusas (2026-10-27)

- Adicionados contadores granulares (XdkDrawRefusal) em xdk_draw_submission.h e telemetria em native_hooks.cpp.
- Dados de gameplay (logs/xdk_refusal_diag_20261027.log):
  - Total submetido: 766.689 draws (89.9%)
  - Total recusado: 73.950 draws
  - **vs_list = 61.702 (83.4% de todas as recusas)**
  - ps_list = 938
  - gen_unsupported = 11.310 (outras mascaras como 5 e 9)
  - miss_ptr = 0, mode_not_4 = 0, dev_flags = 0, prog_bit48 = 0, ps_alt20 = 0, meta_diff = 0, heavy_ps40 = 0, dev10808_80 = 0, cache_tag = 0, cache_mask = 0, unreadable = 0.
- **Descoberta fundamental:** o cache de shaders (82108520) esta dando 100% HIT nos casos D/F. O unico motivo bloqueando mais de 83% das recusas restantes e a presenca de listas de constantes em 82108470 (vs_list != 0), que emite pacotes 0xC0022F00 na ring buffer.
