# P2: câmera no chão e transições de locomoção

## Diagnóstico e desenho (antes da implementação)

O grafo secundário só escolhe o controlador no `SetCameraHomeEntity` do
spawn. Mudanças posteriores de locomoção continuam dirigidas ao singleton
principal. A entrada de rotação do personagem também usa esse singleton.

Os nomes dos recursos no executável distinguem os controladores: manager +544
carrega `FlightCamDefsXML`; +2032 carrega `OnFootCamDefsXML`. Não inverter esses
dois nomes: o movimento do personagem pode fazer a câmera de voo parecer
funcional mesmo com o controlador errado ao pousar.

1. Identificar a locomoção do ator P2 pela interface ACTR (virtual +36), seu
   componente +96 e o controlador ativo +4. Os getters nativos de tipo +4
   identificam FLY (`0x00464C59`, câmera 0) e LOCO (`0x4C4F434F`, câmera 1).
   Ao mudar entre esses modos reconhecidos, chamar `sub_82174758` no manager
   secundário, que já implementa essa escolha. Não repetir resets todo frame.
2. Encaminhar as leituras de câmera das rotinas nativas de input para esse
   manager apenas quando o dono for o ator vivo do port 1, na mesma geração F2.
   Preservar o processamento nativo dos eixos, sensibilidade, limites e colisão.
   Não substituir o singleton nem executar novamente a simulação/manager inteiro.
3. Manter o F2 inicialmente desligado e invalidar o vínculo quando a sessão
   muda, o ator desaparece ou o singleton é recriado. Sem novos REX_FATAL.

## Contratos dos hooks de manifesto

Todos executam **após** o `lwz` do singleton e **antes** de seu uso. Alteram
somente o registrador que contém o manager; não alteram CR, memória ou fluxo.

| PC | Rotina | Manager | Proveniência |
|---|---|---|---|
| 821EAC8C | 821EABE8 | r11 | port em r31; antes de +2032/82160690 (look) |
| 821FFD78 | 821FFD28 | r3 | componente de locomoção em r30; heading |
| 822000E4 | 821FFD28 | r11 | r30; estado da câmera |
| 82200140 | 821FFD28 | r3 | r30; posição |
| 82200148 | 821FFD28 | r11 | r30; alvo |
| 8220044C | 821FFD28 | r11 | r30; antes de +2032/82160690 (look) |
| 821FC538 | 821FC3B8 | r3 | componente em r29; heading |
| 82212F14 | 82212E50 | r29 | ator em r27 e port em r31; movimento relativo |

O componente +100 contém a interface de input; seu getter +108 deve ser
`821AA4F8`, cujo corpo resolve o port pelo ator em interface +4. Validar esse
contrato e o ID do ator em +88, os slots 0/1 distintos, o home secundário e a
geração ativa antes de encaminhar. Outros atores/ports/modos seguem originais.

## Validação prevista

Testes de seleção isolada por ator/port/geração e transições FLY/LOCO. Compilar
o jogo regenerando hooks pelo manifesto. Ampliar o input sintético opcional do
probe com RX/RY, mantendo compatibilidade com comandos antigos, para exercitar
rotação horizontal/vertical separada de cada jogador. Comparar os transforms
antes/depois no chão, em voo e após pouso/F2, com o outro jogador parado. O
benchmark de título não exercita input cooperativo e não comprova esta correção.
