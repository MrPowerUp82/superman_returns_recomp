# Troca de texturas e traje Reino do Amanhã — design (28/10/2026)

## Intenção

O usuário quer jogar com o Superman do **Reino do Amanhã**. A versão de Xbox 360 já tem três trajes (`text.AST`: `Costume_Name_0` "Superman Returns", `_1` "Pod Suit", `_2` "Golden Age Superman"), com menu "Costumes" e desbloqueio jogando (`unlockcostume`, `awardcostume`, `setcostumenum`, `SetActiveCostume`). O Golden Age tem modelo e capa próprios (`supermanclassicent`, `supermanclassicclothcape`).

O plano é repintar as texturas do Golden Age como Reino do Amanhã em tempo de execução, no renderer nativo. A arte é criada por Claude como uma **receita de pintura no código do jogo**: hashes das texturas originais mais operações de pintura (recolorir, desenhar o emblema por formas vetoriais, clarear as têmporas). Ela é aplicada aos pixels originais do jogo de cada pessoa quando a textura é criada. Assim o traje funciona **para qualquer pessoa que baixar o projeto** (decisão do usuário), sem distribuir nada do jogo e sem passo de geração. O usuário aprova a arte por prints no jogo. Por baixo fica um sistema genérico de dump e substituição de texturas por PNG, reaproveitável para mods do próprio usuário (e para o "remaster com texturas novas" citado pelo gangg111).

## Objetivos

1. **Dump:** uma opção grava as texturas que o jogo usa, uma vez por textura distinta, com nome estável derivado do conteúdo. Serve para achar as texturas do Golden Age.
2. **Substituição:** o renderer procura, numa pasta, um arquivo com o mesmo nome e usa a imagem nova no lugar da original, nos backends **D3D12 e Vulkan**.
3. **Arte (receita no código):** o Golden Age repintado como o **Reino do Amanhã da CW, vestido por Brandon Routh** (o mesmo ator do filme do jogo, na série Crisis on Infinite Earths). Referência visual dada pelo usuário em 28/10. A receita parte dos pixels originais de cada jogo:
   - traje **azul**, no tom do filme (não cinza);
   - **emblema do Reino do Amanhã**: escudo vermelho com o "S" estilizado em formas pretas, desenhado por formas vetoriais próprias no lugar do emblema do Golden Age;
   - **sunga vermelha**, **cinto preto com fivela dourada oval** e **botas vermelhas**, ajustando as partes que o Golden Age já tem no modelo; se alguma não existir na textura, registrar e falar com o usuário;
   - **capa vermelho-escura**;
   - cabelo como no original (no visual da referência, o grisalho é discreto; fica como ajuste opcional).
4. **Vestir direto:** um campo "Traje" no launcher (Padrão do jogo, Superman Returns, Pod Suit, Reino do Amanhã) que vira `--sr_costume=default|0|1|2`. Ele veste o traje escolhido ao entrar no gameplay, sem precisar desbloquear. "Reino do Amanhã" é o traje 2 com as texturas novas. "Padrão do jogo" não interfere.
5. Texturas novas ligadas por padrão, com opção para desligar e ver o Golden Age original.

## Fora do escopo

- Mudar geometria, capa ou proporções do modelo. Criar um quarto traje.
- Arte extraída do Nintendo DS ou de outro jogo, que só serve como referência visual.
- Texturas em resolução maior que a original (a primeira versão exige as mesmas dimensões; ver Restrições).
- Ferramenta de edição dentro do jogo.

## Não pode acontecer

- Trocar textura errada: uma textura do cenário ou de outro traje pintada de cinza. A identidade da textura precisa ser exata.
- Os trajes Superman Returns e Pod Suit mudarem.
- Distribuir texturas do jogo ou imagens derivadas delas (no Git, no pacote ou no instalador). Os dumps ficam fora do Git. A receita contém só hashes, cores e formas criadas para este projeto.
- O traje depender de arquivos gerados em uma máquina específica: precisa funcionar em qualquer instalação com o mesmo `default.xex`.
- Perder desempenho perceptível quando não há pasta de substituição: a busca precisa ser barata e feita só quando a textura é criada, não por quadro.
- Forçar o traje corromper o save, ou travar o menu de trajes.

## Fatos levantados (28/10)

- Os dois backends passam pela textura já decodificada (`graphics::guest::LinearTexture`, em `port/src/graphics/guest/texture_layout.h`): no D3D12, em `Renderer::CreateTexture` (`DecodeTexture` antes do upload, `native_renderer.cpp`); no Vulkan, no decodificador `guest::DecodeTextureLayout` injetado pelo `native_provider.cpp`, com upload em `ResourceStore::UploadTexture`. É o ponto comum natural para trocar o conteúdo.
- As texturas já têm hash de conteúdo para revalidação (`HashTextureContent`, XXH3), mas esse hash mistura faixas de memória do guest. A chave de troca precisa ser estável entre execuções e independente do endereço onde a textura foi carregada.
- O tradutor de shaders compensa offsets de amostragem usando as dimensões da textura (`getGuestTexture2DDimensions`, `TEX_SCALE`), então uma troca com outras dimensões quebraria os offsets.
- O ponto do jogo que guarda o traje atual ainda não foi localizado. Há os nomes `SetCostumeNum`/`SetActiveCostume`/`setcostumenum` (scripts e UI Apt) e `costume_lod0..2` em `objtiv.AST`.

## Como funciona (decidido)

