# Referências externas para o renderizador nativo

Pesquisa realizada em 2026-10-07. Foram consultados repositórios públicos e
trechos de implementação, comparados com o estado local após a quarta rodada
de medição. As propostas abaixo são adaptações a validar no Superman Returns;
não são resultados de desempenho já obtidos aqui.

## Resultado principal

A direção mais promissora é invalidar texturas quando seus produtores escrevem
nelas, mantendo uma versão do conteúdo compartilhada pelas duas APIs. Isso
permitiria retirar hashes completos do caminho normal das texturas cuja
cobertura de escritas estiver comprovada. A melhor referência de arquitetura
encontrada foi o Unleashed Recompiled. Para uma mudança menor e independente,
o cache do bloco de constantes compartilhadas é outro candidato concreto.

O alvo local continua relevante: a última rodada D3D12 mostrou aproximadamente
54.7 mil KiB de hashing por quadro e 5.49 ms nesse trabalho. Esses valores são
de uma cena específica e não permitem prever o aumento de FPS de outra solução.
Detalhes em [native-renderer-performance.md](native-renderer-performance.md).

## Projetos e evidências

### Unleashed Recompiled

Revisão examinada: `cf829a9eca8fb680fba4b0409ddeb6ca92f22e3c`.

`LockTextureRect` disponibiliza a memória da textura. `UnlockTextureRect`
enfileira um comando, e `ProcUnlockTextureRect` copia os bytes para upload e
grava a transferência para a textura GPU. Esse caminho tem uma notificação
explícita de atualização. O renderer também mantém estados de constantes
alterados e condiciona os uploads de VS, PS e constantes compartilhadas a esses
estados. Fontes: [atualização de textura](https://github.com/hedge-dev/UnleashedRecomp/blob/cf829a9eca8fb680fba4b0409ddeb6ca92f22e3c/UnleashedRecomp/gpu/video.cpp#L2168-L2210)
e [uploads condicionais de constantes](https://github.com/hedge-dev/UnleashedRecomp/blob/cf829a9eca8fb680fba4b0409ddeb6ca92f22e3c/UnleashedRecomp/gpu/video.cpp#L4520-L4536).

Aplicação aqui: identificar os produtores de texturas dinâmicas e enfileirar
invalidações na ordem dos comandos. Um hook de `Unlock` isolado não basta:
nosso PM4 aceita texturas sem objeto D3D no shadow do dispositivo. É preciso
cobrir também vídeos, streaming, escritas diretas e reutilização de endereços.
Os endereços desses produtores no Superman ainda não foram identificados nesta
pesquisa. Os endereços de hooks do Sonic não são transferíveis ao nosso jogo.

### Need for Speed: Most Wanted — nfsmw-nx

Revisão examinada: `4e3ffc24fe80285189f2cea601606c529d5f1b97`.

Texturas estáveis aumentam progressivamente o intervalo de verificação; o
código limita o intervalo base a 32 quadros e acrescenta uma distribuição de
0–7 quadros no limite. Existem orçamento de leitura por quadro e um caminho
opcional de hashes por amostragem, com comparações completas de controle e
desativação quando encontra divergência. Fontes:
[documentação do renderer](https://github.com/StevensND/nfsmw-nx/blob/4e3ffc24fe80285189f2cea601606c529d5f1b97/docs/native-renderer.md#each-draw)
e [checagem completa e reagendamento](https://github.com/StevensND/nfsmw-nx/blob/4e3ffc24fe80285189f2cea601606c529d5f1b97/app/src/nfsmw_nativo_dibujos.cpp#L8955-L8999).

Aplicação aqui: orçamento e distribuição de verificações podem reduzir picos.
Porém, o Vulkan local já tem espera progressiva de até 16 quadros; implementar
novamente esse mecanismo não seria uma novidade. O D3D12 ainda verifica por
quadro todas as texturas de até 4 MiB. Transferir o intervalo maior para esse
caminho exige tratar uma textura estável que passa a mudar: sem notificação
confiável, a primeira alteração pode aparecer atrasada. Amostragem também pode
deixar de ler justamente os bytes alterados. São políticas experimentais, não
substitutos de uma invalidação completa.

### Conan — referência do rexglue-native-kit

Revisão examinada: `adf4e8ea24c3635a05db76959f392c16cdc55476`.

O cache de texturas consulta as sequências de escrita quando o watch está
ativo. Sem watch, verifica texturas pequenas por quadro e usa uma periodicidade
maior para as demais. O nosso renderer deriva dessa implementação, mas passou
a manter o hash por quadro mesmo com watch para as texturas de até 4 MiB.
Fonte: [GetTextureSrvIndex e decisão de revalidação](https://github.com/crazyriddler/rexglue-native-kit/blob/adf4e8ea24c3635a05db76959f392c16cdc55476/reference/conan/port/src/native/native_renderer.cpp#L2412-L2440).

Aplicação aqui: esse é um exemplo de caminho barato quando as notificações
cobrem os escritores. Simplesmente restaurar a condição do Conan retiraria a
proteção que nosso fallback oferece aos caminhos de escrita não observados.

### Outras referências examinadas

Foi feita uma leitura preliminar do renderer de cena do
[Skate 3 Recomp](https://github.com/mchughalex/skate3recomp/blob/f6e0ae87fdfecbadb5c1e36c55d66a744187a3cd/src/skate3_native_scene_gpu.cpp#L4584-L4633),
que prepara texturas antecipadamente usando snapshots dos descritores e
deduplicação. Esse trecho, sozinho, não estabelece como todas as alterações
dos bytes são invalidadas; ele não fundamenta a proposta principal.
O endereço público encontrado para Rayman Origins retornou 404 na API do
GitHub e não foi usado como evidência técnica.

## Lacunas concretas no nosso código

| Área | Estado local | Consequência |
| --- | --- | --- |
| Texturas D3D12 | `GetTextureSrvIndex` faz fallback de hash por quadro até 4 MiB | Trabalho continua mesmo sem alteração de conteúdo |
| Texturas Vulkan | `CaptureTextures` combina watches, snapshots e espera progressiva | Parte da abordagem do NFSMW já existe |
| Invalidação explícita | `InvalidateGuestRange` percorre `buffer_pages_` e `TrackedBuffer` | O mecanismo dos hooks VB/IB não invalida texturas |
| Bindings de vídeo/UI | `texture_binding.h` aceita fetch PM4 sem objeto D3D | Hooks apenas em objetos de textura deixam lacunas |
| Constantes VS/PS | `UploadConstants` já usa versões do mirror | Não reaplicar um cache que já existe |
| Constantes compartilhadas | `UploadConstants` aloca 4096 bytes a cada draw | Falta cache do bloco final e de sua alocação GPU válida |
| Descritores Vulkan | `DescriptorStore::Shared` já tem cache e retenção por submissão | Não substituir esse gerenciamento por índices sem lifetime |

Um detalhe do SDK reforça a necessidade de investigar os escritores: a
implementação consultada arma callbacks nas regiões A/C/E e trata acessos por
`physical_membase_` como um caminho que contorna callbacks. Isso mostra uma
possível categoria de escrita não observada; não prova qual caminho o vídeo
do Superman usa. Fonte: [tratamento de acesso físico](https://github.com/StevensND/nfsmw-nx/blob/4e3ffc24fe80285189f2cea601606c529d5f1b97/sdk/src/system/xmemory.cpp#L556-L568).

## Adaptação recomendada

1. **Auditar cobertura mantendo o resultado atual.** Nas verificações completas
   existentes, registrar por faixa física: bytes, tempo, mudança de hash e
   mudança de sequência de escrita. Separar mudanças com e sem notificação.
   Identificar os produtores das faixas que mudam sem notificação. O modo de
   auditoria mantém os hashes atuais e não muda a imagem.
2. **Adicionar versões de conteúdo para texturas com produtores cobertos.**
   Invalidações explícitas precisam alcançar texturas nas duas APIs, inclusive
   mipmaps, sobreposições e reutilização de memória. Preservar a ordem entre
   escrita, captura e draw; não alterar um recurso GPU que uma submissão antiga
   ainda usa. Texturas sem cobertura continuam com a política conservadora.
3. **Retirar hashes apenas onde existe contrato de invalidação completo.**
   Hashes de auditoria detectam falhas, mas uma sequência de amostras sem erro
   não prova que todos os escritores foram cobertos. Não promover uma textura
   a imutável somente porque ficou estável durante alguns quadros.
4. **Medir o cache de constantes compartilhadas, separadamente.** Construir o
   bloco final em memória CPU normal, comparar seus bytes com o último bloco
   válido e reutilizar a alocação apenas quando forem idênticos. Incluir todos
   os valores derivados: SRVs, samplers, escala, alpha test, dados de vértices,
   viewport e efeitos. Invalidar o cache no reset do upload ring/frame e
   reaplicar os bindings no command list. Isso adapta a ideia de estado alterado do Unleashed sem
   presumir que uma versão PM4 cobre os valores derivados.

Critérios de aceitação: testes de escrita física e por alias, primeira mudança
após estabilidade, mudança só de mipmap, reutilização de endereço, vídeo/UI,
resolves e recursos ainda em uso; comparação das duas APIs e de trechos de
vídeo, além da cena inicial; medições pareadas repetidas com draws e carga
comparáveis. Registrar bytes de hash evitados, invalidações não observadas,
acertos do cache de constantes e tempo de CPU, sem deduzir ganho só do FPS.

Nenhum código externo foi incorporado ao renderer. Snapshots de código e
metadados das revisões consultadas estão em `build/native-reference-research/`
(local, ignorado pelo Git).

## Primeira implementação da proposta

O experimento D3D12, ativado com `SR_NATIVE_SHARED_CONSTANTS_CACHE=1`, monta o bloco compartilhado final em memória CPU e
compara seus 4096 bytes com a última alocação válida. Um acerto reutiliza o
endereço GPU; uma mudança cria uma alocação imutável e só então atualiza o
cache. O reset do upload ring invalida o endereço. Os três bindings continuam
sendo emitidos em cada draw, inclusive depois de um reset do command list.
Essa alteração não alcança o caminho de constantes Vulkan. O experimento fica
desligado por padrão: a primeira medição evitou uploads, mas aumentou o tempo
de preparação. Sem a variável, permanece a montagem direta no upload original.

`SR_NATIVE_TEXTURE_AUDIT=1` ativa uma auditoria das verificações de hash já
existentes no D3D12 e na captura Vulkan. Ela conta bytes, verificações,
mudanças e mudanças sem notificação; consulta novamente a sequência após o
hash para considerar escritas concorrentes. Há até 64 detalhes por thread
(base, formato, tamanho e presença de watch), além dos totais por 120 frames.
Os totais excluem o intervalo final incompleto. A auditoria adiciona trabalho
e deve ser desligada nas comparações de desempenho.

Essa primeira etapa não instala hooks de produtores nem retira hashes.
Os endereços registrados orientam a investigação dos escritores, mas não
identificam por si só o PC que escreveu e não demonstram cobertura completa.
Resultados medidos e validação estão em `native-renderer-performance.md`.
