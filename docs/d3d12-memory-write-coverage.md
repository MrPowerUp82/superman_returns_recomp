# Cobertura de escritas físicas no runtime D3D12

Revisão base: ReXGlue v0.10.0, `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`.
Estado: integração em desenvolvimento; **não há autorização mecânica de pular hashes**.

O SDK stock não exporta o contrato v1. O teste real de memória falhou, como esperado,
por export ausente nos três casos iniciais. O build opcional usa checkout completo
separado, patch versionado e manifesto SHA256; o recorte de GPU não é modificado.

| Classe | Origem no SDK | Mecanismo / situação |
| --- | --- | --- |
| Stores guest e host físico | `src/system/xmemory.cpp`, `Memory::MapViews`, `AccessViolationCallback` | Guardas de CPU de uso único; perda de cobertura publicada antes de liberar o store |
| Aliases A/C/E | `Memory::MapViews`, `PhysicalHeap::GetPhysicalAddress` | Proteger todas as vistas; E usa offset físico de 4 KiB e mapeamento Windows arredondado a 64 KiB |
| Alias adicional 7F | `Memory::MapViews`, GPU writeback de 16 MiB | Também mapeia os primeiros 16 MiB físicos, embora LookupHeap retorne null para essa vista |
| Alloc/fixed/range e reuso | `BaseHeap`, heap físico pai | Registrar identidade uma vez no pai; alias não cria alocação física independente |
| Proteção, commit, decommit, release, reset | Todos os `Protect`/`AllocFixed` de `src/system/xmemory.cpp`, inclusive recovery de proteção obsoleta | Invalidar antes da chamada OS, sob região crítica; reset não recicla contadores |
| Leitura de arquivos host | `XFile::ReadInternal` → VFS → `Win32FileHandle::Read` | HostWriteScope registra e invalida antes do kernel; termina inclusive em erro parcial/zero bytes. Teste ReadFile real e backend de erro controlado |
| Scatter de arquivos | `XFile::ReadScatter` → `ReadInternal` | Escopo por leitura efetiva; teste cobre duas páginas e callbacks após conclusão |
| Recepção de rede | `XSocket::Recv`, `RecvFrom` → Winsock | Escopo durante operação bloqueante; testes UDP loopback cobrem bytes, época e contador antes/depois |
| Saída de ioctl de rede | `XSocket::IOControl` → `ioctlsocket` | Escopo cobre quatro bytes; teste FIONREAD em página protegida |
| Áudio, memexport e resolves com cópia CPU | XMA e `src/graphics/d3d12/command_processor.cpp` | Stores comuns pelo ponteiro físico devem atingir guardas; não chamar data providers no fault físico |
| Novos mapeamentos ou produtores fora do inventário | Extensões/plugins/API do host | Cobertura desconhecida; não anunciar capability HostProducers sem fechar o inventário |

Na primeira versão, um fault invalida a alocação até nova geração. Não rearmar no fim
do frame. Guardas pertencentes ao SDK não são desfeitas pelo monitor: qualquer
alteração externa de proteção retira a propriedade antes da mudança. A consulta
de múltiplas faixas e a instalação de guardas usam a região crítica global; os
metadados puros têm seu próprio mutex, sempre adquirido depois dela.

A instalação não deve certificar um snapshot enquanto houver stores anteriores
pendentes. A implementação protege todas as vistas antes da
cópia e executa FlushProcessWriteBuffers antes da primeira certificação.
A documentação de [VirtualProtect](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotect)
descreve a alteração de proteção das páginas comprometidas; a de
[FlushProcessWriteBuffers](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-flushprocesswritebuffers)
garante visibilidade das escritas entre processadores. Isso não substitui os
scopes de I/O nem permite rearmar uma alocação após fault. O custo da barreira
inicial deve entrar na medição de criação de watches.

O contrato anuncia a máscara completa (`15`) somente no runtime opcional com
os hooks deste inventário. O cliente rejeita DLL stock, versão/tamanho incompatível,
funções ausentes e capacidades incompletas. Ainda não existe integração de hash
skipping no renderer nem certificação de uploads D3D12.

Scopes são registrados mesmo antes de existir uma sessão: I/O pendente impede
instalação de sessão/watch. Stop restaura suas guardas e separa a sessão; scopes
ativos retêm os metadados até concluir, sem esperar indefinidamente por recv.
Uma faixa de produtor não representável invalida o domínio e bloqueia novas sessões.
O inventário completo encontrou outros ReadSync apenas com buffers host próprios;
XMA, memexport e readbacks executam cópias de CPU que passam pelas guardas físicas.

Verificação da Task 3: suíte real passou em quatro alvos CTest, incluindo processos
isolados para perda de contrato, falha de mapeamento e Memory não inicializada.
Os casos normais cobrem aliases, lifecycle, I/O parcial/erro/zero/scatter, rede,
produtor anterior à sessão e teardown concorrente. Cliente stock passou em 1/1.
A suíte pura contém 210 casos. Essas verificações ainda não demonstram ganho de FPS.

Reprodução: `powershell -NoProfile -File tools/setup_memory_tracking_sdk.ps1`;
depois `SR_MEMORY_TRACKING_SDK=ON`, `SR_BUILD_DIR=port/out/build/win-amd64-write-tracking`
e `build.cmd`. `-VerifyOnly` valida origem/patch/arquivos. O setup não descarta nem
sobrescreve edições desconhecidas. As fontes compartilhadas em `tools/` são a
cópia canônica; o diretório `.tools` é apenas materialização para build.

Compatibilidade do build fonte no Windows: o patch usa os arquivos reais de
`libmspack/libmspack/mspack`, pois `cabextract/mspack` contém symlinks que este Git
materializa como arquivos de texto. O build opcional também aplica
`-march=x86-64-v2`, como o preset `windows-base` do SDK. O build stock passou após
a introdução da opção, sem mudar seus flags.

Comparação inicial (Task 4): título/attract, transição New Game, sequência de
abertura em vídeo, gameplay parado e andando passaram sem divergências registradas.
HUD, minimapa e capa foram inspecionados. Logs e PNGs: `logs/texture_tracking_compare1_*`.
A execução ficou aberta durante a espera pelo aviso de firewall; totais cumulativos
incluem attract. As janelas de gameplay têm 20 s cada e exigem diferenças de
contadores. Este ensaio não aceita ganho de FPS nem substitui imagens alinhadas.
On continua indisponível nesta task.

Task 5: On experimental agora usa decode próprio e publica certificado somente
após sucesso de recurso/SRV e nova consulta de stamps. Identidade contém seis
palavras fetch e versão única; colisão no hash do cache não mistura descritores.
A fixture WARP verificou pixels via readback para escrita antes/depois do upload
e depois da publicação; 33 casos reais e três processos de falha passaram.
Camada de debug indisponível. Smoke On/Off observou título, vídeo, transição,
HUD/minimapa/capa e movimento. Imagens/FPS ainda não constituem aceitação formal:
a captura final Off foi tardia e o HUD desapareceu. Mantém-se Off por padrão.
