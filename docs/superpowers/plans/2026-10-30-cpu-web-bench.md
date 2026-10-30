# CPU web research and benchmark plan

- [x] Consultar fontes primárias Microsoft/AMD/LLVM e correlacionar com tempos locais.
- [x] Criar runner sequencial com checkpoint e hash de executável fixo.
- [x] Concluir 3 blocos de baseline/cache shared/tracking Off, com ordem alternada.
- [x] Testar admissão de queries apenas para uploads com prova publicada.
- [x] Compilar e repetir A/B da admissão, com/sem profiling; preservar hashes e auditoria.
- [x] Investigar pausa longa e preparar Arm Windows por regiões, com testes de direitos/falhas.
- [x] Verificar runtime legado e agrupado, incluindo aliases, vizinhos e direitos de SDK.
- [x] Comparar Arm legado/agrupado em On e Compare, coletando custo e número de syscalls.
- [x] Registrar resultados, limitações e ativar agrupamento como padrão após validação.
- [x] Recompilar e validar default sem override, verificar SDK e testes finais.

Não executar compilação ou testes pesados junto das medições. Tracking Off é
um diagnóstico que também desliga provas de buffers, não uma otimização isolada
de texturas. A máscara de constantes e uploads seletivos continuam Off.
