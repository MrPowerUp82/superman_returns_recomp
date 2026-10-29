# Mapa de texturas do traje Golden Age (2026-10-28)

Base para a skin Reino do Amanhã (`docs/superpowers/plans/2026-10-28-texture-replacement-kingdom-come.md`, Task 4). Nenhum PNG do jogo está no repositório: as chaves abaixo identificam a textura no jogo de quem o executa, e a Task 5 repinta esses pixels em código.

## Como foi levantado

- Dois dumps (`--sr_texture_dump_dir`) no mesmo trecho do jogo (título → cinemática de abertura → Metropolis), D3D12 nativo:
  - sem a troca: o Superman com o traje do filme (`logs/tex_dump_manual`);
  - com `--sr_costume=2`, Golden Age visível no gameplay (`logs/tex_dump_manual2`).
- `python tools/texture_mods/find_costume_textures.py logs/tex_dump_manual logs/tex_dump_manual2` lista 151 chaves só do segundo dump. A maioria é streaming da cidade. As do traje são as que o jogo carrega juntas, logo depois da troca, e foram conferidas abrindo os PNGs.
- As regiões foram medidas por componente de cor numa grade de 4 px (os retângulos têm essa margem).

## Texturas do Golden Age

| Chave | Tamanho | Formato | O que é |
|---|---|---|---|
| `E4620603169D8112` | 512×512 | 8 | Corpo: malha azul (tronco, braços, pernas) com o emblema no canto superior direito |
| `540BE073775E9D42` | 512×512 | 11 | Normal map do corpo |
| `96AE4603385ED3F2` | 512×512 | 8 | Capa, botas e cinto |
| `CA4625C34C0811B4` | 512×512 | 11 | Normal map de capa/botas/cinto |
| `9629E67010AEB124` | 512×512 | 10 | Cabeça: rosto, cabelo, orelha, mão, olho |
| `2D6CE8B699D3D7D8` | 512×512 | 11 | Normal map da cabeça |

Os normal maps não mudam de cor, então a skin não mexe neles.

### Regiões (pixels, x0,y0 – x1,y1)

`E4620603169D8112` (corpo):
- Emblema: escudo triangular com borda preta em (336,0)–(496,132). O amarelo do fundo fica em (356,8)–(480,108) e o "S" vermelho em (384,16)–(448,88).
- O resto da textura é a malha azul; o fundo fora das ilhas UV é preto.

`96AE4603385ED3F2` (capa, botas, cinto):
- Vermelho, peça curva à esquerda: (0,0)–(264,380).
- Vermelho, peça alta à direita, com a linha recortada do cano da bota por volta de y≈185–215: (264,0)–(512,508).
- Cinto amarelo com a fivela redonda à esquerda: (16,436)–(320,512); fivela em torno de (20–100, 440–500).
- Bloco cinza claro: (116,324)–(256,404). Bloco vinho: (140,404)–(236,452).
- Qual peça vermelha é a capa e qual são as botas/sunga ainda é suposição pelo formato (a curva à esquerda parece a capa; a da direita, com o cano, as botas). A Task 5 confirma pintando cada peça com uma cor diferente num print do jogo. O mesmo vale para o cinza e o vinho.

`9629E67010AEB124` (cabeça):
- Cabelo preto: lateral/franja (4,80)–(116,180), costeleta e nuca (148,188)–(252,272), topo (252,0)–(512,184) e parte de trás (256,348)–(408,512). Sobrancelha em (188,32)–(236,40).
- Pele, orelha e mão no resto; íris em (400,396)–(512,512).

## Traje do filme (referência)

- `D25F525A7A0BBE06` (512×512, formato 10): corpo azul com o "S" do filme. É a textura que `--sr_costume` observa para saber quando o jogo voltou ao traje do filme.
- `E786C8E27A1892E6` (512×512) e `F008590FEE139933` (128×128) são o logo do filme da interface e um detalhe dele, não o traje.

## Forçar o traje (`--sr_costume`)

- O menu de trajes (handler Apt `sub_822B29E0`) só escreve `costume+1` na variável `SetCostumeNum` do fluxo `SupermenActorWorldFlow`; `sub_822B18E0` só mostra a caixa "vestindo". Nada vai para o save.
- O script consome a variável em 30–80 ms. Escrita antes de o Superman existir não tem efeito, e o jogo volta ao traje do filme quando recria o Superman.
- A escrita é feita meio segundo depois que o renderizador decodifica `D25F525A7A0BBE06`, com até 3 tentativas a cada ~3 s, até `E4620603169D8112` ser decodificada. A reaplicação a cada 5 s funcionava, mas causava travadas.
- Teste no jogo (2026-10-29): uma única escrita, já no título. O Golden Age se manteve na cinemática de abertura, em Metropolis e na troca para `SunnySkyboxWorldFlow`.
