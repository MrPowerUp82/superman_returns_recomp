# M5: tela gráfica do próprio EA APT

Direção explícita do usuário em05/11/2026 substitui o overlay ImGui. As imagens
fornecidas são referência visual: entrada GRAPHICS OPTIONS entre AUDIO e QUIT,
mesma moldura/fonte/foco/setas/Back do jogo. Preservar11opções do pedido original
com páginas se necessário, e preferências do launcher. Não exigir nova aprovação.

## Evidência antes do código

REA `open_binary(game/default.xex)` rejeitou formato XEX; o PE carregado em
`logs/rt_menu_loaded_image_2410.bin` foi rejeitado por COFF PowerPC0x01F2. Sessão
não abriu; não há EvidenceID de decompilação para citar e não se inventa suporte.
Continuar com a imagem carregada local, PPC gerado e parser1:7:4 já existente.

Assets de `ui.AST` auditados em artifacts/graphics-menu-apt-research. UI AST
SHA2569893245576351240f1362b7d9e8e8121a1a899dd16e605d9decb8096e08c177f.
Pause entry812, Gameplay804, MAIN40. Todos sem compressão. Top hash64 é FNV1a
UTF8 do nome uppercase: verificado contra9/9nomes, incluindo BASICBUTTON e MAIN.
Subpacote mantém IDs -3constant/-2APT e um namespace próprio para o clone.

`ScrPauseMenu` possui9BasicButton. Clone do botãoQuit recebe nome/foco próprios;
inserirGraphics antes dele, preservandoResume/Restart/Save/Stats/Moves/Gameplay/
Controller/Audio. CloneScrGameplayOpt contém Toggle original, métodosSetData,
SetIndex/GetIndex, SetLabel/SetWidth. Fonte original SupermanBodyFont.

CallbackPauseGame822A0F38 retorna umAptValue, recebe contagem emr4.
Getarg(i):826C3328; converterint:826815A0; retornoint:82698550; retorno
string:8269C538. Nova ponte via manifesto midasm na entradaPauseGame intercepta
apenas3args com sentinel21330Get/21331Set. Sem sentinel, original completo.
APTGet(row,field) fields0requested,1applied,2capable,3label,4optionspipe,5reason.
Nenhum endereço guest sintético ou novo REX_FATAL. Os callbacks AS2 próprios
usam esse transporte, sem mudar PauseGame/UnPauseGame comuns.

## Implementação

Gerar extensão local a partir dos assets do dono, sem distribuir bytecodeEA no
Git nem alterar game/DATA/ui.AST. Receita copiar/alterar/adicionar, com digest
do original, aplicada a cache privado; VFS redireciona só o arquivoUI para
HostPathDevice readonly. Não colocar Python no caminho por quadro.
Reutilizar os controles originais e deixar APT cuidar de foco/input/sounds.
Remover instalação/fontes/entrada e captura de input do overlay gráfico.
Renderer continua usandoSharedGraphicsOptions e relatório de execução porfence.

## Validação

Parser deve aceitar resultado, comparar integridade dos assets nãoalterados,
confirmar10botõesPause, rotagraphics,11opções acessíveis e widgetsToggle. Testar
Get/Set em gameplay real: A abre entrada, Back retorna à mesma pausa, Start
retoma, keyboard segue caminhooriginal deinput. Capturas reais semImGui.
Revalidar modosDLSS/hotres/FPS/FXAA/MSAA/scale eINI dirtymerge. Performance
3rodadas builtin separada das capturas; Vulkan usa osmesmos assets/UIguest.

Desconhecidos: ABI completa do loadMovie e namespace interno precisa ser
confirmada por execução; análise estática não comprova clone carregado.


## Ajuste do roteamento VFS, antes da implementação

O primeiro teste real abriu a pausa original. O SDK OpenFile resolve o diretório
pai e chama GetChild; portanto symlink somente no arquivo é insuficiente.
Montar um Device readonly para DATA, com entries de encaminhamento para todos os
filhos originais e apenas ui.AST apontando para o cache privado. Entry.GetChild
continua nativo. Open/OpenMapped dos outros arquivos encaminham ao original,
sem copiar conteúdo nem alterar o diretório do jogo. Validar diretório resolvido,
child UI privado e hash; falha conserva o diretório original. O contrato de
namespace do pacote APT continua pendente de execução.


## ABI de opcode confirmada em execução

REA/Frida observou a falha na VM, LR82688CFC, opcode B9 emr30. A tabela
de handlers começa82931420; a entrada B9 contém0x80 e está fora da versão
deste título. A8/B8 e os handlers de Push96/DefineFunction28E são válidos.
O gerador passa a emitir Push96 referindo constant pool type4 register,
exatamente como os assets originais. Referência genérica AptTools não constitui
prova de suporte a B9 nesta versão. Regressão recursiva recusa bytecodes>B8.
Evidência Frida: ev_50dad0cba8e62edf9e3ba8d34a216283d6d8c41a052b1022c5eeb759d08a7563
instalou a observação; captura posterior do terceiro teste contém registradores
e backchain. A instrumentação só observa o teste e é removida após o processo.