- **Chave:** hash do conteúdo **decodificado** (bytes de `LinearTexture::data` mais largura, altura, formato e número de mips). Não depende de endereço e é igual nos dois backends. Nome do arquivo: `<hash em 16 hex>.png`.
- **Dump:** `--sr_texture_dump_dir=<pasta>` grava cada textura distinta uma vez, no nível 0 (mip base), convertida para PNG RGBA. Também grava um `index.csv` com hash, dimensões, formato original e o primeiro quadro em que apareceu. Desligado por padrão.
- **Substituição:** `--sr_texture_replace_dir=<pasta>` (padrão: `textures/` ao lado do executável; vazia ou ausente não custa nada). Ao criar uma textura, se existe `<hash>.png` com as mesmas dimensões, a imagem é carregada como RGBA8, os mips são gerados, e ela segue pelo upload normal no lugar da original. Dimensões diferentes: ignora e registra um aviso uma vez. `--sr_texture_replace=false` desliga.
- **Traje:** `--sr_costume=default|0|1|2`. Com 0–2, o jogo é levado a usar esse traje ao entrar no gameplay, pelo mesmo caminho que o menu usa, sem gravar desbloqueio permanente no save, se for possível separar as duas coisas.
- **Receita do Reino do Amanhã:** tabela compilada `hash original → operações`, aplicada no mesmo ponto da troca por PNG, só quando o traje escolhido é "Reino do Amanhã" (`--sr_costume_skin=kingdom_come`, enviado pelo launcher junto com `--sr_costume=2`). Operações puras sobre RGBA8: mapear o azul do traje para cinza-escuro preservando a luminância e o sombreamento; desenhar o emblema por formas vetoriais na região do emblema; clarear mechas de cabelo numa região. Um PNG do usuário com o mesmo hash tem prioridade sobre a receita.
- **Launcher:** campo "Traje" com as 4 opções, salvo nas preferências. "Reino do Amanhã" envia `--sr_costume=2 --sr_costume_skin=kingdom_come`; "Golden Age" não existe como opção separada, mas `--sr_costume=2` sem a skin mostra o original.

## Restrições

- Mesmas dimensões e mesmo número de canais lógicos da original. Formatos comprimidos (DXT/BC) viram RGBA8 na troca; a perda de memória é aceitável para poucas texturas.
- A troca acontece só na criação da textura (e na recriação, quando o conteúdo do guest muda); o caminho por quadro não muda.
- Nada do jogo entra no Git: dumps em `logs/`, ignorados. A receita entra (hashes, cores e formas próprias).

## Decisões minhas (podem ser revistas)

- PNG como formato de troca (fácil de editar; DDS fica para depois).
- Chave pelo conteúdo decodificado, não pelo endereço.
- Pasta padrão `textures/` ao lado do executável.
- A receita é C++ puro, testado com imagens sintéticas, sem dependência do SDK. Prototipar os tons e o emblema em Python sobre o dump local é permitido; o que vale é a receita em C++.
- A receita usa coordenadas da textura (regiões do UV) definidas a partir do dump. Se o hash não bater (outra versão do jogo), nada é aplicado e um aviso aparece uma vez no log.

## Deixado para quem construir

- Achar no código do jogo onde o traje atual é guardado e como forçá-lo com segurança (função chamada pelo menu Apt, variável do perfil, etc.). Se não houver caminho seguro, parar e falar com o usuário antes de improvisar.
- Identificar quais texturas do dump são do Golden Age (comparando o dump com o traje vestido e com outro traje vestido).
- Onde exatamente ligar a troca em cada backend, desde que os dois usem a mesma chave e o mesmo carregador.
- O traço do emblema e os tons exatos, que se ajustam pelos prints com o usuário.

## Verificação esperada

- Testes unitários da chave (estável e independente de endereço), do carregamento e da geração de mips, da recusa por dimensões diferentes, e das operações da receita sobre imagens sintéticas (recolorir preserva sombreamento, emblema dentro da região, nada fora dela).
- Dump numa sessão com o Golden Age vestido: as texturas do traje identificadas e listadas.
- Com a pasta de troca: prints no jogo, em D3D12 e Vulkan, do Golden Age virando Reino do Amanhã. Os outros dois trajes e o cenário sem mudança, comparando prints com e sem a pasta.
- Benchmark com e sem a pasta de troca: sem diferença de FPS além da variação conhecida.
- `--sr_costume` 0, 1 e 2 vestem os trajes certos; "default" não interfere; o save não ganha desbloqueios por causa da opção, se isso for separável.

## Resultados (2026-10-29)

- A skin acabou pintada sobre o traje do filme, a pedido do usuário. O escudo foi desenhado a partir da referência que ele enviou, e a arte foi aprovada num print do jogo em D3D12.
- `--sr_costume=2` veste o Golden Age no gameplay com uma única escrita (`docs/kingdom-come-texture-map-2026-10-28.md`).
- Benchmark headless, D3D12, 1280x720, três rodadas:
  - sem a skin: 56,0 / 60,1 / 57,1 FPS (média 57,7);
  - com a skin: 64,7 / 60,3 / 61,3 FPS (média 62,1).
  - A diferença está dentro da variação conhecida (±4,5%): a skin não tem custo mensurável.
- Vulkan com a skin: 45,9 FPS, e o log confirma as duas texturas trocadas pela receita. O resultado no Vulkan não foi conferido visualmente.
- Bug corrigido no caminho: o D3D12 enviava a textura trocada com o formato comprimido original, o que virava ruído.
